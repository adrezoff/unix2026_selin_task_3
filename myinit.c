#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define MAX_LINE_LEN 1024
#define MAX_ARGS 64
#define MAX_CHILDREN 100

typedef struct {
    char *cmd_path;
    char **args;
    char *stdin_file;
    char *stdout_file;
    pid_t pid;
    int active;
} child_process_t;

child_process_t children[MAX_CHILDREN];
int num_children = 0;
char config_file[256];
FILE *log_file;
volatile sig_atomic_t reload_config = 0;

void log_message(const char *format, ...) {
    if (!log_file) return;

    va_list args;
    va_start(args, format);

    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_file, "[%s] ", time_str);
    vfprintf(log_file, format, args);
    fprintf(log_file, "\n");
    fflush(log_file);

    va_end(args);
}

void daemonize() {
    pid_t pid;

    // Создаем дочерний процесс
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        exit(0); // Родительский процесс завершается
    }

    // Создаем новый сеанс
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }

    // Игнорируем терминальные сигналы
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // Второй fork для полного избавления от терминала
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }

    // Меняем текущий каталог на корневой
    if (chdir("/") < 0) {
        perror("chdir");
        exit(1);
    }

    // Закрываем все открытые файлы
    for (int i = 0; i < sysconf(_SC_OPEN_MAX); i++) {
        close(i);
    }

    // Открываем лог-файл
    log_file = fopen("/tmp/myinit.log", "a");
    if (!log_file) {
        // Если не удалось открыть /tmp/myinit.log, пробуем другой путь
        log_file = fopen("/var/log/myinit.log", "a");
        if (!log_file) {
            // В крайнем случае выводим в syslog
            syslog(LOG_ERR, "Cannot open log file");
        }
    }

    umask(0);
}

int parse_config() {
    FILE *cfg = fopen(config_file, "r");
    if (!cfg) {
        log_message("ERROR: Cannot open config file %s", config_file);
        return -1;
    }

    char line[MAX_LINE_LEN];
    int line_num = 0;

    while (fgets(line, sizeof(line), cfg)) {
        line_num++;
        // Убираем символ новой строки
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        // Пропускаем пустые строки и комментарии
        if (line[0] == '\0' || line[0] == '#') continue;

        // Парсим строку: команда с аргументами, stdin, stdout
        char *token;
        char *saveptr;

        // Первая часть: команда с аргументами
        token = strtok_r(line, " ", &saveptr);
        if (!token) continue;

        char *cmd = strdup(token);
        if (!cmd) continue;

        // Проверяем, что путь абсолютный
        if (cmd[0] != '/') {
            log_message("ERROR: Command path is not absolute: %s", cmd);
            free(cmd);
            continue;
        }

        // Собираем аргументы
        char *args[MAX_ARGS];
        int arg_count = 0;
        args[arg_count++] = cmd;

        // Читаем аргументы, пока не дойдем до файлов ввода/вывода
        char *next_token;
        while ((next_token = strtok_r(NULL, " ", &saveptr))) {
            // Если мы нашли два последних токена, то это файлы
            // Но проще: продолжаем собирать аргументы, пока не соберем все
            // Последние два токена - это stdin и stdout
            args[arg_count++] = strdup(next_token);
            if (arg_count >= MAX_ARGS - 1) break;
        }

        // Последние два аргумента - это stdin и stdout (если аргументов >= 2)
        if (arg_count < 2) {
            log_message("ERROR: Not enough fields in line %d", line_num);
            for (int i = 0; i < arg_count; i++) free(args[i]);
            continue;
        }

        char *stdin_file = args[arg_count - 2];
        char *stdout_file = args[arg_count - 1];

        // Проверяем, что пути абсолютные
        if (stdin_file[0] != '/' || stdout_file[0] != '/') {
            log_message("ERROR: stdin/stdout paths must be absolute in line %d", line_num);
            for (int i = 0; i < arg_count; i++) free(args[i]);
            continue;
        }

        // Убираем последние два аргумента из списка аргументов для exec
        arg_count -= 2;
        args[arg_count] = NULL;

        // Сохраняем процесс
        if (num_children < MAX_CHILDREN) {
            children[num_children].cmd_path = cmd;
            children[num_children].args = malloc((arg_count + 1) * sizeof(char*));
            for (int i = 0; i < arg_count; i++) {
                children[num_children].args[i] = args[i];
            }
            children[num_children].args[arg_count] = NULL;
            children[num_children].stdin_file = strdup(stdin_file);
            children[num_children].stdout_file = strdup(stdout_file);
            children[num_children].pid = -1;
            children[num_children].active = 0;
            num_children++;
        } else {
            log_message("ERROR: Too many child processes");
            for (int i = 0; i < arg_count; i++) free(args[i]);
        }
    }

    fclose(cfg);
    return 0;
}

