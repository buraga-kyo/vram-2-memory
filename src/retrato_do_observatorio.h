#ifndef RETRATO_DO_OBSERVATORIO_H
#define RETRATO_DO_OBSERVATORIO_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define QUANTIDADE_DE_DEGRAUS_DA_LATENCIA 8U

/* Cada fila escreve seus proprios atomos; o observador apenas os contempla. */
struct contadores_da_fila {
    atomic_flag colheita_em_curso;
    atomic_uint_fast64_t bytes_lidos;
    atomic_uint_fast64_t bytes_escriptos;
    atomic_uint_fast64_t operacoes_concluidas;
    atomic_uint_fast64_t erros;
    atomic_uint_fast64_t prazos_expirados;
    atomic_uint_fast64_t amostras_perdidas;
    atomic_uint_fast64_t latencias[QUANTIDADE_DE_DEGRAUS_DA_LATENCIA];
};

/* O retrato immutável separa a estatística da arte que a apresentará. */
struct retrato_do_observatorio {
    uint64_t instante_monotonico_em_nanossegundos;
    uint64_t duracao_da_janella_em_nanossegundos;
    uint64_t bytes_lidos;
    uint64_t bytes_escriptos;
    uint64_t operacoes_concluidas;
    uint64_t erros;
    uint64_t prazos_expirados;
    uint64_t amostras_perdidas;
    uint64_t latencia_p50_em_microssegundos;
    uint64_t latencia_p95_em_microssegundos;
    uint64_t latencia_p99_em_microssegundos;
    uint64_t capacidade_em_bytes;
    uint64_t memoria_do_meio_reservada_em_bytes;
    uint64_t memoria_da_cpu_fixada_em_bytes;
    uint32_t temperatura_da_gpu_em_millicelsius;
    uint32_t potencia_da_gpu_em_milliwatts;
    int temperatura_da_gpu_presente;
    int potencia_da_gpu_presente;
};

void registrar_operacao_observada(struct contadores_da_fila *contadores,
                                  int foi_escripta, uint32_t quantidade,
                                  uint64_t latencia_em_nanossegundos,
                                  int resultado);

int colher_retrato_do_observatorio(
    struct retrato_do_observatorio *retrato,
    struct contadores_da_fila *filas, size_t quantidade_de_filas,
    uint64_t instante_actual_em_nanossegundos,
    uint64_t instante_anterior_em_nanossegundos);

/*
 * Proposito: separar de dois retratos somente a marcha da última janella.
 * Pre-condições: épocas ordenadas e contadores jamais regressivos.
 * Effeitos: publica a differença apenas quando todas as ordens são válidas.
 * Retorno: unidade no êxito e zero conservando o destino na recusa.
 * Razão: razão por segundo exige incremento, não total desde o nascimento.
 */
int differenciar_retratos_do_observatorio(
    struct retrato_do_observatorio *differenca,
    const struct retrato_do_observatorio *actual,
    const struct retrato_do_observatorio *anterior);

#endif
