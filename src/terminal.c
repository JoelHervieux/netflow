#include "terminal.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>

enum {
    STATE_NORMAL,
    STATE_ESC,
    STATE_CSI,
    STATE_OSC,
};

static COLORREF defaultPalette[16] = {
    /* Normal (0-7) */
    RGB( 20,  23,  30),    /* 0  black     — matches C_BG_PRIMARY */
    RGB(240, 108, 128),    /* 1  red       */
    RGB( 52, 211, 153),    /* 2  green     — same as success */
    RGB(251, 191, 107),    /* 3  yellow */
    RGB(124, 140, 255),    /* 4  blue      — accent indigo */
    RGB(196, 167, 255),    /* 5  magenta */
    RGB(106, 217, 226),    /* 6  cyan */
    RGB(230, 233, 240),    /* 7  white     — text_primary */

    /* Bright (8-15) */
    RGB( 96, 105, 121),    /* 8  bright black (comments / dim) */
    RGB(255, 145, 165),    /* 9  bright red */
    RGB( 92, 230, 175),    /* 10 bright green */
    RGB(255, 213, 140),    /* 11 bright yellow */
    RGB(165, 178, 255),    /* 12 bright blue */
    RGB(214, 191, 255),    /* 13 bright magenta */
    RGB(150, 232, 240),    /* 14 bright cyan */
    RGB(255, 255, 255),    /* 15 bright white */
};

static void term_ensure_capacity(Terminal *term, int needed) {
    if (needed <= term->capacity) return;
    int newCap = term->capacity * 2;
    if (newCap < needed) newCap = needed;
    term->lines = realloc(term->lines, sizeof(TermLine) * newCap);
    for (int i = term->capacity; i < newCap; i++) {
        term->lines[i].cells = calloc(term->cols, sizeof(TermCell));
        term->lines[i].cols = term->cols;
        for (int j = 0; j < term->cols; j++) {
            term->lines[i].cells[j].ch = L' ';
            term->lines[i].cells[j].fg = term->palette[7];
            term->lines[i].cells[j].bg = term->palette[0];
        }
    }
    term->capacity = newCap;
}

static void term_new_line(Terminal *term) {
    term->cursorX = 0;
    if (term->cursorY < term->rows - 1) {
        term->cursorY++;
    } else {
        term->totalLines++;
        term_ensure_capacity(term, term->totalLines + term->rows);
        int newIdx = term->totalLines + term->rows - 1;
        if (newIdx < term->capacity) {
            TermLine *line = &term->lines[newIdx];
            if (line->cols != term->cols) {
                free(line->cells);
                line->cells = calloc(term->cols, sizeof(TermCell));
                line->cols = term->cols;
            }
            for (int i = 0; i < term->cols; i++) {
                line->cells[i].ch = L' ';
                line->cells[i].fg = term->palette[7];
                line->cells[i].bg = term->palette[0];
            }
        }
        term->scrollTop = term->totalLines;
    }
}

static TermLine *term_get_line(Terminal *term, int row) {
    int idx = term->scrollTop + row;
    if (idx < 0 || idx >= term->capacity) return NULL;
    return &term->lines[idx];
}

static void term_put_char(Terminal *term, WCHAR ch) {
    if (term->cursorX >= term->cols) {
        term_new_line(term);
    }
    TermLine *line = term_get_line(term, term->cursorY);
    if (!line) return;
    TermCell *cell = &line->cells[term->cursorX];
    cell->ch = ch;
    cell->bold = term->bold;
    cell->underline = term->underline;
    cell->inverse = term->inverse;
    cell->fg = term->fg;
    cell->bg = term->bg;
    if (term->bold && term->fg == term->palette[7]) {
        cell->fg = term->palette[15];
    }
    term->cursorX++;
}

static void term_reset_attrs(Terminal *term) {
    term->fg = term->palette[7];
    term->bg = term->palette[0];
    term->bold = FALSE;
    term->underline = FALSE;
    term->inverse = FALSE;
}

