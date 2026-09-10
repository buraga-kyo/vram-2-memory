#ifndef MEIO_CUDA_H
#define MEIO_CUDA_H
#include "contrato_do_meio.h"
#include <cuda.h>
#include <stdint.h>

/* O reservatório reside na GPU; sua capacidade limita todo deslocamento. */
struct meio_cuda {
    CUdeviceptr memoria_da_gpu;
    CUcontext contexto;
    CUdevice dispositivo;
    uint64_t capacidade_em_bytes;
};
/* Cada fila possue corrente CUDA própria sobre o reservatório commum. */
struct transportador_cuda {
    struct meio_cuda *meio;
    CUstream corrente;
    CUevent evento_de_aquecimento;
    uint64_t proxima_ordem;
    uint32_t quantidade_pendente;
};
/*
 * Proposito: escolher a GPU e reservar toda a VRAM antes da publicação.
 * Pre-condições: meio vazio, índice não negativo e capacidade alojável.
 * Effeitos: fixa o dispositivo e adquire VRAM. Retorno: unidade ou zero.
 * Razão: nenhuma reserva CUDA poderá nascer no caminho crítico.
 */
int criar_meio_cuda(struct meio_cuda *meio, int indice_da_gpu,
                    uint64_t capacidade_em_bytes,
                    int consentir_margem_da_vram);

/*
 * Proposito: devolver a VRAM e reduzir o meio á figura vazia.
 * Pre-condições: nenhuma; meio nulo ou vazio não produz effeito.
 * Effeitos: chama cuMemFree no dispositivo possuidor. Retorno: unidade ou zero.
 * Razão: o índice é restaurado antes de tocar a reserva correspondente.
 */
int destruir_meio_cuda(struct meio_cuda *meio);

/*
 * Proposito: crear corrente e evento de aquecimento para uma fila.
 * Pre-condições: meio vivo e transportador vazio.
 * Effeitos: adquire CUstream e CUevent. Retorno: unidade ou zero.
 * Razão: filas independentes não hão de serializar na corrente ordinária.
 */
int criar_transportador_cuda(struct transportador_cuda *transportador,
                             struct meio_cuda *meio);

/*
 * Proposito: destruir a corrente depois de todas as cópias concluídas.
 * Pre-condições: nenhuma. Effeitos: synchroniza e restitue a corrente.
 * Retorno: unidade no êxito e zero se CUDA recusar o termo.
 * Razão: jámais se liberta meio ainda referenciado por operação assíncrona.
 */
int destruir_transportador_cuda(struct transportador_cuda *transportador);

/*
 * Proposito: fixar uma região alinhada já adquirida pelo plano autoral.
 * Pre-condições: contexto corrente, endereço vivo e quantidade positiva.
 * Effeitos: chama cuMemHostRegister. Retorno: unidade ou zero.
 * Razão: o registro separa a posse da RAM de sua aptidão para DMA.
 */
int registrar_memoria_intermediaria_cuda(void *memoria,
                                         size_t quantidade_de_bytes);

/*
 * Proposito: retirar o registro CUDA antes de devolver a RAM autoral.
 * Pre-condições: contexto corrente; endereço registrado ou nulo.
 * Effeitos: chama cuMemHostUnregister. Retorno: unidade ou zero.
 * Razão: a GPU perde accesso antes que a região torne ao alocador ordinário.
 */
int desregistrar_memoria_intermediaria_cuda(void *memoria);

/*
 * Proposito: copiar VRAM para memória CPU fixada pela corrente da fila.
 * Pre-condições: região contida e destino previamente registrado no CUDA.
 * Effeitos: submette DMA e espera sua conclusão. Retorno: unidade ou zero.
 * Razão: a espera antecede a conclusão que reutilizará a memória.
 */
int ler_meio_cuda(struct transportador_cuda *transportador,
                  uint64_t deslocamento, void *destino,
                  uint32_t quantidade_de_bytes);

/*
 * Proposito: copiar memória CPU fixada para VRAM pela corrente da fila.
 * Pre-condições: região contida e origem previamente registrada no CUDA.
 * Effeitos: submette DMA e espera sua conclusão. Retorno: unidade ou zero.
 * Razão: a espera torna a conclusão do bloco posterior á transferência.
 */
int escrever_meio_cuda(struct transportador_cuda *transportador,
                       uint64_t deslocamento, const void *origem,
                       uint32_t quantidade_de_bytes);

/*
 * Proposito: reduzir a zero uma região da VRAM pela corrente da fila.
 * Pre-condições: transportador vivo e intervallo contido.
 * Effeitos: submette cuMemsetD8Async e espera. Retorno: unidade ou zero.
 * Razão: descarte não reclama memória CPU nem abandona dados recuperáveis.
 */
int zerar_meio_cuda(struct transportador_cuda *transportador,
                    uint64_t deslocamento, uint32_t quantidade_de_bytes);

#ifdef PROVAR_INJECCAO_CUDA
/*
 * Proposito: impor uma resposta á próxima consulta de uma etiqueta pendente.
 * Pre-condições: binário de prova, contexto e identidade válidos.
 * Effeitos: arma uma só injecção. Retorno: zero ou -EINVAL.
 * Razão: o servidor de produção jámais conterá o artifício determinístico.
 */
int injectar_consulta_do_evento_cuda(
    void *contexto, int indice_da_fila, int etiqueta, CUresult resultado);

/*
 * Proposito: impor erro á próxima submissão de uma etiqueta ociosa.
 * Pre-condições: binário de prova, contexto e identidade válidos.
 * Effeitos: arma uma só recusa. Retorno: zero ou -EINVAL.
 * Razão: a prova contará zero callbacks depois do erro immediato.
 */
int injectar_erro_da_submissao_cuda(
    void *contexto, int indice_da_fila, int etiqueta);
#endif

/*
 * Proposito: revelar a taboa assíncrona do reservatório CUDA.
 * Pre-condições: nenhuma; a taboa possue duração estática.
 * Effeitos: nenhum. Retorno: operações immutáveis do meio CUDA.
 * Razão: a lei commum encobre streams e eventos pertencentes ás etiquetas.
 */
const struct operacoes_do_meio *obter_operacoes_do_meio_cuda(void);

#endif
