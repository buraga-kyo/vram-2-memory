#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "servico_de_governo.h"
#include "canal_de_governo.h"
#include "ordens_da_instancia.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef PRAZO_DA_AUDIENCIA_EM_SEGUNDOS
#define PRAZO_DA_AUDIENCIA_EM_SEGUNDOS 5
#endif

/*
 * Proposito: atar prazo finito de recepção e envio á audiência de um cliente.
 * Pre-condições: descritor de cliente ligado. Effeitos: liga SO_RCVTIMEO e SO_SNDTIMEO.
 * Retorno: zero no êxito ou erro negativo do systema.
 * Razão: cliente que cala a meio caminho é cortado em tempo finito (slowloris).
 */
static int atar_prazos_da_audiencia(int cliente)
{
    struct timeval prazo = { .tv_sec = PRAZO_DA_AUDIENCIA_EM_SEGUNDOS, .tv_usec = 0 };
    if (setsockopt(cliente, SOL_SOCKET, SO_RCVTIMEO, &prazo, sizeof(prazo)) != 0)
        return -errno;
    if (setsockopt(cliente, SOL_SOCKET, SO_SNDTIMEO, &prazo, sizeof(prazo)) != 0)
        return -errno;
    return 0;
}

static uint64_t prazo_da_mensagem(void)
{
    struct timespec agora;
    if (clock_gettime(CLOCK_MONOTONIC, &agora) != 0) return 0;
    return (uint64_t)agora.tv_sec * 1000000000ULL + agora.tv_nsec +
           (uint64_t)PRAZO_DA_AUDIENCIA_EM_SEGUNDOS * 1000000000ULL;
}

/*
 * A sentença pura da política: root ou o próprio dono do daemon governam.
 */
int credencial_do_governo_e_aceita(uid_t uid_do_par, uid_t uid_esperado)
{
    return uid_do_par == 0 || uid_do_par == uid_esperado;
}

/*
 * Proposito: julgar a credencial do par ligado antes de qualquer ordem.
 * Pre-condições: descritor de cliente ligado e local. Effeitos: nenhum.
 * Retorno: 1 quando o par é aceito, 0 quando recusado ou insondável.
 * Razão: sem provar o uid, o modo 0660 do grupo seria o único porteiro.
 */
static int par_do_governo_e_aceito(int cliente)
{
    struct ucred credencial;
    socklen_t largura = sizeof(credencial);

    if (getsockopt(cliente, SOL_SOCKET, SO_PEERCRED, &credencial, &largura) != 0)
        return 0;
    return credencial_do_governo_e_aceita(credencial.uid, geteuid());
}

/*
 * THEOREMA DA AUDIENCIA SINGULAR
 * Proposito: receber e responder exactamente uma mensagem de um cliente.
 * Pre-condições: tomada em escuta, governo preparado e falha_irrecuperavel vivo.
 * Effeitos: adquire e restitue ligação e carga transitórias; publica em
 *   falha_irrecuperavel se o termo negativo foi interno (accept) ou do cliente.
 * Retorno: zero no percurso ou primeiro erro negativo.
 * Razão: só a falha do accept é interna; a falta do cliente não encerra o laço.
 */
int atender_cliente_do_governo(int tomada_servidora,
                               struct governo_do_apparelho *governo,
                               int *falha_irrecuperavel)
{
    struct mensagem_de_governo mensagem = {0};
    unsigned char resposta[256];
    uint32_t quantidade = 0;
    int cliente;
    int resultado;
    uint64_t prazo;

    if (falha_irrecuperavel != 0) *falha_irrecuperavel = 1;
    if (tomada_servidora < 0 || governo == 0) return -EINVAL;
    do {
        cliente = accept4(tomada_servidora, 0, 0, SOCK_CLOEXEC);
    } while (cliente < 0 && errno == EINTR);
    if (cliente < 0) return -errno;
    if (falha_irrecuperavel != 0) *falha_irrecuperavel = 0;
    if (!par_do_governo_e_aceito(cliente)) {
        (void)close(cliente);
        return 0;
    }
    resultado = atar_prazos_da_audiencia(cliente);
    prazo = prazo_da_mensagem();
    if (resultado == 0)
        resultado = receber_mensagem_de_governo_com_prazo(
            cliente, &mensagem, prazo);
    if (resultado == 0) resultado = cumprir_ordem_da_instancia(
        governo, &mensagem, resposta, sizeof(resposta), &quantidade);
    if (resultado == 0) resultado = enviar_mensagem_de_governo(
        cliente, mensagem.cabecalho.operacao, resposta, quantidade);
    if (resultado == -EAGAIN || resultado == -EWOULDBLOCK) resultado = 0;
    destruir_mensagem_de_governo(&mensagem);
    if (close(cliente) != 0 && resultado == 0) resultado = -errno;
    return resultado;
}

/*
 * COROLLARIO DAS AUDIENCIAS SUCCESSIVAS
 * Proposito: conceder audiências até o máximo, ou até a primeira falha
 *   irrecuperável (accept ou argumento); a falta do cliente é registada e segue.
 * Pre-condições: tomada em escuta e governo vivo; máximo zero não tem termo.
 * Effeitos: regista no stderr cada audiência fallida. Retorno: último resultado.
 * Razão: um só laço proprietário conhece a conta e o termo de toda audiência.
 */
int conceder_audiencias_do_governo(int tomada_servidora,
                                   struct governo_do_apparelho *governo,
                                   unsigned int maximo_de_audiencias)
{
    unsigned int audiencias = 0;
    int resultado;
    int falha_irrecuperavel = 0;

    do {
        resultado = atender_cliente_do_governo(
            tomada_servidora, governo, &falha_irrecuperavel);
        audiencias++;
        if (resultado < 0)
            fprintf(stderr, "A audiência fallou: %d.\n", resultado);
    } while (!falha_irrecuperavel && (maximo_de_audiencias == 0 ||
             audiencias < maximo_de_audiencias));
    return resultado;
}