static void term_process_sgr(Terminal *term) {
    if (term->paramCount == 0) {
        term_reset_attrs(term);
        return;
    }
    for (int i = 0; i < term->paramCount; i++) {
        int p = term->params[i];
        if (p == 0) { term_reset_attrs(term); }
        else if (p == 1) { term->bold = TRUE; }
        else if (p == 4) { term->underline = TRUE; }
        else if (p == 7) { term->inverse = TRUE; }
        else if (p == 22) { term->bold = FALSE; }
        else if (p == 24) { term->underline = FALSE; }
        else if (p == 27) { term->inverse = FALSE; }
        else if (p >= 30 && p <= 37) { term->fg = term->palette[p - 30]; }
        else if (p == 39) { term->fg = term->palette[7]; }
        else if (p >= 40 && p <= 47) { term->bg = term->palette[p - 40]; }
        else if (p == 49) { term->bg = term->palette[0]; }
        else if (p >= 90 && p <= 97) { term->fg = term->palette[p - 90 + 8]; }
        else if (p >= 100 && p <= 107) { term->bg = term->palette[p - 100 + 8]; }
    }
}

static void term_erase_in_line(Terminal *term, int mode) {
    TermLine *line = term_get_line(term, term->cursorY);
    if (!line) return;
    int start = 0, end = term->cols;
    if (mode == 0) start = term->cursorX;
    else if (mode == 1) end = term->cursorX + 1;
    for (int i = start; i < end && i < line->cols; i++) {
        line->cells[i].ch = L' ';
        line->cells[i].fg = term->palette[7];
        line->cells[i].bg = term->palette[0];
        line->cells[i].bold = FALSE;
        line->cells[i].underline = FALSE;
        line->cells[i].inverse = FALSE;
    }
}

static void term_erase_in_display(Terminal *term, int mode) {
    if (mode == 0) {
        term_erase_in_line(term, 0);
        for (int r = term->cursorY + 1; r < term->rows; r++) {
            TermLine *line = term_get_line(term, r);
            if (!line) continue;
            for (int i = 0; i < line->cols; i++) {
                line->cells[i].ch = L' ';
                line->cells[i].fg = term->palette[7];
                line->cells[i].bg = term->palette[0];
                line->cells[i].bold = FALSE;
            }
        }
    } else if (mode == 1) {
        term_erase_in_line(term, 1);
        for (int r = 0; r < term->cursorY; r++) {
            TermLine *line = term_get_line(term, r);
            if (!line) continue;
            for (int i = 0; i < line->cols; i++) {
                line->cells[i].ch = L' ';
                line->cells[i].fg = term->palette[7];
                line->cells[i].bg = term->palette[0];
                line->cells[i].bold = FALSE;
            }
        }
    } else if (mode == 2 || mode == 3) {
        for (int r = 0; r < term->rows; r++) {
            TermLine *line = term_get_line(term, r);
            if (!line) continue;
            for (int i = 0; i < line->cols; i++) {
                line->cells[i].ch = L' ';
                line->cells[i].fg = term->palette[7];
                line->cells[i].bg = term->palette[0];
                line->cells[i].bold = FALSE;
            }
        }
        term->cursorX = 0;
        term->cursorY = 0;
    }
}

static void term_process_csi(Terminal *term, char cmd) {
    int p0 = term->paramCount > 0 ? term->params[0] : 0;
    int p1 = term->paramCount > 1 ? term->params[1] : 0;

    switch (cmd) {
    case 'A': term->cursorY -= (p0 ? p0 : 1); if (term->cursorY < 0) term->cursorY = 0; break;
    case 'B': term->cursorY += (p0 ? p0 : 1); if (term->cursorY >= term->rows) term->cursorY = term->rows - 1; break;
    case 'C': term->cursorX += (p0 ? p0 : 1); if (term->cursorX >= term->cols) term->cursorX = term->cols - 1; break;
    case 'D': term->cursorX -= (p0 ? p0 : 1); if (term->cursorX < 0) term->cursorX = 0; break;
    case 'E': term->cursorX = 0; term->cursorY += (p0 ? p0 : 1); if (term->cursorY >= term->rows) term->cursorY = term->rows - 1; break;
    case 'F': term->cursorX = 0; term->cursorY -= (p0 ? p0 : 1); if (term->cursorY < 0) term->cursorY = 0; break;
    case 'G': term->cursorX = (p0 ? p0 : 1) - 1; if (term->cursorX >= term->cols) term->cursorX = term->cols - 1; break;
    case 'H': case 'f':
        term->cursorY = (p0 ? p0 : 1) - 1;
        term->cursorX = (p1 ? p1 : 1) - 1;
        if (term->cursorY >= term->rows) term->cursorY = term->rows - 1;
        if (term->cursorX >= term->cols) term->cursorX = term->cols - 1;
        if (term->cursorY < 0) term->cursorY = 0;
        if (term->cursorX < 0) term->cursorX = 0;
        break;
    case 'J': term_erase_in_display(term, p0); break;
    case 'K': term_erase_in_line(term, p0); break;
    case 'L': {
        int n = p0 ? p0 : 1;
        for (int i = 0; i < n && term->cursorY + n < term->rows; i++) {
            // scroll down from cursor
        }
        break;
    }
    case 'M': {
        int n = p0 ? p0 : 1;
        for (int i = 0; i < n && term->cursorY + i < term->rows; i++) {
            // scroll up from cursor
        }
        break;
    }
    case 'm': term_process_sgr(term); break;
    case 's': term->savedCursorX = term->cursorX; term->savedCursorY = term->cursorY; break;
    case 'u': term->cursorX = term->savedCursorX; term->cursorY = term->savedCursorY; break;
    case 'h':
        if (p0 == 25) term->cursorVisible = TRUE;
        break;
    case 'l':
        if (p0 == 25) term->cursorVisible = FALSE;
        break;
    case 'r': break; // scroll region — simplified
    case 'd':
        term->cursorY = (p0 ? p0 : 1) - 1;
        if (term->cursorY >= term->rows) term->cursorY = term->rows - 1;
        if (term->cursorY < 0) term->cursorY = 0;
        break;
    }
}

