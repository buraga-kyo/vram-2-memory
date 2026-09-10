#define _POSIX_C_SOURCE 200809L

#include "alvo_ublk.h"

#include <errno.h>
#include <limits.h>
#include <linux/ublk_cmd.h>
#include <stdint.h>
#include <time.h>

/*
 * LEMMA DA SAUDE PUBLICADA
 * Proposito: julgar a sentença do meio e publicar eventual ruína geral.
 * Pre-condições: contexto ligado á saúde singular e á marca do servidor.
 * Effeitos: conserva a série local; sela o servidor ao oitavo erro ou fatal.
 * Retorno: resultado recebido, ou -EIO depois da sentença terminal.
 * Razão: -ENODEV distingue a morte do contexto CUDA das recusas ordinárias.
 */
static int julgar_resultado_do_meio(struct contexto_da_fila_ublk *contexto,
                                    int resultado)
{
    int terminal = registrar_resultado_na_saude_da_fila(
        &contexto->saude, resultado, resultado == -ENODEV);

    if (terminal <= 0) return resultado;
    atomic_store_explicit(contexto->falha_terminal_do_servidor, 1,
                          memory_order_relaxed);
    contexto->resultado_assincrono = -EIO;
    return -EIO;
}

/*
 * COROLARIO DA FRONTEIRA UBLK
 * Proposito: converter a descrição exterior em geometria comum.
 * Effeitos: nenhuma fila ou meio é tocado nesta passagem.
 * Retorno: zero ou domínio inválido.
 */
static int ler_geometria_da_requisicao_ublk(
    const struct ublksrv_io_desc *descritor, uint64_t *deslocamento,
    uint32_t *quantidade, uint8_t *operacao)
{
    if (descritor == 0 || deslocamento == 0 || quantidade == 0 ||
        operacao == 0 || descritor->nr_sectors > UINT32_MAX / 512U ||
        descritor->start_sector > UINT64_MAX / 512U) return -EINVAL;
    *quantidade = descritor->nr_sectors * 512U;
    *deslocamento = descritor->start_sector * 512ULL;
    *operacao = ublksrv_get_op(descritor);
    return 0;
}

/*
 * Proposito: entregar ao ublk a sentença colhida do evento da etiqueta.
 * Pre-condições: registro conserva contexto, origem e identidade vivos.
 * Effeitos: mede, conclue, rearma e registra a operação na própria fila.
 * Retorno: nenhum. Razão: a callback marcha no fio que possue a fila.
 */
static void concluir_transferencia_do_meio(void *argumento, int erro)
{
    struct registro_da_requisicao *registro = argumento;
    struct contexto_da_fila_ublk *contexto =
        registro->contexto_da_conclusao;
    const struct ublksrv_queue *fila_exterior =
        registro->origem_da_conclusao;
    uint64_t instante_final = ler_instante_monotonico();
    uint64_t latencia = instante_final >=
        registro->instante_inicial_em_nanossegundos ?
        instante_final - registro->instante_inicial_em_nanossegundos : 0;
    int resultado = julgar_resultado_do_meio(contexto, erro);
    int resultado_da_entrega;

    if (resultado == 0 &&
        (registro->operacao == UBLK_IO_OP_READ ||
         registro->operacao == UBLK_IO_OP_WRITE))
        resultado = (int)registro->quantidade_de_bytes;
    resultado_da_entrega = entregar_requisicao_ublk(
        contexto, fila_exterior, registro->etiqueta, resultado,
        instante_final);
    registrar_operacao_observada(
        contexto->contadores, registro->operacao != UBLK_IO_OP_READ,
        registro->quantidade_de_bytes, latencia,
        resultado_da_entrega < 0 ? resultado_da_entrega : resultado);
    if (resultado_da_entrega < 0 && contexto->resultado_assincrono == 0)
        contexto->resultado_assincrono = resultado_da_entrega;
}

