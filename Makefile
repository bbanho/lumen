CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
PROOFS  = sel foto loop

all: $(PROOFS)

sel: proofs/sel.c
	$(CC) $(CFLAGS) -o $@ $<

foto: proofs/foto.c
	$(CC) $(CFLAGS) -o $@ $<

loop: proofs/loop.c
	$(CC) $(CFLAGS) -o $@ $<

proof: $(PROOFS)
	@echo "=== sel (Landlock puro, sem userland) ==="
	@./sel /var/home/bruno/repos
	@echo
	@echo "=== foto (snapshot vs egress) ==="
	@./foto
	@echo
	@echo "=== loop (re-derivacao) ==="
	@./loop

clean:
	rm -f $(PROOFS)

.PHONY: all proof clean