Terminal *term_create(int cols, int rows) {
    Terminal *term = calloc(1, sizeof(Terminal));
    term->cols = cols;
    term->rows = rows;
    term->cursorVisible = TRUE;
    term->parseState = STATE_NORMAL;

    memcpy(term->palette, defaultPalette, sizeof(defaultPalette));
    term->fg = term->palette[7];
    term->bg = term->palette[0];

    term->capacity = rows + TERM_SCROLLBACK;
    term->lines = calloc(term->capacity, sizeof(TermLine));
    for (int i = 0; i < term->capacity; i++) {
        term->lines[i].cells = calloc(cols, sizeof(TermCell));
        term->lines[i].cols = cols;
        for (int j = 0; j < cols; j++) {
            term->lines[i].cells[j].ch = L' ';
            term->lines[i].cells[j].fg = term->palette[7];
            term->lines[i].cells[j].bg = term->palette[0];
        }
    }
    term->totalLines = 0;
    term->scrollTop = 0;

    term->selStartX = term->selStartY = -1;
    term->selEndX = term->selEndY = -1;

    return term;
}

void term_destroy(Terminal *term) {
    if (!term) return;
    for (int i = 0; i < term->capacity; i++) {
        free(term->lines[i].cells);
    }
    free(term->lines);
    if (term->font) DeleteObject(term->font);
    free(term);
}

void term_process(Terminal *term, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        switch (term->parseState) {
        case STATE_NORMAL:
            if (c == 0x1b) {
                term->parseState = STATE_ESC;
            } else if (c == '\r') {
                term->cursorX = 0;
            } else if (c == '\n') {
                term_new_line(term);
            } else if (c == '\b') {
                if (term->cursorX > 0) term->cursorX--;
            } else if (c == '\t') {
                term->cursorX = (term->cursorX + 8) & ~7;
                if (term->cursorX >= term->cols) term->cursorX = term->cols - 1;
            } else if (c == '\a') {
                // bell — ignore
            } else if (c >= 0x20) {
                term_put_char(term, (WCHAR)c);
            }
            break;

        case STATE_ESC:
            if (c == '[') {
                term->parseState = STATE_CSI;
                term->paramCount = 0;
                memset(term->params, 0, sizeof(term->params));
            } else if (c == ']') {
                term->parseState = STATE_OSC;
            } else if (c == '7') {
                term->savedCursorX = term->cursorX;
                term->savedCursorY = term->cursorY;
                term->parseState = STATE_NORMAL;
            } else if (c == '8') {
                term->cursorX = term->savedCursorX;
                term->cursorY = term->savedCursorY;
                term->parseState = STATE_NORMAL;
            } else if (c == 'M') {
                if (term->cursorY > 0) term->cursorY--;
                term->parseState = STATE_NORMAL;
            } else if (c == 'D') {
                term_new_line(term);
                term->parseState = STATE_NORMAL;
            } else {
                term->parseState = STATE_NORMAL;
            }
            break;

        case STATE_CSI:
            if (c >= '0' && c <= '9') {
                if (term->paramCount == 0) term->paramCount = 1;
                term->params[term->paramCount - 1] = term->params[term->paramCount - 1] * 10 + (c - '0');
            } else if (c == ';') {
                if (term->paramCount < TERM_MAX_PARAMS) term->paramCount++;
            } else if (c == '?') {
                // private mode marker — ignore, next char will be the command
            } else if (c >= 0x40 && c <= 0x7e) {
                term_process_csi(term, c);
                term->parseState = STATE_NORMAL;
            } else {
                term->parseState = STATE_NORMAL;
            }
            break;

        case STATE_OSC:
            if (c == '\a' || c == 0x1b) {
                term->parseState = (c == 0x1b) ? STATE_ESC : STATE_NORMAL;
            }
            break;
        }
    }
}

