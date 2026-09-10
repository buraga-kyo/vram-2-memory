#include "servidor_ublk.h"
#include "alvo_ublk.h"
#ifndef VRAM_SEM_CUDA
#include "meio_cuda.h"
#endif
#include "meio_simulado.h"
#include "monitor_do_observatorio.h"
#include "observador_de_si.h"
#include "parada_limitada_ublk.h"
#include "plano_da_memoria.h"
#include "reserva_de_buffers.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <ublksrv.h>
#include <ublksrv_utils.h>
#include <unistd.h>

#define PRAZO_DA_PARADA_EM_NANOSSEGUNDOS UINT64_C(2000000000)

struct incumbencia_da_fila_ublk;
/* O servidor reune a configuração, o meio e as duas faces do dispositivo. */
struct estado_do_servidor_ublk {
    const struct configuracao_do_apparelho *configuracao;
    struct governo_do_apparelho *governo;
    const struct operacoes_do_meio *operacoes_do_meio;
    void *contexto_do_meio;
    struct ublksrv_ctrl_dev *controle;
    const struct ublksrv_dev *dispositivo;
    struct contadores_da_fila *contadores;
    struct incumbencia_da_fila_ublk *incumbencias;
    int quantidade_de_filas_preparadas;
    pthread_mutex_t exclusao_da_publicacao;
    pthread_cond_t mudanca_da_publicacao;
    uint32_t filas_que_responderam;
    int filas_liberadas;
    int portao_da_publicacao_iniciado;
    struct reserva_de_buffers reserva_de_buffers;
    pthread_t fio_do_observatorio;
    atomic_int ordenar_termo_do_observatorio;
    atomic_int falha_terminal;
    atomic_int posses_em_quarentena;
    atomic_int estado_da_parada;
    int resultado_da_parada;
    int observatorio_iniciado;
    int empregar_cuda;
    int buffers_registrados;
    int memoria_fixada;
};
/* Cada trabalhador possue fila autoral e fila exterior de igual índice. */
struct incumbencia_da_fila_ublk {
    struct estado_do_servidor_ublk *servidor;
    struct fila_de_requisicoes fila;
    struct contexto_da_fila_ublk contexto;
    pthread_t fio_de_execucao;
    unsigned short indice;
    int resultado;
};
static struct estado_do_servidor_ublk *servidor_em_exercicio;
static pthread_mutex_t exclusao_do_governo = PTHREAD_MUTEX_INITIALIZER;
static atomic_int termo_requerido;

/*
 * LEMMA DA PARADA QUARENTENARIA
 * Proposito: ordenar termo finito e conservar posses quando elle não chega.
 * Pre-condições: servidor e controle vivos.
 * Effeitos: submette STOP_DEV; no erro publica ruína e quarentena.
 * Retorno: resultado da ordem limitada. Razão: limpeza não vence segurança.
 */
static int ordenar_parada_limitada_do_servidor(
    struct estado_do_servidor_ublk *servidor)
{
    uint64_t inicio = ler_instante_monotonico();
    int estado_esperado = 0;
    int resultado;

    if (atomic_compare_exchange_strong_explicit(
            &servidor->estado_da_parada, &estado_esperado, 1,
            memory_order_acq_rel, memory_order_acquire)) {
        resultado = parar_dispositivo_ublk_com_limite(
            servidor->controle, PRAZO_DA_PARADA_EM_NANOSSEGUNDOS);
        servidor->resultado_da_parada = resultado;
        atomic_store_explicit(&servidor->estado_da_parada, 2,
                              memory_order_release);
    } else {
        while (atomic_load_explicit(&servidor->estado_da_parada,
                                    memory_order_acquire) == 1) {
            uint64_t instante_actual = ler_instante_monotonico();
            if (inicio == 0 || instante_actual < inicio ||
                instante_actual - inicio >=
                    PRAZO_DA_PARADA_EM_NANOSSEGUNDOS) break;
            (void)sched_yield();
        }
        resultado = atomic_load_explicit(&servidor->estado_da_parada,
                                         memory_order_acquire) == 2 ?
            servidor->resultado_da_parada : -ETIMEDOUT;
    }

    if (resultado < 0 && resultado != -ENODEV) {
        atomic_store_explicit(&servidor->falha_terminal, 1,
                              memory_order_relaxed);
        atomic_store_explicit(&servidor->posses_em_quarentena, 1,
                              memory_order_relaxed);
    }
    return resultado;
}

/*
 * LEMMA DO PORTAO ANTERIOR
 * Proposito: preparar a synchronização que separa prova e publicação.
 * Pre-condições: servidor singular, zerado e ainda sem trabalhadores.
 * Effeitos: inicia exclusão e condição; restitue a exclusão na falha parcial.
 * Retorno: zero ou o erro negativo de pthread. Razão: nenhum fio improvisa.
 */
static int preparar_portao_da_publicacao(
    struct estado_do_servidor_ublk *servidor)
{
    int resultado;

    resultado = pthread_mutex_init(&servidor->exclusao_da_publicacao, 0);
    if (resultado != 0) return -resultado;
    resultado = pthread_cond_init(&servidor->mudanca_da_publicacao, 0);
    if (resultado != 0) {
        (void)pthread_mutex_destroy(&servidor->exclusao_da_publicacao);
        return -resultado;
    }
    servidor->portao_da_publicacao_iniciado = 1;
    return 0;
}

/*
 * LEMMA DA CONCESSAO FIXAVEL
 * Proposito: confrontar a geometria com RLIMIT_MEMLOCK antes da reserva.
 * Pre-condições: configuração julgada. Effeitos: consulta e poderá informar.
 * Retorno: zero, erro da consulta, do cálculo ou -ENOMEM na insufficiência.
 * Razão: quantidade, limite e remédio devem preceder qualquer publicação.
 */
