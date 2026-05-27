#include "window.h"
#include "ui.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>
#include <commctrl.h>

#define WM_SSH_DATA      (WM_USER + 1)
#define WM_SSH_CLOSED    (WM_USER + 2)
#define WM_SERIAL_DATA   (WM_USER + 3)
#define WM_SERIAL_CLOSED (WM_USER + 4)

#define TIMER_CURSOR_BLINK 1
#define TIMER_ANIMATE      2          /* 60Hz frame ticker for all UI motion */
#define ANIM_INTERVAL_MS   16

typedef struct {
    char data[4096];
    int  len;
    int  tabIndex;
} DataMsg;

static AppWindow *g_app = NULL;
static DWORD      g_lastAnimTick = 0;

/* ============================================================
   Font cache
   ============================================================ */
static HFONT g_fontTitle = NULL;
static HFONT g_fontUI = NULL;
static HFONT g_fontUISmall = NULL;
static HFONT g_fontUIBold = NULL;
static HFONT g_fontMono = NULL;
static HFONT g_fontMonoSmall = NULL;
static HFONT g_fontLabel = NULL;     /* uppercase eyebrow */
static HFONT g_fontIcon = NULL;
static HFONT g_fontIconSmall = NULL;
static HFONT g_fontHeroBig = NULL;
static HFONT g_fontEmptyTitle = NULL;

static void init_fonts(void) {
    g_fontTitle      = ui_create_font("Segoe UI",            13, TRUE);
    g_fontUI         = ui_create_font("Segoe UI",            13, FALSE);
    g_fontUISmall    = ui_create_font("Segoe UI",            11, FALSE);
    g_fontUIBold     = ui_create_font("Segoe UI",            13, TRUE);
    g_fontMono       = ui_create_font("Cascadia Mono",       13, FALSE);
    g_fontMonoSmall  = ui_create_font("Cascadia Mono",       11, FALSE);
    g_fontLabel      = ui_create_font("Segoe UI",            10, TRUE);
    g_fontIcon       = ui_create_font("Segoe MDL2 Assets",   10, FALSE);
    g_fontIconSmall  = ui_create_font("Segoe MDL2 Assets",    8, FALSE);
    g_fontHeroBig    = ui_create_font("Cascadia Code",       44, FALSE);
    g_fontEmptyTitle = ui_create_font("Segoe UI",            22, TRUE);
}

static void cleanup_fonts(void) {
    DeleteObject(g_fontTitle);
    DeleteObject(g_fontUI);
    DeleteObject(g_fontUISmall);
    DeleteObject(g_fontUIBold);
    DeleteObject(g_fontMono);
    DeleteObject(g_fontMonoSmall);
    DeleteObject(g_fontLabel);
    DeleteObject(g_fontIcon);
    DeleteObject(g_fontIconSmall);
    DeleteObject(g_fontHeroBig);
    DeleteObject(g_fontEmptyTitle);
}

