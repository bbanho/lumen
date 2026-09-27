CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
PROOFS  = sel foto loop
BINS    = lumen

# diretorio de capacidades: as unicas palavras que o sistema pode dizer
CAPDIR  ?= /tmp/lumen-cap
WRITEDIR?= /tmp/lumen-w

all: $(PROOFS) $(BINS) caps

sel: proofs/sel.c
	$(CC) $(CFLAGS) -o $@ $<

foto: proofs/foto.c
	$(CC) $(CFLAGS) -o $@ $<

loop: proofs/loop.c
	$(CC) $(CFLAGS) -o $@ $<

lumen: src/lumen.c
	$(CC) $(CFLAGS) -o $@ $<

caps: mark probe
	@mkdir -p $(CAPDIR)
	install -m 0755 mark $(CAPDIR)/mark
	install -m 0755 probe $(CAPDIR)/probe
	@echo "  capacidades em $(CAPDIR): $$(ls $(CAPDIR) | tr '\n' ' ')"

mark: src/caps/mark.c
	$(CC) $(CFLAGS) -o $@ $<

probe: src/caps/probe.c
	$(CC) $(CFLAGS) -o $@ $<

proof: $(PROOFS)
	@echo "=== sel (Landlock puro, sem userland) ==="
	@./sel $(CURDIR)
	@echo
	@echo "=== foto (snapshot vs egress) ==="
	@./foto
	@echo
	@echo "=== loop (re-derivacao) ==="
	@./loop

# O compositor. Lê um plano NDJSON e nomeia cada passo.
# LUMEN_TREE  = leitura declarada (ro)
# LUMEN_WRITE = escrita (rw), separada da leitura
run: $(BINS) caps
	@mkdir -p $(WRITEDIR)
	@LUMEN_CAP=$(CAPDIR) LUMEN_TREE=$(CURDIR) LUMEN_WRITE=$(WRITEDIR) \
	 LUMEN_JOURNAL=$(WRITEDIR)/journal.ndjson ./lumen

# Plano de demonstracao: 3 passos, um deles com efeito externo.
demo: run
	@printf '%s\n' \
	  '{"tool":"mark","intent":"primeira anotacao","effect":"internal"}' \
	  '{"tool":"mark","intent":"segunda anotacao","effect":"internal"}' \
	  '{"tool":"probe","intent":"127.0.0.1:9 checar egress","effect":"external"}' \
	  | $(MAKE) --no-print-directory run
	@echo
	@echo "--- journal (a unica autoridade):"
	@cat $(WRITEDIR)/journal.ndjson

clean:
	rm -f $(PROOFS) $(BINS) mark probe
	rm -rf $(CAPDIR) $(WRITEDIR)

.PHONY: all proof run demo caps clean
