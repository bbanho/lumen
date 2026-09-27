# Changelog

Formato: Keep a Changelog. Toda decisão arquitetural vem pareada com entrada aqui
(ver `docs/ADR-001.md`).

## [Unreleased]

### Adicionado

- **O compositor** (`src/lumen.c`, 340 LOC) — lê um plano NDJSON e para cada
  passo faz `journal → fork → landlock → exec`. Sem daemon, sem async, sem
  userland, sem plugin system.
- Duas capabilities reais (`src/caps/`): `mark` (escrita confinada) e `probe`
  (egress com allowlist literal de host).

### O compositor decide três coisas

1. **O poder é propriedade do passo, não do binário.** O plano pede
   `"write":true`; só então o filho recebe o diretório de escrita no Landlock
   *e* no argv. Mesmo binário, resultado diferente, porque o plano pediu
   diferente. Trocar o plano muda o poder sem recompilar.
2. **stdout do filho é descartado.** O compositor lê o output da tool e o joga
   fora; reconstrói a resposta a partir do exit code. A tool pode mentir à
   vontade sem mudar o que o sistema acredita. `{"status":"ok"}` com exit 1 vira
   `{"status":"denied"}`.
3. **O journal é a única autoridade, e é escrito antes do fork.** Uma tool não
   pode reescrever o registro do que planejou fazer — não tem poder para isso.

Output real (plano de 3 passos):

    {"lumen":"boot","abi":9,"write":"/tmp/lumen-w"}
    mark: 21 bytes em /tmp/lumen-w/transcript.log
    {"tool":"mark","status":"ok","exit":0,"effect":"internal"}
    mark: nenhuma concessao de escrita
    {"tool":"mark","status":"denied","exit":1,"effect":"internal"}
    {"tool":"shell","status":"absent"}
    {"lumen":"done","steps":3,"parse-errors":0}

`shell` → `status:"absent"`: nome não montado é nome inexistente. Sem fallback,
sem `sh`.

### Armadilhas do Landlock, medidas

Duas, ambas com falso-negativo que parece flakiness:

1. `O_TRUNC` exige `TRUNCATE` (ABI ≥ 6), bit **distinto** de `WRITE_FILE`.
   Sem ele, criar arquivo novo funciona e truncar existente dá `EACCES` — a
   demo dava "permitida" na 1ª execução e "Permission denied" na 2ª, sem
   mudança no código.
2. `unlink()` exige `REMOVE_FILE`, não concedido. Limpar o probe falha e o
   `O_EXCL` seguinte dá `EEXIST`.

Solução: path único por execução, `O_CREAT|O_EXCL`, sem estado entre execuções.
Determinístico em 3 execuções seguidas. Registrado em `proofs/README.md`.

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
