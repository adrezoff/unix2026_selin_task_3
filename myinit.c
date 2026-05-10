#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <limits.h>
#include <errno.h>

#define MAXPROC 10
#define MAXARGS 16
#define LOG_PATH "/tmp/myinit.log"

typedef struct {
    char *argv[MAXARGS];
    char in[PATH_MAX];
    char out[PATH_MAX];
    pid_t pid;
} ChildProcess;

ChildProcess pool[MAXPROC];
int pid_count = 0;
char cfg_path[PATH_MAX];

void write_log(const char *msg) {
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd != -1) {
        dprintf(fd, "[PID %d] %s\n", getpid(), msg);
        close(fd);
    }
}

// освобождаем память под argv при перезагрузке конфига
void free_argv(ChildProcess *cp) {
    for (int i = 0; i < MAXARGS; i++) {
        if (cp->argv[i]) {
            free(cp->argv[i]);
            cp->argv[i] = NULL;
        }
    }
}

// разбор строки конфига
void parse_line(char *line, ChildProcess *cp) {
    char *tokens[32];
    int n = 0;

    char *t = strtok(line, " \n");
    while (t && n < 32) {
        tokens[n++] = t;
        t = strtok(NULL, " \n");
    }

    if (n < 3) return;

    // безопасный fallback если realpath не работает
    if (!realpath(tokens[n-2], cp->in)) {
        strncpy(cp->in, tokens[n-2], PATH_MAX);
        cp->in[PATH_MAX-1] = 0;
    }

    if (!realpath(tokens[n-1], cp->out)) {
        strncpy(cp->out, tokens[n-1], PATH_MAX);
        cp->out[PATH_MAX-1] = 0;
    }

    int argc = 0;
    for (int i = 0; i < n-2 && argc < MAXARGS-1; i++) {
        cp->argv[argc++] = strdup(tokens[i]);
    }
    cp->argv[argc] = NULL;
}

// чтение конфигурации
void load_config() {
    FILE *f = fopen(cfg_path, "r");
    if (!f) return;

    char line[1024];

    for (int i = 0; i < MAXPROC; i++) {
        free_argv(&pool[i]);
        memset(&pool[i], 0, sizeof(ChildProcess));
    }

    pid_count = 0;

    while (fgets(line, sizeof(line), f) && pid_count < MAXPROC) {
        parse_line(line, &pool[pid_count]);
        pool[pid_count].pid = 0;
        pid_count++;
    }

    fclose(f);
}

// запуск процесса
void run(int i) {
    if (!pool[i].argv[0])  {
        return;
    }

    if (pool[i].pid > 0) {
        return;
    }

    pid_t cpid = fork();

    if (cpid == 0) {
        int fd_in = open(pool[i].in, O_RDONLY);
        int fd_out = open(pool[i].out, O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (fd_in < 0 || fd_out < 0) exit(1);

        dup2(fd_in, STDIN_FILENO);
        dup2(fd_out, STDOUT_FILENO);

        close(fd_in);
        close(fd_out);

        execvp(pool[i].argv[0], pool[i].argv);
        exit(127);
    }

    if (cpid > 0) {
        pool[i].pid = cpid;

        char buf[512];
        snprintf(buf, sizeof(buf),
                 "Started %s (PID: %d)",
                 pool[i].argv[0], cpid);
        write_log(buf);
    }
}

// SIGHUP
void handle_hup(int sig) {
    (void)sig;
    write_log("SIGHUP: reload");

    for (int i = 0; i < pid_count; i++) {
        if (pool[i].pid > 0) {
            kill(pool[i].pid, SIGTERM);
        }
    }

    for (int i = 0; i < pid_count; i++) {
        if (pool[i].pid > 0) {
            waitpid(pool[i].pid, NULL, 0);
            pool[i].pid = 0;
        }
    }

    load_config();

    for (int i = 0; i < pid_count; i++) {
        run(i);
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] != '/') {
        fprintf(stderr, "Usage: %s /config\n", argv[0]);
        exit(1);
    }

    realpath(argv[1], cfg_path);

    if (fork() != 0) exit(0);
    setsid();

    struct rlimit flim;
    getrlimit(RLIMIT_NOFILE, &flim);
    for (int fd = 0; fd < (int)flim.rlim_max; fd++) close(fd);

    chdir("/");

    int fd0 = open("/dev/null", O_RDWR);
    dup2(fd0, 0);
    dup2(fd0, 1);
    dup2(fd0, 2);

    write_log("--- myinit started ---");

    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_IGN);

    signal(SIGHUP, handle_hup);

    load_config();

    for (int i = 0; i < pid_count; i++) {
        run(i);
    }

    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);

        if (pid <= 0) continue;

        for (int i = 0; i < pid_count; i++) {

            if (pool[i].pid != pid) continue;

            pool[i].pid = 0;

            char buf[256];

            if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);

                snprintf(buf, sizeof(buf),
                         "PID %d exited with code %d",
                         pid, code);
                write_log(buf);

                // sleep НЕ должен умирать
                run(i);
            }

            if (WIFSIGNALED(status)) {
                snprintf(buf, sizeof(buf),
                         "PID %d killed by signal %d",
                         pid, WTERMSIG(status));
                write_log(buf);

                run(i);
            }

            break;
        }
    }
}