/*
 * LEMMA DO INSTANTE MONOTONICO
 * Proposito: medir a marcha da requisição sem sujeição ao calendário civil.
 * Pre-condições: o systema fornece CLOCK_MONOTONIC.
 * Effeitos: consulta o relógio. Retorno: nanossegundos, ou zero na falha.
 * Razão: segundos são cercados antes do producto que os converte.
 */
uint64_t ler_instante_monotonico(void)
{
    struct timespec instante;

    if (clock_gettime(CLOCK_MONOTONIC, &instante) != 0 ||
        (uint64_t)instante.tv_sec > UINT64_MAX / 1000000000ULL) {
        return 0;
    }
    return (uint64_t)instante.tv_sec * 1000000000ULL +
           (uint64_t)instante.tv_nsec;
}

/*
 * THEOREMA DA PASSAGEM PELO CONTRACTO
 * Proposito: submetter uma operação ou reconhecer seu termo immediato.
 * Pre-condições: contexto íntegro e quantidade representável em int.
 * Effeitos: arma promessa por etiqueta sem consultar sua conclusão.
 * Retorno: zero na promessa, um no acto immediato ou erro sem promessa.
 * Razão: somente a colheita da fila poderá completar DMA acceito.
 */
static int transferir_pelo_contrato_do_meio(
    struct contexto_da_fila_ublk *contexto, uint8_t operacao, uint32_t etiqueta,
    uint64_t deslocamento, void *memoria, uint32_t quantidade_de_bytes)
{
    const struct operacoes_do_meio *operacoes = contexto->operacoes_do_meio;
    struct registro_da_requisicao *registro;
    int submissao;
    if (operacoes == 0 || contexto->contexto_do_meio == 0 ||
        contexto->fila == 0 || etiqueta >= contexto->fila->profundidade ||
        quantidade_de_bytes > INT_MAX) return -EINVAL;
    registro = &contexto->fila->registros[etiqueta];
    switch (operacao) {
    case UBLK_IO_OP_READ:
        submissao = operacoes->ler(
            contexto->contexto_do_meio, contexto->indice_da_fila, etiqueta,
            deslocamento, memoria, quantidade_de_bytes,
            concluir_transferencia_do_meio, registro);
        break;
    case UBLK_IO_OP_WRITE:
        submissao = operacoes->escrever(
            contexto->contexto_do_meio, contexto->indice_da_fila, etiqueta,
            deslocamento, memoria, quantidade_de_bytes,
            concluir_transferencia_do_meio, registro);
        break;
    case UBLK_IO_OP_DISCARD:
    case UBLK_IO_OP_WRITE_ZEROES:
        submissao = operacoes->zerar(
            contexto->contexto_do_meio, contexto->indice_da_fila, etiqueta,
            deslocamento, quantidade_de_bytes,
            concluir_transferencia_do_meio, registro);
        break;
    case UBLK_IO_OP_FLUSH:
        return 1;
    default:
        return -EOPNOTSUPP;
    }
    return submissao;
}

/*
 * THEOREMA DA TRANSFERENCIA UBLK
 * Proposito: traduzir a operação exterior para o meio da Casa.
 * Pre-condições: contexto e meio vivos; região arithmeticamente cercada.
 * Effeitos: submette DMA ou reconhece operação immediata.
 * Retorno: zero na promessa, um no acto immediato ou erro sem promessa.
 * Razão: toda operação material deixa a sentença á colheita proprietária.
 */
int transferir_requisicao_ublk(struct contexto_da_fila_ublk *contexto,
                               uint8_t operacao, uint32_t etiqueta,
                               uint64_t deslocamento, void *memoria,
                               uint32_t quantidade_de_bytes)
{
    if (contexto == 0 || quantidade_de_bytes > INT_MAX ||
        contexto->operacoes_do_meio == 0 ||
        contexto->contexto_do_meio == 0) return -EINVAL;
    return transferir_pelo_contrato_do_meio(
        contexto, operacao, etiqueta, deslocamento, memoria,
        quantidade_de_bytes);
}

