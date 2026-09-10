#include "../src/configuracao.h"
#include <limits.h>
/*
 * LEMMA DA RECUSA SINGULAR
 * Proposito: alterar uma grandeza e exigir que a sentença a recuse.
 * Pre-condição: a figura recebida já contém a hypothese a examinar.
 * Effeitos: nenhum. Retorno: zero quando a recusa foi demonstrada.
 * Razão: cada chamada conserva isolada a causa de sua impossibilidade.
 */
static int provar_recusa(struct configuracao_do_apparelho configuracao)
{
    return configuracao_do_apparelho_e_valida(&configuracao) ? 1 : 0;
}
/*
 * PROVA DOS LIMITES DA CONFIGURACAO
 * Proposito: confrontar a figura válida, o vazio e cada fronteira material.
 * Pre-condição: o contracto público conserva o bloco de 4096 octetos.
 * Effeitos: nenhum. Retorno: zero se todas as proposições concordarem.
 * Razão: uma cópia nova impede que uma deformação contamine a seguinte.
 */
int main(void)
{
    const struct configuracao_do_apparelho valida = {
        0, 16U * TAMANHO_DO_BLOCO_EM_BYTES, 2, 4,
        2U * TAMANHO_DO_BLOCO_EM_BYTES, 1000, 0
    };
    struct configuracao_do_apparelho figura;
    if (!configuracao_do_apparelho_e_valida(&valida) ||
        configuracao_do_apparelho_e_valida(0)) return 1;
    figura = valida; figura.indice_da_gpu = -1;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.capacidade_em_bytes = 0;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.quantidade_de_filas = 0;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.profundidade_das_filas = 0;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.maior_operacao_em_bytes = 0;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.prazo_da_operacao_em_milissegundos = 0;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.capacidade_em_bytes++;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.maior_operacao_em_bytes++;
    if (provar_recusa(figura)) return 1;
    figura = valida; figura.maior_operacao_em_bytes = UINT_MAX - 4095U;
    figura.quantidade_de_filas = INT_MAX;
    figura.profundidade_das_filas = INT_MAX;
    return provar_recusa(figura);
}