void start_child(int index) {
    if (index >= num_children) return;

    pid_t pid = fork();

    if (pid < 0) {
        log_message("ERROR: Failed to fork child process %d", index);
        return;
    }

    if (pid == 0) {
        // Дочерний процесс

        // Перенаправляем stdin
        FILE *f_stdin = fopen(children[index].stdin_file, "r");
        if (f_stdin) {
            dup2(fileno(f_stdin), STDIN_FILENO);
            fclose(f_stdin);
        } else {
            // Если файл не открылся, используем /dev/null
            int fd = open("/dev/null", O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        // Перенаправляем stdout
        FILE *f_stdout = fopen(children[index].stdout_file, "w");
        if (f_stdout) {
            dup2(fileno(f_stdout), STDOUT_FILENO);
            fclose(f_stdout);
        } else {
            int fd = open("/dev/null", O_WRONLY);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        // Запускаем программу
        execvp(children[index].cmd_path, children[index].args);

        // Если exec вернулся, значит ошибка
        fprintf(stderr, "Failed to exec %s: %s\n", children[index].cmd_path, strerror(errno));
        exit(1);
    } else {
        children[index].pid = pid;
        children[index].active = 1;
        log_message("Started process %d: %s (pid=%d)", index, children[index].cmd_path, pid);
    }
}

void stop_all_children() {
    for (int i = 0; i < num_children; i++) {
        if (children[i].active) {
            log_message("Terminating process %d: %s (pid=%d)", i, children[i].cmd_path, children[i].pid);
            kill(children[i].pid, SIGTERM);
            children[i].active = 0;

            // Ждем завершения процесса
            int status;
            waitpid(children[i].pid, &status, 0);
            log_message("Process %d terminated with status %d", i, status);
        }
    }
}

void start_all_children() {
    for (int i = 0; i < num_children; i++) {
        if (!children[i].active) {
            start_child(i);
        }
    }
}

void reload_config_and_restart() {
    log_message("SIGHUP received: reloading configuration");

    // Останавливаем всех потомков
    stop_all_children();

    // Освобождаем память от старой конфигурации
    for (int i = 0; i < num_children; i++) {
        free(children[i].cmd_path);
        for (int j = 0; children[i].args[j] != NULL; j++) {
            free(children[i].args[j]);
        }
        free(children[i].args);
        free(children[i].stdin_file);
        free(children[i].stdout_file);
    }
    num_children = 0;

    // Читаем новую конфигурацию
    if (parse_config() == 0) {
        // Запускаем новые процессы
        start_all_children();
        log_message("Configuration reloaded: %d processes started", num_children);
    } else {
        log_message("ERROR: Failed to reload configuration");
    }
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Находим завершившийся процесс
        for (int i = 0; i < num_children; i++) {
            if (children[i].active && children[i].pid == pid) {
                if (WIFEXITED(status)) {
                    log_message("Process %d (pid=%d) exited with status %d",
                               i, pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    log_message("Process %d (pid=%d) killed by signal %d",
                               i, pid, WTERMSIG(status));
                }

                children[i].active = 0;

                // Перезапускаем процесс
                log_message("Restarting process %d: %s", i, children[i].cmd_path);
                start_child(i);
                break;
            }
        }
    }
}

void sighup_handler(int sig) {
    reload_config = 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        exit(1);
    }

    strcpy(config_file, argv[1]);

    // Демонизируемся
    daemonize();

    if (!log_file) {
        syslog(LOG_ERR, "Failed to open log file");
        exit(1);
    }

    log_message("myinit started");

    // Устанавливаем обработчики сигналов
    struct sigaction sa;

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        log_message("ERROR: Cannot set SIGCHLD handler");
        exit(1);
    }

    sa.sa_handler = sighup_handler;
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        log_message("ERROR: Cannot set SIGHUP handler");
        exit(1);
    }

    // Читаем конфигурацию
    if (parse_config() != 0) {
        log_message("ERROR: Failed to parse config file");
        exit(1);
    }

    // Запускаем все процессы
    start_all_children();

    // Главный цикл
    while (1) {
        if (reload_config) {
            reload_config = 0;
            reload_config_and_restart();
        }
        pause(); // Ждем сигнал
    }

    fclose(log_file);
    return 0;
}