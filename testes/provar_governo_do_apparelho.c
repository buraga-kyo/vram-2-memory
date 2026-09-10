#include "../src/governo_do_apparelho.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>

struct contexto_da_prova_do_governo {
    atomic_int iniciou;
    atomic_int terminar;
    struct governo_do_apparelho *governo;
};

/*
 * Proposito: representar serviço que vive até receber sua ordem.
 * Pre-condições: configuração válida e contexto vivo. Effeitos: espera activa.
 * Retorno: zero. Razão: a prova não deve depender de CUDA, ublk ou relógio.
 */
int servir_prova_do_governo(
    const struct configuracao_do_apparelho *configuracao, void *contexto)
{
    struct contexto_da_prova_do_governo *prova = contexto;

    assert(configuracao_do_apparelho_e_valida(configuracao));
    assert(publicar_estado_operacional_do_apparelho(
        prova->governo, ESTADO_DO_GOVERNO_PRONTO) == 0);
    assert(publicar_estado_operacional_do_apparelho(
        prova->governo, ESTADO_DO_GOVERNO_SERVINDO) == 0);
    atomic_store(&prova->iniciou, 1);
    while (!atomic_load(&prova->terminar)) { }
    return 0;
}

/*
 * Proposito: ordenar o termo do serviço fingido.
 * Pre-condições: contexto vivo. Effeitos: publica marca atomica.
 * Retorno: zero. Razão: a mesma faculdade exterior serve á prova e ao real.
 */
int terminar_prova_do_governo(void *contexto)
{
    struct contexto_da_prova_do_governo *prova = contexto;

    atomic_store(&prova->terminar, 1);
    return 0;
}

/*
 * Proposito: representar serviço cuja preparação terminou em ruína.
 * Pre-condições: configuração válida. Effeitos: nenhum.
 * Retorno: -EIO. Razão: a prova deve conservar FALHOU mesmo depois do join.
 */
int falhar_prova_do_governo(
    const struct configuracao_do_apparelho *configuracao, void *contexto)
{
    (void)configuracao;
    (void)contexto;
    return -EIO;
}

/*
 * Proposito: provar create, status, exclusividade, destroy e restituição.
 * Pre-condições: pthread disponível. Effeitos: nasce e reúne um fio fingido.
 * Retorno: zero no êxito. Razão: as ordens são provadas sem meio exterior.
 */
int main(void)
{
    const struct configuracao_do_apparelho configuracao = {
        0, UINT64_C(65536), 1, 2, UINT32_C(4096), UINT32_C(1000), 0
    };
    struct contexto_da_prova_do_governo contexto = {0};
    struct governo_do_apparelho governo;
    enum estado_do_governo_do_apparelho estado;
    int resultado;

    assert(preparar_governo_do_apparelho(
        &governo, servir_prova_do_governo,
        terminar_prova_do_governo, &contexto) == 0);
    contexto.governo = &governo;
    assert(contemplar_apparelho_governado(&governo, &estado, &resultado) == 0);
    assert(estado == ESTADO_DO_GOVERNO_ENCERRADO && resultado == 0);
    assert(crear_apparelho_governado(&governo, &configuracao) == 0);
    while (!atomic_load(&contexto.iniciou)) { }
    assert(contemplar_apparelho_governado(&governo, &estado, &resultado) == 0);
    assert(estado == ESTADO_DO_GOVERNO_SERVINDO);
    assert(crear_apparelho_governado(&governo, &configuracao) == -EBUSY);
    assert(encerrar_governo_do_apparelho(&governo) == -EBUSY);
    assert(destruir_apparelho_governado(&governo) == 0);
    assert(contemplar_apparelho_governado(&governo, &estado, &resultado) == 0);
    assert(estado == ESTADO_DO_GOVERNO_ENCERRADO && resultado == 0);
    assert(encerrar_governo_do_apparelho(&governo) == 0);
    assert(preparar_governo_do_apparelho(
        &governo, falhar_prova_do_governo,
        terminar_prova_do_governo, &contexto) == 0);
    assert(crear_apparelho_governado(&governo, &configuracao) == 0);
    do {
        assert(contemplar_apparelho_governado(
            &governo, &estado, &resultado) == 0);
    } while (estado != ESTADO_DO_GOVERNO_FALHOU);
    assert(resultado == -EIO);
    assert(destruir_apparelho_governado(&governo) == -EIO);
    assert(contemplar_apparelho_governado(
        &governo, &estado, &resultado) == 0);
    assert(estado == ESTADO_DO_GOVERNO_FALHOU && resultado == -EIO);
    assert(encerrar_governo_do_apparelho(&governo) == 0);
    return 0;
}
