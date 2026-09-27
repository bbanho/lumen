/* mark — a capability de escrita confinada.
 *
 * Regra: nao existe estado fora do diretorio concedido, e o diretorio
 * concedido chega em argv[2] — nunca hardcoded, nunca adivinhado. A tool
 * nao verifica nada: se o open falhar, o exit code diz 1. Verificacao por
 * syscall, nao por disciplina.
 *
 * O journal ja foi escrito pelo compositor ANTES do fork. O que esta tool
 * faz e deixar o fato visivel no exit code.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    const char *text = (argc > 1) ? argv[1] : "";
    const char *wdir = (argc > 2) ? argv[2] : NULL;

    if (!wdir) {
        fprintf(stderr, "mark: nenhuma concessao de escrita\n");
        printf("{\"tool\":\"mark\",\"status\":\"absent\"}\n");
        return 1;
    }

    char path[1024];
    snprintf(path, sizeof path, "%s/transcript.log", wdir);

    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (fd < 0) {
        /* A afirmacao e stdout. O fato e o exit code. O compositor so le o
         * fato, entao a tool pode ser honesta ou nao sem mudar o sistema. */
        fprintf(stderr, "mark: %s: %s\n", path, strerror(errno));
        printf("{\"tool\":\"mark\",\"status\":\"denied\"}\n");
        return 1;
    }

    char buf[4096];
    int n = snprintf(buf, sizeof buf, "{\"text\":\"%s\"}\n", text);
    if (n < 0 || write(fd, buf, (size_t)n) != n) {
        fprintf(stderr, "mark: write: %s\n", strerror(errno));
        close(fd);
        printf("{\"tool\":\"mark\",\"status\":\"write-failed\"}\n");
        return 2;
    }
    fsync(fd);
    close(fd);

    fprintf(stderr, "mark: %d bytes em %s\n", n, path);
    printf("{\"tool\":\"mark\",\"status\":\"ok\",\"bytes\":%d}\n", n);
    return 0;
}
