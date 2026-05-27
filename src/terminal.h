#ifndef TERMINAL_H
#define TERMINAL_H

#include <winsock2.h>
#include <windows.h>

#define TERM_DEFAULT_COLS 120
#define TERM_DEFAULT_ROWS 30
#define TERM_MAX_COLS 500
#define TERM_MAX_ROWS 200
#define TERM_SCROLLBACK 10000
#define TERM_MAX_PARAMS 16

typedef struct {
    WCHAR ch;
    COLORREF fg;
    COLORREF bg;
    BOOL bold;
    BOOL underline;
    BOOL inverse;
} TermCell;

typedef struct {
    TermCell *cells;
    int cols;
} TermLine;

typedef struct {
    TermLine *lines;
    int totalLines;
    int capacity;
    int cols;
    int rows;
    int scrollTop;
    int cursorX;
    int cursorY;
    BOOL cursorVisible;

    COLORREF fg;
    COLORREF bg;
    BOOL bold;
    BOOL underline;
    BOOL inverse;

    int savedCursorX;
    int savedCursorY;

    int params[TERM_MAX_PARAMS];
    int paramCount;
    int parseState;

    int selStartX, selStartY;
    int selEndX, selEndY;
    BOOL selecting;
    BOOL hasSelection;

    HFONT font;
    int charWidth;
    int charHeight;
    HWND hwnd;

    COLORREF palette[16];
} Terminal;

Terminal *term_create(int cols, int rows);
void term_destroy(Terminal *term);
void term_process(Terminal *term, const char *data, int len);
void term_paint(Terminal *term, HDC hdc, RECT *clientRect);
void term_resize(Terminal *term, int cols, int rows);
void term_set_font(Terminal *term, HFONT font);
void term_get_size(Terminal *term, int pixelW, int pixelH, int *cols, int *rows);
void term_scroll_up(Terminal *term, int lines);
void term_scroll_down(Terminal *term, int lines);
void term_clear(Terminal *term);

void term_start_selection(Terminal *term, int x, int y);
void term_update_selection(Terminal *term, int x, int y);
void term_end_selection(Terminal *term);
WCHAR *term_get_selection(Terminal *term);

int term_pixel_to_col(Terminal *term, int px);
int term_pixel_to_row(Terminal *term, int py);

#endif
