#ifndef GOVERNO_DO_APPARELHO_H
#define GOVERNO_DO_APPARELHO_H

#include "configuracao.h"

#include <pthread.h>

enum estado_do_governo_do_apparelho {
    ESTADO_DO_GOVERNO_INICIALIZANDO = 0,
    ESTADO_DO_GOVERNO_PRONTO = 1,
    ESTADO_DO_GOVERNO_SERVINDO = 2,
    ESTADO_DO_GOVERNO_ENCERRANDO = 3,
    ESTADO_DO_GOVERNO_ENCERRADO = 4,
    ESTADO_DO_GOVERNO_FALHOU = 5
};

typedef int (*servico_do_apparelho)(
    const struct configuracao_do_apparelho *configuracao, void *contexto);
typedef int (*termo_do_apparelho)(void *contexto);

struct governo_do_apparelho {
    pthread_mutex_t exclusao;
    pthread_t fio;
    struct configuracao_do_apparelho configuracao;
    servico_do_apparelho servir;
    termo_do_apparelho ordenar_termo;
    void *contexto;
    enum estado_do_governo_do_apparelho estado;
    int resultado;
    int fio_nascido;
    int identidade_do_bloco;
};

/*
 * Proposito: preparar um governo vazio com suas duas faculdades exteriores.
 * Pre-condições: destino vivo e faculdades não nulas. Effeitos: inicia mutex.
 * Retorno: zero ou erro negativo. Razão: nenhuma ordem precede esta fundação.
 */
int preparar_governo_do_apparelho(struct governo_do_apparelho *governo,
                                  servico_do_apparelho servir,
                                  termo_do_apparelho ordenar_termo,
                                  void *contexto);

/*
 * Proposito: fazer nascer em fio próprio o serviço de uma configuração.
 * Pre-condições: governo vazio e configuração válida. Effeitos: inicia fio.
 * Retorno: zero ou erro negativo. Razão: a tomada permanece livre para ordens.
 */
int crear_apparelho_governado(struct governo_do_apparelho *governo,
                              const struct configuracao_do_apparelho *figura);

/*
 * Proposito: publicar PRONTO ou SERVINDO quando o serviço os demonstra.
 * Pre-condições: transição consecutiva e governo vivo.
 * Effeitos: muda o estado sob exclusão. Retorno: zero, -EINVAL ou -EPERM.
 * Razão: só o serviço conhece o termo do preparo e a aceitação do núcleo.
 */
int publicar_estado_operacional_do_apparelho(
    struct governo_do_apparelho *governo,
    enum estado_do_governo_do_apparelho estado);

/*
 * Proposito: copiar estado e último resultado sob a exclusão do governo.
 * Pre-condições: três destinos vivos. Effeitos: publica retrato coherente.
 * Retorno: zero ou -EINVAL. Razão: status jámais observa transição partida.
 */
int contemplar_apparelho_governado(
    struct governo_do_apparelho *governo,
    enum estado_do_governo_do_apparelho *estado, int *resultado);

/* Publica a identidade ublk devolvida pelo núcleo; valor negativo significa ausência. */
int publicar_identidade_do_bloco(
    struct governo_do_apparelho *governo, int identidade);

/*
 * Proposito: ordenar termo e reunir exactamente o fio proprietário.
 * Pre-condições: governo preparado. Effeitos: chama termo e faz pthread_join.
 * Retorno: zero, -ENODEV ou erro negativo. Razão: destroy só conclue reunido.
 */
int destruir_apparelho_governado(struct governo_do_apparelho *governo);

/*
 * Proposito: restituir a exclusão de um governo sem fio vivo.
 * Pre-condições: governo preparado e apparelho destruído.
 * Effeitos: destrói o mutex. Retorno: zero ou erro negativo de pthread.
 * Razão: o recinto só morre depois que seu governo perdeu todas as posses.
 */
int encerrar_governo_do_apparelho(struct governo_do_apparelho *governo);

#endif
