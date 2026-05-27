#include "tabs.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>

#define PLUS_BTN_WIDTH 32
#define PLUS_BTN_GAP    6
#define TAB_INSET_Y     6
#define TAB_GAP         2

void tabs_init(TabManager *mgr, HWND parent) {
    memset(mgr, 0, sizeof(TabManager));
    mgr->parentHwnd  = parent;
    mgr->tabBarHeight = TABBAR_HEIGHT;
    mgr->activeTab   = -1;
    mgr->hoverTab    = -1;
    mgr->hoverClose  = -1;
    mgr->hoverPlus   = FALSE;
}

int tabs_add(TabManager *mgr, TabType type, const char *title) {
    if (mgr->count >= MAX_TABS) return -1;
    int idx = mgr->count;
    Tab *tab = &mgr->tabs[idx];
    memset(tab, 0, sizeof(Tab));
    tab->active = TRUE;
    tab->type   = type;
    strncpy(tab->title, title, sizeof(tab->title) - 1);
    tab->terminal = term_create(TERM_DEFAULT_COLS, TERM_DEFAULT_ROWS);
    ui_anim_init(&tab->hover,     0.f, 16.f);
    ui_anim_init(&tab->underline, 0.f, 18.f);
    mgr->count++;
    mgr->activeTab = idx;
    return idx;
}

void tabs_remove(TabManager *mgr, int index) {
    if (index < 0 || index >= mgr->count) return;
    Tab *tab = &mgr->tabs[index];
    if (tab->terminal) { term_destroy(tab->terminal); tab->terminal = NULL; }
    if (tab->type == TAB_TYPE_SSH    && tab->conn.ssh)    { ssh_disconnect(tab->conn.ssh);       tab->conn.ssh = NULL; }
    else if (tab->type == TAB_TYPE_SERIAL && tab->conn.serial) { serial_disconnect(tab->conn.serial); tab->conn.serial = NULL; }
    for (int i = index; i < mgr->count - 1; i++) mgr->tabs[i] = mgr->tabs[i + 1];
    mgr->count--;
    if      (mgr->count == 0)              mgr->activeTab = -1;
    else if (mgr->activeTab >= mgr->count) mgr->activeTab = mgr->count - 1;
    else if (mgr->activeTab >  index)      mgr->activeTab--;
}

void tabs_set_active(TabManager *mgr, int index) {
    if (index >= 0 && index < mgr->count) mgr->activeTab = index;
}

Tab *tabs_get_active(TabManager *mgr) {
    if (mgr->activeTab >= 0 && mgr->activeTab < mgr->count)
        return &mgr->tabs[mgr->activeTab];
    return NULL;
}

static int calc_tab_width(TabManager *mgr, int totalWidth) {
    if (mgr->count <= 0) return TAB_MAX_WIDTH;
    int availW = totalWidth - 8 - PLUS_BTN_WIDTH - PLUS_BTN_GAP;
    int tabW = availW / mgr->count - TAB_GAP;
    if (tabW > TAB_MAX_WIDTH) tabW = TAB_MAX_WIDTH;
    if (tabW < TAB_MIN_WIDTH) tabW = TAB_MIN_WIDTH;
    return tabW;
}

/* ------------------------------------------------------------
   Paint
   - Tab bar bg flush with titlebar (calm).
   - Active tab: subtle elevated surface, accent underline 24px wide,
     centered on the tab. Underline uses tab->underline anim 0..1.
   - Inactive tab: just a label, separators between tabs are 1px
     hairlines at row mid-height (vscode-style).
   ------------------------------------------------------------ */
