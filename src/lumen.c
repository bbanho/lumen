/* lumen — o compositor.
 *
 * Um plano NDJSON em stdin. Para cada passo:
 *
 *     journal  →  fork  →  landlock  →  exec
 *
 * O compositor nao executa nada. Ele nomeia. E o nome e a concessao.
 *
 * Contrato de saida: stdout e NDJSON reconstruido a partir do EXIT CODE do
 * filho, nunca a partir do stdout do filho. O stdout da tool e uma AFIRMAACAO;
 * o exit code e o FATO. Um plano que diz "escrevi" e um filho que retorna 0
 * sao coisas diferentes, e o compositor so acredita no segundo.
 *
 * Sem dependencias. Sem daemon. Sem userland. Sem async.
 *
 *   cc -O2 -o lumen lumen.c && ./lumen < plano.ndjson
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sched.h>
#include <linux/landlock.h>

/* O nome que a capability conhece. Nao e o path do host: o filho recebe
 * um tmpfs montado sobre este ponto, e a tool nunca ve o path real. */
#define WD "/w"

/* ══ landlock ═══════════════════════════════════════════════════════ */

static int ll_create(void *attr, size_t sz, __u32 flags) {
    return syscall(__NR_landlock_create_ruleset, attr, sz, flags);
}
static int ll_add(int fd, int type, const void *attr, __u32 flags) {
    return syscall(__NR_landlock_add_rule, fd, type, attr, flags);
}
static int ll_restrict(int fd, __u32 flags) {
    return syscall(__NR_landlock_restrict_self, fd, flags);
}
static int allow(int rlsd, const char *path, __u64 acc) {
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) return -1;
    struct landlock_path_beneath_attr pb = { .allowed_access = acc, .parent_fd = fd };
    int r = ll_add(rlsd, LANDLOCK_RULE_PATH_BENEATH, &pb, 0);
    close(fd);
    return r;
}

#define RD  (LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR)
#define WR  (LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_REG)
#define ROX (LANDLOCK_ACCESS_FS_EXECUTE | RD)

/* ══ journal ══════════════════════════════════════════════════════════ */

/* O transcript e a UNICA autoridade. Append-only, fora de qualquer tool:
 * uma tool nao pode reescrever o registro do que planejou fazer. */
static FILE *JOURNAL = NULL;

static void jline(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void jline(const char *fmt, ...) {
    if (!JOURNAL) return;
    va_list ap; va_start(ap, fmt);
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    fprintf(JOURNAL, "{\"ts\":%lld.%09ld,\"pid\":%d,",
            (long long)t.tv_sec, t.tv_nsec, (int)getpid());
    vfprintf(JOURNAL, fmt, ap);
    fputs("}\n", JOURNAL);
    fflush(JOURNAL);
    fsync(fileno(JOURNAL));
}

static void jopen(const char *path) {
    JOURNAL = fopen(path, "a");
    if (!JOURNAL) { fprintf(stderr, "lumen: journal %s: %s\n", path, strerror(errno)); exit(3); }
}

/* ══ o plano ═════════════════════════════════════════════════════════ */

/* Um passo e:  {"tool":"nome","args":["..."],"intent":"...","effect":"internal|external"}
 *
 * Deliberadamente nao ha parser JSON. Ha uma gramática, e a gramatica e
 * pequena: a chave e o valor sao separados por ':' e o valor dura ate a
 * virgula ou o fim da linha. Um plano que nomeie uma capacidade ausente
 * nao compila — nao ha caminho para ele.
 */

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

/* Extrai "chave": "valor" (ou valor sem aspas). Devolve 1 se achou. */
static int field(const char *line, const char *key, char *out, size_t n) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        const char *q = p;
        while (*q && *q != '"') q++;
        size_t len = (size_t)(q - p);
        if (len >= n) len = n - 1;
        memcpy(out, p, len); out[len] = 0;
    } else {
        const char *q = p;
        while (*q && *q != ',' && *q != '}') q++;
        size_t len = (size_t)(q - p);
        while (len && (p[len-1] == ' ')) len--;
        if (len >= n) len = n - 1;
        memcpy(out, p, len); out[len] = 0;
    }
    return 1;
}

/* ══ exec confinado ══════════════════════════════════════════════════ */

