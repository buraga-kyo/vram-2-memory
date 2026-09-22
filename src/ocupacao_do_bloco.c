#include "ocupacao_do_bloco.h"

#include <errno.h>
#include <stdio.h>

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
