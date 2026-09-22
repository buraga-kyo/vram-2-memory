#ifndef OCUPACAO_DO_BLOCO_H
#define OCUPACAO_DO_BLOCO_H
#include <stddef.h>

/* Retorna unidade quando montagem ou swap conserva o bloco exacto. */
int bloco_esta_occupado(int identidade_ublk, char *causa, size_t capacidade);

#endif