static int conferir_memoria_fixavel_do_servidor(
    const struct configuracao_do_apparelho *configuracao)
{
    struct rlimit limite;
    uint64_t limite_em_bytes;
    uint64_t necessaria_em_bytes = 0;
    int resultado;

    if (getrlimit(RLIMIT_MEMLOCK, &limite) != 0) return -errno;
    limite_em_bytes = limite.rlim_cur == RLIM_INFINITY ? UINT64_MAX :
        (uint64_t)limite.rlim_cur;
    resultado = conferir_limite_da_memoria_intermediaria(
        configuracao, limite_em_bytes, &necessaria_em_bytes);
    if (resultado == -ENOMEM)
        fprintf(stderr, "Memoria fixada necessaria=%llu limite=%llu; "
                "eleve LimitMEMLOCK ou ulimit -l.\n",
                (unsigned long long)necessaria_em_bytes,
                (unsigned long long)limite_em_bytes);
    return resultado;
}

/*
 * THEOREMA DOS BUFFERS ANTERIORES
 * Proposito: adquirir todos os buffers e registrá-los quando houver CUDA.
 * Pre-condições: meio preparado e geometria julgada.
 * Effeitos: publica a reserva completa e, no CUDA, sua marca de registro.
 * Retorno: zero ou erro negativo depois de restituir posse parcial.
 * Razão: callback de etiqueta limitar-se-á a escolher memória já existente.
 */
static int preparar_buffers_do_servidor(
    struct estado_do_servidor_ublk *servidor)
{
    const struct configuracao_do_apparelho *configuracao;
    int resultado;

    if (servidor == 0) return -EINVAL;
    configuracao = servidor->configuracao;
    resultado = criar_reserva_de_buffers(
        &servidor->reserva_de_buffers, configuracao->quantidade_de_filas,
        configuracao->profundidade_das_filas,
        configuracao->maior_operacao_em_bytes);
    if (resultado < 0) return resultado;
    if (!servidor->empregar_cuda) return 0;
    if (!registrar_memoria_intermediaria_cuda(
            servidor->reserva_de_buffers.inicio,
            (size_t)servidor->reserva_de_buffers.quantidade_em_bytes)) {
        destruir_reserva_de_buffers(&servidor->reserva_de_buffers);
        return -EIO;
    }
    servidor->buffers_registrados = 1;
    return 0;
}

/*
 * LEMMA DO GABINETE DE EXECUCAO
 * Proposito: assegurar a casa onde ublksrv conserva o processo dirigente.
 * Pre-condições: caminho absoluto, não vazio e pertencente á bibliotheca.
 * Effeitos: crea o directório ausente; não altera um directório existente.
 * Retorno: zero no êxito ou erro negativo quando o caminho não é directório.
 * Razão: a instalação local não possue gestor de serviços que prepare /run.
 */
static int preparar_directorio_de_execucao(const char *caminho)
{
    struct stat estado;

    if (caminho == 0 || caminho[0] == 0) return -EINVAL;
    if (mkdir(caminho, 0755) == 0) return 0;
    if (errno != EEXIST) return -errno;
    if (stat(caminho, &estado) != 0) return -errno;
    return S_ISDIR(estado.st_mode) ? 0 : -ENOTDIR;
}

/*
 * Proposito: descobrir novamente a largura concedida pelo terminal.
 * Pre-condições: a saída de erros poderá ou não ser um terminal.
 * Effeitos: consulta TIOCGWINSZ sem mudar o apparelho.
 * Retorno: largura observada ou oitenta columnas na ignorância.
 * Razão: cada quadro responde naturalmente a SIGWINCH na colheita seguinte.
 */
static size_t descobrir_largura_do_observatorio(void)
{
    struct winsize dimensao = {0};

    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &dimensao) == 0 &&
        dimensao.ws_col != 0) return dimensao.ws_col;
    return 80;
}

/*
 * THEOREMA DO OBSERVADOR FRIO
 * Proposito: colher e mostrar retratos sem deter as filas.
 * Pre-condições: servidor preparado, contadores vivos e configuração válida.
 * Effeitos: escreve quadros periódicos até receber ordem atomica de termo.
 * Retorno: o argumento original; perdas de saída tornam-se estatística.
 * Razão: espera e apresentação vivem fora do caminho das requisições.
 */
static void *observar_servidor_ublk(void *argumento)
{
    struct estado_do_servidor_ublk *servidor = argumento;
    struct retrato_do_observatorio retrato, anterior = {0}, janella;
    struct configuracao_do_monitor configuracao;
    struct configuracao_da_narracao configuracao_da_voz;
    const struct timespec repouso = {1, 0};
    uint64_t instante_anterior = ler_instante_monotonico();
    char quadro[2048];
    char voz[256];
    char saida[2304];
    int flags_originais = fcntl(STDERR_FILENO, F_GETFL);

    if (flags_originais < 0 || fcntl(
            STDERR_FILENO, F_SETFL, flags_originais | O_NONBLOCK) < 0)
        return argumento;

    while (!atomic_load_explicit(&servidor->ordenar_termo_do_observatorio,
                                 memory_order_relaxed)) {
        uint64_t instante_actual = ler_instante_monotonico();
        size_t tamanho_do_quadro;
        size_t tamanho_da_voz;
        ssize_t escriptos;
        size_t tamanho_da_saida;

        anterior.instante_monotonico_em_nanossegundos = instante_anterior;
        if (!colher_retrato_do_observatorio(
                &retrato, servidor->contadores,
                (size_t)servidor->configuracao->quantidade_de_filas,
                instante_actual, instante_anterior)) break;
        retrato.capacidade_em_bytes =
            servidor->configuracao->capacidade_em_bytes;
        retrato.memoria_do_meio_reservada_em_bytes =
            retrato.capacidade_em_bytes;
        retrato.memoria_da_cpu_fixada_em_bytes =
            (uint64_t)servidor->configuracao->quantidade_de_filas *
            servidor->configuracao->profundidade_das_filas *
            servidor->configuracao->maior_operacao_em_bytes;
        if (!differenciar_retratos_do_observatorio(
                &janella, &retrato, &anterior)) break;
        configuracao.largura_em_colunas = descobrir_largura_do_observatorio();
        configuracao.empregar_cor = isatty(STDERR_FILENO);
        tamanho_do_quadro = escrever_quadro_do_observatorio(
            quadro, sizeof(quadro), &janella, &configuracao);
        configuracao_da_voz.modo = configuracao.empregar_cor ?
            MODO_DA_NARRACAO_THEATRAL : MODO_DA_NARRACAO_SOBRIO;
        configuracao_da_voz.idade_maxima_em_nanossegundos = 2000000000ULL;
        configuracao_da_voz.p99_alarmante_em_microssegundos =
            (uint64_t)servidor->configuracao
                ->prazo_da_operacao_em_milissegundos * 1000ULL;
        tamanho_da_voz = narrar_observador_de_si(
            voz, sizeof(voz), &janella, &configuracao_da_voz, instante_actual);
        tamanho_da_saida = tamanho_do_quadro == SIZE_MAX ||
            tamanho_da_voz == SIZE_MAX ? 0 : tamanho_do_quadro + tamanho_da_voz;
        if (tamanho_da_saida != 0 && tamanho_da_saida <= sizeof(saida)) {
            memcpy(saida, quadro, tamanho_do_quadro);
            memcpy(saida + tamanho_do_quadro, voz, tamanho_da_voz);
            if (tamanho_da_saida <= sizeof(saida)) {
                escriptos = write(STDERR_FILENO, saida, tamanho_da_saida);
            } else escriptos = -1;
        } else escriptos = -1;
        if (escriptos < 0 || (size_t)escriptos != tamanho_da_saida)
            atomic_fetch_add_explicit(&servidor->contadores[0].amostras_perdidas,
                                      1, memory_order_relaxed);
        anterior = retrato;
        instante_anterior = instante_actual;
        nanosleep(&repouso, 0);
    }
    (void)fcntl(STDERR_FILENO, F_SETFL, flags_originais);
    return argumento;
}

