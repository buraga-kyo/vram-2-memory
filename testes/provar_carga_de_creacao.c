#include "../src/carga_de_creacao.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

/*
 * Proposito: provar octetos canônicos, percurso inverso e tamanho exacto.
 * Pre-condições: nenhuma. Effeitos: termina ao primeiro desvio.
 * Retorno: zero no êxito. Razão: create não pode depender da machina nativa.
 */
int main(void)
{
    const struct configuracao_do_apparelho original = {
        1, UINT64_C(65536), 2, 4, UINT32_C(8192), UINT32_C(1000), 0
    };
    const unsigned char esperado[TAMANHO_DA_CARGA_DE_CREACAO] = {
        0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2,
        0, 0, 0, 4, 0, 0, 32, 0, 0, 0, 3, 232, 0, 0, 0, 0
    };
    unsigned char carga[TAMANHO_DA_CARGA_DE_CREACAO];
    struct configuracao_do_apparelho recebida = {0};

    assert(escrever_carga_de_creacao(carga, sizeof(carga), &original) == 0);
    assert(memcmp(carga, esperado, sizeof(carga)) == 0);
    assert(ler_carga_de_creacao(&recebida, carga, sizeof(carga)) == 0);
    assert(recebida.indice_da_gpu == original.indice_da_gpu);
    assert(recebida.capacidade_em_bytes == original.capacidade_em_bytes);
    assert(recebida.quantidade_de_filas == original.quantidade_de_filas);
    assert(recebida.profundidade_das_filas == original.profundidade_das_filas);
    assert(recebida.maior_operacao_em_bytes == original.maior_operacao_em_bytes);
    assert(recebida.prazo_da_operacao_em_milissegundos ==
           original.prazo_da_operacao_em_milissegundos);
    assert(ler_carga_de_creacao(&recebida, carga, sizeof(carga) - 1) ==
           -EMSGSIZE);
    memset(carga + 24, 0, 4);
    assert(ler_carga_de_creacao(&recebida, carga, sizeof(carga)) == -EINVAL);
    return 0;
}