/*
 * THEOREMA DA ENTREGA TEMPESTIVA
 * Proposito: concluir dentro do prazo ou fallir e parar o apparelho.
 * Pre-condições: etiqueta transferindo e instante final monotónico.
 * Effeitos: entrega ao núcleo; rearma no êxito ou torna a falha terminal.
 * Retorno: resultado exterior ou -ETIMEDOUT quando o prazo se consome.
 * Razão: uma fila vencida não poderá receber trabalho novo honestamente.
 */
int entregar_requisicao_ublk(struct contexto_da_fila_ublk *contexto,
                             const struct ublksrv_queue *fila_exterior,
                             uint32_t etiqueta, int resultado,
                             uint64_t instante_final)
{
    int resultado_da_entrega;

    if (contexto == 0 || fila_exterior == 0) return -EINVAL;
    if (falhar_requisicao_vencida(
            contexto->fila, etiqueta, instante_final,
            contexto->prazo_em_nanossegundos, -ETIMEDOUT)) {
        ublksrv_complete_io(fila_exterior, etiqueta, -ETIMEDOUT);
        return -ETIMEDOUT;
    }
    if (!concluir_requisicao_na_fila(
            contexto->fila, etiqueta, resultado)) return -EIO;
    resultado_da_entrega = ublksrv_complete_io(
        fila_exterior, etiqueta, resultado);
    if (resultado_da_entrega >= 0 && !atomic_load_explicit(
            contexto->falha_terminal_do_servidor, memory_order_relaxed) &&
        !rearmar_requisicao_na_fila(contexto->fila, etiqueta)) return -EIO;
    return resultado_da_entrega;
}

/*
 * THEOREMA DA PASSAGEM UBLK
 * Proposito: possuir, transportar e entregar uma requisição exterior.
 * Pre-condições: fila e dados pertencem ao mesmo contexto publicado.
 * Effeitos: conclue a etiqueta uma vez e só então a restitue.
 * Retorno: zero na entrega ou erro negativo antes de nova busca.
 * Razão: sectores são cercados antes da conversão para octetos.
 */
int tratar_requisicao_ublk(const struct ublksrv_queue *fila_exterior,
                           const struct ublk_io_data *dados)
{
    struct contexto_da_fila_ublk *contexto;
    const struct ublksrv_io_desc *descritor;
    struct registro_da_requisicao *registro;
    uint64_t deslocamento;
    uint32_t quantidade;
    uint8_t operacao;
    void *memoria;
    int submissao;

    if (fila_exterior == 0 || dados == 0 || dados->iod == 0) return -EINVAL;
    contexto = fila_exterior->private_data;
    descritor = dados->iod;
    if (contexto == 0 || contexto->fila == 0 || dados->tag < 0) return -EINVAL;
    if (atomic_load_explicit(contexto->falha_terminal_do_servidor,
                             memory_order_relaxed)) {
        (void)ublksrv_complete_io(fila_exterior, (unsigned)dados->tag, -EIO);
        return -EIO;
    }
    if (ler_geometria_da_requisicao_ublk(
            descritor, &deslocamento, &quantidade, &operacao) < 0)
        return -EINVAL;
    memoria = ublksrv_queue_get_io_buf(fila_exterior, dados->tag);
    if (quantidade != 0 && memoria == 0) return -EFAULT;
    registro = iniciar_requisicao_na_fila(
        contexto->fila, (uint32_t)dados->tag, deslocamento, quantidade,
        operacao, memoria, ler_instante_monotonico());
    if (registro == 0) return -EBUSY;
    registro->contexto_da_conclusao = contexto;
    registro->origem_da_conclusao = fila_exterior;
    submissao = transferir_requisicao_ublk(
        contexto, operacao, (uint32_t)dados->tag, deslocamento, memoria,
        quantidade);
    if (submissao != 0)
        concluir_transferencia_do_meio(registro, submissao < 0 ? submissao : 0);
    return contexto->resultado_assincrono;
}