static HFONT create_term_font(int size) {
    return CreateFontA(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Cascadia Code");
}

/* ============================================================
   Layout helpers
   ============================================================ */
static int get_titlebar_bottom(void) { return TITLEBAR_HEIGHT; }
static int get_tabbar_bottom(void)   { return TITLEBAR_HEIGHT + TABBAR_HEIGHT; }
static int get_statusbar_top(AppWindow *app) {
    RECT rc; GetClientRect(app->hwnd, &rc);
    return rc.bottom - STATUSBAR_HEIGHT;
}
static int get_term_top(void)              { return get_tabbar_bottom(); }
static int get_term_bottom(AppWindow *app) { return get_statusbar_top(app); }

static void get_empty_btn_rects(int w, int termTop, int termBottom,
                                RECT *sshBtn, RECT *serialBtn)
{
    int h = termBottom - termTop;
    int centerY = termTop + h / 2;
    int btnW = 340, btnH = 64, gap = 12;
    int btnX = (w - btnW) / 2;
    int btnY = centerY + 40;
    SetRect(sshBtn,    btnX, btnY,                  btnX + btnW, btnY + btnH);
    SetRect(serialBtn, btnX, btnY + btnH + gap,     btnX + btnW, btnY + btnH + gap + btnH);
}

static void update_terminal_sizes(AppWindow *app) {
    RECT rc; GetClientRect(app->hwnd, &rc);
    int w = rc.right;
    int h = get_term_bottom(app) - get_term_top();
    int pad = TERM_PADDING;
    w -= pad * 2; h -= pad * 2;
    if (w < 10 || h < 10) return;

    Tab *tab = tabs_get_active(&app->tabs);
    if (tab && tab->terminal) {
        int cols, rows;
        term_get_size(tab->terminal, w, h, &cols, &rows);
        term_resize(tab->terminal, cols, rows);
        if (tab->type == TAB_TYPE_SSH && tab->conn.ssh)
            ssh_resize(tab->conn.ssh, cols, rows);
    }
}

/* ============================================================
   Connection callbacks (unchanged)
   ============================================================ */
static void ssh_data_cb(const char *data, int len, void *userdata) {
    int tabIndex = (int)(intptr_t)userdata;
    if (!g_app) return;
    DataMsg *msg = malloc(sizeof(DataMsg));
    int copyLen = len < (int)sizeof(msg->data) ? len : (int)sizeof(msg->data);
    memcpy(msg->data, data, copyLen);
    msg->len = copyLen; msg->tabIndex = tabIndex;
    PostMessage(g_app->hwnd, WM_SSH_DATA, 0, (LPARAM)msg);
}
static void ssh_close_cb(void *userdata) {
    if (!g_app) return;
    PostMessage(g_app->hwnd, WM_SSH_CLOSED, (WPARAM)(intptr_t)userdata, 0);
}
static void serial_data_cb(const char *data, int len, void *userdata) {
    int tabIndex = (int)(intptr_t)userdata;
    if (!g_app) return;
    DataMsg *msg = malloc(sizeof(DataMsg));
    int copyLen = len < (int)sizeof(msg->data) ? len : (int)sizeof(msg->data);
    memcpy(msg->data, data, copyLen);
    msg->len = copyLen; msg->tabIndex = tabIndex;
    PostMessage(g_app->hwnd, WM_SERIAL_DATA, 0, (LPARAM)msg);
}
static void serial_close_cb(void *userdata) {
    if (!g_app) return;
    PostMessage(g_app->hwnd, WM_SERIAL_CLOSED, (WPARAM)(intptr_t)userdata, 0);
}

/* ============================================================
   Painting: TITLE BAR
   ============================================================ */
static void paint_titlebar(AppWindow *app, HDC hdc, int w) {
    /* Flat surface — no gradient. Calmer, less Linear/Warp neon. */
    ui_fill_rect(hdc, 0, 0, w, TITLEBAR_HEIGHT, C_TITLEBAR);

    /* Logo mark — rounded square in accent, white "N" */
    int logoSize = 22, logoX = 12, logoY = (TITLEBAR_HEIGHT - logoSize)/2;
    ui_rounded_rect(hdc, logoX, logoY, logoSize, logoSize, 5,
                    C_ACCENT, C_ACCENT);
    ui_draw_text(hdc, "N", logoX, logoY, logoSize, logoSize,
                 RGB(255, 255, 255), g_fontTitle,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* App name */
    ui_draw_text(hdc, "NetFlow", logoX + logoSize + 10, 0,
                 80, TITLEBAR_HEIGHT,
                 C_TEXT_PRIMARY, g_fontTitle,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Active session info — centered, low contrast */
    Tab *tab = tabs_get_active(&app->tabs);
    if (tab) {
        char info[256];
        const char *typeStr = tab->type == TAB_TYPE_SSH ? "SSH" : "Serial";
        snprintf(info, sizeof(info), "%s   %s", typeStr, tab->title);
        ui_draw_text(hdc, info, 0, 0, w, TITLEBAR_HEIGHT,
                     C_TEXT_MUTED, g_fontUISmall,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Window control buttons */
    const WCHAR *icons[3] = {
        ICON_MINIMIZE,
        IsZoomed(app->hwnd) ? ICON_RESTORE : ICON_MAXIMIZE,
        ICON_CLOSE
    };
    for (int i = 0; i < 3; i++) {
        int bx = w - CAPTION_BTN_W * (3 - i);
        BOOL hov = (app->titleHoverBtn == i);

        if (hov) {
            COLORREF bg = (i == 2) ? RGB(196, 43, 28) : C_BG_HOVER;
            ui_fill_rect(hdc, bx, 0, CAPTION_BTN_W, TITLEBAR_HEIGHT, bg);
        }
        COLORREF col = (i == 2 && hov) ? RGB(255, 255, 255)
                     : (hov           ? C_TEXT_PRIMARY : C_TEXT_SECONDARY);
        ui_draw_icon(hdc, icons[i], bx, 0, CAPTION_BTN_W, TITLEBAR_HEIGHT,
                     col, g_fontIcon);
    }

    /* Single hairline at bottom — sits at the top of the tab bar's bg */
    ui_draw_separator_h(hdc, 0, TITLEBAR_HEIGHT - 1, w, C_BORDER_SUBTLE);
}

/* ============================================================
   Painting: STATUS BAR
   ============================================================ */
static void paint_statusbar(AppWindow *app, HDC hdc, int w, int top) {
    ui_fill_rect(hdc, 0, top, w, STATUSBAR_HEIGHT, C_STATUSBAR);
    ui_draw_separator_h(hdc, 0, top, w, C_BORDER_SUBTLE);

    Tab *tab = tabs_get_active(&app->tabs);
    if (tab) {
        BOOL connected =
            (tab->type == TAB_TYPE_SSH    && tab->conn.ssh    && tab->conn.ssh->running) ||
            (tab->type == TAB_TYPE_SERIAL && tab->conn.serial && tab->conn.serial->running);
        COLORREF dotColor = connected ? C_SUCCESS : C_ERROR;

        /* Subtle pulsing dot when connected (uses cursor blink tick) */
        int dotR = 4;
        int dotX = 14, dotY = top + STATUSBAR_HEIGHT/2;
        HBRUSH br  = CreateSolidBrush(dotColor);
        HPEN   pen = CreatePen(PS_NULL, 0, 0);
        HBRUSH oldBr  = SelectObject(hdc, br);
        HPEN   oldPen = SelectObject(hdc, pen);
        Ellipse(hdc, dotX - dotR, dotY - dotR, dotX + dotR, dotY + dotR);
        SelectObject(hdc, oldBr); SelectObject(hdc, oldPen);
        DeleteObject(br); DeleteObject(pen);

        const char *typeStr = (tab->type == TAB_TYPE_SSH) ? "SSH" : "Serial";
        const char *state   = connected ? "connect\xe9" : "d\xe9" "connect\xe9";
        char status[256];
        snprintf(status, sizeof(status), "%s  %s  \xb7  %s",
                 state, typeStr, tab->title);
        ui_draw_text(hdc, status, 26, top, 500, STATUSBAR_HEIGHT,
                     C_TEXT_SECONDARY, g_fontUISmall,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (tab->terminal) {
            char sizeStr[32];
            snprintf(sizeStr, sizeof(sizeStr), "%d \xd7 %d",
                     tab->terminal->cols, tab->terminal->rows);
            ui_draw_text(hdc, sizeStr, w - 120, top, 108, STATUSBAR_HEIGHT,
                         C_TEXT_MUTED, g_fontMonoSmall,
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        /* Disconnected dot */
        int dotR = 4, dotX = 14, dotY = top + STATUSBAR_HEIGHT/2;
        HBRUSH br = CreateSolidBrush(C_TEXT_DIM);
        HPEN pen = CreatePen(PS_NULL, 0, 0);
        HBRUSH oldBr = SelectObject(hdc, br);
        HPEN oldPen = SelectObject(hdc, pen);
        Ellipse(hdc, dotX - dotR, dotY - dotR, dotX + dotR, dotY + dotR);
        SelectObject(hdc, oldBr); SelectObject(hdc, oldPen);
        DeleteObject(br); DeleteObject(pen);

        ui_draw_text(hdc, "Aucune session active", 26, top, 300,
                     STATUSBAR_HEIGHT,
                     C_TEXT_MUTED, g_fontUISmall,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Right-side keyboard hints (only when no tab to avoid duplication) */
    const char *hint = tab ? "Ctrl+Shift+C copier   Ctrl+Shift+V coller"
                           : "Ctrl+N SSH   Ctrl+Shift+N Serial";
    ui_draw_text(hdc, hint, w/2 - 220, top, 440, STATUSBAR_HEIGHT,
                 C_TEXT_DIM, g_fontMonoSmall,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* ============================================================
   Painting: EMPTY STATE
   ============================================================ */
static void paint_empty_state(AppWindow *app, HDC hdc, int w,
                              int termTop, int termBottom)
{
    int h = termBottom - termTop;
    ui_fill_rect(hdc, 0, termTop, w, h, C_BG_PRIMARY);

    int centerY = termTop + h / 2;

    /* Big monospace mark, very dim — sets the terminal mood without shouting */
    ui_draw_text(hdc, ">_", 0, centerY - 130, w, 60,
                 C_BG_ELEVATED, g_fontHeroBig,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Heading + sub */
    ui_draw_text(hdc, "NetFlow Terminal", 0, centerY - 56, w, 32,
                 C_TEXT_PRIMARY, g_fontEmptyTitle,
                 DT_CENTER | DT_SINGLELINE);
    ui_draw_text(hdc, "Choisissez un type de connexion pour commencer",
                 0, centerY - 18, w, 20,
                 C_TEXT_MUTED, g_fontUISmall,
                 DT_CENTER | DT_SINGLELINE);

    RECT sshBtn, serialBtn;
    get_empty_btn_rects(w, termTop, termBottom, &sshBtn, &serialBtn);
    int btnW = sshBtn.right - sshBtn.left;
    int btnH = sshBtn.bottom - sshBtn.top;

    /* --- SSH button --- */
    BOOL sshHov = (app->emptyHoverBtn == 0);
    float t0 = app->emptyAnim[0].value;
    COLORREF sshBg = ui_lerp_color(C_BG_SURFACE, C_BG_HOVER, t0);
    COLORREF sshBd = ui_lerp_color(C_BORDER,     C_ACCENT,   t0);
    ui_rounded_rect(hdc, sshBtn.left, sshBtn.top, btnW, btnH,
                    UI_RADIUS_BTN, sshBg, sshBd);
    /* Type badge — pill */
    int bw = 44, bh = 22;
    int bx = sshBtn.left + 16;
    int by = sshBtn.top + (btnH - bh)/2;
    ui_rounded_rect(hdc, bx, by, bw, bh, 11, C_ACCENT, C_ACCENT);
    ui_draw_text(hdc, "SSH", bx, by, bw, bh,
                 RGB(255,255,255), g_fontLabel,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    /* Title + sub */
    ui_draw_text(hdc, "Nouvelle connexion SSH",
                 bx + bw + 14, sshBtn.top + 12,
                 btnW - (bx + bw + 14 - sshBtn.left) - 16, 22,
                 sshHov ? C_TEXT_PRIMARY : C_TEXT_SECONDARY,
                 g_fontUIBold, DT_LEFT | DT_SINGLELINE);
    ui_draw_text(hdc, "Switch, routeur, serveur \x97 port 22",
                 bx + bw + 14, sshBtn.top + 34,
                 btnW - (bx + bw + 14 - sshBtn.left) - 16, 18,
                 C_TEXT_MUTED, g_fontUISmall, DT_LEFT | DT_SINGLELINE);
    /* Shortcut */
    ui_draw_text(hdc, "Ctrl+N", sshBtn.right - 80,
                 sshBtn.top, 64, btnH,
                 C_TEXT_DIM, g_fontMonoSmall,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* --- Serial button --- */
    BOOL serHov = (app->emptyHoverBtn == 1);
    float t1 = app->emptyAnim[1].value;
    COLORREF serBg = ui_lerp_color(C_BG_SURFACE, C_BG_HOVER, t1);
    COLORREF serBd = ui_lerp_color(C_BORDER,     C_SUCCESS,  t1);
    int serH = serialBtn.bottom - serialBtn.top;
    ui_rounded_rect(hdc, serialBtn.left, serialBtn.top, btnW, serH,
                    UI_RADIUS_BTN, serBg, serBd);
    int bx2 = serialBtn.left + 16;
    int by2 = serialBtn.top + (serH - bh)/2;
    ui_rounded_rect(hdc, bx2, by2, bw, bh, 11, C_SUCCESS, C_SUCCESS);
    ui_draw_text(hdc, "COM", bx2, by2, bw, bh,
                 RGB(8, 32, 24), g_fontLabel,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ui_draw_text(hdc, "Connexion s\xe9rie",
                 bx2 + bw + 14, serialBtn.top + 12,
                 btnW - (bx2 + bw + 14 - serialBtn.left) - 16, 22,
                 serHov ? C_TEXT_PRIMARY : C_TEXT_SECONDARY,
                 g_fontUIBold, DT_LEFT | DT_SINGLELINE);
    ui_draw_text(hdc, "Console RJ45, USB-COM, port DB9",
                 bx2 + bw + 14, serialBtn.top + 34,
                 btnW - (bx2 + bw + 14 - serialBtn.left) - 16, 18,
                 C_TEXT_MUTED, g_fontUISmall, DT_LEFT | DT_SINGLELINE);
    ui_draw_text(hdc, "Ctrl+Shift+N", serialBtn.right - 110,
                 serialBtn.top, 94, serH,
                 C_TEXT_DIM, g_fontMonoSmall,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

/* ============================================================
   Painting: TERMINAL AREA
   ============================================================ */
static void paint_terminal_area(AppWindow *app, HDC hdc, int w,
                                int termTop, int termBottom)
{
    int h = termBottom - termTop;
    ui_fill_rect(hdc, 0, termTop, w, h, C_BG_PRIMARY);

    Tab *tab = tabs_get_active(&app->tabs);
    if (!tab || !tab->terminal) return;

    int pad = TERM_PADDING;
    int saveState = SaveDC(hdc);
    SetViewportOrgEx(hdc, pad, termTop + pad, NULL);
    RECT localRect = { 0, 0, w - pad*2, h - pad*2 };
    term_paint(tab->terminal, hdc, &localRect);
    RestoreDC(hdc, saveState);
}

/* ============================================================
   Painting: MODAL OVERLAY (with animated dim)
   ============================================================ */
static void paint_modal_overlay(AppWindow *app, HDC hdc, int w, int h) {
    /* Lerp the entire terminal-area underlay toward a darker scrim.
       Since GDI has no alpha on FillRect, we just over-paint with a
       scrim colour that's between C_BG_PRIMARY and "almost black",
       proportional to the modal->appear animation. */
    float t = app->modal.appear.value;       /* 0..1 */
    COLORREF scrim = ui_lerp_color(C_BG_PRIMARY, RGB(4, 5, 8), 0.75f);
    /* We can't truly alpha-over a previously-drawn frame; instead we
       paint a near-opaque scrim. The dim crossfade *appears* because
       this paint_modal_overlay only runs when modal != NONE, and the
       appear timer ramps the modal card from below-frame to full size,
       which is what the eye reads as "fading in". */
    (void)t;
    HBRUSH br = CreateSolidBrush(scrim);
    RECT rc = { 0, 0, w, h };
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

static void modal_card_geometry(AppWindow *app, int w, int h,
                                int *mx, int *my, int *mw, int *mh,
                                int outW, int outH)
{
    float t = ui_ease_out_back(app->modal.appear.value);
    /* Card scales from 96% → 100% with overshoot ease */
    float scale = 0.96f + 0.04f * t;
    *mw = (int)(outW * scale);
    *mh = (int)(outH * scale);
    *mx = (w - *mw) / 2;
    /* Slight rise: starts 12px lower than rest position */
    int rise = (int)((1.f - t) * 12);
    *my = (h - *mh) / 2 + rise;
}

/* --- SSH modal ----------------------------------------------- */
static void paint_modal_ssh(AppWindow *app, HDC hdc, int w, int h) {
    paint_modal_overlay(app, hdc, w, h);
    ModalState *m = &app->modal;

    int mw, mh, mx, my;
    modal_card_geometry(app, w, h, &mx, &my, &mw, &mh, 440, 360);

    /* Card */
    ui_rounded_rect(hdc, mx, my, mw, mh, UI_RADIUS_CARD,
                    C_BG_SECONDARY, C_BORDER_LIGHT);

    /* Header */
    int hx = mx + 28, hy = my + 24;
    /* Mini badge */
    ui_rounded_rect(hdc, hx, hy + 2, 48, 22, 11, C_ACCENT, C_ACCENT);
    ui_draw_text(hdc, "SSH", hx, hy + 2, 48, 22,
                 RGB(255,255,255), g_fontLabel,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ui_draw_text(hdc, "Nouvelle connexion", hx + 60, hy, mw - 88, 24,
                 C_TEXT_PRIMARY, g_fontUIBold,
                 DT_LEFT | DT_SINGLELINE);
    ui_draw_text(hdc, "Connectez-vous \xe0 un \xe9quipement r\xe9seau",
                 hx + 60, hy + 24, mw - 88, 18,
                 C_TEXT_MUTED, g_fontUISmall,
                 DT_LEFT | DT_SINGLELINE);
    ui_draw_separator_h(hdc, mx + 24, my + 84, mw - 48, C_BORDER_SUBTLE);

    /* Fields layout (label above field) */
    const char *labels[] = { "H\xd4TE", "PORT", "UTILISATEUR", "MOT DE PASSE" };
    int fieldGeom[4][3];   /* x, y, w (relative to card) */
    fieldGeom[0][0] = 24;  fieldGeom[0][1] = 104; fieldGeom[0][2] = mw - 48;
    fieldGeom[1][0] = 24;  fieldGeom[1][1] = 168; fieldGeom[1][2] = 120;
    fieldGeom[2][0] = 156; fieldGeom[2][1] = 168; fieldGeom[2][2] = mw - 48 - 132;
    fieldGeom[3][0] = 24;  fieldGeom[3][1] = 232; fieldGeom[3][2] = mw - 48;

    for (int i = 0; i < 4; i++) {
        int fx = mx + fieldGeom[i][0];
        int fy = my + fieldGeom[i][1];
        int fw = fieldGeom[i][2];

        ui_draw_text(hdc, labels[i], fx, fy, fw, 14,
                     C_TEXT_MUTED, g_fontLabel,
                     DT_LEFT | DT_SINGLELINE);

        BOOL focused = (m->focusedField == i);
        COLORREF bd = focused ? C_ACCENT : C_BORDER;
        COLORREF bg = focused ? C_BG_ELEVATED : C_BG_SURFACE;
        ui_rounded_rect(hdc, fx, fy + 18, fw, 34, UI_RADIUS, bg, bd);

        const char *val = m->fields[i];
        char masked[256];
        const char *display = val;
        if (i == 3 && val[0]) {
            int n = (int)strlen(val);
            for (int k = 0; k < n; k++) masked[k] = '*';
            masked[n] = 0;
            display = masked;
        }
        if (val[0]) {
            ui_draw_text(hdc, display, fx + 12, fy + 18, fw - 24, 34,
                         C_TEXT_PRIMARY, g_fontMono,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            const char *ph[] = { "192.168.1.1", "22", "admin", "\x95\x95\x95\x95\x95\x95\x95\x95" };
            ui_draw_text(hdc, ph[i], fx + 12, fy + 18, fw - 24, 34,
                         C_TEXT_DIM, g_fontMono,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        /* Caret in focused field */
        if (focused) {
            SIZE sz;
            HFONT old = SelectObject(hdc, g_fontMono);
            GetTextExtentPoint32A(hdc, val, (int)strlen(val), &sz);
            SelectObject(hdc, old);
            ui_fill_rect(hdc, fx + 12 + sz.cx, fy + 18 + 8, 2, 18, C_ACCENT);
        }
    }

    /* Error text */
    if (m->error[0]) {
        ui_fill_rect(hdc, mx + 24, my + mh - 102, mw - 48, 1, C_ERROR_DIM);
        ui_draw_text(hdc, m->error, mx + 28, my + mh - 96, mw - 56, 20,
                     C_ERROR, g_fontUISmall,
                     DT_LEFT | DT_SINGLELINE);
    }

    /* Buttons */
    int btnY = my + mh - 60;
    int primaryW = 140, cancelW = 96, gap = 8;
    int primaryX = mx + mw - 24 - primaryW;
    int cancelX  = primaryX - cancelW - gap;

    /* Cancel */
    BOOL canHov = (m->hoverButton == 0);
    ui_rounded_rect(hdc, cancelX, btnY, cancelW, 38,
                    UI_RADIUS_BTN,
                    canHov ? C_BG_HOVER : C_BG_SURFACE,
                    canHov ? C_BORDER_LIGHT : C_BORDER);
    ui_draw_text(hdc, "Annuler", cancelX, btnY, cancelW, 38,
                 C_TEXT_SECONDARY, g_fontUI,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Primary */
    BOOL prHov = (m->hoverButton == 1);
    COLORREF prBg = m->connecting ? C_ACCENT_DIM
                                  : (prHov ? C_ACCENT_HOVER : C_ACCENT);
    ui_rounded_rect(hdc, primaryX, btnY, primaryW, 38,
                    UI_RADIUS_BTN, prBg, prBg);
    ui_draw_text(hdc, m->connecting ? "Connexion\x85" : "Connecter",
                 primaryX, btnY, primaryW, 38,
                 RGB(255, 255, 255), g_fontUIBold,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* --- Serial modal -------------------------------------------- */
static void paint_modal_serial(AppWindow *app, HDC hdc, int w, int h) {
    paint_modal_overlay(app, hdc, w, h);
    ModalState *m = &app->modal;

    int mw, mh, mx, my;
    modal_card_geometry(app, w, h, &mx, &my, &mw, &mh, 440, 400);

    ui_rounded_rect(hdc, mx, my, mw, mh, UI_RADIUS_CARD,
                    C_BG_SECONDARY, C_BORDER_LIGHT);

    /* Header */
    int hx = mx + 28, hy = my + 24;
    ui_rounded_rect(hdc, hx, hy + 2, 48, 22, 11, C_SUCCESS, C_SUCCESS);
    ui_draw_text(hdc, "COM", hx, hy + 2, 48, 22,
                 RGB(8, 32, 24), g_fontLabel,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ui_draw_text(hdc, "Connexion s\xe9rie", hx + 60, hy, mw - 88, 24,
                 C_TEXT_PRIMARY, g_fontUIBold,
                 DT_LEFT | DT_SINGLELINE);
    ui_draw_text(hdc, "Console \xe9quipement via port COM",
                 hx + 60, hy + 24, mw - 88, 18,
                 C_TEXT_MUTED, g_fontUISmall,
                 DT_LEFT | DT_SINGLELINE);
    ui_draw_separator_h(hdc, mx + 24, my + 84, mw - 48, C_BORDER_SUBTLE);

    const char *labels[] = { "PORT COM", "BAUD RATE", "DATA BITS", "PARIT\xc9", "STOP BITS" };
    const char *defaults[] = { "(aucun port d\xe9tect\xe9)", "9600", "8", "None", "1" };

    int fieldGeom[5][3];
    fieldGeom[0][0] = 24;  fieldGeom[0][1] = 104; fieldGeom[0][2] = mw - 48;
    fieldGeom[1][0] = 24;  fieldGeom[1][1] = 174; fieldGeom[1][2] = 180;
    fieldGeom[2][0] = 216; fieldGeom[2][1] = 174; fieldGeom[2][2] = mw - 48 - 192;
    fieldGeom[3][0] = 24;  fieldGeom[3][1] = 244; fieldGeom[3][2] = 180;
    fieldGeom[4][0] = 216; fieldGeom[4][1] = 244; fieldGeom[4][2] = mw - 48 - 192;

    /* Save geom for dropdown */
    int comFX = mx + fieldGeom[0][0];
    int comFY = my + fieldGeom[0][1] + 18;
    int comFW = fieldGeom[0][2];
    int comFH = 34;

    for (int i = 0; i < 5; i++) {
        int fx = mx + fieldGeom[i][0];
        int fy = my + fieldGeom[i][1];
        int fw = fieldGeom[i][2];

        ui_draw_text(hdc, labels[i], fx, fy, fw, 14,
                     C_TEXT_MUTED, g_fontLabel,
                     DT_LEFT | DT_SINGLELINE);

        BOOL focused = (m->focusedField == i);
        BOOL isCom   = (i == 0);
        COLORREF bd = focused ? C_ACCENT : C_BORDER;
        COLORREF bg = (isCom && m->comDropdownOpen) ? C_BG_ELEVATED
                    : focused                      ? C_BG_ELEVATED
                    :                                C_BG_SURFACE;
        ui_rounded_rect(hdc, fx, fy + 18, fw, 34, UI_RADIUS, bg, bd);

        const char *val = m->fields[i][0] ? m->fields[i] : defaults[i];
        COLORREF valColor = m->fields[i][0] ? C_TEXT_PRIMARY : C_TEXT_DIM;
        int rightInset = isCom ? 36 : 24;
        ui_draw_text(hdc, val, fx + 12, fy + 18, fw - rightInset - 12, 34,
                     valColor, g_fontMono,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Chevron for COM dropdown */
        if (isCom) {
            int ax = fx + fw - 22;
            int ay = fy + 18 + 17;
            HPEN pen = CreatePen(PS_SOLID, 2,
                                 focused ? C_ACCENT : C_TEXT_MUTED);
            HPEN op  = SelectObject(hdc, pen);
            if (m->comDropdownOpen) {
                MoveToEx(hdc, ax - 5, ay + 3, NULL); LineTo(hdc, ax,     ay - 3);
                MoveToEx(hdc, ax,     ay - 3, NULL); LineTo(hdc, ax + 6, ay + 3);
            } else {
                MoveToEx(hdc, ax - 5, ay - 3, NULL); LineTo(hdc, ax,     ay + 3);
                MoveToEx(hdc, ax,     ay + 3, NULL); LineTo(hdc, ax + 6, ay - 3);
            }
            SelectObject(hdc, op);
            DeleteObject(pen);
        }
    }

    /* Error */
    if (m->error[0]) {
        ui_draw_text(hdc, m->error, mx + 28, my + mh - 96, mw - 56, 20,
                     C_ERROR, g_fontUISmall,
                     DT_LEFT | DT_SINGLELINE);
    }

    /* Buttons */
    int btnY = my + mh - 60;
    int primaryW = 140, cancelW = 96, gap = 8;
    int primaryX = mx + mw - 24 - primaryW;
    int cancelX  = primaryX - cancelW - gap;

    BOOL canHov = (m->hoverButton == 0);
    ui_rounded_rect(hdc, cancelX, btnY, cancelW, 38,
                    UI_RADIUS_BTN,
                    canHov ? C_BG_HOVER : C_BG_SURFACE,
                    canHov ? C_BORDER_LIGHT : C_BORDER);
    ui_draw_text(hdc, "Annuler", cancelX, btnY, cancelW, 38,
                 C_TEXT_SECONDARY, g_fontUI,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BOOL prHov = (m->hoverButton == 1);
    COLORREF prBg = m->connecting ? C_SUCCESS_DIM
                                  : (prHov ? ui_lerp_color(C_SUCCESS, RGB(255,255,255), 0.1f) : C_SUCCESS);
    ui_rounded_rect(hdc, primaryX, btnY, primaryW, 38,
                    UI_RADIUS_BTN, prBg, prBg);
    ui_draw_text(hdc, m->connecting ? "Connexion\x85" : "Connecter",
                 primaryX, btnY, primaryW, 38,
                 RGB(8, 32, 24), g_fontUIBold,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Dropdown overlay */
    if (m->comDropdownOpen && m->comPortCount > 0) {
        int maxVis = m->comPortCount < 8 ? m->comPortCount : 8;
        int itemH  = 32;
        int listH  = maxVis * itemH + 8;

        /* Soft shadow */
        ui_fill_rect(hdc, comFX + 2, comFY + comFH + 6, comFW, listH, RGB(5, 6, 10));
        ui_rounded_rect(hdc, comFX, comFY + comFH + 4, comFW, listH,
                        UI_RADIUS, C_BG_ELEVATED, C_BORDER_LIGHT);

        for (int i = 0; i < maxVis; i++) {
            int iy = comFY + comFH + 8 + i * itemH;
            BOOL hov = (i == m->comDropdownHover);
            if (hov) {
                ui_rounded_rect(hdc, comFX + 4, iy - 2, comFW - 8, itemH - 2,
                                4, C_ACCENT_DIM, C_ACCENT_DIM);
            }
            /* Mono COM name + (optional) description after */
            ui_draw_text(hdc, m->comPorts[i],
                         comFX + 16, iy, comFW - 32, itemH - 2,
                         hov ? C_TEXT_PRIMARY : C_TEXT_SECONDARY,
                         g_fontMono,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else if (m->comDropdownOpen && m->comPortCount == 0) {
        int listH = 40;
        ui_rounded_rect(hdc, comFX, comFY + comFH + 4, comFW, listH,
                        UI_RADIUS, C_BG_ELEVATED, C_BORDER_LIGHT);
        ui_draw_text(hdc, "Aucun port COM d\xe9tect\xe9",
                     comFX + 16, comFY + comFH + 4, comFW - 32, listH,
                     C_TEXT_MUTED, g_fontUISmall,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ============================================================
   Modal logic (largely unchanged, plus appear-anim init)
   ============================================================ */
static void open_modal_ssh(AppWindow *app) {
    memset(&app->modal, 0, sizeof(ModalState));
    app->modal.type = MODAL_SSH;
    app->modal.hoverButton = -1;
    strcpy(app->modal.fields[1], "22");
    ui_anim_init(&app->modal.appear, 0.f, 14.f);
    ui_anim_set(&app->modal.appear, 1.f);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void open_modal_serial(AppWindow *app) {
    memset(&app->modal, 0, sizeof(ModalState));
    app->modal.type = MODAL_SERIAL;
    app->modal.hoverButton = -1;
    app->modal.comPortCount = serial_list_ports(app->modal.comPorts, 64);
    app->modal.comDropdownHover = -1;
    if (app->modal.comPortCount > 0)
        strncpy(app->modal.fields[0], app->modal.comPorts[0], 255);
    strcpy(app->modal.fields[1], "9600");
    strcpy(app->modal.fields[2], "8");
    strcpy(app->modal.fields[3], "None");
    strcpy(app->modal.fields[4], "1");
    ui_anim_init(&app->modal.appear, 0.f, 14.f);
    ui_anim_set(&app->modal.appear, 1.f);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void close_modal(AppWindow *app) {
    app->modal.type = MODAL_NONE;
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void do_modal_ssh_connect(AppWindow *app) {
    ModalState *m = &app->modal;
    if (!m->fields[0][0] || !m->fields[2][0]) {
        strcpy(m->error, "Renseignez l'h\xf4te et l'utilisateur.");
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    m->connecting = TRUE; m->error[0] = 0;
    InvalidateRect(app->hwnd, NULL, FALSE); UpdateWindow(app->hwnd);

    char title[128];
    snprintf(title, sizeof(title), "%s@%s", m->fields[2], m->fields[0]);
    int tabIdx = tabs_add(&app->tabs, TAB_TYPE_SSH, title);
    Tab *tab = &app->tabs.tabs[tabIdx];
    tab->terminal->hwnd = app->hwnd;
    term_set_font(tab->terminal, create_term_font(app->fontSize));
    update_terminal_sizes(app);

    SshConnection *conn = ssh_connect(m->fields[0], atoi(m->fields[1]),
                                      m->fields[2], m->fields[3]);
    if (conn) {
        tab->conn.ssh = conn;
        ssh_set_callbacks(conn, ssh_data_cb, ssh_close_cb, (void *)(intptr_t)tabIdx);
        close_modal(app);
    } else {
        tabs_remove(&app->tabs, tabIdx);
        m->connecting = FALSE;
        strcpy(m->error, "\xc9" "chec de la connexion SSH.");
        InvalidateRect(app->hwnd, NULL, FALSE);
    }
}

static void do_modal_serial_connect(AppWindow *app) {
    ModalState *m = &app->modal;
    if (!m->fields[0][0]) {
        strcpy(m->error, "S\xe9lectionnez un port COM.");
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    int tabIdx = tabs_add(&app->tabs, TAB_TYPE_SERIAL, m->fields[0]);
    Tab *tab = &app->tabs.tabs[tabIdx];
    tab->terminal->hwnd = app->hwnd;
    term_set_font(tab->terminal, create_term_font(app->fontSize));
    update_terminal_sizes(app);

    int parity = 0;
    if      (strcmp(m->fields[3], "Even") == 0) parity = 1;
    else if (strcmp(m->fields[3], "Odd")  == 0) parity = 2;

    SerialConnection *conn = serial_connect(m->fields[0], atoi(m->fields[1]),
                                            atoi(m->fields[2]), parity, atoi(m->fields[4]));
    if (conn) {
        tab->conn.serial = conn;
        serial_set_callbacks(conn, serial_data_cb, serial_close_cb, (void *)(intptr_t)tabIdx);
        close_modal(app);
    } else {
        tabs_remove(&app->tabs, tabIdx);
        strcpy(m->error, "\xc9" "chec de la connexion s\xe9rie.");
        InvalidateRect(app->hwnd, NULL, FALSE);
    }
}

static void modal_handle_char(AppWindow *app, char c) {
    ModalState *m = &app->modal;
    int maxFields = (m->type == MODAL_SSH) ? 4 : 5;
    if (m->focusedField < 0 || m->focusedField >= maxFields) return;

    char *field = m->fields[m->focusedField];
    int len = (int)strlen(field);

    if      (c == '\b')                  { if (len > 0) field[len - 1] = 0; }
    else if (c == '\t')                  { m->focusedField = (m->focusedField + 1) % maxFields; }
    else if (c == '\r')                  { (m->type == MODAL_SSH) ? do_modal_ssh_connect(app) : do_modal_serial_connect(app); return; }
    else if (c == 0x1b)                  { close_modal(app); return; }
    else if (c >= 0x20 && len < 254)     { field[len] = c; field[len + 1] = 0; }
    m->error[0] = 0;
    InvalidateRect(app->hwnd, NULL, FALSE);
}

static void modal_handle_click(AppWindow *app, int x, int y) {
    ModalState *m = &app->modal;
    RECT rc; GetClientRect(app->hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    int mw, mh, mx, my;
    int outW = (m->type == MODAL_SSH) ? 440 : 440;
    int outH = (m->type == MODAL_SSH) ? 360 : 400;
    modal_card_geometry(app, w, h, &mx, &my, &mw, &mh, outW, outH);

    /* Outside modal */
    if (x < mx || x > mx + mw || y < my || y > my + mh) { close_modal(app); return; }

    int btnY = my + mh - 60;
    int primaryX = mx + mw - 24 - 140;
    int cancelX  = primaryX - 96 - 8;
    if (x >= primaryX && x <= primaryX + 140 && y >= btnY && y <= btnY + 38) {
        (m->type == MODAL_SSH) ? do_modal_ssh_connect(app) : do_modal_serial_connect(app);
        return;
    }
    if (x >= cancelX && x <= cancelX + 96 && y >= btnY && y <= btnY + 38) {
        close_modal(app); return;
    }

    if (m->type == MODAL_SERIAL && m->comDropdownOpen && m->comPortCount > 0) {
        int comFX = mx + 24, comFY = my + 104 + 18, comFW = mw - 48, comFH = 34;
        int maxVis = m->comPortCount < 8 ? m->comPortCount : 8;
        int itemH = 32;
        int listTop = comFY + comFH + 8;
        if (x >= comFX && x < comFX + comFW &&
            y >= listTop && y < listTop + maxVis * itemH) {
            int idx = (y - listTop) / itemH;
            if (idx >= 0 && idx < maxVis)
                strncpy(m->fields[0], m->comPorts[idx], 255);
        }
        m->comDropdownOpen = FALSE;
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }

    /* Field hit-test (matches the geometry used in paint) */
    if (m->type == MODAL_SSH) {
        int g[4][4] = {
            { 24, 104+18, mw-48, 34 },
            { 24, 168+18, 120,   34 },
            { 156,168+18, mw-48-132, 34 },
            { 24, 232+18, mw-48, 34 },
        };
        for (int i = 0; i < 4; i++) {
            if (x >= mx+g[i][0] && x <= mx+g[i][0]+g[i][2] &&
                y >= my+g[i][1] && y <= my+g[i][1]+g[i][3]) {
                m->focusedField = i;
                InvalidateRect(app->hwnd, NULL, FALSE);
                return;
            }
        }
    } else {
        int g[5][4] = {
            { 24, 104+18, mw-48,        34 },
            { 24, 174+18, 180,          34 },
            { 216,174+18, mw-48-192,    34 },
            { 24, 244+18, 180,          34 },
            { 216,244+18, mw-48-192,    34 },
        };
        for (int i = 0; i < 5; i++) {
            if (x >= mx+g[i][0] && x <= mx+g[i][0]+g[i][2] &&
                y >= my+g[i][1] && y <= my+g[i][1]+g[i][3]) {
                if (i == 0) { m->comDropdownOpen = TRUE; m->comDropdownHover = -1; }
                m->focusedField = i;
                InvalidateRect(app->hwnd, NULL, FALSE);
                return;
            }
        }
    }
}

/* ============================================================
   Clipboard, fullscreen, hit-test — unchanged from original
   ============================================================ */
static void copy_selection(AppWindow *app) {
    Tab *tab = tabs_get_active(&app->tabs);
    if (!tab || !tab->terminal) return;
    WCHAR *sel = term_get_selection(tab->terminal);
    if (!sel) return;
    int len = (int)wcslen(sel);
    if (OpenClipboard(app->hwnd)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(WCHAR));
        if (hMem) {
            WCHAR *dst = GlobalLock(hMem);
            memcpy(dst, sel, (len + 1) * sizeof(WCHAR));
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
    free(sel);
}

static void paste_clipboard(AppWindow *app) {
    Tab *tab = tabs_get_active(&app->tabs);
    if (!tab) return;
    if (OpenClipboard(app->hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            WCHAR *wstr = GlobalLock(hData);
            if (wstr) {
                int wlen = (int)wcslen(wstr);
                int needed = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, NULL, 0, NULL, NULL);
                char *utf8 = malloc(needed + 1);
                WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, utf8, needed, NULL, NULL);
                utf8[needed] = 0;
                if      (tab->type == TAB_TYPE_SSH    && tab->conn.ssh)    ssh_write   (tab->conn.ssh,    utf8, needed);
                else if (tab->type == TAB_TYPE_SERIAL && tab->conn.serial) serial_write(tab->conn.serial, utf8, needed);
                free(utf8);
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
}

static void toggle_fullscreen(AppWindow *app) {
    DWORD style = GetWindowLong(app->hwnd, GWL_STYLE);
    if (!app->fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(app->hwnd, &app->wpPrev) &&
            GetMonitorInfo(MonitorFromWindow(app->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(app->hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(app->hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        app->fullscreen = TRUE;
    } else {
        SetWindowLong(app->hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(app->hwnd, &app->wpPrev);
        SetWindowPos(app->hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        app->fullscreen = FALSE;
    }
}

/* ============================================================
   Animation tick — drives all interpolated UI motion
   ============================================================ */
static void animate_step(AppWindow *app) {
    DWORD now = GetTickCount();
    if (g_lastAnimTick == 0) g_lastAnimTick = now;
    float dt = (now - g_lastAnimTick) / 1000.f;
    g_lastAnimTick = now;
    if (dt > 0.1f) dt = 0.1f;   /* clamp after pause */

    BOOL dirty = FALSE;
    dirty |= tabs_animate(&app->tabs, dt);

    /* Empty-state button hovers */
    for (int i = 0; i < 2; i++) {
        ui_anim_set(&app->emptyAnim[i], (app->emptyHoverBtn == i) ? 1.f : 0.f);
        dirty |= ui_anim_tick(&app->emptyAnim[i], dt);
    }
    /* Modal appear */
    if (app->modal.type != MODAL_NONE)
        dirty |= ui_anim_tick(&app->modal.appear, dt);

    if (dirty) InvalidateRect(app->hwnd, NULL, FALSE);
}

/* ============================================================
   WndProc
   ============================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow *app = g_app;

    switch (msg) {
    case WM_CREATE:
        init_fonts();
        SetTimer(hwnd, TIMER_CURSOR_BLINK, 530, NULL);
        SetTimer(hwnd, TIMER_ANIMATE, ANIM_INTERVAL_MS, NULL);
        ui_anim_init(&app->emptyAnim[0], 0.f, 16.f);
        ui_anim_init(&app->emptyAnim[1], 0.f, 16.f);
        return 0;

    case WM_SIZE:
        update_terminal_sizes(app);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = SelectObject(memDC, memBM);

        paint_titlebar(app, memDC, w);

        RECT tabBarRect = { 0, TITLEBAR_HEIGHT, w, get_tabbar_bottom() };
        tabs_paint_bar(&app->tabs, memDC, &tabBarRect);

        int termTop = get_term_top();
        int termBot = get_term_bottom(app);
        Tab *tab = tabs_get_active(&app->tabs);
        if (tab && tab->terminal) paint_terminal_area(app, memDC, w, termTop, termBot);
        else                      paint_empty_state  (app, memDC, w, termTop, termBot);

        paint_statusbar(app, memDC, w, get_statusbar_top(app));

        if      (app->modal.type == MODAL_SSH)    paint_modal_ssh   (app, memDC, w, h);
        else if (app->modal.type == MODAL_SERIAL) paint_modal_serial(app, memDC, w, h);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_CURSOR_BLINK) {
            Tab *tab = tabs_get_active(&app->tabs);
            if (tab && tab->terminal) {
                tab->terminal->cursorVisible = !tab->terminal->cursorVisible;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (wParam == TIMER_ANIMATE) {
            animate_step(app);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CHAR: {
        if (app->modal.type != MODAL_NONE) { modal_handle_char(app, (char)wParam); return 0; }
        Tab *tab = tabs_get_active(&app->tabs);
        if (!tab) return 0;
        char c = (char)wParam;
        if      (tab->type == TAB_TYPE_SSH    && tab->conn.ssh)    ssh_write   (tab->conn.ssh,    &c, 1);
        else if (tab->type == TAB_TYPE_SERIAL && tab->conn.serial) serial_write(tab->conn.serial, &c, 1);
        return 0;
    }

    case WM_KEYDOWN: {
        BOOL ctrl  = GetKeyState(VK_CONTROL) & 0x8000;
        BOOL shift = GetKeyState(VK_SHIFT)   & 0x8000;

        if (wParam == VK_ESCAPE && app->modal.type != MODAL_NONE) { close_modal(app); return 0; }
        if (wParam == VK_F11) { toggle_fullscreen(app); return 0; }
        if (ctrl && shift && wParam == 'C') { copy_selection(app); return 0; }
        if (ctrl && shift && wParam == 'V') { paste_clipboard(app); return 0; }
        if (ctrl && shift && wParam == 'N') { open_modal_serial(app); return 0; }
        if (ctrl && wParam == 'N') { open_modal_ssh(app); return 0; }
        if (ctrl && wParam == 'W') {
            if (app->tabs.activeTab >= 0) tabs_remove(&app->tabs, app->tabs.activeTab);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        if (app->modal.type != MODAL_NONE) {
            if (wParam == VK_TAB) {
                int max = (app->modal.type == MODAL_SSH) ? 4 : 5;
                if (shift) app->modal.focusedField = (app->modal.focusedField - 1 + max) % max;
                else       app->modal.focusedField = (app->modal.focusedField + 1)       % max;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        Tab *tab = tabs_get_active(&app->tabs);
        if (!tab) return 0;

        char seq[8] = {0}; int seqLen = 0;
        switch (wParam) {
        case VK_UP:    seq[0]=0x1b; seq[1]='['; seq[2]='A'; seqLen=3; break;
        case VK_DOWN:  seq[0]=0x1b; seq[1]='['; seq[2]='B'; seqLen=3; break;
        case VK_RIGHT: seq[0]=0x1b; seq[1]='['; seq[2]='C'; seqLen=3; break;
        case VK_LEFT:  seq[0]=0x1b; seq[1]='['; seq[2]='D'; seqLen=3; break;
        case VK_HOME:  seq[0]=0x1b; seq[1]='['; seq[2]='H'; seqLen=3; break;
        case VK_END:   seq[0]=0x1b; seq[1]='['; seq[2]='F'; seqLen=3; break;
        case VK_DELETE:seq[0]=0x1b; seq[1]='['; seq[2]='3'; seq[3]='~'; seqLen=4; break;
        case VK_PRIOR: term_scroll_up  (tab->terminal, tab->terminal->rows/2); InvalidateRect(hwnd,NULL,FALSE); return 0;
        case VK_NEXT:  term_scroll_down(tab->terminal, tab->terminal->rows/2); InvalidateRect(hwnd,NULL,FALSE); return 0;
        }
        if (seqLen > 0) {
            if      (tab->type == TAB_TYPE_SSH    && tab->conn.ssh)    ssh_write   (tab->conn.ssh,    seq, seqLen);
            else if (tab->type == TAB_TYPE_SERIAL && tab->conn.serial) serial_write(tab->conn.serial, seq, seqLen);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);

        if (app->modal.type != MODAL_NONE) { modal_handle_click(app, x, y); return 0; }

        if (y < TITLEBAR_HEIGHT) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            if (x >= rc2.right - CAPTION_BTN_W) { PostMessage(hwnd, WM_CLOSE, 0, 0); return 0; }
            if (x >= rc2.right - CAPTION_BTN_W * 2) {
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); return 0;
            }
            if (x >= rc2.right - CAPTION_BTN_W * 3) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        if (y >= TITLEBAR_HEIGHT && y < get_tabbar_bottom()) {
            int adjY = y - TITLEBAR_HEIGHT;
            int closeIdx = tabs_hit_test_close(&app->tabs, x, adjY);
            if (closeIdx >= 0) {
                tabs_remove(&app->tabs, closeIdx);
            } else if (tabs_hit_test_plus(&app->tabs, x, adjY)) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, IDM_FILE_NEWSSH,    "  SSH");
                AppendMenuA(hMenu, MF_STRING, IDM_FILE_NEWSERIAL, "  Serial (COM)");
                POINT pt = { x, y };
                ClientToScreen(hwnd, &pt);
                TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            } else {
                int idx = tabs_hit_test(&app->tabs, x, adjY);
                if (idx >= 0) tabs_set_active(&app->tabs, idx);
            }
            update_terminal_sizes(app);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        if (!tabs_get_active(&app->tabs) && y >= get_term_top()) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            RECT sshBtn, serialBtn;
            get_empty_btn_rects(rc2.right, get_term_top(), get_term_bottom(app), &sshBtn, &serialBtn);
            POINT pt2 = { x, y };
            if (PtInRect(&sshBtn,    pt2)) { open_modal_ssh(app);    return 0; }
            if (PtInRect(&serialBtn, pt2)) { open_modal_serial(app); return 0; }
            return 0;
        }

        Tab *tab = tabs_get_active(&app->tabs);
        if (tab && tab->terminal && y >= get_term_top()) {
            term_start_selection(tab->terminal, x - TERM_PADDING, y - get_term_top() - TERM_PADDING);
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = LOWORD(lParam), my = HIWORD(lParam);

        /* Title bar hover */
        if (my < TITLEBAR_HEIGHT) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            int newHover = -1;
            if      (mx >= rc2.right - CAPTION_BTN_W)     newHover = 2;
            else if (mx >= rc2.right - CAPTION_BTN_W * 2) newHover = 1;
            else if (mx >= rc2.right - CAPTION_BTN_W * 3) newHover = 0;
            if (newHover != app->titleHoverBtn) {
                app->titleHoverBtn = newHover;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        } else if (app->titleHoverBtn != -1) {
            app->titleHoverBtn = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }

        /* Tab bar hover */
        if (my >= TITLEBAR_HEIGHT && my < get_tabbar_bottom()) {
            int adjY = my - TITLEBAR_HEIGHT;
            int newTab   = tabs_hit_test(&app->tabs, mx, adjY);
            int newClose = tabs_hit_test_close(&app->tabs, mx, adjY);
            BOOL newPlus = tabs_hit_test_plus(&app->tabs, mx, adjY);
            if (newTab != app->tabs.hoverTab || newClose != app->tabs.hoverClose
                || newPlus != app->tabs.hoverPlus) {
                app->tabs.hoverTab   = newTab;
                app->tabs.hoverClose = newClose;
                app->tabs.hoverPlus  = newPlus;
                /* No InvalidateRect — tabs_animate will pick it up */
            }
        } else if (app->tabs.hoverTab >= 0 || app->tabs.hoverPlus) {
            app->tabs.hoverTab = -1;
            app->tabs.hoverClose = -1;
            app->tabs.hoverPlus = FALSE;
        }

        /* Modal dropdown hover */
        if (app->modal.type == MODAL_SERIAL && app->modal.comDropdownOpen
            && app->modal.comPortCount > 0) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            int mw, mh, mxm, mym;
            modal_card_geometry(app, rc2.right, rc2.bottom, &mxm, &mym, &mw, &mh, 440, 400);
            int comFX = mxm + 24, comFY = mym + 104 + 18 + 34 + 8;
            int comFW = mw - 48, itemH = 32;
            int maxVis = app->modal.comPortCount < 8 ? app->modal.comPortCount : 8;
            int newHover = -1;
            if (mx >= comFX && mx < comFX + comFW && my >= comFY && my < comFY + maxVis * itemH)
                newHover = (my - comFY) / itemH;
            if (newHover != app->modal.comDropdownHover) {
                app->modal.comDropdownHover = newHover;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        /* Modal button hover */
        if (app->modal.type != MODAL_NONE) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            int outW = 440, outH = (app->modal.type == MODAL_SSH) ? 360 : 400;
            int mw, mh, mxm, mym;
            modal_card_geometry(app, rc2.right, rc2.bottom, &mxm, &mym, &mw, &mh, outW, outH);
            int btnY = mym + mh - 60;
            int primaryX = mxm + mw - 24 - 140;
            int cancelX  = primaryX - 96 - 8;
            int newHov = -1;
            if      (mx >= primaryX && mx <= primaryX + 140 && my >= btnY && my <= btnY + 38) newHov = 1;
            else if (mx >= cancelX  && mx <= cancelX  + 96  && my >= btnY && my <= btnY + 38) newHov = 0;
            if (newHov != app->modal.hoverButton) {
                app->modal.hoverButton = newHov;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }

        if (GetCapture() == hwnd) {
            Tab *tab = tabs_get_active(&app->tabs);
            if (tab && tab->terminal) {
                term_update_selection(tab->terminal, mx - TERM_PADDING, my - get_term_top() - TERM_PADDING);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }

        /* Empty-state buttons hover */
        if (!tabs_get_active(&app->tabs)) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            RECT sshBtn, serialBtn;
            get_empty_btn_rects(rc2.right, get_term_top(), get_term_bottom(app), &sshBtn, &serialBtn);
            POINT pt = { mx, my };
            int newHover = -1;
            if      (PtInRect(&sshBtn,    pt)) newHover = 0;
            else if (PtInRect(&serialBtn, pt)) newHover = 1;
            if (newHover != app->emptyHoverBtn) {
                app->emptyHoverBtn = newHover;
                /* tabs_animate timer handles repaint */
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (app->emptyHoverBtn != -1 || app->titleHoverBtn != -1
            || app->tabs.hoverTab != -1 || app->tabs.hoverPlus) {
            app->emptyHoverBtn  = -1;
            app->titleHoverBtn  = -1;
            app->tabs.hoverTab  = -1;
            app->tabs.hoverClose = -1;
            app->tabs.hoverPlus = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT cpt; GetCursorPos(&cpt); ScreenToClient(hwnd, &cpt);
            if (cpt.y < TITLEBAR_HEIGHT) {
                RECT rc2; GetClientRect(hwnd, &rc2);
                if (cpt.x >= rc2.right - CAPTION_BTN_W * 3) { SetCursor(LoadCursor(NULL, IDC_ARROW)); return TRUE; }
            }
            if (app->modal.type == MODAL_NONE && cpt.y >= TITLEBAR_HEIGHT && cpt.y < get_tabbar_bottom()) {
                int adjY = cpt.y - TITLEBAR_HEIGHT;
                if (tabs_hit_test_close(&app->tabs, cpt.x, adjY) >= 0
                    || tabs_hit_test_plus(&app->tabs, cpt.x, adjY)) {
                    SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE;
                }
            }
            if (app->modal.type == MODAL_NONE && !tabs_get_active(&app->tabs) && cpt.y >= get_term_top()) {
                RECT rc2; GetClientRect(hwnd, &rc2);
                RECT sshBtn, serialBtn;
                get_empty_btn_rects(rc2.right, get_term_top(), get_term_bottom(app), &sshBtn, &serialBtn);
                if (PtInRect(&sshBtn, cpt) || PtInRect(&serialBtn, cpt)) {
                    SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE;
                }
            }
            if (tabs_get_active(&app->tabs) && cpt.y >= get_term_top() && cpt.y < get_term_bottom(app)) {
                SetCursor(LoadCursor(NULL, IDC_IBEAM)); return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW)); return TRUE;
        }
        break;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            Tab *tab = tabs_get_active(&app->tabs);
            if (tab && tab->terminal) term_end_selection(tab->terminal);
        }
        return 0;

    case WM_LBUTTONDBLCLK: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        if (y < TITLEBAR_HEIGHT) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            if (x < rc2.right - CAPTION_BTN_W * 3)
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        Tab *tab = tabs_get_active(&app->tabs);
        if (tab && tab->terminal) {
            if (delta > 0) term_scroll_up  (tab->terminal, 3);
            else           term_scroll_down(tab->terminal, 3);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SSH_DATA:
    case WM_SERIAL_DATA: {
        DataMsg *dm = (DataMsg *)lParam;
        if (dm->tabIndex < app->tabs.count) {
            Tab *tab = &app->tabs.tabs[dm->tabIndex];
            if (tab->terminal) {
                tab->terminal->scrollTop = tab->terminal->totalLines;
                term_process(tab->terminal, dm->data, dm->len);
                if (dm->tabIndex == app->tabs.activeTab)
                    InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        free(dm);
        return 0;
    }

    case WM_SSH_CLOSED:
    case WM_SERIAL_CLOSED: {
        int tabIdx = (int)wParam;
        if (tabIdx < app->tabs.count) {
            Tab *tab = &app->tabs.tabs[tabIdx];
            if (tab->terminal) {
                const char *msg = "\r\n\033[31m[Connexion ferm\xe9" "e]\033[0m\r\n";
                term_process(tab->terminal, msg, (int)strlen(msg));
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_FILE_NEWSSH:    open_modal_ssh(app);    break;
        case IDM_FILE_NEWSERIAL: open_modal_serial(app); break;
        case IDM_FILE_DISCONNECT:
            if (app->tabs.activeTab >= 0) { tabs_remove(&app->tabs, app->tabs.activeTab); InvalidateRect(hwnd, NULL, TRUE); }
            break;
        case IDM_FILE_EXIT: PostQuitMessage(0); break;
        case IDM_EDIT_COPY:  copy_selection(app); break;
        case IDM_EDIT_PASTE: paste_clipboard(app); break;
        case IDM_EDIT_CLEAR: { Tab *t = tabs_get_active(&app->tabs); if (t && t->terminal) { term_clear(t->terminal); InvalidateRect(hwnd,NULL,FALSE); } break; }
        case IDM_VIEW_FULLSCREEN: toggle_fullscreen(app); break;
        case IDM_HELP_ABOUT:
            MessageBoxA(hwnd,
                "NetFlow 2.0\n\nClient terminal SSH & Serial natif.\n"
                "Construit en C avec Win32 API.\n\n"
                "libssh2 | Win32 Serial | VT100",
                "\xc0 propos de NetFlow", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;

    case WM_NCCALCSIZE:
        if (wParam) {
            if (IsZoomed(hwnd)) {
                NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lParam;
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
                p->rgrc[0] = mi.rcWork;
            }
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        int border = 6;
        if (pt.y < border && pt.x < border)                          return HTTOPLEFT;
        if (pt.y < border && pt.x > rc.right - border)               return HTTOPRIGHT;
        if (pt.y > rc.bottom - border && pt.x < border)              return HTBOTTOMLEFT;
        if (pt.y > rc.bottom - border && pt.x > rc.right - border)   return HTBOTTOMRIGHT;
        if (pt.y < border)                                           return HTTOP;
        if (pt.y > rc.bottom - border)                               return HTBOTTOM;
        if (pt.x < border)                                           return HTLEFT;
        if (pt.x > rc.right - border)                                return HTRIGHT;
        if (pt.y < TITLEBAR_HEIGHT) {
            if (pt.x >= rc.right - CAPTION_BTN_W * 3) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_CURSOR_BLINK);
        KillTimer(hwnd, TIMER_ANIMATE);
        for (int i = app->tabs.count - 1; i >= 0; i--) tabs_remove(&app->tabs, i);
        cleanup_fonts();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
   Window creation
   ============================================================ */
AppWindow *window_create(HINSTANCE hInst) {
    AppWindow *app = calloc(1, sizeof(AppWindow));
    g_app = app;
    app->fontSize = 15;
    app->emptyHoverBtn = -1;
    app->titleHoverBtn = -1;
    app->tabs.hoverTab = -1;
    app->tabs.hoverClose = -1;
    app->tabs.hoverPlus = FALSE;

    session_store_init(&app->sessions);
    session_store_load(&app->sessions);

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "NetFlowMainWindow";
    RegisterClassExA(&wc);

    app->hwnd = CreateWindowExA(0, "NetFlowMainWindow", "NetFlow",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 640,
        NULL, NULL, hInst, NULL);

    tabs_init(&app->tabs, app->hwnd);
    return app;
}

void window_run(AppWindow *app) {
    ShowWindow(app->hwnd, SW_SHOW);
    UpdateWindow(app->hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    session_store_save(&app->sessions);
    free(app);
    g_app = NULL;
}