struct step {
    char tool[256];
    char intent[512];
    char effect[16];
    int  wants_write;   /* o plano pediu poder de escrita? */
    int  wants_read;    /* o plano pediu poder de leitura?   */
};

static char CAPDIR[512];

/* O poder e propriedade do PASSO, nao do binario.
 *
 * Um passo so recebe o que o plano pediu, palavra por palavra. Uma tool
 * que pede escrita recebe o diretorio de escrita montado; a mesma tool
 * num passo de leitura recebe um tmpfs vazio, e a escrita e negada.
 * A concessao nao mora na tool — mora no plano. E por isso que trocar o
 * plano muda o poder, sem recompilar nada.
 */
static int seal(const char *tree, const char *wdir, int want_w, int want_r) {
    int abi = ll_create(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 4) return abi;                     /* sem gramatica neste kernel */

    __u64 handled = LANDLOCK_ACCESS_FS_EXECUTE | RD | WR
                  | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE
                  | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_SOCK
                  | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK
                  | LANDLOCK_ACCESS_FS_MAKE_SYM
                  | LANDLOCK_ACCESS_FS_MAKE_CHAR;
    if (abi >= 5) handled |= LANDLOCK_ACCESS_FS_REFER;
    if (abi >= 6) handled |= LANDLOCK_ACCESS_FS_TRUNCATE;
    if (abi >= 9) handled |= LANDLOCK_ACCESS_FS_IOCTL_DEV;

    struct landlock_ruleset_attr ra = { .handled_access_fs = handled };
    int rlsd = ll_create(&ra, sizeof ra, 0);
    if (rlsd < 0) return -1;

    if (allow(rlsd, "/usr", ROX) < 0)   return -1;
    if (allow(rlsd, "/etc", RD)  < 0)   { /* opcional */ }
    if (allow(rlsd, CAPDIR, ROX) < 0)  return -1;   /* so as capacidades */

    if (want_r && tree && allow(rlsd, tree, RD) < 0) return -1;
    if (want_w && allow(rlsd, wdir, RD | WR) < 0)   return -1;

    /* O que NAO foi concedido simplesmente nao existe. Um path ausente
     * do ruleset e um path inexistente — mesma resposta do kernel. */

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -1;
    if (ll_restrict(rlsd, 0) < 0) return -1;
    close(rlsd);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    return abi;
}

/* Uma chamada. O stdout do filho e capturado e DESCARTADO: o compositor
 * reconstrói a saida a partir do codigo. O stderr passa direto (log). */
static int run_step(const struct step *s, const char *tree, const char *wdir,
                    char *claim, size_t cn) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", CAPDIR, s->tool);

    if (access(path, X_OK) != 0) {
        /* Nome inexistente. Nao ha "fallback" e nao ha sh. */
        jline("\"ev\":\"reject\",\"tool\":\"%s\",\"why\":\"capacidade ausente\"", s->tool);
        snprintf(claim, cn, "{\"tool\":\"%s\",\"status\":\"absent\"}", s->tool);
        return 127;
    }

    jline("\"ev\":\"commit\",\"tool\":\"%s\",\"intent\":\"%s\",\"effect\":\"%s\",\"write\":%s,\"read\":%s",
          s->tool, s->intent, s->effect,
          s->wants_write ? "true" : "false",
          s->wants_read  ? "true" : "false");

    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (pid == 0) {
        /* ── filho: lacra, executa, morre ───────────────────────────── */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }

        /* O filesystem e' fechado pelo Landlock, no seal() abaixo: um passo
         * com "write" recebe RD|WR apenas sobre o diretorio de escrita, e
         * nada mais. Nao ha montagem nem namespace extra aqui — o isolamento
         * e de acesso, e o unico namespace em jogo e o de mount do proprio
         * Landlock. Um passo sem "write" recebe esse mesmo diretorio apenas
         * para leitura, e qualquer escrita nele e negada pelo kernel. */
        int abi = seal(tree, wdir, s->wants_write, s->wants_read);
        if (abi < 4) _exit(126);          /* sem Landlock: nao executa */
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);

        char *av[4];
        av[0] = (char *)path;
        av[1] = (char *)s->intent;
        /* O path concedido so existe no argv de quem TEM o poder. Sem
         * "write" no plano, argv[2] e NULL: a tool nao descobre o path
         * nem por argv nem por variavel de ambiente. A concessao nao
         * informa onde ela esta — apenas permite. */
        av[2] = s->wants_write ? (char *)wdir : NULL;
        av[3] = NULL;
        execv(path, av);
        _exit(127);
    }

    /* ── pai: le e DESCARTA o stdout do filho ───────────────────────── */
    close(pipefd[1]);
    char buf[4096];
    ssize_t got = 0, r;
    while ((r = read(pipefd[0], buf + got, sizeof buf - 1 - (size_t)got)) > 0) {
        got += r;
        if ((size_t)got >= sizeof buf - 1) break;
    }
    close(pipefd[0]);
    (void)got;   /* a AFIRMACAO do filho. Nao acreditamos nela. */

    int st = 0;
    waitpid(pid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);

    /* ── a SAIDA e reconstruida a partir do FATO ───────────────────── */
    jline("\"ev\":\"result\",\"tool\":\"%s\",\"exit\":%d", s->tool, code);

    const char *status = code == 0   ? "ok"
                       : code == 127 ? "absent"
                       : code == 126 ? "no-grammar"
                       : "denied";
    snprintf(claim, cn,
             "{\"tool\":\"%s\",\"status\":\"%s\",\"exit\":%d,\"effect\":\"%s\"}",
             s->tool, status, code, s->effect);
    return code;
}

