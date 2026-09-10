COMPILADOR ?= cc
DIRECTORIO_DO_CUDA ?= /usr/local/cuda
DISPOSITIVO ?=
AVISOS := -std=c11 -Wall -Wextra -Wpedantic -Werror
DIRECTORIO_DA_CONSTRUCAO := construcao
PROVAS := $(DIRECTORIO_DA_CONSTRUCAO)/provar_transicoes \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_configuracao \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_configuracao_decimal \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_plano_da_memoria \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_reserva_de_buffers \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_carga_de_creacao \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_governo_do_apparelho \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_ordens_da_instancia \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_ordem_do_cliente \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_instancia_do_servidor \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_meio_simulado \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_fila_de_requisicoes \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_protocolo_de_governo \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_canal_de_governo \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_morada_do_governo \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_registro_do_governo \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_tomada_do_governo \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_porteiro_do_governo \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_observatorio \
	$(DIRECTORIO_DA_CONSTRUCAO)/provar_servico_de_governo
FONTES_DO_SERVIDOR := src/principal_do_servidor.c src/instancia_do_servidor.c \
	src/morada_do_governo.c src/registro_do_governo.c src/tomada_do_governo.c \
	src/governo_do_apparelho.c src/servico_de_governo.c src/canal_de_governo.c \
	src/protocolo_de_governo.c src/ordens_da_instancia.c src/carga_de_creacao.c \
	src/servidor_ublk.c src/alvo_ublk.c src/configuracao.c src/numero_decimal.c \
	src/parada_limitada_ublk.c \
	src/estado_da_requisicao.c src/plano_da_memoria.c src/reserva_de_buffers.c \
	src/meio_simulado.c \
	src/meio_cuda.c src/fila_de_requisicoes.c src/retrato_do_observatorio.c \
	src/monitor_do_observatorio.c src/observador_de_si.c
FONTES_DO_CLIENTE := src/principal_do_governo.c src/canal_de_governo.c \
	src/protocolo_de_governo.c src/morada_do_governo.c src/tomada_do_governo.c \
	src/ordem_do_cliente.c src/carga_de_creacao.c src/configuracao_decimal.c \
	src/numero_decimal.c src/configuracao.c
SERVIDOR := $(DIRECTORIO_DA_CONSTRUCAO)/vramdiskd
CLIENTE := $(DIRECTORIO_DA_CONSTRUCAO)/vramdiskctl
PROVA_CUDA := $(DIRECTORIO_DA_CONSTRUCAO)/provar_meio_cuda
DEMONSTRACAO := $(DIRECTORIO_DA_CONSTRUCAO)/demonstrar_observatorio

.PHONY: provar provar_integracao provar_vm demonstrar_simulacao provar_cuda provar_pressao preparar_ublk preparar_cuda preparar_cliente limpar

provar: $(PROVAS)
	@for prova in $(PROVAS); do echo "PROVA: $$prova"; $$prova || exit $$?; done

provar_integracao: provar provar_vm

provar_vm:
	./testes/provar_ublk_simulado_vm.sh

demonstrar_simulacao: $(DEMONSTRACAO)
	$(DEMONSTRACAO)

