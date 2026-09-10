#include "carga_de_creacao.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>

/*
 * THEOREMA DA CARGA EXTERIOR
 * Proposito: escrever os seis campos como sete palavras de trinta e dois bits.
 * Pre-condições: figura válida e destino bastante. Effeitos: grava tudo.
 * Retorno: zero, -EINVAL ou -ENOBUFS, sem escripta nos erros.
 * Razão: palavras maiores primeiro independem da representação da machina.
 */
int escrever_carga_de_creacao(unsigned char *destino, size_t capacidade,
                              const struct configuracao_do_apparelho *figura)
{
    uint32_t campos[8];
    size_t indice;

    if (destino == 0 || !configuracao_do_apparelho_e_valida(figura))
        return -EINVAL;
    if (capacidade < TAMANHO_DA_CARGA_DE_CREACAO) return -ENOBUFS;
    campos[0] = (uint32_t)figura->indice_da_gpu;
    campos[1] = (uint32_t)(figura->capacidade_em_bytes >> 32);
    campos[2] = (uint32_t)figura->capacidade_em_bytes;
    campos[3] = (uint32_t)figura->quantidade_de_filas;
    campos[4] = (uint32_t)figura->profundidade_das_filas;
    campos[5] = figura->maior_operacao_em_bytes;
    campos[6] = figura->prazo_da_operacao_em_milissegundos;
    campos[7] = (uint32_t)figura->consentir_margem_da_vram;
    for (indice = 0; indice < 8; indice++) {
        destino[indice * 4] = (unsigned char)(campos[indice] >> 24);
        destino[indice * 4 + 1] = (unsigned char)(campos[indice] >> 16);
        destino[indice * 4 + 2] = (unsigned char)(campos[indice] >> 8);
        destino[indice * 4 + 3] = (unsigned char)campos[indice];
    }
    return 0;
}

/*
 * THEOREMA DA CARGA CERCADA
 * Proposito: ler sete palavras e reconstruir a configuração de create.
 * Pre-condições: quantidade exacta. Effeitos: publica só figura válida.
 * Retorno: zero ou erro negativo de domínio, tamanho ou largura.
 * Razão: nenhuma conversão estreita antecede a prova de seus limites.
 */
int ler_carga_de_creacao(struct configuracao_do_apparelho *destino,
                         const unsigned char *origem, size_t quantidade)
{
    struct configuracao_do_apparelho figura;
    uint32_t campos[8];
    size_t indice;

    if (destino == 0 || origem == 0) return -EINVAL;
    if (quantidade != TAMANHO_DA_CARGA_DE_CREACAO) return -EMSGSIZE;
    for (indice = 0; indice < 8; indice++) {
        campos[indice] = (uint32_t)origem[indice * 4] << 24 |
                         (uint32_t)origem[indice * 4 + 1] << 16 |
                         (uint32_t)origem[indice * 4 + 2] << 8 |
                         origem[indice * 4 + 3];
    }
    if (campos[0] > INT_MAX || campos[3] > INT_MAX || campos[4] > INT_MAX)
        return -ERANGE;
    figura.indice_da_gpu = (int)campos[0];
    figura.capacidade_em_bytes = (uint64_t)campos[1] << 32 | campos[2];
    figura.quantidade_de_filas = (int)campos[3];
    figura.profundidade_das_filas = (int)campos[4];
    figura.maior_operacao_em_bytes = campos[5];
    figura.prazo_da_operacao_em_milissegundos = campos[6];
    figura.consentir_margem_da_vram = (int)campos[7];
    if (!configuracao_do_apparelho_e_valida(&figura)) return -EINVAL;
    *destino = figura;
    return 0;
}
