#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../src/canal_de_governo.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/*
 * Proposito: demonstrar transporte integral por duas tomadas locaes ligadas.
 * Pre-condições: o systema fornece socketpair com SOCK_STREAM.
 * Effeitos: abre duas tomadas, transporta carga e restitue todas as posses.
 * Retorno: unidade na identidade e no termo, zero na primeira divergência.
 * Razão: o par local prova o canal sem nome, privilégio ou processo exterior.
 */
int provar_transporte_do_canal_de_governo(void)
{
    static const unsigned char carga[] = {3, 5, 7, 11, 13};
    struct mensagem_de_governo mensagem = {0};
    int tomadas[2] = {-1, -1};
    int resultado = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, tomadas) != 0) goto termo;
    if (enviar_mensagem_de_governo(
            tomadas[0], OPERACAO_DE_GOVERNO_CONTEMPLAR,
            carga, (uint32_t)sizeof(carga)) < 0 ||
        receber_mensagem_de_governo(tomadas[1], &mensagem) < 0) goto termo;
    if (mensagem.cabecalho.operacao != OPERACAO_DE_GOVERNO_CONTEMPLAR ||
        mensagem.cabecalho.quantidade_da_carga != sizeof(carga) ||
        mensagem.carga == 0 ||
        memcmp(mensagem.carga, carga, sizeof(carga)) != 0) goto termo;
    resultado = 1;

termo:
    destruir_mensagem_de_governo(&mensagem);
    destruir_mensagem_de_governo(&mensagem);
    if (mensagem.carga != 0 || mensagem.cabecalho.magia != 0) resultado = 0;
    if (tomadas[0] >= 0) close(tomadas[0]);
    if (tomadas[1] >= 0) close(tomadas[1]);
    return resultado;
}

/*
 * Proposito: provar que termo precoce não publica carga mutilada.
 * Pre-condições: o par local permitte encerrar somente a escripta.
 * Effeitos: envia cabeçalho, dois de quatro octetos e fecha a corrente.
 * Retorno: unidade na recusa sem estado parcial, zero no restante.
 * Razão: uma carga incompleta não poderá adquirir posse no destino.
 */
int provar_carga_truncada_no_canal(void)
{
    unsigned char envio[TAMANHO_DO_CABECALHO_DE_GOVERNO + 2U];
    struct mensagem_de_governo mensagem = {0};
    int tomadas[2] = {-1, -1};
    int resultado = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, tomadas) != 0) goto termo;
    if (escrever_cabecalho_de_governo(
            envio, sizeof(envio), OPERACAO_DE_GOVERNO_CREAR, 4) < 0)
        goto termo;
    envio[TAMANHO_DO_CABECALHO_DE_GOVERNO] = 17;
    envio[TAMANHO_DO_CABECALHO_DE_GOVERNO + 1U] = 19;
    if (send(tomadas[0], envio, sizeof(envio), 0) != (ssize_t)sizeof(envio) ||
        shutdown(tomadas[0], SHUT_WR) != 0) goto termo;
    if (receber_mensagem_de_governo(tomadas[1], &mensagem) != -ECONNRESET ||
        mensagem.carga != 0 || mensagem.cabecalho.magia != 0) goto termo;
    resultado = 1;

termo:
    destruir_mensagem_de_governo(&mensagem);
    if (tomadas[0] >= 0) close(tomadas[0]);
    if (tomadas[1] >= 0) close(tomadas[1]);
    return resultado;
}

static int provar_prazo_absoluto_da_carga(void)
{
    unsigned char cabecalho[TAMANHO_DO_CABECALHO_DE_GOVERNO];
    struct mensagem_de_governo mensagem = {0};
    struct timespec agora;
    int tomadas[2] = {-1, -1};
    int resultado = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, tomadas) != 0) goto termo;
    if (escrever_cabecalho_de_governo(
            cabecalho, sizeof(cabecalho), OPERACAO_DE_GOVERNO_CREAR, 4) < 0)
        goto termo;
    if (send(tomadas[0], cabecalho, sizeof(cabecalho), 0) !=
            (ssize_t)sizeof(cabecalho) || send(tomadas[0], "x", 1, 0) != 1)
        goto termo;
    if (clock_gettime(CLOCK_MONOTONIC, &agora) != 0) goto termo;
    if (receber_mensagem_de_governo_com_prazo(
            tomadas[1], &mensagem,
            (uint64_t)agora.tv_sec * 1000000000ULL + agora.tv_nsec +
                1000000ULL) != -ETIMEDOUT || mensagem.carga != 0) goto termo;
    resultado = 1;
termo:
    destruir_mensagem_de_governo(&mensagem);
    if (tomadas[0] >= 0) close(tomadas[0]);
    if (tomadas[1] >= 0) close(tomadas[1]);
    return resultado;
}

/*
 * Proposito: reunir as demonstrações portáteis do canal de governo.
 * Pre-condições: nenhuma tomada nomeada ou privilégio é necessário.
 * Effeitos: abre somente pares locaes efêmeros. Retorno: zero ou unidade.
 * Razão: uma receita singular denuncia a primeira lei contradita.
 */
int main(void)
{
    return provar_transporte_do_canal_de_governo() &&
           provar_carga_truncada_no_canal() &&
           provar_prazo_absoluto_da_carga() ? 0 : 1;
}