$(DIRECTORIO_DA_CONSTRUCAO):
	mkdir -p $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_transicoes: \
		testes/provar_transicoes.c src/estado_da_requisicao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_configuracao: \
		testes/provar_configuracao.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_configuracao_decimal: testes/provar_configuracao_decimal.c \
		src/configuracao_decimal.c src/numero_decimal.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_plano_da_memoria: \
		testes/provar_plano_da_memoria.c src/plano_da_memoria.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_reserva_de_buffers: testes/provar_reserva_de_buffers.c \
		src/reserva_de_buffers.c src/plano_da_memoria.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_carga_de_creacao: \
		testes/provar_carga_de_creacao.c src/carga_de_creacao.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_governo_do_apparelho: \
		testes/provar_governo_do_apparelho.c src/governo_do_apparelho.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -pthread $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_ordens_da_instancia: testes/provar_ordens_da_instancia.c \
		src/ordens_da_instancia.c src/governo_do_apparelho.c src/carga_de_creacao.c \
		src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -pthread $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_ordem_do_cliente: testes/provar_ordem_do_cliente.c \
		src/ordem_do_cliente.c src/carga_de_creacao.c src/configuracao_decimal.c \
		src/numero_decimal.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_instancia_do_servidor: \
		testes/provar_instancia_do_servidor.c src/instancia_do_servidor.c \
		src/morada_do_governo.c src/registro_do_governo.c src/tomada_do_governo.c \
		src/governo_do_apparelho.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -pthread $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_meio_simulado: \
		testes/provar_meio_simulado.c src/meio_simulado.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_fila_de_requisicoes: \
		testes/provar_fila_de_requisicoes.c src/fila_de_requisicoes.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_protocolo_de_governo: \
		testes/provar_protocolo_de_governo.c src/protocolo_de_governo.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_canal_de_governo: testes/provar_canal_de_governo.c \
		src/canal_de_governo.c src/protocolo_de_governo.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_morada_do_governo: \
		testes/provar_morada_do_governo.c src/morada_do_governo.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_registro_do_governo: \
		testes/provar_registro_do_governo.c src/registro_do_governo.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_tomada_do_governo: \
		testes/provar_tomada_do_governo.c src/tomada_do_governo.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_porteiro_do_governo: testes/provar_porteiro_do_governo.c \
		src/servico_de_governo.c src/canal_de_governo.c src/protocolo_de_governo.c \
		src/ordens_da_instancia.c src/governo_do_apparelho.c src/carga_de_creacao.c \
		src/configuracao.c src/tomada_do_governo.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -D_GNU_SOURCE -DPRAZO_DA_AUDIENCIA_EM_SEGUNDOS=1 -pthread $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_observatorio: testes/provar_observatorio.c \
		src/retrato_do_observatorio.c src/monitor_do_observatorio.c \
		src/observador_de_si.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

$(DIRECTORIO_DA_CONSTRUCAO)/provar_servico_de_governo: \
		testes/provar_servico_de_governo.c src/servico_de_governo.c \
		src/canal_de_governo.c src/protocolo_de_governo.c \
		src/ordens_da_instancia.c src/carga_de_creacao.c \
		src/instancia_do_servidor.c src/morada_do_governo.c \
		src/registro_do_governo.c src/tomada_do_governo.c \
		src/governo_do_apparelho.c src/configuracao.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -D_GNU_SOURCE -pthread $^ -o $@

$(DEMONSTRACAO): demonstracoes/demonstrar_observatorio.c src/meio_simulado.c \
		src/retrato_do_observatorio.c src/monitor_do_observatorio.c \
		src/observador_de_si.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

preparar_ublk: $(SERVIDOR)
preparar_cuda: $(SERVIDOR)
preparar_cliente: $(CLIENTE)
provar_cuda: $(PROVA_CUDA)
	$(PROVA_CUDA)

provar_pressao:
	./testes/provar_pressao_e_swap.sh "$(DISPOSITIVO)"

$(PROVA_CUDA): testes/provar_meio_cuda.c src/meio_cuda.c \
		src/reserva_de_buffers.c src/plano_da_memoria.c | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -I$(DIRECTORIO_DO_CUDA)/include $^ -o $@ \
		-DPROVAR_INJECCAO_CUDA -lcuda

$(SERVIDOR): $(FONTES_DO_SERVIDOR) | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) -D_GNU_SOURCE -I$(DIRECTORIO_DO_CUDA)/include \
		-isystem$$(pkg-config --variable=includedir ublksrv) $^ -o $@ \
		$$(pkg-config --libs ublksrv) -lcuda -pthread

$(CLIENTE): $(FONTES_DO_CLIENTE) | $(DIRECTORIO_DA_CONSTRUCAO)
	$(COMPILADOR) $(AVISOS) $^ -o $@

limpar:
	rm -f $(PROVAS) $(PROVA_CUDA) $(SERVIDOR) $(CLIENTE) $(DEMONSTRACAO)
