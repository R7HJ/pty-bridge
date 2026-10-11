// R7HJ 2026

#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>

int main(void) {
    int master1, slave1, master2, slave2;
    char name1[256], name2[256];

    if (openpty(&master1, &slave1, name1, NULL, NULL) == -1) {
        perror("openpty 1");
        return 1;
    }
    if (openpty(&master2, &slave2, name2, NULL, NULL) == -1) {
        perror("openpty 2");
        return 1;
    }

    printf("Порт 1: %s\n", name1);
    printf("Порт 2: %s\n", name2);
    fflush(stdout);

    fd_set readfds;
    char buf[4096];

    for (;;) {
        FD_ZERO(&readfds);
        FD_SET(master1, &readfds);
        FD_SET(master2, &readfds);

        if (select((master1 > master2 ? master1 : master2) + 1,
                   &readfds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }

        if (FD_ISSET(master1, &readfds)) {
            ssize_t n = read(master1, buf, sizeof(buf));
            if (n <= 0) break;
            write(master2, buf, n);
        }
        if (FD_ISSET(master2, &readfds)) {
            ssize_t n = read(master2, buf, sizeof(buf));
            if (n <= 0) break;
            write(master1, buf, n);
        }
    }

    close(master1); close(slave1);
    close(master2); close(slave2);
    return 0;
}