/* ══ main ════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    const char *capdir = getenv("LUMEN_CAP");
    const char *tree  = getenv("LUMEN_TREE");
    const char *wdir  = getenv("LUMEN_WRITE");
    const char *jrnl  = getenv("LUMEN_JOURNAL");
    snprintf(CAPDIR, sizeof CAPDIR, "%s", capdir ? capdir : "/cap");

    if (!wdir) { fprintf(stderr, "lumen: LUMEN_WRITE obrigatorio\n"); return 2; }

    char w[1024];
    snprintf(w, sizeof w, "%s", wdir);
    if (mkdir(w, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "lumen: %s: %s\n", w, strerror(errno)); return 2;
    }
    jopen(jrnl ? jrnl : "/tmp/lumen-journal.log");

    int abi = ll_create(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    jline("\"ev\":\"boot\",\"abi\":%d,\"cap\":\"%s\",\"tree\":\"%s\",\"write\":\"%s\"",
          abi, CAPDIR, tree ? tree : "", w);
    printf("{\"lumen\":\"boot\",\"abi\":%d,\"write\":\"%s\"}\n", abi, w);
    fflush(stdout);

    char line[4096];
    int n = 0, bad = 0;
    while (fgets(line, sizeof line, stdin)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;

        struct step st;
        memset(&st, 0, sizeof st);
        if (!field(s, "tool", st.tool, sizeof st.tool)) {
            jline("\"ev\":\"parse-error\",\"reason\":\"sem tool\"");
            printf("{\"status\":\"parse-error\",\"reason\":\"sem tool\"}\n");
            bad++;
            continue;
        }
        field(s, "intent", st.intent, sizeof st.intent);
        field(s, "effect", st.effect, sizeof st.effect);
        if (!st.effect[0]) snprintf(st.effect, sizeof st.effect, "internal");

        /* O poder e pedido, nao concedido por padrao. Uma tool nao recebe
         * /w so porque existe — recebe porque o plano pediu. */
        char flag[32];
        st.wants_write = field(s, "write", flag, sizeof flag) &&
                         (strstr(flag, "true") || strstr(flag, "1"));
        st.wants_read  = field(s, "read",  flag, sizeof flag) &&
                         (strstr(flag, "true") || strstr(flag, "1"));

        if (abi < 4) {
            /* Kernel sem Landlock: a gramatica nao existe. Dizemos e paramos. */
            printf("{\"status\":\"no-grammar\",\"abi\":%d}\n", abi);
            jline("\"ev\":\"halt\",\"why\":\"ABI %d < 4\"", abi);
            return 2;
        }

        char claim[512];
        int rc = run_step(&st, tree ? tree : w, w, claim, sizeof claim);
        printf("%s\n", claim);
        fflush(stdout);
        n++;
        if (rc == 126) break;               /* filho nao lacrou: para */
    }

    printf("{\"lumen\":\"done\",\"steps\":%d,\"parse-errors\":%d}\n", n, bad);
    jline("\"ev\":\"halt\",\"steps\":%d,\"parse-errors\":%d", n, bad);
    fclose(JOURNAL);
    return 0;
}
