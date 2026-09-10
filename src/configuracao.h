#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H

#include <stdint.h>

/* O bloco é a menor pedra que esta geometria consente medir. */
#define TAMANHO_DO_BLOCO_EM_BYTES 4096U

/*
 * Reúne as grandezas que determinam a figura inteira do apparelho.
 * Todas devem ser conhecidas antes que se reserve memória ou se abra fila;
 * assim, nenhuma pressão ulterior poderá alterar a planta já demonstrada.
 */
struct configuracao_do_apparelho {
    int indice_da_gpu;
    uint64_t capacidade_em_bytes;
    int quantidade_de_filas;
    int profundidade_das_filas;
    uint32_t maior_operacao_em_bytes;
    uint32_t prazo_da_operacao_em_milissegundos;
    int consentir_margem_da_vram;
};

/*
 * Julga se cada grandeza pertence ao domínio consentido pelo apparelho.
 * Requer somente um ponteiro, que poderá ser nulo sem provocar accesso.
 * Não altera memória nem estado; devolve verdadeiro quando a configuração
 * é inteira, alinhada ao bloco e livre de productos impossíveis de alojar.
 * Esta sentença única impede que cada parte invente limites divergentes.
 */
int configuracao_do_apparelho_e_valida(
    const struct configuracao_do_apparelho *configuracao);

#endif