void term_paint(Terminal *term, HDC hdc, RECT *clientRect) {
    HFONT oldFont = NULL;
    if (term->font) oldFont = SelectObject(hdc, term->font);

    SetBkMode(hdc, OPAQUE);

    // fill background
    HBRUSH bgBrush = CreateSolidBrush(term->palette[0]);
    FillRect(hdc, clientRect, bgBrush);
    DeleteObject(bgBrush);

    for (int row = 0; row < term->rows; row++) {
        TermLine *line = term_get_line(term, row);
        if (!line) continue;

        for (int col = 0; col < term->cols && col < line->cols; col++) {
            TermCell *cell = &line->cells[col];
            int x = col * term->charWidth;
            int y = row * term->charHeight;

            COLORREF fg = cell->fg;
            COLORREF bg = cell->bg;
            if (cell->inverse) {
                COLORREF tmp = fg;
                fg = bg;
                bg = tmp;
            }

            BOOL inSel = FALSE;
            if (term->hasSelection) {
                int absRow = term->scrollTop + row;
                int s1 = term->selStartY, s2 = term->selEndY;
                int sx1 = term->selStartX, sx2 = term->selEndX;
                if (s1 > s2 || (s1 == s2 && sx1 > sx2)) {
                    int t = s1; s1 = s2; s2 = t;
                    t = sx1; sx1 = sx2; sx2 = t;
                }
                if (absRow > s1 && absRow < s2) inSel = TRUE;
                else if (absRow == s1 && absRow == s2) inSel = (col >= sx1 && col <= sx2);
                else if (absRow == s1) inSel = (col >= sx1);
                else if (absRow == s2) inSel = (col <= sx2);
            }

            if (inSel) {
                fg = term->palette[0];
                bg = C_ACCENT_DIM;
            }

            SetTextColor(hdc, fg);
            SetBkColor(hdc, bg);

            RECT cellRect = { x, y, x + term->charWidth, y + term->charHeight };
            WCHAR wc = cell->ch;
            ExtTextOutW(hdc, x, y, ETO_OPAQUE | ETO_CLIPPED, &cellRect, &wc, 1, NULL);

            if (cell->underline) {
                HPEN pen = CreatePen(PS_SOLID, 1, fg);
                HPEN oldPen = SelectObject(hdc, pen);
                MoveToEx(hdc, x, y + term->charHeight - 1, NULL);
                LineTo(hdc, x + term->charWidth, y + term->charHeight - 1);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }
        }
    }

    // cursor
    if (term->cursorVisible && term->cursorX < term->cols && term->cursorY < term->rows) {
        int cx = term->cursorX * term->charWidth;
        int cy = term->cursorY * term->charHeight;
        RECT cursorRect = { cx, cy, cx + term->charWidth, cy + term->charHeight };
        InvertRect(hdc, &cursorRect);
    }

    if (oldFont) SelectObject(hdc, oldFont);
}

