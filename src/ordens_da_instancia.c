#include "ordens_da_instancia.h"
#include "carga_de_creacao.h"
#include <errno.h>
#include <stdio.h>

#define VERSAO_DO_SCHEMA_DO_ESTADO 1

static int escrever_estado_integral(
    unsigned char *resposta, size_t capacidade,
    const struct governo_do_apparelho *governo,
    enum estado_do_governo_do_apparelho estado, int resultado)
{
    const struct configuracao_do_apparelho *figura = &governo->configuracao;

    return snprintf((char *)resposta, capacidade,
        "{\"ok\":true,\"schema\":%d,\"estado\":\"%s\",\"resultado\":%d,"
        "\"geometria\":{\"capacidade_bytes\":%llu,\"filas\":%d,"
        "\"profundidade\":%d,\"operacao_maxima_bytes\":%u,\"prazo_ms\":%u},"
        "\"memoria\":{\"vram_reservada_bytes\":null,\"ram_fixada_bytes\":null},"
        "\"operacoes\":{\"leitura\":null,\"escripta\":null,\"zeragem\":null},"
        "\"medidas\":{\"bytes\":null,\"erros\":null,\"prazos\":null,"
        "\"p50_us\":null,\"p95_us\":null,\"p99_us\":null,\"perdidas\":null},"
        "\"gpu\":null,\"controlador\":null,\"pcie\":null}",
        VERSAO_DO_SCHEMA_DO_ESTADO, nome_do_estado_do_governo(estado), resultado,
        (unsigned long long)figura->capacidade_em_bytes,
        figura->quantidade_de_filas, figura->profundidade_das_filas,
        figura->maior_operacao_em_bytes, figura->prazo_da_operacao_em_milissegundos);
}

/*
 * Proposito: converter o estado nativo em vocábulo exterior permanente.
 * Pre-condições: nenhuma. Effeitos: nenhum.
 * Retorno: endereço de texto estático. Razão: números não vazam no JSON.
 */
const char *nome_do_estado_do_governo(
    enum estado_do_governo_do_apparelho estado)
{
    switch (estado) {
    case ESTADO_DO_GOVERNO_INICIALIZANDO: return "INICIALIZANDO";
    case ESTADO_DO_GOVERNO_PRONTO: return "PRONTO";
    case ESTADO_DO_GOVERNO_SERVINDO: return "SERVINDO";
    case ESTADO_DO_GOVERNO_ENCERRANDO: return "ENCERRANDO";
    case ESTADO_DO_GOVERNO_ENCERRADO: return "ENCERRADO";
    case ESTADO_DO_GOVERNO_FALHOU: return "FALHOU";
    default: return "DESCONHECIDO";
    }
}

/*
 * THEOREMA DA ORDEM GOVERNADA
 * Proposito: cumprir uma operação e formar seu retrato JSON breve.
 * Pre-condições: argumentos vivos e capacidade verdadeira.
 * Effeitos: governa o fio e publica resposta integral.
 * Retorno: zero ou erro negativo de domínio ou espaço da resposta.
 * Razão: erro da ordem pertence ao JSON; erro da fronteira pertence á chamada.
 */
int cumprir_ordem_da_instancia(
    struct governo_do_apparelho *governo,
    const struct mensagem_de_governo *mensagem,
    unsigned char *resposta, size_t capacidade, uint32_t *quantidade)
{
    struct configuracao_do_apparelho configuracao;
    enum estado_do_governo_do_apparelho estado = ESTADO_DO_GOVERNO_ENCERRADO;
    int resultado_do_servico = 0;
    int erro = 0;
    int tamanho;
    if (governo == 0 || mensagem == 0 || resposta == 0 || quantidade == 0)
        return -EINVAL;
    switch (mensagem->cabecalho.operacao) {
    case OPERACAO_DE_GOVERNO_CREAR:
        erro = ler_carga_de_creacao(&configuracao, mensagem->carga,
                                    mensagem->cabecalho.quantidade_da_carga);
        if (erro == 0) erro = crear_apparelho_governado(governo, &configuracao);
        break;
    case OPERACAO_DE_GOVERNO_CONTEMPLAR:
        if (mensagem->cabecalho.quantidade_da_carga != 0) erro = -EMSGSIZE;
        break;
    case OPERACAO_DE_GOVERNO_DESTRUIR:
        if (mensagem->cabecalho.quantidade_da_carga != 0) erro = -EMSGSIZE;
        else erro = destruir_apparelho_governado(governo);
        break;
    default: erro = -EOPNOTSUPP;
    }
    if (contemplar_apparelho_governado(
            governo, &estado, &resultado_do_servico) < 0 && erro == 0)
        erro = -EINVAL;
    if (mensagem->cabecalho.operacao == OPERACAO_DE_GOVERNO_CONTEMPLAR &&
        erro == 0) {
        tamanho = escrever_estado_integral(
            resposta, capacidade, governo, estado, resultado_do_servico);
        if (tamanho < 0 || (size_t)tamanho >= capacidade) return -ENOBUFS;
        *quantidade = (uint32_t)tamanho;
        return 0;
    }
    tamanho = snprintf((char *)resposta, capacidade,
        "{\"ok\":%s,\"estado\":\"%s\",\"erro\":%d,\"resultado\":%d}",
        erro == 0 ? "true" : "false", nome_do_estado_do_governo(estado),
        erro, resultado_do_servico);
    if (tamanho < 0 || (size_t)tamanho >= capacidade) return -ENOBUFS;
    *quantidade = (uint32_t)tamanho;
    return 0;
}
