#include "ocupacao_do_bloco.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