void term_set_font(Terminal *term, HFONT font) {
    if (term->font) DeleteObject(term->font);
    term->font = font;

    HDC hdc = GetDC(term->hwnd);
    HFONT old = SelectObject(hdc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    term->charWidth = tm.tmAveCharWidth;
    term->charHeight = tm.tmHeight;
    SelectObject(hdc, old);
    ReleaseDC(term->hwnd, hdc);
}

void term_get_size(Terminal *term, int pixelW, int pixelH, int *cols, int *rows) {
    if (term->charWidth > 0 && term->charHeight > 0) {
        *cols = pixelW / term->charWidth;
        *rows = pixelH / term->charHeight;
    } else {
        *cols = TERM_DEFAULT_COLS;
        *rows = TERM_DEFAULT_ROWS;
    }
    if (*cols < 1) *cols = 1;
    if (*rows < 1) *rows = 1;
    if (*cols > TERM_MAX_COLS) *cols = TERM_MAX_COLS;
    if (*rows > TERM_MAX_ROWS) *rows = TERM_MAX_ROWS;
}

void term_resize(Terminal *term, int cols, int rows) {
    if (cols == term->cols && rows == term->rows) return;

    int newCap = term->totalLines + rows + TERM_SCROLLBACK;
    TermLine *newLines = calloc(newCap, sizeof(TermLine));

    int copyCount = term->totalLines + term->rows;
    if (copyCount > term->capacity) copyCount = term->capacity;

    for (int i = 0; i < newCap; i++) {
        newLines[i].cells = calloc(cols, sizeof(TermCell));
        newLines[i].cols = cols;
        for (int j = 0; j < cols; j++) {
            newLines[i].cells[j].ch = L' ';
            newLines[i].cells[j].fg = term->palette[7];
            newLines[i].cells[j].bg = term->palette[0];
        }
        if (i < copyCount) {
            int copyCols = term->lines[i].cols < cols ? term->lines[i].cols : cols;
            memcpy(newLines[i].cells, term->lines[i].cells, copyCols * sizeof(TermCell));
        }
    }

    for (int i = 0; i < term->capacity; i++) {
        free(term->lines[i].cells);
    }
    free(term->lines);

    term->lines = newLines;
    term->capacity = newCap;
    term->cols = cols;
    term->rows = rows;
    if (term->cursorX >= cols) term->cursorX = cols - 1;
    if (term->cursorY >= rows) term->cursorY = rows - 1;
}

void term_scroll_up(Terminal *term, int lines) {
    term->scrollTop -= lines;
    if (term->scrollTop < 0) term->scrollTop = 0;
}

void term_scroll_down(Terminal *term, int lines) {
    term->scrollTop += lines;
    if (term->scrollTop > term->totalLines)
        term->scrollTop = term->totalLines;
}

void term_clear(Terminal *term) {
    term_erase_in_display(term, 2);
    term->totalLines = 0;
    term->scrollTop = 0;
}

int term_pixel_to_col(Terminal *term, int px) {
    if (term->charWidth <= 0) return 0;
    int c = px / term->charWidth;
    if (c < 0) c = 0;
    if (c >= term->cols) c = term->cols - 1;
    return c;
}

int term_pixel_to_row(Terminal *term, int py) {
    if (term->charHeight <= 0) return 0;
    int r = py / term->charHeight;
    if (r < 0) r = 0;
    if (r >= term->rows) r = term->rows - 1;
    return r;
}

void term_start_selection(Terminal *term, int x, int y) {
    term->selStartX = term_pixel_to_col(term, x);
    term->selStartY = term->scrollTop + term_pixel_to_row(term, y);
    term->selEndX = term->selStartX;
    term->selEndY = term->selStartY;
    term->selecting = TRUE;
    term->hasSelection = FALSE;
}

void term_update_selection(Terminal *term, int x, int y) {
    if (!term->selecting) return;
    term->selEndX = term_pixel_to_col(term, x);
    term->selEndY = term->scrollTop + term_pixel_to_row(term, y);
    term->hasSelection = (term->selStartX != term->selEndX || term->selStartY != term->selEndY);
}

void term_end_selection(Terminal *term) {
    term->selecting = FALSE;
}

WCHAR *term_get_selection(Terminal *term) {
    if (!term->hasSelection) return NULL;

    int s1 = term->selStartY, s2 = term->selEndY;
    int sx1 = term->selStartX, sx2 = term->selEndX;
    if (s1 > s2 || (s1 == s2 && sx1 > sx2)) {
        int t = s1; s1 = s2; s2 = t;
        t = sx1; sx1 = sx2; sx2 = t;
    }

    int bufSize = (s2 - s1 + 1) * (term->cols + 2) + 1;
    WCHAR *buf = calloc(bufSize, sizeof(WCHAR));
    int pos = 0;

    for (int row = s1; row <= s2 && row < term->capacity; row++) {
        TermLine *line = &term->lines[row];
        int startCol = (row == s1) ? sx1 : 0;
        int endCol = (row == s2) ? sx2 : line->cols - 1;
        for (int c = startCol; c <= endCol && c < line->cols; c++) {
            buf[pos++] = line->cells[c].ch;
        }
        if (row < s2) {
            buf[pos++] = L'\r';
            buf[pos++] = L'\n';
        }
    }
    buf[pos] = 0;
    return buf;
}
