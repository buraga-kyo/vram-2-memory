#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "canal_de_governo.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

static int esperar_leitura(int descritor, uint64_t prazo)
{
    struct timespec agora;
    struct pollfd espera = { .fd = descritor, .events = POLLIN };
    uint64_t restante;
    int milissegundos;
    if (prazo == 0) return 0;
    if (clock_gettime(CLOCK_MONOTONIC, &agora) != 0) return -errno;
    restante = (uint64_t)agora.tv_sec * 1000000000ULL +
               (uint64_t)agora.tv_nsec;
    if (restante >= prazo) return -ETIMEDOUT;
    restante = prazo - restante;
    milissegundos = (int)(restante / 1000000ULL);
    if (milissegundos == 0) milissegundos = 1;
    while (poll(&espera, 1, milissegundos) < 0 && errno == EINTR) {}
    if (espera.revents == 0) return -ETIMEDOUT;
    if (espera.revents & (POLLERR | POLLNVAL)) return -EIO;
    return 0;
}

/*
 * Proposito: escrever exactamente a extensão promettida numa tomada.
 * Pre-condições: descritor e memória vivos. Effeitos: avança a tomada.
 * Retorno: zero no êxito ou erro negativo na primeira ruptura.
 * Razão: escriptas parciaes e EINTR não mutilam a mensagem silenciosamente.
 */
static int escrever_todos_os_octetos(int descritor,
                                     const unsigned char *origem,
                                     size_t quantidade)
{
    size_t escriptos = 0;
    while (escriptos < quantidade) {
        ssize_t parcela = send(descritor, origem + escriptos,
                               quantidade - escriptos, MSG_NOSIGNAL);
        if (parcela < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (parcela == 0) return -EPIPE;
        escriptos += (size_t)parcela;
    }
    return 0;
}

/*
 * Proposito: enviar cabeçalho e carga sob a ordem canônica do protocolo.
 * Pre-condições: descritor ligado e carga viva quando positiva.
 * Effeitos: escreve duas parcelas sem signal SIGPIPE. Retorno: zero ou erro.
 * Razão: o cabeçalho sempre antecede os octetos cuja extensão declara.
 */
int enviar_mensagem_de_governo(int descritor, uint16_t operacao,
                               const void *carga, uint32_t quantidade)
{
    unsigned char cabecalho[TAMANHO_DO_CABECALHO_DE_GOVERNO];
    int resultado;

    if (quantidade != 0 && carga == 0) return -EINVAL;
    resultado = escrever_cabecalho_de_governo(
        cabecalho, sizeof(cabecalho), operacao, quantidade);
    if (resultado < 0) return resultado;
    resultado = escrever_todos_os_octetos(
        descritor, cabecalho, sizeof(cabecalho));
    if (resultado < 0 || quantidade == 0) return resultado;
    return escrever_todos_os_octetos(descritor, carga, quantidade);
}

/*
 * Proposito: receber cabeçalho e carga exactos de uma tomada ligada.
 * Pre-condições: destino sem carga possuída e descritor bloqueante.
 * Effeitos: adquire carga somente após validar o cabeçalho. Retorno: zero ou erro.
 * Razão: cada laço conhece a fronteira antes de pedir o próximo octeto.
 */
int receber_mensagem_de_governo_com_prazo(
    int descritor, struct mensagem_de_governo *destino,
    uint64_t prazo_absoluto_em_nanossegundos)
{
    unsigned char octetos[TAMANHO_DO_CABECALHO_DE_GOVERNO];
    struct cabecalho_de_governo cabecalho;
    unsigned char *carga = 0;
    size_t recebidos = 0;
    int resultado;
    if (descritor < 0 || destino == 0 || destino->carga != 0) return -EINVAL;
    while (recebidos < sizeof(octetos)) {
        resultado = esperar_leitura(descritor, prazo_absoluto_em_nanossegundos);
        if (resultado < 0) return resultado;
        ssize_t parcela = recv(descritor, octetos + recebidos,
                               sizeof(octetos) - recebidos, 0);
        if (parcela < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (parcela == 0) return -ECONNRESET;
        recebidos += (size_t)parcela;
    }
    resultado = ler_cabecalho_de_governo(&cabecalho, octetos, sizeof(octetos));
    if (resultado < 0) return resultado;
    if (cabecalho.quantidade_da_carga != 0) {
        carga = malloc((size_t)cabecalho.quantidade_da_carga);
        if (carga == 0) return -ENOMEM;
        recebidos = 0;
        while (recebidos < cabecalho.quantidade_da_carga) {
            resultado = esperar_leitura(descritor, prazo_absoluto_em_nanossegundos);
            if (resultado < 0) { free(carga); return resultado; }
            ssize_t parcela = recv(
                descritor, carga + recebidos,
                (size_t)cabecalho.quantidade_da_carga - recebidos, 0);
            if (parcela < 0 && errno == EINTR) continue;
            if (parcela <= 0) {
                resultado = parcela == 0 ? -ECONNRESET : -errno;
                free(carga);
                return resultado;
            }
            recebidos += (size_t)parcela;
        }
    }
    destino->cabecalho = cabecalho;
    destino->carga = carga;
    return 0;
}

int receber_mensagem_de_governo(int descritor,
                                struct mensagem_de_governo *destino)
{
    return receber_mensagem_de_governo_com_prazo(descritor, destino, 0);
}

/*
 * Proposito: restituir a carga e reduzir a mensagem á figura vazia.
 * Pre-condições: nenhuma; mensagem nula é termo regular.
 * Effeitos: liberta a carga e apaga todas as grandezas.
 * Retorno: nenhum. Razão: o zero torna segura a restituição repetida.
 */
void destruir_mensagem_de_governo(struct mensagem_de_governo *mensagem)
{
    if (mensagem == 0) return;
    free(mensagem->carga);
    mensagem->carga = 0;
    mensagem->cabecalho.magia = 0;
    mensagem->cabecalho.versao = 0;
    mensagem->cabecalho.operacao = 0;
    mensagem->cabecalho.quantidade_da_carga = 0;
}
