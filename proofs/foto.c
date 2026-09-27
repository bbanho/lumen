/* Foto do argumento: snapshot rewind o que está dentro. O que está
 * fora, não.
 *
 *   /snap    = o estado que você pode restaurar
 *   /outside = ligação ao host (credencial, egress, git push)
 *
 *   gcc -O2 -o foto foto.c && ./foto
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>

static void wr(const char *p, const char *s) {
    int f = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (f < 0) { printf("  %-24s BLOQUEADO (%s)\n", p, strerror(errno)); return; }
    write(f, s, strlen(s)); close(f);
    printf("  %-24s escrito\n", p);
}
static void rd(const char *p, const char *tag) {
    char b[4096] = {0};
    int f = open(p, O_RDONLY);
    if (f < 0) { printf("  %-24s %-9s ausente\n", p, tag); return; }
    ssize_t n = read(f, b, sizeof b - 1); close(f);
    printf("  %-24s %-9s %s", p, tag, n > 0 ? b : "(vazio)\n");
}

int main(void) {
    mkdir("/tmp/foto", 0700);
    system("rm -rf /tmp/foto/inside /tmp/foto/outside; "
           "mkdir -p /tmp/foto/inside /tmp/foto/outside");

    puts("\n[1] SNAPSHOT  —  o estado bom, congelado");
    wr("/tmp/foto/inside/state.json", "{\"step\":0}\n");
    rd("/tmp/foto/inside/state.json", "snap:");

    puts("\n[2] O AGENTE AGE  —  dentro do snapshot");
    wr("/tmp/foto/inside/state.json", "{\"step\":1,\"secreto\":\"sk-live-...\"}\n");
    wr("/tmp/foto/inside/leak.txt",     "contexto do modelo CONTEM o segredo\n");

    puts("\n[3] RESTAURAÇÃO  —  rewind, segundos");
    wr("/tmp/foto/inside/state.json", "{\"step\":0}\n");   /* copy-on-write */
    rd("/tmp/foto/inside/state.json", "depois:");
    rd("/tmp/foto/inside/leak.txt",   "depois:");

    puts("\n[4] O MESMO AGENTE, MESMO INSTANTE  —  do lado de fora");
    wr("/tmp/foto/outside/git-push.log", "commit publicado: secrets\n");
    wr("/tmp/foto/outside/api-call.log",  "POST /v1/chat  3021 tokens\n");
    wr("/tmp/foto/outside/key-use.log",   "sk-live-... usada em api.externa\n");

    puts("\n[5] RESTAURAÇÃO DE NOVO  —  e agora?");
    rd("/tmp/foto/outside/git-push.log", "depois:");
    rd("/tmp/foto/outside/api-call.log",  "depois:");
    rd("/tmp/foto/outside/key-use.log",   "depois:");

    puts("\n      /snap  -> restauravel      /fora  -> IMPERDOEL");
    return 0;
}
