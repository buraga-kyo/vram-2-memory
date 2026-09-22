#include "governo_do_apparelho.h"

#include <errno.h>
#include <string.h>

/*
 * THEOREMA DO FIO PROPRIETARIO
 * Proposito: executar a faculdade de serviço e publicar seu termo.
 * Pre-condições: configuração copiada e estado inicializando.
 * Effeitos: executa o serviço; afinal encerra ou publica sua falha.
 * Retorno: o governo recebido. Razão: resultado nasce no único fio possuidor.
 */
static void *servir_apparelho_governado(void *argumento)
{
    struct governo_do_apparelho *governo = argumento;
    int resultado;

    resultado = governo->servir(&governo->configuracao, governo->contexto);
    (void)pthread_mutex_lock(&governo->exclusao);
    governo->resultado = resultado;
    governo->estado = resultado < 0 ? ESTADO_DO_GOVERNO_FALHOU :
                                      ESTADO_DO_GOVERNO_ENCERRADO;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return governo;
}

/*
 * THEOREMA DA PUBLICACAO OPERACIONAL
 * Proposito: publicar os dois marcos que somente o serviço pode conhecer.
 * Pre-condições: governo vivo; pronto segue inicializando, servindo segue pronto.
 * Effeitos: muda o estado sob a exclusão. Retorno: zero ou erro de transição.
 * Razão: o governo não pode fingir que filas ou núcleo já estão preparados.
 */
int publicar_estado_operacional_do_apparelho(
    struct governo_do_apparelho *governo,
    enum estado_do_governo_do_apparelho estado)
{
    enum estado_do_governo_do_apparelho esperado;

    if (governo == 0 || (estado != ESTADO_DO_GOVERNO_PRONTO &&
        estado != ESTADO_DO_GOVERNO_SERVINDO)) return -EINVAL;
    esperado = estado == ESTADO_DO_GOVERNO_PRONTO ?
        ESTADO_DO_GOVERNO_INICIALIZANDO : ESTADO_DO_GOVERNO_PRONTO;
    (void)pthread_mutex_lock(&governo->exclusao);
    if (governo->estado != esperado) {
        (void)pthread_mutex_unlock(&governo->exclusao);
        return -EPERM;
    }
    governo->estado = estado;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return 0;
}
/*
 * PROPOSICAO DO GOVERNO VAZIO
 * Proposito: fundar a exclusão e as faculdades de um governo sem apparelho.
 * Pre-condições: destino e faculdades vivos. Effeitos: prepara figura vazia.
 * Retorno: zero ou erro negativo do domínio ou de pthread.
 * Razão: toda transição futura depende de uma única exclusão já existente.
 */
int preparar_governo_do_apparelho(struct governo_do_apparelho *governo,
                                  servico_do_apparelho servir,
                                  termo_do_apparelho ordenar_termo,
                                  void *contexto)
{
    int resultado;

    if (governo == 0 || servir == 0 || ordenar_termo == 0) return -EINVAL;
    memset(governo, 0, sizeof(*governo));
    resultado = pthread_mutex_init(&governo->exclusao, 0);
    if (resultado != 0) return -resultado;
    governo->servir = servir;
    governo->ordenar_termo = ordenar_termo;
    governo->contexto = contexto;
    governo->estado = ESTADO_DO_GOVERNO_ENCERRADO;
    governo->identidade_do_bloco = -1;
    return 0;
}

int publicar_identidade_do_bloco(
    struct governo_do_apparelho *governo, int identidade)
{
    if (governo == 0 || identidade < 0) return -EINVAL;
    (void)pthread_mutex_lock(&governo->exclusao);
    governo->identidade_do_bloco = identidade;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return 0;
}

/*
 * Proposito: copiar a configuração e iniciar seu fio proprietário.
 * Pre-condições: governo vazio e figura válida. Effeitos: marca inicialização.
 * Retorno: zero ou erro negativo. Razão: a cópia precede o nascimento do fio.
 */
