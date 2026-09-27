# Changelog

Formato: Keep a Changelog. Toda decisão arquitetural vem pareada com entrada aqui
(ver `docs/ADR-001.md`).

## [Unreleased]

### Adicionado

- ADR-001 — a gramática como sistema: vocabulário = conjunto de nomes que o
  sistema pode dizer; distroless = ausência de identificadores não-ligados.
- Três provas executáveis em `proofs/`, sem dependências externas:
  - `sel.c` — Landlock puro (ABI 9) lacrando em `main()`, sem bwrap, sem
    systemd, sem compositor. Prova o vocabulário: `.hermes/.env` e `.ssh` fora
    do ruleset, `repos` dentro.
  - `foto.c` — snapshot vs. egress. `/snap` restaurável, `/fora` imperdoel
    (git push publicado, chave consumida, API chamada).
  - `loop.c` — snapshot → step → restore → replay com medição. Em 7 execuções,
    `replay/passo` < `passo` sempre (0.006–0.010 ms vs 0.009–0.017 ms).
- `Makefile` com `make proof` (compila e roda as três).

### Decidido

- Assimetria explícita entre **vocabulário** (imposto por compositor + kernel,
  à prova de modelo hostil) e **veracidade** (depende de syscall real na tool,
  defense e não garantia). Medido: tool que afirma escrita inexistente com
  exit 0 quando `/w` não está montado.
- Efeito externo não tem snapshot — tem **journal** e compensação. É a razão de
  existir um compositor: ser o único ponto onde o efeito é nomeado antes de
  acontecer.
- Sem daemon. Isolamento a 0.00 s (`bwrap` unshare-all) permite um processo
  novo e confinado por operação.
- Critério de qualidade distroless: a tool é *closed sobre suas dependências* —
  soma das entradas = soma das permissões.

### Rejeitado

- Reescrita de agente existente em Rust (561k LOC, consumidor puro de upstream).
- Electron → Docker com Nginx (o `Dockerfile` upstream já builda o dist;
  `web_server.py` já faz `mount_spa`).
- `network_mode: host` como isolamento.
- D-Bus como barramento; Flatpak portal como RPC.
- Nix/Guix: resolvem reprodutibilidade de build, que é outro eixo.

### Limites declarados

- A veracidade é convenção honesta — o que um LLM pode quebrar. O compositor
  reescreve stdout a partir do exit code; isso é defense, não garantia.
- O número de replay vale para efeito interno. Tool com efeito externo não é
  idempotente e não fica mais barata na segunda execução.
