#include "retrato_do_observatorio.h"

#include <errno.h>

/*
 * Proposito: situar uma demora num degrau logarithmico e finito.
 * Pre-condições: nenhuma; zero pertence ao primeiro degrau.
 * Effeitos: nenhum. Retorno: índice contido no histogramma.
 * Razão: oito ordens binárias bastam ao primeiro painel sem divisão cara.
 */
static size_t achar_degrau_da_latencia(uint64_t nanossegundos)
{
    size_t degrau = 0;
    uint64_t limite = 1000;

    while (degrau + 1 < QUANTIDADE_DE_DEGRAUS_DA_LATENCIA &&
           nanossegundos > limite) {
        limite *= 4;
        ++degrau;
    }
    return degrau;
}

/*
 * Proposito: registrar a conclusão sem tomar trava ou reservar memória.
 * Pre-condições: contadores pertencem á fila chamadora.
 * Effeitos: soma octetos, conclusão, erro e um ponto do histogramma.
 * Retorno: nenhum; entrada nula é recusada sem effeito.
 * Razão: atomos relaxados dão independência ás filas e ao leitor frio.
 */
void registrar_operacao_observada(struct contadores_da_fila *contadores,
                                  int foi_escripta, uint32_t quantidade,
                                  uint64_t latencia_em_nanossegundos,
                                  int resultado)
{
    size_t degrau;

    if (contadores == 0) return;
    degrau = achar_degrau_da_latencia(latencia_em_nanossegundos);
    atomic_fetch_add_explicit(&contadores->operacoes_concluidas, 1,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&contadores->latencias[degrau], 1,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(foi_escripta ? &contadores->bytes_escriptos :
                              &contadores->bytes_lidos, quantidade,
                              memory_order_relaxed);
    if (resultado < 0)
        atomic_fetch_add_explicit(&contadores->erros, 1, memory_order_relaxed);
    if (resultado == -ETIMEDOUT)
        atomic_fetch_add_explicit(&contadores->prazos_expirados, 1,
                                  memory_order_relaxed);
}

/*
 * Proposito: approximar um percentil pelo primeiro degrau que o alcança.
 * Pre-condições: histogramma contém oito sommas já consolidadas.
 * Effeitos: nenhum. Retorno: limite superior em microssegundos.
 * Razão: a posição inteira evita ponto fluctuante no observatório basal.
 */
static uint64_t calcular_percentil(const uint64_t *histogramma,
                                  uint64_t total, uint64_t centesimos)
{
    static const uint64_t limites[] = {1, 4, 16, 64, 256, 1024, 4096, 16384};
    uint64_t alvo;
    uint64_t acumulado = 0;
    size_t indice;

    if (histogramma == 0 || total == 0 || centesimos == 0) return 0;
    alvo = total / 100 * centesimos +
           (total % 100 * centesimos + 99) / 100;
    for (indice = 0; indice < QUANTIDADE_DE_DEGRAUS_DA_LATENCIA; ++indice) {
        acumulado += histogramma[indice];
        if (acumulado >= alvo) return limites[indice];
    }
    return limites[QUANTIDADE_DE_DEGRAUS_DA_LATENCIA - 1];
}

/*
 * Proposito: colher numerosas filas numa só figura sem lhes deter o labor.
 * Pre-condições: destino, filas e ordem monotónica dos instantes são válidos.
 * Effeitos: publica o destino somente depois de completar a figura local.
 * Retorno: unidade no êxito e zero sem modificar o retrato na recusa.
 * Razão: a cópia derradeira dá ao leitor uma época única e immutável.
 */
int colher_retrato_do_observatorio(
    struct retrato_do_observatorio *retrato,
    struct contadores_da_fila *filas, size_t quantidade_de_filas,
    uint64_t instante_actual_em_nanossegundos,
    uint64_t instante_anterior_em_nanossegundos)
{
    struct retrato_do_observatorio figura = {0};
    uint64_t histogramma[QUANTIDADE_DE_DEGRAUS_DA_LATENCIA] = {0};
    uint64_t amostras_de_latencia = 0;
    size_t fila, degrau;
    if (retrato == 0 || filas == 0 || quantidade_de_filas == 0 ||
        instante_actual_em_nanossegundos < instante_anterior_em_nanossegundos)
        return 0;
    if (atomic_flag_test_and_set_explicit(
            &filas[0].colheita_em_curso, memory_order_acquire)) return 0;
    figura.instante_monotonico_em_nanossegundos =
        instante_actual_em_nanossegundos;
    figura.duracao_da_janella_em_nanossegundos =
        instante_actual_em_nanossegundos - instante_anterior_em_nanossegundos;
    for (fila = 0; fila < quantidade_de_filas; ++fila) {
        figura.bytes_lidos += atomic_load_explicit(
            &filas[fila].bytes_lidos, memory_order_relaxed);
        figura.bytes_escriptos += atomic_load_explicit(
            &filas[fila].bytes_escriptos, memory_order_relaxed);
        figura.operacoes_concluidas += atomic_load_explicit(
            &filas[fila].operacoes_concluidas, memory_order_relaxed);
        figura.erros += atomic_load_explicit(&filas[fila].erros,
                                             memory_order_relaxed);
        figura.prazos_expirados += atomic_load_explicit(
            &filas[fila].prazos_expirados, memory_order_relaxed);
        figura.amostras_perdidas += atomic_load_explicit(
            &filas[fila].amostras_perdidas, memory_order_relaxed);
        for (degrau = 0; degrau < QUANTIDADE_DE_DEGRAUS_DA_LATENCIA; ++degrau) {
            uint64_t amostras = atomic_exchange_explicit(
                &filas[fila].latencias[degrau], 0, memory_order_relaxed);
            histogramma[degrau] += amostras;
            amostras_de_latencia += amostras;
        }
    }
    figura.latencia_p50_em_microssegundos = calcular_percentil(
        histogramma, amostras_de_latencia, 50);
    figura.latencia_p95_em_microssegundos = calcular_percentil(
        histogramma, amostras_de_latencia, 95);
    figura.latencia_p99_em_microssegundos = calcular_percentil(
        histogramma, amostras_de_latencia, 99);
    *retrato = figura;
    atomic_flag_clear_explicit(&filas[0].colheita_em_curso,
                               memory_order_release);
    return 1;
}

/*
 * Proposito: produzir o incremento observado entre duas épocas.
 * Pre-condições: ponteiros válidos, relógio e contadores não regressivos.
 * Effeitos: conserva grandezas instantâneas e subtrai somente accumulos.
 * Retorno: unidade no êxito e zero sem tocar o destino na recusa.
 * Razão: toda subtracção é precedida pela prova de sua ordem.
 */
int differenciar_retratos_do_observatorio(
    struct retrato_do_observatorio *differenca,
    const struct retrato_do_observatorio *actual,
    const struct retrato_do_observatorio *anterior)
{
    struct retrato_do_observatorio figura;

    if (differenca == 0 || actual == 0 || anterior == 0 ||
        actual->instante_monotonico_em_nanossegundos <
            anterior->instante_monotonico_em_nanossegundos ||
        actual->bytes_lidos < anterior->bytes_lidos ||
        actual->bytes_escriptos < anterior->bytes_escriptos ||
        actual->operacoes_concluidas < anterior->operacoes_concluidas ||
        actual->erros < anterior->erros ||
        actual->prazos_expirados < anterior->prazos_expirados ||
        actual->amostras_perdidas < anterior->amostras_perdidas) return 0;
    figura = *actual;
    figura.duracao_da_janella_em_nanossegundos =
        actual->instante_monotonico_em_nanossegundos -
        anterior->instante_monotonico_em_nanossegundos;
    figura.bytes_lidos -= anterior->bytes_lidos;
    figura.bytes_escriptos -= anterior->bytes_escriptos;
    figura.operacoes_concluidas -= anterior->operacoes_concluidas;
    figura.erros -= anterior->erros;
    figura.prazos_expirados -= anterior->prazos_expirados;
    figura.amostras_perdidas -= anterior->amostras_perdidas;
    *differenca = figura;
    return 1;
}
