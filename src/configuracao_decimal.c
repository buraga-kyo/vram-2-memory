#include "configuracao_decimal.h"
#include "numero_decimal.h"

#include <errno.h>
#include <limits.h>

/*
 * THEOREMA DA CONFIGURACAO DECIMAL
 * Proposito: converter e estreitar os textos que configuram o apparelho.
 * Pre-condições: destino vivo e cinco ou seis argumentos.
 * Effeitos: publica somente figura integralmente válida.
 * Retorno: zero, -EINVAL no texto ou -ERANGE na largura.
 * Razão: uma figura local impede publicação parcial durante a travessia.
 */
int ler_configuracao_decimal(struct configuracao_do_apparelho *destino,
                             int quantidade, char *argumentos[])
{
    struct configuracao_do_apparelho figura = {0};
    uint64_t numeros[6] = {0};
    int indice;

    if (destino == 0 || argumentos == 0 ||
        (quantidade < 5 || quantidade > 7)) return -EINVAL;
    for (indice = 0; indice < quantidade; indice++) {
        if (!converter_numero_decimal(argumentos[indice], &numeros[indice]))
            return -EINVAL;
    }
    if (numeros[1] > INT_MAX || numeros[2] > INT_MAX ||
        numeros[3] > UINT32_MAX || numeros[4] > UINT32_MAX ||
        numeros[5] > INT_MAX) return -ERANGE;
    figura.capacidade_em_bytes = numeros[0];
    figura.quantidade_de_filas = (int)numeros[1];
    figura.profundidade_das_filas = (int)numeros[2];
    figura.maior_operacao_em_bytes = (uint32_t)numeros[3];
    figura.prazo_da_operacao_em_milissegundos = (uint32_t)numeros[4];
    figura.indice_da_gpu = (int)numeros[5];
    figura.consentir_margem_da_vram = quantidade == 7 ? (int)numeros[6] : 0;
    if (!configuracao_do_apparelho_e_valida(&figura)) return -EINVAL;
    *destino = figura;
    return 0;
}
