#ifndef CARGA_DE_CREACAO_H
#define CARGA_DE_CREACAO_H

#include "configuracao.h"

#include <stddef.h>

#define TAMANHO_DA_CARGA_DE_CREACAO 32U

/*
 * Proposito: escrever uma configuração na carga canônica da ordem create.
 * Pre-condições: destino declara capacidade e configuração completa.
 * Effeitos: grava vinte e oito octetos em ordem maior primeiro.
 * Retorno: zero no êxito ou erro negativo sem escripta parcial.
 * Razão: o cliente jámais transmite a representação nativa da estructura.
 */
int escrever_carga_de_creacao(unsigned char *destino, size_t capacidade,
                              const struct configuracao_do_apparelho *figura);

/*
 * Proposito: ler a configuração canônica recebida pela ordem create.
 * Pre-condições: quantidade mede exactamente a carga disponível.
 * Effeitos: publica somente uma configuração materialmente válida.
 * Retorno: zero no êxito ou erro negativo sem figura parcial.
 * Razão: o servidor julga largura e domínio antes de tocar CUDA ou ublk.
 */
int ler_carga_de_creacao(struct configuracao_do_apparelho *destino,
                         const unsigned char *origem, size_t quantidade);

#endif
