#ifndef UI_H
#define UI_H

#include <winsock2.h>
#include <windows.h>

/* ============================================================
   NetFlow — Design Tokens
   Palette: cool slate, single confident indigo accent.
   Inspired by Warp / Arc / Windows Terminal / VS Code.
   ============================================================ */

/* --- Surfaces (low → high) ------------------------------------ */
#define C_BG_DEEP        RGB( 13,  15,  20)   /* #0d0f14  titlebar */
#define C_BG_PRIMARY     RGB( 20,  23,  30)   /* #14171e  terminal canvas */
#define C_BG_SECONDARY   RGB( 26,  30,  38)   /* #1a1e26  modal body */
#define C_BG_SURFACE     RGB( 32,  36,  45)   /* #20242d  inputs, cards */
#define C_BG_ELEVATED    RGB( 42,  47,  58)   /* #2a2f3a  focused field */
#define C_BG_HOVER       RGB( 47,  52,  65)   /* #2f3441  hover state */
#define C_BG_ACTIVE      RGB( 53,  59,  74)   /* #353b4a  pressed state */

/* Tab + status surfaces: keep them flush with title for calm */
#define C_TITLEBAR       C_BG_DEEP
#define C_TABBAR         RGB( 17,  20,  26)   /* #11141a  one notch above deep */
#define C_STATUSBAR      C_BG_DEEP

/* --- Borders -------------------------------------------------- */
#define C_BORDER_SUBTLE  RGB( 31,  36,  45)   /* #1f242d  hairlines */
#define C_BORDER         RGB( 44,  50,  62)   /* #2c323e  field borders */
#define C_BORDER_LIGHT   RGB( 60,  68,  83)   /* #3c4453  emphasized */

/* --- Text (high → low contrast) ------------------------------- */
#define C_TEXT_PRIMARY   RGB(230, 233, 240)   /* #e6e9f0 */
#define C_TEXT_SECONDARY RGB(164, 173, 186)   /* #a4adba */
#define C_TEXT_MUTED     RGB(107, 114, 128)   /* #6b7280 */
#define C_TEXT_DIM       RGB( 66,  73,  85)   /* #424955 */

/* --- Accent (single, calm indigo) ----------------------------- */
#define C_ACCENT         RGB(124, 140, 255)   /* #7c8cff */
#define C_ACCENT_HOVER   RGB(149, 163, 255)   /* #95a3ff */
#define C_ACCENT_DIM     RGB( 74,  84, 138)   /* #4a548a  selection bg */
#define C_ACCENT_GLOW    RGB( 56,  64, 110)   /* #38406e  bottom of titlebar grad */

/* --- Semantic ------------------------------------------------- */
#define C_SUCCESS        RGB( 52, 211, 153)   /* #34d399  connected, COM badge */
#define C_SUCCESS_DIM    RGB( 31, 130, 100)
#define C_ERROR          RGB(240, 108, 128)   /* #f06c80 */
#define C_ERROR_DIM      RGB(168,  67,  84)
#define C_WARNING        RGB(251, 191, 107)   /* #fbbf6b */

/* ============================================================
   Dimensions
   ============================================================ */
#define TITLEBAR_HEIGHT     36
#define TABBAR_HEIGHT       38
#define STATUSBAR_HEIGHT    26
#define SIDEBAR_WIDTH       240
#define TAB_MAX_WIDTH       220
#define TAB_MIN_WIDTH       120
#define TAB_PADDING_H       12
#define TAB_CLOSE_SIZE      18
#define UI_PADDING          12
#define UI_RADIUS           6      /* fields, tabs */
#define UI_RADIUS_BTN       8      /* buttons */
#define UI_RADIUS_CARD     12      /* modal cards */
#define CAPTION_BTN_W       46
#define TERM_PADDING         8

/* ============================================================
   Icon Glyphs (Segoe MDL2 Assets)
   ============================================================ */
#define ICON_MINIMIZE   L"\uE921"
#define ICON_MAXIMIZE   L"\uE922"
#define ICON_RESTORE    L"\uE923"
#define ICON_CLOSE      L"\uE8BB"
#define ICON_ADD        L"\uE710"
#define ICON_CANCEL     L"\uE8BB"
#define ICON_CHEVRON_D  L"\uE70D"
#define ICON_CHEVRON_U  L"\uE70E"
#define ICON_PLUG       L"\uE839"
#define ICON_SERIAL     L"\uE950"

/* ============================================================
   Drawing primitives
   ============================================================ */
void ui_fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF color);
void ui_rounded_rect(HDC hdc, int x, int y, int w, int h, int r,
                     COLORREF fill, COLORREF border);
void ui_draw_text(HDC hdc, const char *text, int x, int y, int w, int h,
                  COLORREF color, HFONT font, UINT flags);
void ui_draw_text_centered(HDC hdc, const char *text, RECT *rc,
                           COLORREF color, HFONT font);
void ui_gradient_v(HDC hdc, int x, int y, int w, int h,
                   COLORREF top, COLORREF bottom);
void ui_draw_icon(HDC hdc, const WCHAR *glyph, int x, int y, int w, int h,
                  COLORREF color, HFONT iconFont);
void ui_draw_icon_x(HDC hdc, int cx, int cy, int size, COLORREF color);
void ui_draw_separator_h(HDC hdc, int x, int y, int w, COLORREF color);
HFONT ui_create_font(const char *name, int size, BOOL bold);

/* ============================================================
   Color / animation utilities
   ============================================================ */
COLORREF ui_lerp_color(COLORREF a, COLORREF b, float t);
float    ui_ease_out_cubic(float t);
float    ui_ease_in_out_cubic(float t);
float    ui_ease_out_back(float t);
float    ui_clamp01(float v);

/* A self-contained animated float between [0..1].
   Drive it from a single 60Hz WM_TIMER. */
typedef struct {
    float value;       /* current visible value 0..1 */
    float target;      /* 0 or 1 */
    float speed;       /* per-second rate at which value chases target */
} UiAnim;

void  ui_anim_init  (UiAnim *a, float initial, float speed);
void  ui_anim_set   (UiAnim *a, float target);
BOOL  ui_anim_tick  (UiAnim *a, float dt);   /* returns TRUE if value changed */

/* ============================================================
   Sidebar (kept for compat — unused in v2 layout)
   ============================================================ */
#define SIDEBAR_MAX_ITEMS 64

typedef struct {
    BOOL visible;
    int  width;
    int  scrollY;
    int  hoverItem;
    int  hoverButton;
} SidebarState;

/* ============================================================
   Modal / overlay state
   ============================================================ */
typedef enum {
    MODAL_NONE,
    MODAL_SSH,
    MODAL_SERIAL,
    MODAL_ABOUT,
} ModalType;

typedef struct {
    ModalType type;
    char  fields[8][256];
    int   focusedField;
    int   comboSelection[4];
    char  error[256];
    BOOL  connecting;
    int   hoverButton;          /* 0 = cancel, 1 = primary */
    char  comPorts[64][32];
    int   comPortCount;
    BOOL  comDropdownOpen;
    int   comDropdownHover;
    UiAnim appear;              /* 0 → 1 fade + scale on open */
} ModalState;

#endif