/*
 * Proposito: fazer nascer o observador somente depois das filas preparadas.
 * Pre-condições: servidor e taboa estatística permanecem vivos.
 * Effeitos: inicia um fio frio e registra sua existência.
 * Retorno: zero no êxito ou erro negativo de pthread_create.
 * Razão: a marca só se publica depois que o fio realmente nasceu.
 */
static int iniciar_observatorio_ublk(struct estado_do_servidor_ublk *servidor)
{
    int resultado;

    if (servidor == 0 || servidor->contadores == 0) return -EINVAL;
    atomic_store_explicit(&servidor->ordenar_termo_do_observatorio, 0,
                          memory_order_relaxed);
    resultado = pthread_create(&servidor->fio_do_observatorio, 0,
                               observar_servidor_ublk, servidor);
    if (resultado != 0) return -resultado;
    servidor->observatorio_iniciado = 1;
    return 0;
}

/*
 * Proposito: ordenar o termo e recolher exactamente o observador nascido.
 * Pre-condições: servidor válido; a marca declara a posse do fio.
 * Effeitos: publica a ordem, reúne o fio e apaga a marca.
 * Retorno: zero ou erro negativo de pthread_join.
 * Razão: contadores só podem morrer depois que cessou quem os contempla.
 */
static int encerrar_observatorio_ublk(struct estado_do_servidor_ublk *servidor)
{
    int resultado;

    if (servidor == 0) return -EINVAL;
    if (!servidor->observatorio_iniciado) return 0;
    atomic_store_explicit(&servidor->ordenar_termo_do_observatorio, 1,
                          memory_order_relaxed);
    resultado = pthread_join(servidor->fio_do_observatorio, 0);
    servidor->observatorio_iniciado = 0;
    return resultado == 0 ? 0 : -resultado;
}
/*
 * LEMMA DA FIGURA DO ALVO
 * Proposito: declarar á libublksrv a capacidade e a disciplina das filas.
 * Pre-condições: servidor único preparado antes de iniciar o dispositivo.
 * Effeitos: preenche somente as grandezas autorais do alvo.
 * Retorno: zero no êxito ou erro negativo se falta contexto.
 * Razão: a bibliotheca chama sem argumento autoral; o servidor é singular.
 */
int inicializar_alvo_ublk(struct ublksrv_dev *dispositivo, int tipo,
                          int quantidade_de_argumentos, char *argumentos[])
{
    const struct ublksrv_ctrl_dev_info *informacao;

    (void)tipo;
    (void)quantidade_de_argumentos;
    (void)argumentos;
    if (dispositivo == 0 || servidor_em_exercicio == 0) return -EINVAL;
    informacao = ublksrv_ctrl_get_dev_info(
        ublksrv_get_ctrl_dev(dispositivo));
    if (informacao == 0) return -ENODEV;
    dispositivo->tgt.dev_size =
        servidor_em_exercicio->configuracao->capacidade_em_bytes;
    dispositivo->tgt.tgt_ring_depth = informacao->queue_depth;
    dispositivo->tgt.nr_fds = 0;
    dispositivo->tgt.tgt_data = servidor_em_exercicio;
    return 0;
}

/*
 * LEMMA DA RESPOSTA ANTERIOR
 * Proposito: contar a prova da fila e retê-la até a sentença dirigente.
 * Pre-condições: portão vivo e resultado definitivo da preparação da fila.
 * Effeitos: publica a resposta e espera abertura, inclusive quando fallou.
 * Retorno: nenhum. Razão: toda saída do trabalhador encontra o dirigente.
 */
static void responder_ao_portao_da_publicacao(
    struct incumbencia_da_fila_ublk *incumbencia)
{
    struct estado_do_servidor_ublk *servidor = incumbencia->servidor;
    int resultado = 0;

    (void)pthread_mutex_lock(&servidor->exclusao_da_publicacao);
    servidor->filas_que_responderam++;
    (void)pthread_cond_broadcast(&servidor->mudanca_da_publicacao);
    while (!servidor->filas_liberadas && resultado == 0) {
        resultado = pthread_cond_wait(&servidor->mudanca_da_publicacao,
                                      &servidor->exclusao_da_publicacao);
    }
    if (resultado != 0 && incumbencia->resultado >= 0)
        incumbencia->resultado = -resultado;
    if (servidor->filas_liberadas < 0 && incumbencia->resultado >= 0)
        incumbencia->resultado = -ECANCELED;
    (void)pthread_mutex_unlock(&servidor->exclusao_da_publicacao);
}

