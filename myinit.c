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

#define MAXPROC 10
#define LOG_PATH "/tmp/myinit.log"

typedef struct {
    char path[256]; // Путь к исполняемому файлу
    char in[256];   // Файл перенаправления stdin
    char out[256];  // Файл перенаправления stdout
    pid_t pid;      // текущий pid процесса
} ChildProcess; // Структура для хранения данных о дочернем процессе

ChildProcess pool[MAXPROC];
int pid_count = 0;
char cfg_path[256];

void write_log(const char *msg) {
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd != -1) {
        dprintf(fd, "[PID %d] %s\n", getpid(), msg);
        close(fd);
    }
}

// запуск дочернего процесса
void run(int i) {
    if (pool[i].path[0] != '/' || pool[i].in[0] != '/' || pool[i].out[0] != '/') {
        write_log("Error: Non-absolute path detected!");
        return;
    }

    pid_t cpid = fork();
    if (cpid == 0) {
        int fd_in = open(pool[i].in, O_RDONLY);
        int fd_out = open(pool[i].out, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd_in < 0 || fd_out < 0) exit(1);

        dup2(fd_in, STDIN_FILENO);
        dup2(fd_out, STDOUT_FILENO);

        // Подавляем stderr, чтобы ошибки дочерних процессов не шли в консоль
        int fd_err = open("/dev/null", O_WRONLY);
        dup2(fd_err, STDERR_FILENO);

        close(fd_in);
        close(fd_out);

        // 100 — аргумент для sleep, чтобы процесс жил дольше, логи становится понятнее
        execl(pool[i].path, pool[i].path, "100", (char *)NULL);
        exit(1);
    } else if (cpid > 0) {
        pool[i].pid = cpid;
        char buf[512];
        sprintf(buf, "Started %s (PID: %d)", pool[i].path, cpid);
        write_log(buf);
    }
}

void load_config() {
    FILE *f = fopen(cfg_path, "r");
    if (!f) return;
    pid_count = 0;
    while (pid_count < MAXPROC && fscanf(f, "%s %s %s", pool[pid_count].path,
                  pool[pid_count].in, pool[pid_count].out) == 3) {
        pid_count++;
    }
    fclose(f);
}

// убивает старые дочерние процессы и перечитывает конфиг
void handle_hup(int sig) {
    write_log("SIGHUP: reloading config and restarting proceses");
    for (int i = 0; i < pid_count; i++) {
        if (pool[i].pid > 0)
        {
            kill(pool[i].pid, SIGTERM);
        }
    }
    load_config();
    for (int i = 0; i < pid_count; i++) {
        run(i)
    };
}

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] != '/') {
        fprintf(stderr, "Usage: %s /absolute/path/to/config\n", argv[0]);
        exit(1);
    }
    realpath(argv[1], cfg_path);

    if (fork() != 0) exit(0);
    setsid();

    struct rlimit flim;
    getrlimit(RLIMIT_NOFILE, &flim);
    for (int fd = 0; fd < (int)flim.rlim_max; fd++) close(fd);

    chdir("/");

    write_log("--- myinit daemon started ---");

    signal(SIGHUP, handle_hup);

    load_config();
    for (int i = 0; i < pid_count; i++) run(i);

    // мониторим состояния дочерних процессов
    while (1) {
        int status;
        pid_t terminated_pid = waitpid(-1, &status, 0);
        if (terminated_pid > 0) {
            for (int i = 0; i < pid_count; i++) {
                if (pool[i].pid == terminated_pid) {
                    char buf[256];
                    sprintf(buf, "Process %d died. Restarting...", terminated_pid);
                    write_log(buf);
                    // перезапуск упавшего процесса
                    run(i);
                    break;
                }
            }
        }
    }
    return 0;
}