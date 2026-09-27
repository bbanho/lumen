# lumen/proofs — demonstrações executáveis com números medidos

Três provas de conceito independentes, compiláveis sem deps externas, cada uma
com output real colado abaixo.

**Build:** use o `Makefile` da raiz (`make proof` compila e roda os três).
O alvo `sel` da raiz passa o path a ler como argumento.

---

## sel — Landlock como fronteira declarada

**Output real medido (2026-09-26, kernel 7.1.13-200.fc44, Landlock ABI 9):**

```
{"tool":"sel","abi":9,"vocab":["/var/home/bruno/repos/lumen"],"status":"sealed"}
  /var/home/bruno/.hermes/.env           fora (EPERM/EACCES)
  /var/home/bruno/repos                  fora (EPERM/EACCES)
  /var/home/bruno/.ssh                   fora (EPERM/EACCES)
  escrita em /tmp/.sel-probe-<pid>: permitida
```

**O que prova:** o binário declara seu vocabulário (paths permitidos) *antes*
de qualquer lógica; o kernel enforça. Tudo fora falha com EPERM/EACCES — não
por disciplina do código, mas por enforcement do kernel.

**Armadilha do Landlock, medida e corrigida aqui (vale registrar):**
`O_TRUNC` exige `LANDLOCK_ACCESS_FS_TRUNCATE` (ABI ≥ 6), que é um bit **distinto**
de `WRITE_FILE`. Conceder `WRITE_FILE` sem `TRUNCATE` faz *criar* arquivo novo
funcionar (`MAKE_REG`) e *truncar* existente falhar com `EACCES` — a mesma
demonstração dava "permitida" na 1ª execução e "Permission denied" na 2ª, sem
nada ter mudado no código. Por isso o probe usa `O_EXCL` num nome único e é
removido ao fim. Não confunda flakiness com mudança de comportamento.

---

## foto — snapshot rewind vs. fora irrecuperável

```
[3] RESTAURAÇÃO
  /tmp/foto/inside/state.json depois:   {"step":0}          restaurado
  /tmp/foto/inside/leak.txt   depois:   contexto do modelo CONTEM o segredo  SOBREVIVE
[5] RESTAURAÇÃO DE NOVO
  /outside/git-push.log   depois:   commit publicado: secrets    IMPERDOEL
  /outside/api-call.log   depois:   POST /v1/chat  3021 tokens   IMPERDOEL
  /outside/key-use.log    depois:   sk-live-... usada            IMPERDOEL
```

**O que prova:** dentro do snapshot o estado é restaurável. Fora, efeito externo
(`git push`, API, key use) **não** é revertido. A fronteira entre
"restaurável" e "imperdoável" é o escopo do snapshot — e é por isso que o
compositor journaliza o efeito *antes* de ele acontecer.

---

## loop — transcript como fonte da verdade; replay em milissegundos

**Números medidos, 7 execuções (faixa, não número único):**

| | ms/passo | total (ms) |
|---|---|---|
| `passo` (executar) | 0.010 – 0.022 | — |
| `replay/passo` (re-derivar) | 0.006 – 0.010 | 1.1 – 1.9 |
| `restore` (descartar) | — | 1.29 – 2.36 |

A relação que sustenta a tese: **`replay/passo < passo` em 7 de 7 execuções.**
Refazer é mais barato que executar, e muito mais barato que descartar, porque
re-derivar não custa I/O e recuperar custa. É a assinatura de operações puras.
O `restore` paga `fork` de shell (`system()`), por isso varia mais.

**Limite honesto:** a tool sintética (`square(i)=i²`) é função pura O(1). Replay
de tool com efeito externo (rede, disco, API) **não** é idempotente e **não** fica
mais barato na segunda vez. O número prova a tese para **efeito interno puro**,
não para rede. O transcript é a verdade; o estado derivado é cache descartável.

---

**Requisitos:** kernel Linux com Landlock (para `sel`), `cc`, libc padrão.
Sem dependências externas. Sem rede.