/*
 * THEOREMA DA COLHEITA DA FILA
 * Proposito: consultar eventos até sentenciar as transferências presentes.
 * Pre-condições: fio proprietário, meio vinculado e fila exterior viva.
 * Effeitos: executa callbacks na própria fila e cede CPU quando nada termina.
 * Retorno: zero na drenagem ou primeiro erro do meio ou da entrega.
 * Razão: nova espera ublk só começa depois de resolver os DMAs já recebidos.
 */
static int colher_transferencias_da_fila(
    struct incumbencia_da_fila_ublk *incumbencia)
{
    struct contexto_da_fila_ublk *contexto = &incumbencia->contexto;

    while (contar_requisicoes_transferindo(contexto->fila) > 0) {
        uint32_t etiqueta_vencida;
        uint64_t instante_actual = ler_instante_monotonico();
        int quantidade_colhida = contexto->operacoes_do_meio->colher(
            contexto->contexto_do_meio, contexto->indice_da_fila,
            (int)contexto->fila->profundidade);
        if (instante_actual == 0 || quantidade_colhida < 0) {
            atomic_store_explicit(&incumbencia->servidor->falha_terminal, 1,
                                  memory_order_relaxed);
            atomic_store_explicit(
                &incumbencia->servidor->posses_em_quarentena, 1,
                memory_order_relaxed);
            return instante_actual == 0 ? -EIO : quantidade_colhida;
        }
        if (contexto->resultado_assincrono < 0)
            return contexto->resultado_assincrono;
        if (falhar_primeira_requisicao_vencida(
                contexto->fila, instante_actual,
                contexto->prazo_em_nanossegundos, -ETIMEDOUT,
                &etiqueta_vencida)) {
            struct registro_da_requisicao *registro =
                &contexto->fila->registros[etiqueta_vencida];
            registrar_operacao_observada(
                contexto->contadores,
                registro->operacao != UBLK_IO_OP_READ,
                registro->quantidade_de_bytes,
                instante_actual - registro->instante_inicial_em_nanossegundos,
                -ETIMEDOUT);
            (void)registrar_resultado_na_saude_da_fila(
                &contexto->saude, -ETIMEDOUT, 1);
            contexto->resultado_assincrono = -ETIMEDOUT;
            atomic_store_explicit(&incumbencia->servidor->falha_terminal, 1,
                                  memory_order_relaxed);
            atomic_store_explicit(
                &incumbencia->servidor->posses_em_quarentena, 1,
                memory_order_relaxed);
            return -ETIMEDOUT;
        }
        if (atomic_load_explicit(&incumbencia->servidor->falha_terminal,
                                 memory_order_relaxed)) return -EIO;
        if (quantidade_colhida == 0) (void)sched_yield();
    }
    return contexto->resultado_assincrono;
}

/*
 * THEOREMA DO TRABALHADOR DA FILA
 * Proposito: servir uma fila exterior no seu único fio de execução.
 * Pre-condições: dispositivo e incumbência preparados antes do fio nascer.
 * Effeitos: reserva registros, processa pedidos e restitue toda a fila.
 * Retorno: o proprio argumento; o resultado fica gravado na incumbência.
 * Razão: um só proprietário dispensa synchronização dentro de cada fila.
 */
void *servir_fila_ublk(void *argumento)
{
    struct incumbencia_da_fila_ublk *incumbencia = argumento;
    const struct ublksrv_queue *fila_exterior = 0;
    void *memoria;

    incumbencia->resultado = -ENOMEM;
    incumbencia->contexto.fila = &incumbencia->fila;
    incumbencia->contexto.contadores =
        &incumbencia->servidor->contadores[incumbencia->indice];
    incumbencia->contexto.operacoes_do_meio =
        incumbencia->servidor->operacoes_do_meio;
    incumbencia->contexto.contexto_do_meio =
        incumbencia->servidor->contexto_do_meio;
    incumbencia->contexto.indice_da_fila = incumbencia->indice;
    incumbencia->contexto.falha_terminal_do_servidor =
        &incumbencia->servidor->falha_terminal;
    incumbencia->resultado = incumbencia->contexto.operacoes_do_meio
        ->vincular_fila(incumbencia->contexto.contexto_do_meio,
                       incumbencia->contexto.indice_da_fila);
    if (incumbencia->resultado < 0) goto responder;
    memoria = achar_buffer_reservado(
        &incumbencia->servidor->reserva_de_buffers, incumbencia->indice, 0,
        incumbencia->servidor->configuracao->maior_operacao_em_bytes);
    incumbencia->resultado = incumbencia->contexto.operacoes_do_meio
        ->aquecer_fila(incumbencia->contexto.contexto_do_meio,
                       incumbencia->contexto.indice_da_fila, memoria,
                       incumbencia->servidor->configuracao
                           ->maior_operacao_em_bytes);
    if (incumbencia->resultado < 0) goto responder;
responder:
    responder_ao_portao_da_publicacao(incumbencia);
    if (incumbencia->resultado < 0) return argumento;
    incumbencia->contexto.prazo_em_nanossegundos =
        (uint64_t)incumbencia->servidor->configuracao
            ->prazo_da_operacao_em_milissegundos * 1000000ULL;
    fila_exterior = ublksrv_queue_init(
        incumbencia->servidor->dispositivo, incumbencia->indice,
        &incumbencia->contexto);
    if (fila_exterior == 0) {
        incumbencia->resultado = -ENODEV;
        (void)ordenar_parada_limitada_do_servidor(incumbencia->servidor);
        return argumento;
    }
    do {
        if (atomic_load_explicit(&incumbencia->servidor->falha_terminal,
                                 memory_order_relaxed)) {
            incumbencia->resultado = -EIO;
            break;
        }
        incumbencia->resultado = ublksrv_process_io(fila_exterior);
        if (incumbencia->resultado >= 0)
            incumbencia->resultado = colher_transferencias_da_fila(
                incumbencia);
    } while (incumbencia->resultado >= 0 &&
             !ublksrv_queue_is_done(fila_exterior));
    /* A parada ordenada poderá romper a espera com código exterior negativo. */
    if (ublksrv_queue_is_done(fila_exterior)) {
        incumbencia->resultado = 0;
    } else if (incumbencia->resultado < 0) {
        /* Uma fila enferma ordena que suas irmãs também convirjam. */
        (void)ordenar_parada_limitada_do_servidor(incumbencia->servidor);
    }
    if (!atomic_load_explicit(&incumbencia->servidor->posses_em_quarentena,
                              memory_order_relaxed))
        ublksrv_queue_deinit(fila_exterior);
    return argumento;
}

