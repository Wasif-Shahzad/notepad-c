/* Includes */

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/* Data */

struct termios orig_termios;

/* Terminal */

void die(const char* msg) {
    perror(msg);
    exit(1);
}

void disableRawMode() {
    // we reset original terminal settings
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enterRawMode() {
    // we get attributes for original terminal
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }

    // disableRawMode() will always run when the program exits
    atexit(disableRawMode);

    struct termios raw = orig_termios;

    // setting/disabling all the necessary flags
    raw.c_iflag &= ~(BRKINT | INPCK | IXON | ICRNL | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

/* Init */

int main(void) {
    enterRawMode();
    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
            die("read");
        }
        if (c == 'q') break;
    }
    printf("\r\n");
    return 0;
}