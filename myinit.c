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

#define MAXPROC 10
#define MAXARGS 16
#define LOG_PATH "/tmp/myinit.log"

typedef struct {
    char *argv[MAXARGS];   // команда + аргументы из конфига
    char in[PATH_MAX];     // Файл перенаправления stdin
    char out[PATH_MAX];    // Файл перенаправления stdout
    pid_t pid;             // текущий pid процесса
} ChildProcess; // Структура для хранения данных о дочернем процессе

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
    for (int i = 0; cp->argv[i]; i++) {
        free(cp->argv[i]);
        cp->argv[i] = NULL;
    }
}

// разбор строки конфига: последние два слова — in/out, всё остальное — команда
void parse_line(char *line, ChildProcess *cp) {
    char *tokens[32];
    int n = 0;

    char *t = strtok(line, " \n");
    while (t && n < 32) {
        tokens[n++] = t;
        t = strtok(NULL, " \n");
    }

    if (n < 3) return;

    // realpath гарантирует, что путь абсолютный и существует
    realpath(tokens[n-2], cp->in);
    realpath(tokens[n-1], cp->out);

    int argc = 0;
    for (int i = 0; i < n-2 && argc < MAXARGS-1; i++) {
        cp->argv[argc++] = strdup(tokens[i]);
    }
    cp->argv[argc] = NULL;
}

// чтение конфигурационного файла
void load_config() {
    FILE *f = fopen(cfg_path, "r");
    if (!f) return;

    char line[1024];
    pid_count = 0;

    while (fgets(line, sizeof(line), f) && pid_count < MAXPROC) {
        parse_line(line, &pool[pid_count]);
        pid_count++;
    }
    fclose(f);
}

// запуск дочернего процесса
void run(int i) {
    char resolved[PATH_MAX];

    // проверяем путь к исполняемому файлу через realpath
    if (!realpath(pool[i].argv[0], resolved)) {
        write_log("Error: Bad executable path!");
        return;
    }

    pid_t cpid = fork();
    if (cpid == 0) {
        int fd_in = open(pool[i].in, O_RDONLY);
        int fd_out = open(pool[i].out, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd_in < 0 || fd_out < 0) exit(1);

        dup2(fd_in, STDIN_FILENO);
        dup2(fd_out, STDOUT_FILENO);

        close(fd_in);
        close(fd_out);

        // запускаем программу с аргументами из конфига
        execvp(pool[i].argv[0], pool[i].argv);
        exit(1);
    } else if (cpid > 0) {
        pool[i].pid = cpid;
        char buf[512];
        sprintf(buf, "Started %s (PID: %d)", pool[i].argv[0], cpid);
        write_log(buf);
    }
}

// убивает старые дочерние процессы и перечитывает конфиг
void handle_hup(int sig) {
    (void)sig; // избавляемся от warning: unused parameter
    write_log("SIGHUP: reloading config and restarting processes");

    // сначала убиваем все старые процессы
    for (int i = 0; i < pid_count; i++) {
        if (pool[i].pid > 0) {
            kill(pool[i].pid, SIGTERM);
        }
    }

    // обязательно дожидаемся их завершения
    for (int i = 0; i < pid_count; i++) {
        if (pool[i].pid > 0) {
            waitpid(pool[i].pid, NULL, 0);
            free_argv(&pool[i]);
        }
    }

    // читаем новый конфиг и стартуем процессы заново
    load_config();
    for (int i = 0; i < pid_count; i++) {
        run(i);
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] != '/') {
        fprintf(stderr, "Usage: %s /absolute/path/to/config\n", argv[0]);
        exit(1);
    }

    realpath(argv[1], cfg_path);

    // демонизация процесса
    if (fork() != 0) exit(0);
    setsid();

    struct rlimit flim;
    getrlimit(RLIMIT_NOFILE, &flim);
    for (int fd = 0; fd < (int)flim.rlim_max; fd++) close(fd);

    chdir("/");

    // демон обязан иметь /dev/null на stdin/stdout/stderr
    int fd0 = open("/dev/null", O_RDWR);
    dup2(fd0, 0);
    dup2(fd0, 1);
    dup2(fd0, 2);

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

                    if (WIFEXITED(status)) {
                        int code = WEXITSTATUS(status);

                        sprintf(buf, "PID %d exited with code %d",
                                terminated_pid, code);
                        write_log(buf);

                        // рестарт только если НЕ нормальный выход
                        if (code != 0) {
                            run(i);
                        }
                    }
                    else if (WIFSIGNALED(status)) {
                        sprintf(buf, "PID %d killed by signal %d",
                                terminated_pid, WTERMSIG(status));
                        write_log(buf);

                        // убит сигналом рестартим
                        run(i);
                    }

                    break;
                }
            }
        }
    }
    return 0;
}