/*
 * LEMMA DOS PARAMETROS DO BLOCO
 * Proposito: declarar capacidade, bloco e maior operação ao núcleo.
 * Pre-condições: controle vivo e configuração já julgada.
 * Effeitos: envia UBLK_CMD_SET_PARAMS. Retorno: resultado da bibliotheca.
 * Razão: sectores de 512 octetos são exactos porque o bloco mede 4096.
 */
int configurar_parametros_ublk(struct estado_do_servidor_ublk *servidor)
{
    struct ublk_params parametros = {0};

    if (servidor == 0 || servidor->controle == 0) return -EINVAL;
    parametros.len = sizeof(parametros);
    parametros.types = UBLK_PARAM_TYPE_BASIC | UBLK_PARAM_TYPE_DISCARD;
    parametros.basic.attrs = UBLK_ATTR_VOLATILE_CACHE | UBLK_ATTR_FUA;
    parametros.basic.logical_bs_shift = 12;
    parametros.basic.physical_bs_shift = 12;
    parametros.basic.io_opt_shift = 12;
    parametros.basic.io_min_shift = 12;
    parametros.basic.max_sectors =
        servidor->configuracao->maior_operacao_em_bytes >> 9;
    parametros.basic.dev_sectors =
        servidor->configuracao->capacidade_em_bytes >> 9;
    parametros.discard.discard_alignment = TAMANHO_DO_BLOCO_EM_BYTES;
    parametros.discard.discard_granularity = TAMANHO_DO_BLOCO_EM_BYTES;
    parametros.discard.max_discard_sectors =
        servidor->configuracao->maior_operacao_em_bytes >> 9;
    parametros.discard.max_write_zeroes_sectors =
        servidor->configuracao->maior_operacao_em_bytes >> 9;
    parametros.discard.max_discard_segments = 1;
    return ublksrv_ctrl_set_params(servidor->controle, &parametros);
}

/*
 * THEOREMA DA ABERTURA DAS FILAS
 * Proposito: dar a cada fila sua incumbência e seu fio exclusivo.
 * Pre-condições: taboa dimensionada pela configuração e dispositivo vivo.
 * Effeitos: cria fios até a primeira falha e conta quantos nasceram.
 * Retorno: zero no êxito ou erro negativo conservando a contagem parcial.
 * Razão: a contagem torna possível recolher exactamente o que nasceu.
 */
int iniciar_filas_ublk(struct estado_do_servidor_ublk *servidor,
                       struct incumbencia_da_fila_ublk *incumbencias,
                       uint32_t *quantidade_iniciada)
{
    int resultado;

    if (servidor == 0 || incumbencias == 0 || quantidade_iniciada == 0) {
        return -EINVAL;
    }
    *quantidade_iniciada = 0;
    while (*quantidade_iniciada <
           (uint32_t)servidor->configuracao->quantidade_de_filas) {
        struct incumbencia_da_fila_ublk *incumbencia =
            &incumbencias[*quantidade_iniciada];
        incumbencia->servidor = servidor;
        incumbencia->indice = (unsigned short)*quantidade_iniciada;
        resultado = pthread_create(&incumbencia->fio_de_execucao, 0,
                                   servir_fila_ublk, incumbencia);
        if (resultado != 0) return -resultado;
        (*quantidade_iniciada)++;
    }
    return 0;
}

/*
 * THEOREMA DA PUBLICACAO JULGADA
 * Proposito: esperar todas as provas, publicar no êxito e abrir o portão.
 * Pre-condições: somente a quantidade informada de fios chegou a nascer.
 * Effeitos: chama START_DEV uma vez no êxito e liberta todos os respondentes.
 * Retorno: primeiro erro da abertura, prova, espera ou publicação.
 * Razão: o núcleo não verá fila cuja matéria e transporte não foram provados.
 */
static int julgar_filas_e_publicar(
    struct estado_do_servidor_ublk *servidor, uint32_t quantidade_iniciada,
    int resultado_da_abertura)
{
    uint32_t indice;
    int resultado = resultado_da_abertura;
    int resultado_da_espera = 0;

    (void)pthread_mutex_lock(&servidor->exclusao_da_publicacao);
    while (servidor->filas_que_responderam < quantidade_iniciada &&
           resultado_da_espera == 0) {
        resultado_da_espera = pthread_cond_wait(
            &servidor->mudanca_da_publicacao,
            &servidor->exclusao_da_publicacao);
    }
    if (resultado == 0 && resultado_da_espera != 0)
        resultado = -resultado_da_espera;
    for (indice = 0; resultado == 0 && indice < quantidade_iniciada;
         indice++) {
        if (servidor->incumbencias[indice].resultado < 0)
            resultado = servidor->incumbencias[indice].resultado;
    }
    if (resultado == 0 && atomic_load_explicit(
            &termo_requerido, memory_order_relaxed)) resultado = -ECANCELED;
    if (resultado == 0) resultado = publicar_estado_operacional_do_apparelho(
        servidor->governo, ESTADO_DO_GOVERNO_PRONTO);
    servidor->filas_liberadas = resultado == 0 ? 1 : -1;
    (void)pthread_cond_broadcast(&servidor->mudanca_da_publicacao);
    (void)pthread_mutex_unlock(&servidor->exclusao_da_publicacao);
    if (resultado == 0)
        resultado = ublksrv_ctrl_start_dev(servidor->controle, getpid());
    if (resultado == 0) resultado = publicar_estado_operacional_do_apparelho(
        servidor->governo, ESTADO_DO_GOVERNO_SERVINDO);
    return resultado;
}

/*
 * COROLLARIO DA RECOLHA DAS FILAS
 * Proposito: reunir todos os fios nascidos e conservar a primeira falha.
 * Pre-condições: a quantidade é aquella contada durante a abertura.
 * Effeitos: espera o termo de cada fio. Retorno: zero ou primeiro erro.
 * Razão: nenhuma falha posterior deve occultar a causa mais antiga.
 */