int crear_apparelho_governado(struct governo_do_apparelho *governo,
                              const struct configuracao_do_apparelho *figura)
{
    int resultado;

    if (governo == 0 || !configuracao_do_apparelho_e_valida(figura))
        return -EINVAL;
    (void)pthread_mutex_lock(&governo->exclusao);
    if (governo->fio_nascido ||
        governo->estado != ESTADO_DO_GOVERNO_ENCERRADO) {
        (void)pthread_mutex_unlock(&governo->exclusao);
        return -EBUSY;
    }
    governo->configuracao = *figura;
    governo->estado = ESTADO_DO_GOVERNO_INICIALIZANDO;
    resultado = pthread_create(&governo->fio, 0,
                               servir_apparelho_governado, governo);
    if (resultado == 0) governo->fio_nascido = 1;
    else governo->estado = ESTADO_DO_GOVERNO_ENCERRADO;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return resultado == 0 ? 0 : -resultado;
}

/*
 * Proposito: copiar um retrato indivisível do governo presente.
 * Pre-condições: governo, estado e resultado vivos. Effeitos: copia ambos.
 * Retorno: zero ou -EINVAL. Razão: uma só exclusão conserva sua coherência.
 */
int contemplar_apparelho_governado(
    struct governo_do_apparelho *governo,
    enum estado_do_governo_do_apparelho *estado, int *resultado)
{
    if (governo == 0 || estado == 0 || resultado == 0) return -EINVAL;
    (void)pthread_mutex_lock(&governo->exclusao);
    *estado = governo->estado;
    *resultado = governo->resultado;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return 0;
}

/*
 * THEOREMA DO TERMO REUNIDO
 * Proposito: ordenar termo e recolher o único fio que nasceu.
 * Pre-condições: governo preparado. Effeitos: transita, ordena e reúne.
 * Retorno: zero, erro da ordem, de pthread ou do serviço concluído.
 * Razão: destroy victorioso significa ausência demonstrada do proprietário.
 */
int destruir_apparelho_governado(struct governo_do_apparelho *governo)
{
    pthread_t fio;
    int resultado;

    if (governo == 0) return -EINVAL;
    (void)pthread_mutex_lock(&governo->exclusao);
    if (!governo->fio_nascido) {
        (void)pthread_mutex_unlock(&governo->exclusao);
        return -ENODEV;
    }
    governo->estado = ESTADO_DO_GOVERNO_ENCERRANDO;
    fio = governo->fio;
    (void)pthread_mutex_unlock(&governo->exclusao);
    resultado = governo->ordenar_termo(governo->contexto);
    if (resultado < 0) return resultado;
    resultado = pthread_join(fio, 0);
    if (resultado != 0) return -resultado;
    (void)pthread_mutex_lock(&governo->exclusao);
    governo->fio_nascido = 0;
    resultado = governo->resultado;
    governo->estado = resultado < 0 && resultado != -ECANCELED ?
        ESTADO_DO_GOVERNO_FALHOU : ESTADO_DO_GOVERNO_ENCERRADO;
    (void)pthread_mutex_unlock(&governo->exclusao);
    return resultado == -ECANCELED ? 0 : resultado;
}

/*
 * Proposito: restituir a exclusão depois da ausência comprovada do fio.
 * Pre-condições: governo preparado. Effeitos: destrói seu mutex.
 * Retorno: zero, -EBUSY ou erro negativo de pthread.
 * Razão: a última posse interna jámais pode morrer sob um trabalhador vivo.
 */
int encerrar_governo_do_apparelho(struct governo_do_apparelho *governo)
{
    int resultado;

    if (governo == 0) return -EINVAL;
    if (governo->fio_nascido) return -EBUSY;
    resultado = pthread_mutex_destroy(&governo->exclusao);
    return resultado == 0 ? 0 : -resultado;
}
