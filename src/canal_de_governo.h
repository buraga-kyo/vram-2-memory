#ifndef CANAL_DE_GOVERNO_H
#define CANAL_DE_GOVERNO_H

#include "protocolo_de_governo.h"

#include <stdint.h>

/* A mensagem recebida possue a carga até sua restituição explícita. */
struct mensagem_de_governo {
    struct cabecalho_de_governo cabecalho;
    unsigned char *carga;
};

/*
 * Proposito: enviar cabeçalho e carga integralmente por um descritor ligado.
 * Pre-condições: descritor vivo; carga não nula quando sua extensão é positiva.
 * Effeitos: escreve a mensagem ou encerra na primeira falha.
 * Retorno: zero no êxito ou erro negativo do protocolo ou do systema.
 * Razão: laços internos absorvem interrupções e escriptas parciaes.
 */
int enviar_mensagem_de_governo(int descritor, uint16_t operacao,
                               const void *carga, uint32_t quantidade);

/*
 * Proposito: receber e julgar uma mensagem inteira de um descritor ligado.
 * Pre-condições: destino vazio e descritor vivo em modo bloqueante.
 * Effeitos: adquire a carga exacta somente após validar o cabeçalho.
 * Retorno: zero no êxito, ou erro negativo sem estado parcial.
 * Razão: nenhum octeto futuro é lido antes de sua extensão ser conhecida.
 */
int receber_mensagem_de_governo(int descritor,
                                struct mensagem_de_governo *destino);

int receber_mensagem_de_governo_com_prazo(
    int descritor, struct mensagem_de_governo *destino,
    uint64_t prazo_absoluto_em_nanossegundos);

/*
 * Proposito: restituir a carga e reduzir a mensagem á figura vazia.
 * Pre-condições: nenhuma; destino nulo ou vazio produz termo regular.
 * Effeitos: liberta a carga e apaga o cabeçalho.
 * Retorno: nenhum. Razão: o zero torna idempotente a restituição.
 */
void destruir_mensagem_de_governo(struct mensagem_de_governo *mensagem);

#endif
