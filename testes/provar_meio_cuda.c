#include "../src/meio_cuda.h"
#include "../src/reserva_de_buffers.h"

#include <errno.h>

/* Conserva quantas sentenças CUDA chegaram e qual foi a derradeira. */
struct testemunho_da_conclusao_cuda {
    int quantidade;
    int erro;
    int ordem[4];
};

/* Liga uma marca de etiqueta ao testemunho commum da experiência. */
struct incumbencia_da_conclusao_cuda {
    struct testemunho_da_conclusao_cuda *testemunho;
    int marca;
};

/*
 * Proposito: registrar cada conclusão entregue pela taboa CUDA.
 * Pre-condições: argumento aponta para testemunho vivo.
 * Effeitos: soma uma sentença e conserva seu erro.
 * Retorno: nenhum. Razão: contar torna visível omissão ou duplicação.
 */
void testemunhar_conclusao_cuda(void *argumento, int erro)
{
    struct incumbencia_da_conclusao_cuda *incumbencia = argumento;
    struct testemunho_da_conclusao_cuda *testemunho =
        incumbencia->testemunho;

    if (testemunho->quantidade < 4)
        testemunho->ordem[testemunho->quantidade] = incumbencia->marca;
    testemunho->quantidade++;
    testemunho->erro = erro;
}

/*
 * Proposito: provar a promessa differida da taboa CUDA em GPU real.
 * Pre-condições: GPU zero e 4096 octetos de VRAM disponíveis.
 * Effeitos: reserva contexto e memória fixada durante a experiência.
 * Retorno: unidade na convergência ou zero na primeira contradicção.
 * Razão: a execução material deve honrar a mesma cardinalidade do simulador.
 */
int provar_contrato_assincrono_cuda(void)
{
    struct configuracao_do_apparelho configuracao = {0};
    struct testemunho_da_conclusao_cuda testemunho = {0};
    struct incumbencia_da_conclusao_cuda primeira = {&testemunho, 1};
    struct incumbencia_da_conclusao_cuda segunda = {&testemunho, 0};
    struct incumbencia_da_conclusao_cuda terceira = {&testemunho, 2};
    struct reserva_de_buffers reserva = {0};
    const struct operacoes_do_meio *operacoes = obter_operacoes_do_meio_cuda();
    unsigned char *memoria = 0;
    void *contexto = 0;
    int indice;
    int memoria_registrada = 0;
    int resultado = 0;
    int tentativas = 0;

    configuracao.indice_da_gpu = 0;
    configuracao.capacidade_em_bytes = 4096;
    configuracao.quantidade_de_filas = 2;
    configuracao.profundidade_das_filas = 2;
    if (operacoes == 0 ||
        operacoes->preparar(&contexto, &configuracao) < 0 ||
        criar_reserva_de_buffers(&reserva, 2, 2, 4096) < 0 ||
        !registrar_memoria_intermediaria_cuda(
            reserva.inicio, (size_t)reserva.quantidade_em_bytes)) goto termo;
    memoria_registrada = 1;
    for (indice = 0; indice < 2; indice++) {
        memoria = achar_buffer_reservado(&reserva, indice, 0, 4096);
        if (operacoes->vincular_fila(contexto, indice) < 0 ||
            operacoes->aquecer_fila(
                contexto, indice, memoria, 4096) < 0) goto termo;
    }
    memoria = achar_buffer_reservado(&reserva, 1, 1, 4096);
    memoria[0] = 29;
    if (operacoes->escrever(contexto, 1, 1, 0, memoria, 1,
                            testemunhar_conclusao_cuda, &primeira) < 0 ||
        operacoes->zerar(contexto, 1, 0, 0, 1,
                         testemunhar_conclusao_cuda, &segunda) < 0 ||
        operacoes->escrever(contexto, 1, 1, 0, memoria, 1,
                            testemunhar_conclusao_cuda, &primeira) != -EBUSY ||
        injectar_consulta_do_evento_cuda(
            contexto, 1, 1, CUDA_ERROR_NOT_READY) < 0 ||
        operacoes->colher(contexto, 1, 2) != 0 ||
        testemunho.quantidade != 0) goto termo;
    while (testemunho.quantidade < 2 && tentativas < 1000000) {
        if (operacoes->colher(contexto, 1, 2) < 0) goto termo;
        tentativas++;
    }
    if (testemunho.quantidade != 2 || testemunho.erro != 0 ||
        testemunho.ordem[0] != 1 || testemunho.ordem[1] != 0 ||
        operacoes->escrever(contexto, 1, 2, 0, memoria, 1,
                            testemunhar_conclusao_cuda, &primeira) >= 0 ||
        operacoes->colher(contexto, 1, 1) != 0) goto termo;
    memoria = achar_buffer_reservado(&reserva, 1, 0, 4096);
    if (injectar_erro_da_submissao_cuda(contexto, 1, 0) < 0 ||
        operacoes->escrever(contexto, 1, 0, 0, memoria, 1,
                            testemunhar_conclusao_cuda, &terceira) != -EIO ||
        testemunho.quantidade != 2 ||
        operacoes->escrever(contexto, 1, 0, 0, memoria, 1,
                            testemunhar_conclusao_cuda, &terceira) < 0 ||
        injectar_consulta_do_evento_cuda(
            contexto, 1, 0, CUDA_ERROR_UNKNOWN) < 0 ||
        operacoes->colher(contexto, 1, 1) != 1 ||
        testemunho.quantidade != 3 || testemunho.erro != -ENODEV ||
        testemunho.ordem[2] != 2 ||
        operacoes->colher(contexto, 1, 1) != 0 ||
        injectar_consulta_do_evento_cuda(
            contexto, 1, 0, CUDA_SUCCESS) >= 0) goto termo;
    resultado = 1;

termo:
    if (memoria_registrada &&
        !desregistrar_memoria_intermediaria_cuda(reserva.inicio)) resultado = 0;
    destruir_reserva_de_buffers(&reserva);
    if (contexto != 0) operacoes->destruir(contexto);
    return resultado;
}

