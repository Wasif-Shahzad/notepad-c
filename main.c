/* Includes */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Function Prototypes */
void editorSetStatusMessage(const char* fmt, ...);

/* Defines */
#define VERSION "0.0.1"
#define CTRL_KEY(k) (0x1f & k)
#define TAB_SIZE 4
#define FORCED_QUIT_CNT 3

enum editorKeys {
    BACKSPACE = 127,
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
    int modified;
    char* fileName;
    char statusMsg[80];
    time_t statusMsgTime;
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

void disableRawMode(void) {
    // we reset original terminal settings
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enterRawMode(void) {
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
    int idx = 0;
    for (int i = 0; i < cx; i++) {
        if (row->text[i] == '\t') idx += TAB_SIZE;
        else idx++;
    }
    return idx;
}

void editorUpdateRow(eRow* row) {
    free(row->render);

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
    int idx = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->text[i] == '\t') {
            for (int j = 0; j < TAB_SIZE; j++) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->text[i];
        }
    }
    row->render[len] = '\0';
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
    editorUpdateRow(&E.row[at]);

    E.numRows++;
    E.modified++;
}

void editorRowInsertChar(eRow* row, int at, char c) {
    if (at < 0 || at > row->size) at = row->size;
    char* tmp = realloc(row->text, row->size + 2);
    if (tmp == NULL) {
        free(tmp);
        return;
    }
    row->text = tmp;
    memmove(&row->text[at + 1], &row->text[at], row->size - at + 1);
    row->size++;
    row->text[at] = c;
    editorUpdateRow(row);
    E.modified++;
}

/* Editor Operations */

void editorInsertChar(const char c) {
    if (E.cy == E.numRows) {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/* File I/o */

char* editorRowsToString(int* bufLen) {
    int totalLen = 0;
    for (int i = 0; i < E.numRows; i++) {
        totalLen += E.row[i].size + 1;
    }
    *bufLen = totalLen;
    char* buf = malloc(totalLen);
    char* p = buf;
    for (int i = 0; i < E.numRows; i++) {
        memcpy(p, E.row[i].text, E.row[i].size);
        p += E.row[i].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(const char* fileName) {
    free(E.fileName);
    E.fileName = strdup(fileName);

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
    E.modified = 0;
    free(line);
    fclose(fp);
}

void editorSave(void) {
    if (E.fileName == NULL) return;

    int len;
    char* buff = editorRowsToString(&len);

    int fd = open(E.fileName, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buff, len) == len) {
                close(fd);
                free(buff);
                editorSetStatusMessage("%d bytes written to disk", len);
                E.modified = 0;
                return;
            }
        }
        close(fd);
    }
    free(buff);
    editorSetStatusMessage("Can't Save! I/O Error: %s", strerror(errno));
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
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct append_buffer* ab) {
    abAppend(ab, "\x1b[7m", 4);

    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.fileName ? E.fileName : "[No Name]", E.numRows, (E.modified ? "(modified)" : ""));
    if (len > E.screenCols) len = E.screenCols;
    abAppend(ab, status, len);
    int rLen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numRows);
    while (len < E.screenCols) {
        if (E.screenCols - len == rLen) {
            abAppend(ab, rstatus, rLen);
            break;
        }
        abAppend(ab, " ", 1);
        len++;
    }

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct append_buffer* ab) {
    abAppend(ab, "\x1b[K", 3);
    int msgLen = (int)strlen(E.statusMsg);
    if (msgLen > E.screenCols) msgLen = E.screenCols;
    if (msgLen && time(NULL) - E.statusMsgTime <= 5) {
        abAppend(ab, E.statusMsg, msgLen);
    }
}

void editorRefreshScreen(void) {
    editorScroll();

    struct append_buffer ab = ABUFF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff) + 1, (E.rx - E.colOff) + 1);
    abAppend(&ab, buf, (int)strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.p, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, args);
    va_end(args);
    E.statusMsgTime = time(NULL);
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
    } else if (c == DOWN_KEY && E.cy + 1 < E.numRows) {
        E.cy++;
        const int lineLen = E.row[E.cy].size;
        if (lineLen < E.cx) E.cx = lineLen;
    } else if (c == RIGHT_KEY) {
        if (E.row != NULL) {
            const int lineLen = E.row[E.cy].size;
            if (E.cx < lineLen) {
                E.cx++;
            } else if (E.cy < E.numRows) {
                E.cy++;
                E.cx = 0;
            }
        }
    }
}

void editorProcessKeypress(void) {
    const int c = editorReadKey();
    static int remaining_quits = FORCED_QUIT_CNT;

    switch (c) {
        case '\r':
            // TODO
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            // TODO
            break;
        case CTRL_KEY('l'):
        case '\x1b':
            // TODO
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('q'):
            if (E.modified && remaining_quits > 0) {
                editorSetStatusMessage("WARNING! File has unsaved changes!",
                    "Press Ctrl-Q %d more times to force quit.", remaining_quits);
                remaining_quits--;
                return;
            }
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
            E.cy = E.rowOff;
            int times = E.screenRows;
            while (times--) {
                editorMoveCursor(UP_KEY);
            }
            break;
        }
        case PAGE_DOWN: {
            E.cy = E.rowOff + E.screenRows - 1;
            if (E.cy > E.numRows) E.cy = E.numRows;
            int times = E.screenRows;
            while (times--) {
                editorMoveCursor(DOWN_KEY);
            }
            break;
        }
        case HOME_KEY: {
            E.cx = 0;
            break;
        }
        case END_KEY: {
            if (E.cy < E.numRows) {
                const int lineLen = E.row[E.cy].size;
                E.cx = lineLen;
            }
            break;
        }
        default:
            editorInsertChar(c);
            break;
    }

    remaining_quits = FORCED_QUIT_CNT;
}


/* Init */

void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numRows = 0;
    E.rowOff = 0;
    E.colOff = 0;
    E.row = NULL;
    E.fileName = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsgTime = 0;
    E.modified = 0;
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) {
        die("getWindowSize");
    }
    E.screenRows -= 2;
}

int main(const int argc, char* argv[]) {
    enterRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}