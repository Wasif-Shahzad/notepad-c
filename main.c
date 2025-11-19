/* Includes */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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
#define TAB_SIZE 4

enum editorKeys {
    UP_KEY = 1000,
    DOWN_KEY,
    LEFT_KEY,
    RIGHT_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY,
};

/* Data */

typedef struct {
    char* text;
    char* render;
    int size;
    int rsize;
    int* cxTorx;
} eRow;

struct editorConfig {
    int cx;
    int cy;
    int rx;
    int screenRows;
    int screenCols;
    int rowOff;
    int colOff;
    int numRows;
    eRow* row;
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
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return UP_KEY;
                    case 'B': return DOWN_KEY;
                    case 'C': return RIGHT_KEY;
                    case 'D': return LEFT_KEY;
                    case 'F': return END_KEY;
                    case 'H': return HOME_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'F': return END_KEY;
                case 'H': return HOME_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
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

/* Row operations */

int editorRowCxToRx(const eRow* row, const int cx) {
    return row->cxTorx[cx];
}

void editorUpdateRow(eRow* row) {
    free(row->render);
    free(row->cxTorx);

    int len = 0;
    for (int idx = 0; idx < row->size; idx++) {
        if (row->text[idx] == '\t') {
            len += TAB_SIZE;
        } else {
            len++;
        }
    }

    row->rsize = len;
    row->render = malloc(len + 1);
    row->cxTorx = calloc(len + 1, sizeof(int));
    int idx = 0;
    for (int i = 0; i < row->size; i++) {
        row->cxTorx[i] = idx;
        if (row->text[i] == '\t') {
            for (int j = 0; j < TAB_SIZE; j++) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->text[i];
        }
    }
    row->render[len] = '\0';
    row->cxTorx[row->size] = len;
}

void editorAppendRow(const char* s, const int len) {
    eRow* tmp = realloc(E.row, sizeof(eRow) * (E.numRows + 1));
    if (tmp == NULL) {
        free(tmp);
        die("editorAppendRow");
    }

    E.row = tmp;
    const int at = E.numRows;
    E.row[at].size = len;
    E.row[at].text = malloc(len + 1);
    memcpy(E.row[at].text, s, len);
    E.row[at].text[len] = '\0';

    E.row[at].rsize = len;
    E.row[at].render = NULL;
    E.row[at].cxTorx = NULL;
    editorUpdateRow(&E.row[at]);

    E.numRows++;
}

/* File I/o */

void editorOpen(const char* fileName) {
    FILE* fp = fopen(fileName, "r");
    if (fp == NULL) {
        die("fopen");
    }
    char* line = NULL;
    size_t lineCap = 0;
    ssize_t lineLen;
    while ((lineLen = getline(&line, &lineCap, fp)) != -1) {
        while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
            lineLen--;
        }
        editorAppendRow(line, (int)lineLen);
    }
    free(line);
    fclose(fp);
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

void abFree(const struct append_buffer* ab) {
    free(ab->p);
}

/* Output */

void editorScroll(void) {
    E.rx = 0;
    if (E.cy < E.numRows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    if (E.cy < E.rowOff) {
        E.rowOff = E.cy;
    }
    if (E.cy >= E.rowOff + E.screenRows) {
        E.rowOff = E.cy - E.screenRows + 1;
    }

    if (E.rx < E.colOff) {
        E.colOff = E.rx;
    }
    if (E.rx >= E.colOff + E.screenCols) {
        E.colOff = E.rx - E.screenCols + 1;
    }
}

void editorDrawRows(struct append_buffer* ab) {
    for (int y = 0; y < E.screenRows; y++) {
        const int fileRows = y + E.rowOff;
        if (fileRows >= E.numRows) {
            if (E.numRows == 0 && y == E.screenRows / 3) {
                char welcome[80];
                int welcomeLen = snprintf(welcome, sizeof(welcome),
                    "Notepad --- Version: %s", VERSION);
                if (welcomeLen > E.screenCols) welcomeLen = E.screenCols;
                int padding = (E.screenCols - welcomeLen) / 2;
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
        } else {
            int len = E.row[fileRows].rsize - E.colOff;
            if (len < 0) len = 0;
            if (E.screenCols < len) len = E.screenCols;
            abAppend(ab, E.row[fileRows].render + E.colOff, len);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(void) {
    editorScroll();

    struct append_buffer ab = ABUFF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff) + 1, (E.rx - E.colOff) + 1);
    abAppend(&ab, buf, (int)strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.p, ab.len);
    abFree(&ab);
}

/* Input */

void editorMoveCursor(const int c) {
    if (c == UP_KEY && E.cy > 0) {
        E.cy--;
        const int lineLen = E.row[E.cy].size;
        if (lineLen < E.cx) E.cx = lineLen;
    } else if (c == LEFT_KEY) {
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
    } else if (c == DOWN_KEY && E.cy < E.numRows) {
        E.cy++;
        const int lineLen = E.row[E.cy].size;
        if (lineLen < E.cx) E.cx = lineLen;
    } else if (c == RIGHT_KEY) {
        const int lineLen = E.row[E.cy].size;
        if (E.cx < lineLen) {
            E.cx++;
        } else if (E.cy < E.numRows) {
            E.cy++;
            E.cx = 0;
        }
    }
}

void editorProcessKeypress(void) {
    const int c = editorReadKey();

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
            while (current_position < E.screenRows - 1) {
                editorMoveCursor(DOWN_KEY);
                current_position++;
            }
            break;
        }
        case HOME_KEY: {
            E.cx = 0;
            break;
        }
        case END_KEY: {
            const int lineLen = E.row[E.cy].size;
            E.cx = lineLen;
            break;
        }
        default:
            break;
    }
}


/* Init */

void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numRows = 0;
    E.row = NULL;
    E.rowOff = 0;
    E.colOff = 0;
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) {
        die("getWindowSize");
    }
}

int main(const int argc, char* argv[]) {
    enterRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}