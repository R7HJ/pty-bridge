// R7HJ 2026.10.12

#define _GNU_SOURCE
#include <pty.h>          // openpty()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>

#define LINK0       "/tmp/ttyV0"
#define LINK1       "/tmp/ttyV1"
#define BUF_SIZE    4096

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

/* Классическая демонизация: двойной fork + setsid + отвязка от tty */
static int daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(EXIT_SUCCESS);      // родитель выходит

    if (setsid() < 0) return -1;

    pid = fork();                          // второй fork — не становимся лидером сессии
    if (pid < 0) return -1;
    if (pid > 0) _exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") < 0) return -1;

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return -1;
    if (dup2(fd, STDIN_FILENO)  < 0) return -1;
    if (dup2(fd, STDOUT_FILENO) < 0) return -1;
    if (dup2(fd, STDERR_FILENO) < 0) return -1;
    if (fd > STDERR_FILENO) close(fd);

    return 0;
}

/* Атомарно (пере)создаём симлинк: пишем во временный и rename() */
static int make_symlink(const char *target, const char *linkpath) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", linkpath, (int)getpid());

    unlink(tmp);
    if (symlink(target, tmp) == -1) {
        syslog(LOG_ERR, "symlink(%s -> %s): %s", tmp, target, strerror(errno));
        return -1;
    }
    if (rename(tmp, linkpath) == -1) {
        syslog(LOG_ERR, "rename(%s -> %s): %s", tmp, linkpath, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int run_as_daemon = 0;
    int opt;
    while ((opt = getopt(argc, argv, "dh")) != -1) {
        switch (opt) {
            case 'd': run_as_daemon = 1; break;
            case 'h':
                printf("Usage: %s [-d]\n", argv[0]);
                printf("  -d   запустить как демон\n");
                return 0;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                return 1;
        }
    }

    if (run_as_daemon) {
        openlog("pty_bridge", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        if (daemonize() < 0) {
            syslog(LOG_ERR, "daemonize: %s", strerror(errno));
            return 1;
        }
    }

    /* --- создаём два PTY --- */
    int master1 = -1, slave1 = -1;
    int master2 = -1, slave2 = -1;
    char name1[256] = {0};
    char name2[256] = {0};

    if (openpty(&master1, &slave1, name1, NULL, NULL) == -1) {
        if (run_as_daemon) syslog(LOG_ERR, "openpty 1: %s", strerror(errno));
        else               perror("openpty 1");
        return 1;
    }
    if (openpty(&master2, &slave2, name2, NULL, NULL) == -1) {
        if (run_as_daemon) syslog(LOG_ERR, "openpty 2: %s", strerror(errno));
        else               perror("openpty 2");
        close(master1); close(slave1);
        return 1;
    }

    /* Симлинки с предсказуемыми именами */
    if (make_symlink(name1, LINK0) == -1 ||
        make_symlink(name2, LINK1) == -1) {
        if (!run_as_daemon)
            fprintf(stderr, "Не удалось создать симлинки\n");
        close(master1); close(slave1);
        close(master2); close(slave2);
        return 1;
    }

    if (run_as_daemon) {
        syslog(LOG_INFO, "started: %s -> %s, %s -> %s",
               LINK0, name1, LINK1, name2);
    } else {
        printf("Порт 1: %s -> %s\n", LINK0, name1);
        printf("Порт 2: %s -> %s\n", LINK1, name2);
        printf("Нажмите Ctrl+C для выхода.\n");
        fflush(stdout);
    }

    /* --- обработчики сигналов --- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* --- главный цикл: пересылаем байты между master1 и master2 --- */
    int maxfd = (master1 > master2 ? master1 : master2) + 1;
    char buf[BUF_SIZE];

    while (!g_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(master1, &rfds);
        FD_SET(master2, &rfds);

        int rc = select(maxfd, &rfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) continue;      // сигнал — вернёмся к проверке g_stop
            if (run_as_daemon) syslog(LOG_ERR, "select: %s", strerror(errno));
            break;
        }

        if (FD_ISSET(master1, &rfds)) {
            ssize_t n = read(master1, buf, sizeof(buf));
            if (n <= 0) break;
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(master2, buf + off, n - off);
                if (w <= 0) { if (errno == EINTR) continue; break; }
                off += w;
            }
        }
        if (FD_ISSET(master2, &rfds)) {
            ssize_t n = read(master2, buf, sizeof(buf));
            if (n <= 0) break;
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(master1, buf + off, n - off);
                if (w <= 0) { if (errno == EINTR) continue; break; }
                off += w;
            }
        }
    }

    /* --- очистка --- */
    if (run_as_daemon) syslog(LOG_INFO, "stopping");

    unlink(LINK0);
    unlink(LINK1);

    close(master1); close(slave1);
    close(master2); close(slave2);

    if (run_as_daemon) closelog();
    return 0;
}
