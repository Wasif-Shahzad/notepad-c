/* Includes */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Defines */
#define VERSION "0.0.1"
#define CTRL_KEY(k) (0x1f & k)

enum editorKeys {
    UP_KEY = 1000,
    DOWN_KEY,
    LEFT_KEY,
    RIGHT_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

/* Data */

struct editorConfig {
    int cx;
    int cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/* Terminal */

void die(const char* msg) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(msg);
    exit(1);
}

void disableRawMode() {
    // we reset original terminal settings
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enterRawMode() {
    // we get attributes for original terminal
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }

    // disableRawMode() will always run when the program exits
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    // setting/disabling all the necessary flags
    raw.c_iflag &= ~(BRKINT | INPCK | IXON | ICRNL | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

int editorReadKey(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                } else {
                    return '\x1b';
                }
            } else {
                switch (seq[1]) {
                    case 'A': return UP_KEY;
                    case 'B': return DOWN_KEY;
                    case 'C': return RIGHT_KEY;
                    case 'D': return LEFT_KEY;
                }
            }
        }

        return '\x1b';
    } else {
        return c;
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) return -1;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return -1;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999B\x1b[999C", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}

/* Append Buffer */

struct append_buffer {
    char* p;
    int len;
};

#define ABUFF_INIT {NULL, 0}

void abAppend(struct append_buffer* ab, char* s, int len) {
    char* new = realloc(ab->p, ab->len + len);
    if (new == NULL) {
        return;
    }
    memcpy(new + ab->len, s, len);
    ab->p = new;
    ab->len += len;
}

void abFree(struct append_buffer* ab) {
    free(ab->p);
}

/* Output */

void editorDrawRows(struct append_buffer* ab) {
    for (int y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome),
                "Notepad --- Version: %s", VERSION);
            if (welcomeLen > E.screencols) welcomeLen = E.screencols;
            int padding = (E.screencols - welcomeLen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding > 0) {
                abAppend(ab, " ", 1);
                padding--;
            }
            abAppend(ab, welcome, welcomeLen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct append_buffer ab = ABUFF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.p, ab.len);
    abFree(&ab);
}

/* Input */

void editorMoveCursor(int c) {
    if (c == UP_KEY && E.cy > 0) {
        E.cy--;
    } else if (c == LEFT_KEY && E.cx > 0) {
        E.cx--;
    } else if (c == DOWN_KEY && E.cy < E.screenrows - 1) {
        E.cy++;
    } else if (c == RIGHT_KEY && E.cx < E.screencols - 1) {
        E.cx++;
    }
}

void editorProcessKeypress(void) {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        case UP_KEY:
        case LEFT_KEY:
        case DOWN_KEY:
        case RIGHT_KEY:
            editorMoveCursor(c);
            break;
        case PAGE_UP: {
            int current_position = E.cy;
            while (current_position > 0) {
                editorMoveCursor(UP_KEY);
                current_position--;
            }
            break;
        }
        case PAGE_DOWN: {
            int current_position = E.cy;
            while (current_position < E.screenrows - 1) {
                editorMoveCursor(DOWN_KEY);
                current_position++;
            }
            break;
        }
    }
}

/* Init */

void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }
}

int main(void) {
    enterRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}