/*** includes ***/

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

/*** defines ***/

#define VIC_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

typedef struct erow {
    int size;
    char *chars;
}erow;

struct editorConfig {
    int cx, cy; // cursor position cords
    int screenrows;
    int screencols;
    int numrows;
    erow row;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char * s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO,TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}
void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | IEXTEN | ICANON | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;}
    else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** file i/o ***/

void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline(&line, &linecap,fp);
    if(linelen != -1){
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\n')){
            linelen--;
        }
        E.row.size = linelen;
        E.row.chars = malloc(linelen + 1);
        memcpy(E.row.chars, line, linelen);
        E.row.chars[linelen] = '\0';
        E.numrows = 1;
    }
    free(line);
    fclose(fp);
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL,0}

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;

    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}

/*** output ***/

void editorDrawsRows(struct abuf *ab){
    int y;
    for(y = 0; y< E.screenrows; y++){
        if(y>= E.numrows){
            if(E.numrows == 0 && y == E.screenrows / 3){
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "ViC editor -- version %s", VIC_VERSION);
                int padding = (E.screencols - welcomelen) / 2;
                if (padding){
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                abAppend(ab, welcome, welcomelen);
            }
            else{
                abAppend(ab, "~", 1);
            }
        }
        else{
            int len = E.row.size;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, E.row.chars, len);
        }

        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;
    

    abAppend(&ab, "\x1b[?25l", 6); //hide the cursor
    abAppend(&ab, "\x1b[H", 3); //move cursor to home position

    editorDrawsRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy +1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6); //show the cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/


void editorMoveCursor(char key){
    switch(key){
        case 'h':
            if (E.cx > 0) E.cx--;
            break;
        case 'm':
            if (E.cx < E.screencols - 1) E.cx++;
            break;
        case 'k':
            if (E.cy > 0) E.cy--;
            break;
        case 'j':
            if (E.cy < E.screenrows - 1 ) E.cy++;
            break;
    }
}

void editorProcessKeypress(){
    char c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[H", 3); //move cursor to home position
        write(STDOUT_FILENO, "\x1b[2J", 4); //clear the screen

        exit(0);
        break;
    case 'h':
    case 'j':
    case 'k':
    case 'm':
        editorMoveCursor(c);
        break;
    }

}

/*** init ***/

int main(int argc, char *argv[]){

    enableRawMode();
    initEditor();
    if (argc >= 2) editorOpen(argv[1]);

    while (1){
        editorRefreshScreen();
        editorProcessKeypress();
    }

}