/*
 * PROVA DA TRAVESSIA CUDA
 * Proposito: confrontar escripta e leitura byte a byte por memória fixada.
 * Pre-condições: GPU de índice zero e 8192 octetos de VRAM disponíveis.
 * Effeitos: reserva VRAM, corrente e dois buffers CPU fixados.
 * Retorno: zero na convergência e unidade na primeira divergência.
 * Razão: padrões distinctos denunciam omissão, troca ou invasão no DMA.
 */
int main(void)
{
    struct meio_cuda meio = {0};
    struct transportador_cuda transportador = {0};
    struct reserva_de_buffers reserva = {0};
    unsigned char *origem = 0;
    unsigned char *destino = 0;
    int memoria_registrada = 0;
    int resultado = 1;

    if (!criar_meio_cuda(&meio, 0, 8192, 1) ||
        !criar_transportador_cuda(&transportador, &meio)) goto termo;
    if (criar_reserva_de_buffers(&reserva, 1, 2, 4096) < 0 ||
        !registrar_memoria_intermediaria_cuda(
            reserva.inicio, (size_t)reserva.quantidade_em_bytes)) goto termo;
    memoria_registrada = 1;
    origem = achar_buffer_reservado(&reserva, 0, 0, 4096);
    destino = achar_buffer_reservado(&reserva, 0, 1, 4096);
    for (unsigned int indice = 0; indice < 4096; indice++) {
        origem[indice] = (unsigned char)(indice * 29U + 7U);
        destino[indice] = 0;
    }
    if (!escrever_meio_cuda(&transportador, 2048, origem, 4096) ||
        !ler_meio_cuda(&transportador, 2048, destino, 4096)) goto termo;
    for (unsigned int indice = 0; indice < 4096; indice++) {
        if (origem[indice] != destino[indice]) goto termo;
    }
    if (!zerar_meio_cuda(&transportador, 2048, 4096) ||
        !ler_meio_cuda(&transportador, 2048, destino, 4096)) goto termo;
    for (unsigned int indice = 0; indice < 4096; indice++) {
        if (destino[indice] != 0) goto termo;
    }
    if (ler_meio_cuda(&transportador, 4097, destino, 4096) ||
        escrever_meio_cuda(&transportador, 8192, origem, 1)) goto termo;
    resultado = 0;

termo:
    if (memoria_registrada &&
        !desregistrar_memoria_intermediaria_cuda(reserva.inicio)) resultado = 1;
    destruir_reserva_de_buffers(&reserva);
    if (!destruir_transportador_cuda(&transportador)) resultado = 1;
    if (!destruir_meio_cuda(&meio)) resultado = 1;
    if (resultado == 0 && !provar_contrato_assincrono_cuda()) resultado = 1;
    return resultado;
}
