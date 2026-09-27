# lumen — o sistema como sua própria linguagem

Um sistema distroless não é um sistema sem dependências. É um sistema **sem
identificadores não-ligados**.

`sh` não é uma feature. É um nome que resolve para código arbitrário com os
seus bits. Um interpretador no namespace é um `eval` no vocabulário do sistema.
Tirá-lo não é remover dependência — é remover a habilidade de o sistema se
reescrever sozinho.

> **O vocabulário de um sistema é o conjunto de nomes que ele pode dizer.**
> Distroless é o que torna esse conjunto explícito.

## A tese em três partes

**1. Gramática.** Um plano que nomeie uma capacidade não montada não compila.
Não é "bloqueado por uma política" — é rejeitado como nome inexistente. O kernel
é o type checker.

**2. Assimetria.** O sistema tem duas metades com responsabilidades diferentes,
e a distinção importa:

| | Quem fecha | Mecanismo |
|---|---|---|
| **Vocabulário** (o que pode dizer) | compositor + kernel | namespace: nome não montado = nome inexistente |
| **Veracidade** (o que disse que fez) | a própria tool | syscall real + exit code, verificado pelo compositor |

A segunda metade é defense, não garantia. O kernel torna a mentira *detectável*
(uma afirmação sem syscall atrás tem de falhar); ele não impede a mentira. Fechar
a veracidade é trabalho da tool, e o compositor reescreve stdout a partir do
exit code, nunca o contrário.

**3. Re-derivação.** Snapshot restaura o *estado*. Gramática restaura o *sentido*.
Se toda operação é função pura do transcript, o estado é cache e o replay é mais
barato que o restore — medido abaixo.

## Números medidos

Host: Bluefin/Silverblue, kernel 7.1.13-200.fc44.x86_64, medido em 2026-09-26.

    bwrap (unshare-all completo):        0.00 s
    Landlock ABI version:                9

`loop` sobre 200 passos, 7 execuções (faixa, não número único — o `restore` usa
`system()` e paga fork de shell, por isso varia):

    passo           0.009 – 0.017 ms   executar
    replay/passo    0.006 – 0.010 ms   re-derivar
    restore total   1.2 – 2.7 ms      descartar

A relação que importa é estável: **replay/passo < passo em 7 de 7 execuções.**
Refazer é mais barato que executar, e muito mais barato que descartar, porque
re-derivar não custa I/O e recuperar custa. Essa é a assinatura de operações
puras.

Limite honesto do número: `square(i) = i²` é O(1) e pura. Replay de tool com
efeito externo **não** é idempotente nem fica mais barato na segunda vez. O
0.006 ms vale para efeito interno, não para rede.

Isolamento a 0.00 s tem uma consequência architectural: **não precisa de daemon**.
Um processo novo, confinado, por operação. Um daemon persistente é o que faz um
monólito, e o custo zero o torna desnecessário.

## Limite honesto: `/snap` restaurável, `/fora` não

    [3] RESTAURAÇÃO   /snap/inside/state.json  → {"step":0}          restaurado
                     /snap/inside/leak.txt     → segredo no contexto  SOBREVIVE
    [5] RESTAURAÇÃO   /outside/git-push.log    → commit publicado     IMPERDOEL
                     /outside/api-call.log     → POST /v1/chat        IMPERDOEL
                     /outside/key-use.log      → sk-live usada        IMPERDOEL

Snapshot é um sistema de arquivos com superpoder de rewind. Ele não tem
jurisdiction sobre o mundo, e um modelo com prompt injection não precisa de nada
além de um `curl` para sair do snapshot em 40 ms.

Por isso a resposta honesta não é "restaura tudo":

| Tipo de efeito | Reversível como | Mecanismo |
|---|---|---|
| arquivo, estado derivado | **replay** | CoW + re-derivação do transcript |
| escrita confinada (`/w`) | **replay** | tmpfs por chamada, descartado com o processo |
| efeito externo (push, API, email) | **compensação** | journal antes do efeito; desfazer é outro passo nomeado |

Efeito externo não tem snapshot de mundo. Tem journal. O compositor existe
exatamente para ser o único ponto onde o efeito é nomeado antes de acontecer.

## Provas

Cada uma é um executável sem dependências. `make proof` compila e roda as três.

- `proofs/sel.c` — Landlock puro, sem bwrap, sem systemd, sem compositor. Lacre
  em `main()`, prova o vocabulário: o que está fora do ruleset não abre.
- `proofs/foto.c` — snapshot vs. egress. Mostra que `/snap` rewind e `/fora` não.
- `proofs/loop.c` — snapshot → step → restore → replay, com os tempos.
- `proofs/README.md` — as três provas com output real e as duas armadilhas do
  Landlock que custaram uma execução cada.

## O compositor

`src/lumen.c` (340 LOC) + `src/caps/{mark,probe}.c`. Lê um plano NDJSON e para
cada passo executa `journal → fork → landlock → exec`.

```
{"lumen":"boot","abi":9,"write":"/tmp/lumen-w"}
mark: 21 bytes em /tmp/lumen-w/transcript.log
{"tool":"mark","status":"ok","exit":0,"effect":"internal"}
mark: nenhuma concessao de escrita
{"tool":"mark","status":"denied","exit":1,"effect":"internal"}
{"tool":"shell","status":"absent"}
```

Três decisões:

1. **O poder é propriedade do passo, não do binário.** O plano pede
   `"write":true`; só então o filho recebe escrita no Landlock *e* no argv.
   Mesmo binário, resultado diferente — porque o plano pediu diferente.
2. **stdout do filho é descartado.** A saída é reconstruída a partir do exit
   code. A tool pode mentir sem mudar o que o sistema acredita.
3. **O journal é a única autoridade**, escrito antes do fork. A tool não tem
   poder para reescrever o registro do que planejou.

`shell` → `absent`: nome não montado é nome inexistente.

## Armadilhas do Landlock (medidas, custaram uma execução cada)

1. `O_TRUNC` exige `TRUNCATE` (ABI ≥ 6), bit **distinto** de `WRITE_FILE`. Sem
   ele: criar novo funciona, truncar existente dá `EACCES`. A demo dava
   "permitida" na 1ª execução e "Permission denied" na 2ª, sem mudança no código.
2. `unlink()` exige `REMOVE_FILE`, não concedido. A limpeza falha e o `O_EXCL`
   seguinte dá `EEXIST` — falso-negativo parecendo "arquivo já existe".

Solução: path único por execução, `O_CREAT|O_EXCL`, sem estado entre execuções.
Determinístico em 3 execuções seguidas.

## Critério de qualidade

Uma tool distroless é **closed sobre suas dependências**: a soma das suas entradas
é exatamente a soma das suas permissões. Se uma tool precisa de algo que não
declarou, ela não é distroless — é um processo com ambição.

## Estado

ADR-001 aceito. Compositor (parser do plano, journal antes do efeito,
`commit → fork → landlock → exec`) em construção — ver `docs/ADR-001.md`.