void tabs_paint_bar(TabManager *mgr, HDC hdc, RECT *rect) {
    int barW  = rect->right - rect->left;
    int baseY = rect->top;
    int h     = rect->bottom - baseY;

    ui_fill_rect(hdc, rect->left, baseY, barW, h, C_TABBAR);

    HFONT labelFont   = ui_create_font("Segoe UI", 12, FALSE);
    HFONT labelFontA  = ui_create_font("Segoe UI", 12, TRUE);
    HFONT typeFont    = ui_create_font("Segoe UI",  9, TRUE);
    HFONT iconFont    = ui_create_font("Segoe MDL2 Assets",  8, FALSE);
    HFONT iconFontPls = ui_create_font("Segoe MDL2 Assets", 10, FALSE);

    int tabW = calc_tab_width(mgr, barW);

    for (int i = 0; i < mgr->count; i++) {
        Tab *tab    = &mgr->tabs[i];
        BOOL active = (i == mgr->activeTab);
        BOOL hovered= (i == mgr->hoverTab);
        int  x = 4 + i * (tabW + TAB_GAP);
        int  y = baseY + TAB_INSET_Y;
        int  th = TABBAR_HEIGHT - TAB_INSET_Y - 2;

        /* Background fill — animated via tab->hover.value (0..1).
           Active sits at a deeper level than hover. */
        COLORREF bg;
        if (active) {
            bg = C_BG_PRIMARY;   /* same as terminal — visually attaches to content */
        } else {
            float t = tab->hover.value;
            bg = ui_lerp_color(C_TABBAR, C_BG_SURFACE, t * 0.7f);
        }
        ui_rounded_rect(hdc, x, y, tabW, th, UI_RADIUS, bg, bg);

        /* Hairline separator between inactive neighbours (skip near active) */
        if (!active && i > 0 && (i - 1) != mgr->activeTab) {
            ui_fill_rect(hdc, x - 1, y + 8, 1, th - 16, C_BORDER_SUBTLE);
        }

        /* Badge: SSH / COM — coloured chip on the left */
        const char *typeStr = (tab->type == TAB_TYPE_SSH) ? "SSH" : "COM";
        BOOL isCom = (tab->type == TAB_TYPE_SERIAL);
        COLORREF typeBg  = isCom ? C_SUCCESS : C_ACCENT;
        COLORREF typeFgActive = isCom ? RGB(8, 32, 26) : RGB(255, 255, 255);
        /* Pill badge */
        int badgeW = 34, badgeH = 16;
        int badgeX = x + 10, badgeY = y + (th - badgeH) / 2;
        ui_rounded_rect(hdc, badgeX, badgeY, badgeW, badgeH, 8,
                        active ? typeBg : C_BG_ELEVATED,
                        active ? typeBg : C_BG_ELEVATED);
        ui_draw_text(hdc, typeStr, badgeX, badgeY, badgeW, badgeH,
                     active ? typeFgActive : typeBg,
                     typeFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        /* Title */
        COLORREF textColor = active   ? C_TEXT_PRIMARY :
                             hovered  ? C_TEXT_SECONDARY :
                                        C_TEXT_MUTED;
        ui_draw_text(hdc, tab->title,
                     badgeX + badgeW + 8, y,
                     tabW - (badgeX + badgeW + 8 - x) - 26, th,
                     textColor,
                     active ? labelFontA : labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        /* Close button — only visible if hover or active (Chrome behaviour) */
        BOOL showClose = active || hovered;
        if (showClose) {
            BOOL hovClose = (mgr->hoverClose == i);
            int cbW = 18, cbH = 18;
            int cbX = x + tabW - cbW - 6;
            int cbY = y + (th - cbH) / 2;
            if (hovClose) {
                ui_rounded_rect(hdc, cbX, cbY, cbW, cbH, 4,
                                C_BG_HOVER, C_BG_HOVER);
            }
            ui_draw_icon_x(hdc, cbX + cbW/2, cbY + cbH/2, 7,
                           hovClose ? C_TEXT_PRIMARY :
                           active   ? C_TEXT_SECONDARY : C_TEXT_MUTED);
        }

        /* Animated underline accent for the active tab */
        if (active || tab->underline.value > 0.01f) {
            float u = active ? tab->underline.value : tab->underline.value;
            int ulW = (int)(24 * u);
            if (ulW > 0) {
                int ulX = x + (tabW - ulW) / 2;
                ui_fill_rect(hdc, ulX, y + th - 2, ulW, 2,
                             tab->type == TAB_TYPE_SSH ? C_ACCENT : C_SUCCESS);
            }
        }
    }

    /* "+" button */
    {
        int plusX = 4 + mgr->count * (tabW + TAB_GAP) + PLUS_BTN_GAP;
        int plusY = baseY + TAB_INSET_Y;
        int th    = TABBAR_HEIGHT - TAB_INSET_Y - 2;

        COLORREF bg = mgr->hoverPlus ? C_BG_HOVER : C_TABBAR;
        ui_rounded_rect(hdc, plusX, plusY, PLUS_BTN_WIDTH, th, UI_RADIUS, bg, bg);
        /* Draw "+" as crossed lines for crisp rendering at small size */
        int cx = plusX + PLUS_BTN_WIDTH/2, cy = plusY + th/2;
        HPEN pen = CreatePen(PS_SOLID, 1,
                             mgr->hoverPlus ? C_TEXT_PRIMARY : C_TEXT_SECONDARY);
        HPEN op  = SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 5, cy, NULL); LineTo(hdc, cx + 6, cy);
        MoveToEx(hdc, cx, cy - 5, NULL); LineTo(hdc, cx, cy + 6);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }

    /* Bottom border that DOESN'T cross the active tab — gives the
       "attached document" feel (Chrome/VSCode). */
    int sepY = rect->bottom - 1;
    int activeX0 = -1, activeX1 = -1;
    if (mgr->activeTab >= 0) {
        activeX0 = 4 + mgr->activeTab * (tabW + TAB_GAP);
        activeX1 = activeX0 + tabW;
    }
    if (activeX0 < 0) {
        ui_fill_rect(hdc, rect->left, sepY, barW, 1, C_BORDER_SUBTLE);
    } else {
        ui_fill_rect(hdc, rect->left, sepY, activeX0 - rect->left, 1, C_BORDER_SUBTLE);
        ui_fill_rect(hdc, activeX1,  sepY, rect->right - activeX1, 1, C_BORDER_SUBTLE);
    }

    DeleteObject(labelFont);
    DeleteObject(labelFontA);
    DeleteObject(typeFont);
    DeleteObject(iconFont);
    DeleteObject(iconFontPls);
}

static int get_tab_width(TabManager *mgr) {
    RECT rc; GetClientRect(mgr->parentHwnd, &rc);
    return calc_tab_width(mgr, rc.right);
}

int tabs_hit_test(TabManager *mgr, int x, int y) {
    if (y < TAB_INSET_Y || y > TABBAR_HEIGHT - 2) return -1;
    int tabW = get_tab_width(mgr);
    for (int i = 0; i < mgr->count; i++) {
        int tx = 4 + i * (tabW + TAB_GAP);
        if (x >= tx && x < tx + tabW) return i;
    }
    return -1;
}

int tabs_hit_test_close(TabManager *mgr, int x, int y) {
    if (y < TAB_INSET_Y || y > TABBAR_HEIGHT - 2) return -1;
    int tabW = get_tab_width(mgr);
    for (int i = 0; i < mgr->count; i++) {
        int tx   = 4 + i * (tabW + TAB_GAP);
        int cbX  = tx + tabW - 18 - 6;
        if (x >= cbX - 2 && x <= cbX + 20 &&
            y >= TAB_INSET_Y && y < TABBAR_HEIGHT - 2)
            return i;
    }
    return -1;
}

BOOL tabs_hit_test_plus(TabManager *mgr, int x, int y) {
    if (y < TAB_INSET_Y || y > TABBAR_HEIGHT - 2) return FALSE;
    int tabW = get_tab_width(mgr);
    int plusX = 4 + mgr->count * (tabW + TAB_GAP) + PLUS_BTN_GAP;
    return (x >= plusX && x < plusX + PLUS_BTN_WIDTH);
}

/* ------------------------------------------------------------
   Animation tick — called from main timer.
   Drives:
   - tab->hover     : 0..1 chase toward hover state
   - tab->underline : 0 → 1 when becoming active (back-easing handled in paint)
   ------------------------------------------------------------ */
BOOL tabs_animate(TabManager *mgr, float dt) {
    BOOL dirty = FALSE;
    for (int i = 0; i < mgr->count; i++) {
        Tab *t = &mgr->tabs[i];
        ui_anim_set(&t->hover,     (i == mgr->hoverTab) ? 1.f : 0.f);
        ui_anim_set(&t->underline, (i == mgr->activeTab) ? 1.f : 0.f);
        dirty |= ui_anim_tick(&t->hover,     dt);
        dirty |= ui_anim_tick(&t->underline, dt);
    }
    return dirty;
}
