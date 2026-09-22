#define _POSIX_C_SOURCE 200809L
#include "ocupacao_do_bloco.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static int swap_conserva_bloco(unsigned int maior_esperado, unsigned int menor_esperado)
{
    char caminho[4096];
    char linha[8192];
    struct stat estado;
    FILE *folha = fopen("/proc/swaps", "r");
    int primeira = 1;
    if (folha == 0) return -errno;
    while (fgets(linha, sizeof(linha), folha) != 0) {
        if (primeira) { primeira = 0; continue; }
        if (sscanf(linha, "%4095s", caminho) != 1) continue;
        if (stat(caminho, &estado) == 0 && S_ISBLK(estado.st_mode) &&
            major(estado.st_rdev) == maior_esperado &&
            minor(estado.st_rdev) == menor_esperado) {
            (void)fclose(folha);
            return 1;
        }
    }
    if (ferror(folha)) {
        int erro = errno == 0 ? EIO : errno;
        (void)fclose(folha);
        return -erro;
    }
    return fclose(folha) == 0 ? 0 : -errno;
}

static int montagem_conserva_bloco(unsigned int maior, unsigned int menor)
{
    char chave[48];
    char *linha = 0;
    size_t capacidade = 0;
    FILE *folha;
    int achou = 0;
    int largura;

    largura = snprintf(chave, sizeof(chave), " %u:%u ", maior, menor);
    if (largura < 0 || (size_t)largura >= sizeof(chave)) return -EOVERFLOW;
    folha = fopen("/proc/self/mountinfo", "r");
    if (folha == 0) return -errno;
    while (getline(&linha, &capacidade, folha) >= 0) {
        if (strstr(linha, chave) != 0) {
            achou = 1;
            break;
        }
    }
    free(linha);
    if (fclose(folha) != 0 && !achou) return -errno;
    return achou;
}

static int ler_identidade_do_nucleo(
    int identidade_ublk, unsigned int *maior, unsigned int *menor)
{
    char caminho[96];
    FILE *folha;
    int largura;

    if (identidade_ublk < 0 || maior == 0 || menor == 0) return -EINVAL;
    largura = snprintf(caminho, sizeof(caminho),
                       "/sys/block/ublkb%d/dev", identidade_ublk);
    if (largura < 0 || (size_t)largura >= sizeof(caminho)) return -ENAMETOOLONG;
    folha = fopen(caminho, "r");
    if (folha == 0) return -errno;
    largura = fscanf(folha, "%u:%u", maior, menor);
    if (fclose(folha) != 0 && largura == 2) return -errno;
    return largura == 2 ? 0 : -EPROTO;
}

int bloco_esta_occupado(int identidade_ublk, char *causa, size_t capacidade)
{
    unsigned int maior, menor;
    int resultado;

    if (causa == 0 || capacidade == 0) return -EINVAL;
    causa[0] = 0;
    resultado = ler_identidade_do_nucleo(identidade_ublk, &maior, &menor);
    if (resultado < 0) return resultado;
    resultado = montagem_conserva_bloco(maior, menor);
    if (resultado < 0) return resultado;
    if (resultado) {
        (void)snprintf(causa, capacidade, "montado %u:%u", maior, menor);
        return 1;
    }
    resultado = swap_conserva_bloco(maior, menor);
    if (resultado < 0) return resultado;
    if (resultado)
        (void)snprintf(causa, capacidade, "swap activo %u:%u", maior, menor);
    return resultado;
}
