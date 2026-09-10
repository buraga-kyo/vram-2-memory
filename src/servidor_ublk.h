#ifndef SERVIDOR_UBLK_H
#define SERVIDOR_UBLK_H

#include "configuracao.h"
#include "governo_do_apparelho.h"

/*
 * Proposito: publicar e servir um dispositivo ublk sobre o meio escolhido.
 * Pre-condições: configuração, governo e dependências exteriores válidos.
 * Effeitos: cria filas, publica o bloco e serve até ordem de parada.
 * Retorno: zero no termo regular ou erro negativo na primeira falha.
 * Razão: uma só entrada governa acquisição e restituição de todos os recursos.
 */
int executar_servidor_ublk(
    const struct configuracao_do_apparelho *configuracao,
    struct governo_do_apparelho *governo);

/*
 * Proposito: publicar e servir ublk sobre VRAM atravessada por DMA CUDA.
 * Pre-condições: configuração, governo, GPU, CUDA e libublksrv válidos.
 * Effeitos: reserva VRAM, fixa buffers e serve até parada.
 * Retorno: zero no termo regular ou erro negativo na primeira falha.
 * Razão: esta entrada distingue a experiência RAM do apparelho real.
 */
int executar_servidor_cuda(
    const struct configuracao_do_apparelho *configuracao,
    struct governo_do_apparelho *governo);

/*
 * Proposito: ordenar por chamada ordinária o termo do servidor singular.
 * Pre-condições: uma execução poderá estar nascendo, servindo ou terminando.
 * Effeitos: conserva a ordem e solicita parada quando o controle já existe.
 * Retorno: zero; a convergência pertence ao fio que executa o servidor.
 * Razão: o governo local não deve fingir um signal para exercer sua vontade.
 */
int ordenar_termo_do_servidor_ublk(void);

int ordenar_termo_do_servidor_ublk_em_contexto(void *contexto);
/* Consulta assíncrona usada pela audiência ordinária após EINTR. */
int termo_do_servidor_ublk_foi_requerido(void);

#endif
