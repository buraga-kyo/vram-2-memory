#include "configuracao.h"
/*
 * THEOREMA DOS LIMITES DA CONFIGURACAO
 *
 * Proposito: julgar se as grandezas formam um apparelho realizavel.
 * Pre-condição: nenhuma; o ponteiro nulo pertence ao domínio recusado.
 * Effeitos: nenhum estado ou memória é alterado.
 * Retorno: unidade para a figura válida e zero para toda impossibilidade.
 * Razão: cada producto é cercado antes de ser calculado, de sorte que o
 * infinito arithmetico jámais transborde o vaso finito de sessenta e quatro
 * algarismos binários.
 */
int configuracao_do_apparelho_e_valida(
    const struct configuracao_do_apparelho *configuracao)
{
    uint64_t quantidade_de_operacoes;
    /* Sem manuscripto não ha grandeza que se possa honestamente julgar. */
    if (configuracao == 0) {
        return 0;
    }

    /* Toda medida discreta deve ser positiva antes de entrar no calculo. */
    if (configuracao->indice_da_gpu < 0 ||
        configuracao->capacidade_em_bytes == 0 ||
        configuracao->quantidade_de_filas <= 0 ||
        configuracao->profundidade_das_filas <= 0 ||
        (configuracao->consentir_margem_da_vram != 0 &&
         configuracao->consentir_margem_da_vram != 1) ||
        configuracao->maior_operacao_em_bytes == 0 ||
        configuracao->prazo_da_operacao_em_milissegundos == 0) {
        return 0;
    }

    /* Capacidade e operação hão de coincidir com a pedra fundamental. */
    if (configuracao->capacidade_em_bytes % TAMANHO_DO_BLOCO_EM_BYTES != 0 ||
        configuracao->maior_operacao_em_bytes % TAMANHO_DO_BLOCO_EM_BYTES != 0 ||
        configuracao->maior_operacao_em_bytes > configuracao->capacidade_em_bytes) {
        return 0;
    }

    /* Cerque-se o primeiro producto antes que elle adquira existência. */
    if ((uint64_t)configuracao->quantidade_de_filas >
        UINT64_MAX / (uint64_t)configuracao->profundidade_das_filas) {
        return 0;
    }
    quantidade_de_operacoes = (uint64_t)configuracao->quantidade_de_filas *
                              (uint64_t)configuracao->profundidade_das_filas;

    /* Cada operação reclama um quinhão finito de memória intermediária. */
    return quantidade_de_operacoes <=
           UINT64_MAX / configuracao->maior_operacao_em_bytes;
}
