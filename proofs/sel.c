/* sel — a capability that IS its own boundary.
 *
 * NO compositor. NO daemon. NO init. NO userland.
 * The binary declares its vocabulary; the kernel enforces it.
 * Landlock is applied in main() before any logic runs.
 *
 * Vocabulary = the exact set of paths this process may touch.
 * Everything else is a name that does not resolve.
 *
 *   cc -O2 -o sel sel.c && ./sel
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/landlock.h>

/* ── Landlock ABI ──────────────────────────────────────────────────── */
static int ll_create(void *attr, size_t sz, __u32 flags) {
    return syscall(__NR_landlock_create_ruleset, attr, sz, flags);
}
static int ll_add(int fd, int type, const void *attr, __u32 flags) {
    return syscall(__NR_landlock_add_rule, fd, type, attr, flags);
}
static int ll_restrict(int fd, __u32 flags) {
    return syscall(__NR_landlock_restrict_self, fd, flags);
}

/* Grant: read+exec on a path, nothing else. */
static int allow_path(int rlsd, const char *path, __u64 access) {
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) return -1;
    struct landlock_path_beneath_attr pb = {
        .allowed_access = access,
        .parent_fd = fd,
    };
    int r = ll_add(rlsd, LANDLOCK_RULE_PATH_BENEATH, &pb, 0);
    close(fd);
    return r;
}

#define RD (LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR)
#define WR (LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_REG)

/* ── the grammar ─────────────────────────────────────────────────────
 * Everything this process can SAY. One verb, one path, one line out.
 */
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 1. Am I on a kernel that has grammar? */
    int abi = ll_create(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 4) {
        printf("{\"tool\":\"sel\",\"status\":\"unsupported\",\"abi\":%d}\n", abi);
        return 2;
    }

    /* 2. Announce: "I will speak, and only speak, about these paths." */
    const char *tree = (argc > 1) ? argv[1] : "/var/home/bruno/repos";

    __u64 handled = LANDLOCK_ACCESS_FS_EXECUTE
                  | LANDLOCK_ACCESS_FS_WRITE_FILE
                  | LANDLOCK_ACCESS_FS_READ_FILE
                  | LANDLOCK_ACCESS_FS_READ_DIR
                  | LANDLOCK_ACCESS_FS_REMOVE_DIR
                  | LANDLOCK_ACCESS_FS_REMOVE_FILE
                  | LANDLOCK_ACCESS_FS_MAKE_CHAR
                  | LANDLOCK_ACCESS_FS_MAKE_DIR
                  | LANDLOCK_ACCESS_FS_MAKE_REG
                  | LANDLOCK_ACCESS_FS_MAKE_SOCK
                  | LANDLOCK_ACCESS_FS_MAKE_FIFO
                  | LANDLOCK_ACCESS_FS_MAKE_BLOCK
                  | LANDLOCK_ACCESS_FS_MAKE_SYM;
    if (abi >= 5) handled |= LANDLOCK_ACCESS_FS_REFER;      /* rename/link */
    if (abi >= 6) handled |= LANDLOCK_ACCESS_FS_TRUNCATE;
    if (abi >= 9) handled |= LANDLOCK_ACCESS_FS_IOCTL_DEV;

    struct landlock_ruleset_attr ra = { .handled_access_fs = handled };
    int rlsd = ll_create(&ra, sizeof ra, 0);
    if (rlsd < 0) { perror("create"); return 2; }

    /* 3. The only permissions. Everything else: revoked. */
    __u64 read_only = LANDLOCK_ACCESS_FS_EXECUTE | RD;
    if (allow_path(rlsd, "/usr", read_only) < 0) perror("rule:/usr");
    if (allow_path(rlsd, "/etc", RD) < 0)          perror("rule:/etc");
    if (allow_path(rlsd, tree, RD | WR) < 0)       perror("rule:tree");
    if (allow_path(rlsd, "/tmp", RD | WR) < 0)     perror("rule:/tmp");

    /* 4. Seal. From here: enforced by the kernel, not by my discipline. */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) perror("nnp");
    if (ll_restrict(rlsd, 0) < 0) { perror("restrict"); return 2; }
    close(rlsd);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);   /* no core, no ptrace peek */

    /* 5. The grammar is live. Speak from inside it. */
    printf("{\"tool\":\"sel\",\"abi\":%d,\"vocab\":[\"%s\"],\"status\":\"sealed\"}\n",
           abi, tree);

    /* Proof: the vocabulary holds. */
    const char *probe[] = { "/var/home/bruno/.hermes/.env",
                            "/var/home/bruno/repos",
                            "/var/home/bruno/.ssh" };
    for (int i = 0; i < 3; i++) {
        int fd = open(probe[i], O_RDONLY);
        if (fd >= 0) { printf("  %-38s DENTRO\n", probe[i]); close(fd); }
        else         printf("  %-38s fora (EPERM/EACCES)\n", probe[i]);
    }

    /* Proof: it can act where it declared.
     *
     * CUIDADO — duas armadilhas reais do Landlock, medidas aqui:
     *
     * 1. `O_TRUNC` exige LANDLOCK_ACCESS_FS_TRUNCATE (ABI >= 6), que e' um bit
     *    DISTINTO de WRITE_FILE. Conceder WRITE_FILE sem TRUNCATE faz criar
     *    arquivo novo funcionar (MAKE_REG) e truncar um existente falhar com
     *    EACCES — a mesma demonstracao daria "permitida" na 1a execucao e
     *    "Permission denied" na 2a, sem nada ter mudado no codigo.
     *
     * 2. `unlink()` exige LANDLOCK_ACCESS_FS_REMOVE_FILE, tambem nao concedido
     *    aqui. Se usassemos unlink() para limpar o probe, a limpeza falharia
     *    e o O_EXCL seguinte daria EEXIST — outro falso-negativo.
     *
     * Solucao sem tocar no ruleset: um path unico por execucao, dentro de um
     * diretorio proprio. Sem unlink, sem O_EXCL, sem estado entre execucoes.
     */
    char probefile[256];
    snprintf(probefile, sizeof probefile, "/tmp/.sel-probe-%d", (int)getpid());
    int w = open(probefile, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (w >= 0) { write(w, "x", 1); close(w);
                  printf("  escrita em %s: permitida\n", probefile);
                  unlink(probefile);   /* pode falhar; o path e' unico */
    }
    else        printf("  escrita em %s: %s\n", probefile, strerror(errno));
    return 0;
}
