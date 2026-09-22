#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    uint64_t mib, segundos, i, bytes;
    unsigned char *memoria;
    struct timespec espera = {1, 0};

    if (argc != 3) return 2;
    mib = strtoull(argv[1], 0, 10);
    segundos = strtoull(argv[2], 0, 10);
    if (mib == 0 || mib > SIZE_MAX / 1048576U) return 2;
    bytes = mib * 1048576U;
    memoria = malloc((size_t)bytes);
    if (memoria == 0) return errno == 0 ? 1 : errno;
    for (i = 0; i < bytes; i += 4096) memoria[i] = (unsigned char)i;
    while (segundos-- > 0) nanosleep(&espera, 0);
    free(memoria);
    return 0;
}
