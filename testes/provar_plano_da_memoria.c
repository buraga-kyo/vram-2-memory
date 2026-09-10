#include "../src/plano_da_memoria.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>

/*
 * Proposito: provar medida exacta, domínio recusado e producto transbordado.
 * Pre-condições: uint64_t representa a grandeza pública do plano.
 * Effeitos: nenhum além da variável que recebe cada sentença.
 * Retorno: zero na convergência ou unidade na primeira contradicção.
 * Razão: o infinito e o zero não poderão disfarçar-se de reserva pequena.
 */
int main(void)
{
    struct configuracao_do_apparelho configuracao = {
        0, 65536, 2, 4, 8192, 1000, 0
    };
    uint64_t quantidade = 17;

    if (calcular_memoria_intermediaria(&configuracao, &quantidade) != 0 ||
        quantidade != 65536 ||
        conferir_limite_da_memoria_intermediaria(
            &configuracao, 65536, &quantidade) != 0 ||
        conferir_limite_da_memoria_intermediaria(
            &configuracao, 65535, &quantidade) != -ENOMEM ||
        quantidade != 65536 ||
        calcular_memoria_intermediaria(0, &quantidade) != -EINVAL ||
        calcular_memoria_intermediaria(&configuracao, 0) != -EINVAL)
        return 1;
    configuracao.quantidade_de_filas = 0;
    if (calcular_memoria_intermediaria(&configuracao, &quantidade) != -EINVAL)
        return 1;
    configuracao.quantidade_de_filas = INT_MAX;
    configuracao.profundidade_das_filas = INT_MAX;
    configuracao.maior_operacao_em_bytes = UINT_MAX;
    quantidade = 29;
    return calcular_memoria_intermediaria(&configuracao, &quantidade) !=
               -EOVERFLOW || quantidade != 29;
}