int recolher_filas_ublk(struct incumbencia_da_fila_ublk *incumbencias,
                        uint32_t quantidade)
{
    uint32_t indice;
    int primeiro_resultado = 0;

    if (incumbencias == 0 && quantidade != 0) return -EINVAL;
    for (indice = 0; indice < quantidade; indice++) {
        int resultado = pthread_join(
            incumbencias[indice].fio_de_execucao, 0);
        if (primeiro_resultado == 0 && resultado != 0) {
            primeiro_resultado = -resultado;
        }
        if (primeiro_resultado == 0 &&
            incumbencias[indice].resultado < 0) {
            primeiro_resultado = incumbencias[indice].resultado;
        }
    }
    return primeiro_resultado;
}

/*
 * LEMMA DAS OPERACOES DO ALVO
 * Proposito: reunir as duas chamadas obrigatórias de libublksrv.
 * Pre-condições: os contractos de inicialização e passagem permanecem vivos.
 * Effeitos: nenhum. Retorno: endereço immutável da taboa singular.
 * Razão: uma taboa estática jámais depende do tempo de vida da pilha.
 */
const struct ublksrv_tgt_type *obter_operacoes_do_alvo_ublk(void)
{
    static const struct ublksrv_tgt_type operacoes = {
        .name = "vram_2_memory",
        .init_tgt = inicializar_alvo_ublk,
        .handle_io_async = tratar_requisicao_ublk
    };

    return &operacoes;
}

/*
 * LEMMA DA CESSAO PREPARADA
 * Proposito: entregar á libublksrv o quinhão já registrado da etiqueta.
 * Pre-condições: servidor singular, fila e reserva integralmente preparados.
 * Effeitos: nenhum. Retorno: buffer exacto ou nulo na identidade estranha.
 * Razão: o caminho exterior escolhe memória, mas jámais a reserva.
 */
#ifndef VRAM_SEM_CUDA
static void *ceder_buffer_cuda_preparado(const struct ublksrv_queue *fila,
                                         int etiqueta, int tamanho)
{
    if (fila == 0 || servidor_em_exercicio == 0 ||
        !servidor_em_exercicio->buffers_registrados) return 0;
    return achar_buffer_reservado(
        &servidor_em_exercicio->reserva_de_buffers,
        fila->q_id, etiqueta, tamanho);
}

/*
 * COROLLARIO DA RESTITUICAO DIFFERIDA
 * Proposito: conservar o quinhão até que a reserva integral seja desfeita.
 * Pre-condições: buffer nasceu da reserva singular preparada.
 * Effeitos: nenhum. Retorno: nenhum.
 * Razão: cada etiqueta não possue separadamente a região ou seu registro.
 */
static void conservar_buffer_cuda_preparado(
    const struct ublksrv_queue *fila, void *memoria, int etiqueta)
{
    (void)fila;
    (void)memoria;
    (void)etiqueta;
}

/*
 * COROLLARIO DAS OPERACOES CUDA
 * Proposito: acrescentar buffers fixados á passagem commum das requisições.
 * Pre-condições: execução CUDA disponível antes de iniciar as filas.
 * Effeitos: nenhum. Retorno: endereço immutável da taboa CUDA.
 * Razão: somente esta variante promette DMA e por isso fixa seus buffers.
 */
const struct ublksrv_tgt_type *obter_operacoes_do_alvo_cuda(void)
{
    static const struct ublksrv_tgt_type operacoes = {
        .name = "vram_2_memory_cuda",
        .init_tgt = inicializar_alvo_ublk,
        .handle_io_async = tratar_requisicao_ublk,
        .alloc_io_buf = ceder_buffer_cuda_preparado,
        .free_io_buf = conservar_buffer_cuda_preparado
    };

    return &operacoes;
}
#endif

/*
 * COROLLARIO DA ORDEM DE PARADA
 * Proposito: converter a interrupção exterior em termo do dispositivo.
 * Pre-condições: o servidor singular já publicou seu controle.
 * Effeitos: solicita parada á libublksrv. Retorno: nenhum.
 * Razão: o sinal só aponta a porta pela qual os fios hão de convergir.
 */
void ordenar_parada_do_servidor_ublk(int sinal_recebido)
{
    (void)sinal_recebido;
    if (servidor_em_exercicio != 0 &&
        servidor_em_exercicio->controle != 0) {
        (void)ordenar_parada_limitada_do_servidor(servidor_em_exercicio);
    }
}

/*
 * Proposito: conservar e cumprir uma ordem ordinária de termo.
 * Pre-condições: nenhuma. Effeitos: marca o termo e para o controle existente.
 * Retorno: zero. Razão: a exclusão impede corrida com o desmonte do controle.
 */
int ordenar_termo_do_servidor_ublk(void)
{
    atomic_store_explicit(&termo_requerido, 1, memory_order_relaxed);
    (void)pthread_mutex_lock(&exclusao_do_governo);
    if (servidor_em_exercicio != 0 &&
        servidor_em_exercicio->controle != 0) {
        (void)ordenar_parada_limitada_do_servidor(servidor_em_exercicio);
    }
    (void)pthread_mutex_unlock(&exclusao_do_governo);
    return 0;
}

int ordenar_termo_do_servidor_ublk_em_contexto(void *contexto)
{
    struct estado_do_servidor_ublk *servidor = contexto;

    if (servidor == 0) return -EINVAL;
    atomic_store_explicit(&termo_requerido, 1, memory_order_relaxed);
    if (servidor->controle != 0)
        (void)ordenar_parada_limitada_do_servidor(servidor);
    return 0;
}

/*
 * LEMMA DA PORTA DE CONTROLE
 * Proposito: crear no núcleo o par de dispositivos ainda não publicado.
 * Pre-condições: configuração cabe nas larguras impostas por ublk.
 * Effeitos: adquire controle e acrescenta dispositivo.
 * Retorno: zero no êxito ou erro negativo com limpeza na falha.
 * Razão: a porta deve existir antes das filas, mas não recebe tráfego ainda.
 */
