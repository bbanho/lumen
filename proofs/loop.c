/* loop — a tese completa em um executável:
 *
 *   snapshot -> step -> restore -> replay
 *
 * Mede quanto tempo leva para o sistema voltar ao estado bom e
 * refazer o trabalho. Se o replay e ordem de milissegundos, entao
 * o snapshot vira detalhe e o estado real e o transcript.
 *
 *   gcc -O2 -o loop loop.c && ./loop
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#define STEPS 200

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static void mkdirp(const char *p) { char b[512]; snprintf(b, sizeof b, "%s", p); system(b); }
static void reset(void) {
    system("rm -rf /tmp/loop/snap /tmp/loop/live; "
           "mkdir -p /tmp/loop/snap /tmp/loop/live");
}

/* Um "tool": le input, escreve output. Funcoes puras por construcao. */
static void tool_run(const char *in, char *out, size_t n) {
    int f = open(in, O_RDONLY);
    if (f < 0) { snprintf(out, n, "{\"err\":\"sem input\"}"); return; }
    char b[4096] = {0};
    ssize_t r = read(f, b, sizeof b - 1); close(f);
    if (r <= 0) { snprintf(out, n, "{\"err\":\"vazio\"}"); return; }
    int step = atoi(b);
    snprintf(out, n, "{\"step\":%d,\"acc\":%d}\n", step, step * step);
}

int main(void) {
    mkdirp("mkdir -p /tmp/loop");
    reset();

    /* ── 1. SNAPSHOT: o transcript como fonte da verdade ───────────── */
    puts("[1] SNAPSHOT  —  o transcript e a unica fonte da verdade");
    FILE *t = fopen("/tmp/loop/snap/transcript.log", "w");
    for (int i = 0; i < STEPS; i++) fprintf(t, "{\"i\":%d,\"tool\":\"square\",\"arg\":\"%d\"}\n", i, i);
    fclose(t);
    struct stat st; stat("/tmp/loop/snap/transcript.log", &st);
    printf("  transcript: %d passos, %ld bytes\n", STEPS, (long)st.st_size);
    /* snapshot = copia do transcript. Nao do estado derivado. */
    system("cp /tmp/loop/snap/transcript.log /tmp/loop/snap/.t && "
           "mv /tmp/loop/snap/.t /tmp/loop/snap/transcript.log");

    /* ── 2. STEP: o agente age, o estado deriva ────────────────────── */
    puts("\n[2] STEP  —  o agente age sobre o estado derivado");
    double t0 = now_ms();
    for (int i = 0; i < STEPS; i++) {
        char in[256], out[256];
        snprintf(in, sizeof in, "/tmp/loop/snap/transcript.log");
        /* deriva: roda a tool, guarda resultado */
        char scratch[256] = {0};
        int f = open("/tmp/loop/snap/.in", O_WRONLY|O_CREAT|O_TRUNC, 0600);
        int v = i; char nb[32]; int k = snprintf(nb, sizeof nb, "%d", v);
        write(f, nb, k); close(f);
        tool_run("/tmp/loop/snap/.in", scratch, sizeof scratch);
        int g = open("/tmp/loop/live/out.json", O_WRONLY|O_CREAT|O_TRUNC, 0600);
        write(g, scratch, strlen(scratch)); close(g);
    }
    double t1 = now_ms();
    printf("  %d passos executados em %.1f ms  (%.3f ms/passo)\n",
           STEPS, t1 - t0, (t1 - t0) / STEPS);

    /* ── 3. RESTORE: restaura o transcript, nao o estado ───────────── */
    puts("\n[3] RESTORE  —  o que se restaura e o INPUT, nao o OUTPUT");
    double r0 = now_ms();
    system("rm -f /tmp/loop/live/out.json /tmp/loop/snap/.in");
    double r1 = now_ms();
    printf("  estado derivado descartado em %.2f ms\n", r1 - r0);
    /* o transcript nem foi tocado: o agente nao pode escrever nele */

    /* ── 4. REPLAY: refaz do zero, a partir da gramática ───────────── */
    puts("\n[4] REPLAY  —  o sistema e re-derivado, nao recuperado");
    double p0 = now_ms();
    FILE *in = fopen("/tmp/loop/snap/transcript.log", "r");
    char line[256]; int n = 0;
    while (in && fgets(line, sizeof line, in)) {
        int idx = 0, arg = 0;
        if (sscanf(line, "{\"i\":%d,\"tool\":\"square\",\"arg\":\"%d\"}", &idx, &arg) == 2) {
            char scratch[256];
            char tmp[256];
            snprintf(tmp, sizeof tmp, "/tmp/loop/snap/.in");
            int f = open(tmp, O_WRONLY|O_CREAT|O_TRUNC, 0600);
            char nb[32]; int k = snprintf(nb, sizeof nb, "%d", arg);
            write(f, nb, k); close(f);
            tool_run(tmp, scratch, sizeof scratch);
            n++;
        }
    }
    if (in) fclose(in);
    double p1 = now_ms();
    printf("  %d passos re-derivados em %.1f ms  (%.3f ms/passo)\n",
           n, p1 - p0, (p1 - p0) / n);

    /* ── 5. O VEREDICTO ────────────────────────────────────────────── */
    puts("\n[5] VEREDICTO");
    printf("  restore = %.2f ms   (descartar)\n", r1 - r0);
    printf("  replay  = %.1f ms   (refazer do zero)\n", p1 - p0);
    printf("  passo   = %.3f ms  (executar)\n", (t1 - t0) / STEPS);
    printf("  replay/passo = %.3f ms  vs  passo = %.3f ms\n",
           (p1 - p0) / n, (t1 - t0) / STEPS);
    printf("  se replay < 1 s: o snapshot de disco e desnecessario.\n");
    printf("  o estado e o transcript; o resto e cache.\n");
    return 0;
}
