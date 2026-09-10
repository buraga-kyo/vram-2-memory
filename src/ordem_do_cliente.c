#include "ordem_do_cliente.h"
#include "configuracao_decimal.h"
#include "numero_decimal.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

/*
 * THEOREMA DA ORDEM TEXTUAL
 * Proposito: converter argumentos clientes numa ordem canônica indivisível.
 * Pre-condições: destino vivo e operação seguida do índice.
 * Effeitos: publica a figura somente depois de todas as provas.
 * Retorno: zero ou erro negativo de texto, largura, domínio ou operação.
 * Razão: a tomada jámais recebe interpretação parcial da linha de comando.
 */
int formar_ordem_do_cliente(struct ordem_do_cliente *destino,
                            int quantidade, char *argumentos[])
{
    struct ordem_do_cliente figura = {0};
    struct configuracao_do_apparelho configuracao;
    uint64_t indice;
    int resultado;

    if (destino == 0 || argumentos == 0 || quantidade < 2) return -EINVAL;
    if (!converter_numero_decimal(argumentos[1], &indice) || indice > UINT_MAX)
        return -ERANGE;
    figura.indice = (unsigned int)indice;
    if (strcmp(argumentos[0], "create") == 0) {
        if (quantidade < 7 || quantidade > 9) return -EINVAL;
        resultado = ler_configuracao_decimal(
            &configuracao, quantidade - 2, argumentos + 2);
        if (resultado < 0) return resultado;
        resultado = escrever_carga_de_creacao(
            figura.carga, sizeof(figura.carga), &configuracao);
        if (resultado < 0) return resultado;
        figura.operacao = OPERACAO_DE_GOVERNO_CREAR;
        figura.quantidade_da_carga = TAMANHO_DA_CARGA_DE_CREACAO;
    } else if (strcmp(argumentos[0], "status") == 0) {
        if (quantidade != 2) return -EINVAL;
        figura.operacao = OPERACAO_DE_GOVERNO_CONTEMPLAR;
    } else if (strcmp(argumentos[0], "destroy") == 0) {
        if (quantidade != 2) return -EINVAL;
        figura.operacao = OPERACAO_DE_GOVERNO_DESTRUIR;
    } else return -EOPNOTSUPP;
    *destino = figura;
    return 0;
}
