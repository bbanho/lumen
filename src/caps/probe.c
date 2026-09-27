/* probe — a capability de rede. A unica com efeito externo.
 *
 * Esta e a tool que o snapshot nao alcança. Se voce trocar o host por um
 * endpoint de verdade, este argv vira um POST e o git push vira um push.
 * O que NAO muda: o compositor ja escreveu no journal antes do fork, e o
 * exit code e o unico fato.
 *
 * Sem DNS proprio, sem libcurl, sem TLS: apenas o suficiente para mostrar
 * que a fronteira existe e que ela e imposta de fora.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

/* Allowlist literal. Uma tool distroless e closed sobre suas entradas:
 * a soma do que ela pode falar e a soma do que ela recebeu. */
static const char *ALLOWED[] = { "127.0.0.1", "localhost", NULL };

static int permitted(const char *host) {
    for (int i = 0; ALLOWED[i]; i++)
        if (strcmp(host, ALLOWED[i]) == 0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *host = (argc > 1) ? argv[1] : "";
    const char *port = (argc > 2) ? argv[2] : "80";

    if (!*host) {
        fprintf(stderr, "probe: sem host\n");
        printf("{\"tool\":\"probe\",\"status\":\"absent\"}\n");
        return 1;
    }
    if (!permitted(host)) {
        /* Nao e um aviso. E a fronteira. */
        fprintf(stderr, "probe: %s nao esta na allowlist\n", host);
        printf("{\"tool\":\"probe\",\"status\":\"denied\",\"host\":\"%s\"}\n", host);
        return 1;
    }

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) {
        fprintf(stderr, "probe: %s:%s nao resolve\n", host, port);
        printf("{\"tool\":\"probe\",\"status\":\"unresolved\"}\n");
        return 1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return 1; }
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rc != 0) {
        fprintf(stderr, "probe: %s:%s connect: %s\n", host, port, strerror(errno));
        printf("{\"tool\":\"probe\",\"status\":\"unreachable\"}\n");
        return 1;
    }
    close(fd);
    fprintf(stderr, "probe: conectado a %s:%s\n", host, port);
    printf("{\"tool\":\"probe\",\"status\":\"ok\",\"host\":\"%s\"}\n", host);
    return 0;
}