int abrir_controle_ublk(struct estado_do_servidor_ublk *servidor)
{
    struct ublksrv_dev_data dados = {0};
    int resultado;

    if (servidor == 0 || servidor->configuracao == 0 ||
        servidor->configuracao->quantidade_de_filas > USHRT_MAX ||
        (unsigned int)servidor->configuracao->quantidade_de_filas >
            UBLK_MAX_NR_QUEUES ||
        servidor->configuracao->profundidade_das_filas > UBLK_MAX_QUEUE_DEPTH ||
        servidor->configuracao->maior_operacao_em_bytes > INT_MAX) {
        return -EINVAL;
    }
    dados.dev_id = -1;
    dados.max_io_buf_bytes =
        servidor->configuracao->maior_operacao_em_bytes;
    dados.nr_hw_queues =
        (unsigned short)servidor->configuracao->quantidade_de_filas;
    dados.queue_depth =
        (unsigned short)servidor->configuracao->profundidade_das_filas;
#ifdef VRAM_SEM_CUDA
    if (servidor->empregar_cuda) return -ENOTSUP;
#endif
#ifdef VRAM_SEM_CUDA
    dados.tgt_type = "vram_2_memory";
    dados.tgt_ops = obter_operacoes_do_alvo_ublk();
#else
    dados.tgt_type = servidor->empregar_cuda ?
        "vram_2_memory_cuda" : "vram_2_memory";
    dados.tgt_ops = servidor->empregar_cuda ?
        obter_operacoes_do_alvo_cuda() : obter_operacoes_do_alvo_ublk();
#endif
    dados.run_dir = ublksrv_get_pid_dir();
    resultado = preparar_directorio_de_execucao(dados.run_dir);
    if (resultado < 0) return resultado;
    servidor->controle = ublksrv_ctrl_init(&dados);
    if (servidor->controle == 0) return -ENODEV;
    resultado = ublksrv_ctrl_add_dev(servidor->controle);
    if (resultado < 0) {
        ublksrv_ctrl_deinit(servidor->controle);
        servidor->controle = 0;
    }
    return resultado;
}

/*
 * COROLLARIO DO DESMONTE DO SERVIDOR
 * Proposito: restituir dispositivo, controle e meio na ordem inversa.
 * Pre-condições: todos os fios já convergiram ou nenhum foi iniciado.
 * Effeitos: liberta posses seguras ou conserva as tocadas por DMA vencido.
 * Retorno: -EIO na quarentena ou resultado da remoção e limpeza.
 * Razão: cada camada exterior morre antes da matéria que ella referencia.
 */
int desmontar_servidor_ublk(struct estado_do_servidor_ublk *servidor)
{
    int resultado = 0;

    if (servidor == 0) return -EINVAL;
    (void)pthread_mutex_lock(&exclusao_do_governo);
    if (servidor_em_exercicio == servidor) servidor_em_exercicio = 0;
    (void)pthread_mutex_unlock(&exclusao_do_governo);
    /* A memória tocada por DMA sem sentença fica viva até o termo do processo. */
    if (atomic_load_explicit(&servidor->posses_em_quarentena,
                             memory_order_relaxed)) return -EIO;
    if (servidor->dispositivo != 0) {
        ublksrv_dev_deinit(servidor->dispositivo);
        servidor->dispositivo = 0;
    }
    if (servidor->controle != 0) {
        resultado = ublksrv_ctrl_del_dev(servidor->controle);
        ublksrv_ctrl_deinit(servidor->controle);
        servidor->controle = 0;
    }
    if (servidor->buffers_registrados) {
        if (!desregistrar_memoria_intermediaria_cuda(
                servidor->reserva_de_buffers.inicio)) {
            if (resultado == 0) resultado = -EIO;
        } else {
            servidor->buffers_registrados = 0;
        }
    }
    if (!servidor->buffers_registrados)
        destruir_reserva_de_buffers(&servidor->reserva_de_buffers);
    if (servidor->operacoes_do_meio != 0 &&
        servidor->contexto_do_meio != 0) {
        servidor->operacoes_do_meio->destruir(servidor->contexto_do_meio);
        servidor->contexto_do_meio = 0;
    }
    while (servidor->quantidade_de_filas_preparadas > 0) {
        servidor->quantidade_de_filas_preparadas--;
        destruir_fila_de_requisicoes(
            &servidor->incumbencias[servidor->quantidade_de_filas_preparadas]
                .fila);
    }
    free(servidor->contadores);
    servidor->contadores = 0;
    free(servidor->incumbencias);
    servidor->incumbencias = 0;
    if (servidor->memoria_fixada) {
        if (munlockall() != 0 && resultado == 0) resultado = -errno;
        servidor->memoria_fixada = 0;
    }
    if (servidor->portao_da_publicacao_iniciado) {
        (void)pthread_cond_destroy(&servidor->mudanca_da_publicacao);
        (void)pthread_mutex_destroy(&servidor->exclusao_da_publicacao);
        servidor->portao_da_publicacao_iniciado = 0;
    }
    atomic_store_explicit(&termo_requerido, 0, memory_order_relaxed);
    return resultado;
}

/*
 * THEOREMA DA PREPARACAO INTEGRAL
 * Proposito: adquirir meio, controle, dispositivo e parâmetros antes das filas.
 * Pre-condições: configuração válida e nenhum outro servidor em exercício.
 * Effeitos: estabelece todas as camadas que antecedem os trabalhadores.
 * Retorno: zero no êxito ou erro negativo depois de restituição completa.
 * Razão: a publicação só virá quando toda memória e geometria existirem.
 */
int preparar_servidor_ublk(struct estado_do_servidor_ublk *servidor,
                           const struct configuracao_do_apparelho *configuracao,
                           int empregar_cuda)
{
    int resultado;

    if (servidor == 0 || !configuracao_do_apparelho_e_valida(configuracao))
        return -EINVAL;
    resultado = conferir_memoria_fixavel_do_servidor(configuracao);
    if (resultado < 0) return resultado;
    (void)pthread_mutex_lock(&exclusao_do_governo);
    if (servidor_em_exercicio != 0) {
        (void)pthread_mutex_unlock(&exclusao_do_governo);
        return -EBUSY;
    }
    servidor_em_exercicio = servidor;
    (void)pthread_mutex_unlock(&exclusao_do_governo);
    servidor->configuracao = configuracao;
    servidor->empregar_cuda = empregar_cuda != 0;
#ifdef VRAM_SEM_CUDA
    if (servidor->empregar_cuda) {
        desmontar_servidor_ublk(servidor);
        return -ENOTSUP;
    }
#endif
    servidor->operacoes_do_meio = servidor->empregar_cuda ?
        obter_operacoes_do_meio_cuda() : obter_operacoes_do_meio_simulado();
    /* O servidor de swap jámais poderá depender do proprio dispositivo. */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        resultado = -errno;
        desmontar_servidor_ublk(servidor);
        return resultado;
    }
    servidor->memoria_fixada = 1;
    resultado = preparar_portao_da_publicacao(servidor);
    if (resultado < 0) {
        desmontar_servidor_ublk(servidor);
        return resultado;
    }
    servidor->incumbencias = calloc(
        (size_t)configuracao->quantidade_de_filas,
        sizeof(*servidor->incumbencias));
    servidor->contadores = calloc(
        (size_t)configuracao->quantidade_de_filas,
        sizeof(*servidor->contadores));
    if (servidor->incumbencias == 0 || servidor->contadores == 0) {
        desmontar_servidor_ublk(servidor);
        return -ENOMEM;
    }
    while (servidor->quantidade_de_filas_preparadas <
           configuracao->quantidade_de_filas) {
        if (!criar_fila_de_requisicoes(
                &servidor->incumbencias[
                    servidor->quantidade_de_filas_preparadas].fila,
                configuracao->profundidade_das_filas)) {
            desmontar_servidor_ublk(servidor);
            return -ENOMEM;
        }
        servidor->quantidade_de_filas_preparadas++;
    }
    resultado = servidor->operacoes_do_meio->preparar(
        &servidor->contexto_do_meio, configuracao);
    if (resultado < 0) {
        desmontar_servidor_ublk(servidor);
        return resultado;
    }
    resultado = preparar_buffers_do_servidor(servidor);
    if (resultado < 0) {
        desmontar_servidor_ublk(servidor);
        return resultado;
    }
    resultado = abrir_controle_ublk(servidor);
    if (resultado < 0) {
        desmontar_servidor_ublk(servidor);
        return resultado;
    }
    if (atomic_load_explicit(&termo_requerido, memory_order_relaxed)) {
        desmontar_servidor_ublk(servidor);
        return -ECANCELED;
    }
    servidor->dispositivo = ublksrv_dev_init(servidor->controle);
    if (servidor->dispositivo == 0) {
        desmontar_servidor_ublk(servidor);
        return -ENODEV;
    }
    resultado = configurar_parametros_ublk(servidor);
    if (resultado == 0) {
        /* A affinidade precede o nascimento dos fios que ella orientará. */
        resultado = ublksrv_ctrl_get_affinity(servidor->controle);
    }
    if (resultado < 0) desmontar_servidor_ublk(servidor);
    return resultado;
}

/*
 * THEOREMA DO SERVICO UBLK
 * Proposito: preparar, publicar, servir e desmontar o apparelho completo.
 * Pre-condições: configuração, governo e libublksrv disponíveis.
 * Effeitos: expõe bloco volátil até interrupção ou falha de fila.
 * Retorno: zero no termo regular ou a primeira falha negativa.
 * Razão: toda saída converge pela mesma successão inversa de limpeza.
 */
int executar_servidor_com_meio(
    const struct configuracao_do_apparelho *configuracao, int empregar_cuda,
    struct governo_do_apparelho *governo)
{
    struct estado_do_servidor_ublk servidor = {0};
    uint32_t quantidade_iniciada = 0;
    int resultado;
    int resultado_do_desmonte;

    if (governo == 0) return -EINVAL;
    (void)pthread_mutex_lock(&governo->exclusao);
    governo->contexto = &servidor;
    (void)pthread_mutex_unlock(&governo->exclusao);
    servidor.governo = governo;
    resultado = preparar_servidor_ublk(
        &servidor, configuracao, empregar_cuda);
    if (resultado < 0) return resultado;
    if (signal(SIGINT, ordenar_parada_do_servidor_ublk) == SIG_ERR ||
        signal(SIGTERM, ordenar_parada_do_servidor_ublk) == SIG_ERR) {
        desmontar_servidor_ublk(&servidor);
        return -errno;
    }
    resultado = iniciar_filas_ublk(
        &servidor, servidor.incumbencias, &quantidade_iniciada);
    resultado = julgar_filas_e_publicar(
        &servidor, quantidade_iniciada, resultado);
    if (resultado == 0) resultado = iniciar_observatorio_ublk(&servidor);
    if (resultado < 0) (void)ordenar_parada_limitada_do_servidor(&servidor);
    {
        int resultado_das_filas = recolher_filas_ublk(
            servidor.incumbencias, quantidade_iniciada);
        if (resultado == 0) resultado = resultado_das_filas;
    }
    {
        int resultado_do_observatorio = encerrar_observatorio_ublk(&servidor);
        if (resultado == 0) resultado = resultado_do_observatorio;
    }
    resultado_do_desmonte = desmontar_servidor_ublk(&servidor);
    if (resultado == 0) resultado = resultado_do_desmonte;
    (void)pthread_mutex_lock(&governo->exclusao);
    governo->contexto = 0;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return resultado;
}

/*
 * Proposito: servir a experiência ublk sobre RAM ordinária.
 * Pre-condições: configuração e governo válidos. Effeitos: serve por inteiro.
 * Retorno: zero ou primeira falha. Razão: conserva a prova sem GPU.
 */
int executar_servidor_ublk(
    const struct configuracao_do_apparelho *configuracao,
    struct governo_do_apparelho *governo)
{
    return executar_servidor_com_meio(configuracao, 0, governo);
}

/*
 * Proposito: servir o apparelho ublk sobre VRAM e DMA CUDA.
 * Pre-condições: configuração, governo, GPU e dependências válidas.
 * Effeitos: governa o serviço completo. Retorno: zero ou primeira falha.
 * Razão: esta entrada activa correntes e buffers fixados em cada fila.
 */
int executar_servidor_cuda(
    const struct configuracao_do_apparelho *configuracao,
    struct governo_do_apparelho *governo)
{
    return executar_servidor_com_meio(configuracao, 1, governo);
}
