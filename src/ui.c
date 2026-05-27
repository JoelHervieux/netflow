#include "ui.h"
#include <math.h>
#include <stdlib.h>

/* ============================================================
   Drawing primitives
   ============================================================ */

void ui_fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF color) {
    RECT rc = { x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(color);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

void ui_rounded_rect(HDC hdc, int x, int y, int w, int h, int r,
                     COLORREF fill, COLORREF border)
{
    HBRUSH br  = CreateSolidBrush(fill);
    HPEN   pen = (border != fill) ? CreatePen(PS_SOLID, 1, border)
                                  : CreatePen(PS_NULL, 0, 0);
    HBRUSH oldBr  = SelectObject(hdc, br);
    HPEN   oldPen = SelectObject(hdc, pen);
    if (r <= 0) Rectangle(hdc, x, y, x + w, y + h);
    else        RoundRect (hdc, x, y, x + w, y + h, r * 2, r * 2);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

void ui_draw_text(HDC hdc, const char *text, int x, int y, int w, int h,
                  COLORREF color, HFONT font, UINT flags)
{
    RECT rc = { x, y, x + w, y + h };
    HFONT old = font ? SelectObject(hdc, font) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextA(hdc, text, -1, &rc, flags | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

void ui_draw_text_centered(HDC hdc, const char *text, RECT *rc,
                           COLORREF color, HFONT font)
{
    HFONT old = font ? SelectObject(hdc, font) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextA(hdc, text, -1, rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

void ui_gradient_v(HDC hdc, int x, int y, int w, int h,
                   COLORREF top, COLORREF bottom)
{
    /* GDI has no native linear gradient on rectangles outside
       GradientFill, which is fine but adds dep — keep the band-fill
       approach but limit overhead by stepping when h > 16. */
    int step = (h > 64) ? 2 : 1;
    for (int i = 0; i < h; i += step) {
        int r = GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * i / h;
        int g = GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * i / h;
        int b = GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * i / h;
        ui_fill_rect(hdc, x, y + i, w, step, RGB(r, g, b));
    }
}

HFONT ui_create_font(const char *name, int size, BOOL bold) {
    return CreateFontA(-size, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, name);
}

void ui_draw_icon_x(HDC hdc, int cx, int cy, int size, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN old = SelectObject(hdc, pen);
    int s = size / 2;
    MoveToEx(hdc, cx - s,     cy - s,     NULL); LineTo(hdc, cx + s + 1, cy + s + 1);
    MoveToEx(hdc, cx + s,     cy - s,     NULL); LineTo(hdc, cx - s - 1, cy + s + 1);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void ui_draw_icon(HDC hdc, const WCHAR *glyph, int x, int y, int w, int h,
                  COLORREF color, HFONT iconFont)
{
    RECT rc = { x, y, x + w, y + h };
    HFONT old = iconFont ? SelectObject(hdc, iconFont) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, glyph, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

void ui_draw_separator_h(HDC hdc, int x, int y, int w, COLORREF color) {
    ui_fill_rect(hdc, x, y, w, 1, color);
}

/* ============================================================
   Color & easing math
   ============================================================ */

float ui_clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

COLORREF ui_lerp_color(COLORREF a, COLORREF b, float t) {
    t = ui_clamp01(t);
    int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
    int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return RGB(r, g, bl);
}

float ui_ease_out_cubic(float t) {
    t = ui_clamp01(t);
    float f = 1.f - t;
    return 1.f - f * f * f;
}

float ui_ease_in_out_cubic(float t) {
    t = ui_clamp01(t);
    if (t < 0.5f) return 4.f * t * t * t;
    float f = (-2.f * t) + 2.f;
    return 1.f - (f * f * f) * 0.5f;
}

float ui_ease_out_back(float t) {
    t = ui_clamp01(t);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.f;
    float f = t - 1.f;
    return 1.f + c3 * f * f * f + c1 * f * f;
}

/* ============================================================
   UiAnim — 0/1 chase, frame-rate-independent
   ============================================================ */

void ui_anim_init(UiAnim *a, float initial, float speed) {
    a->value  = initial;
    a->target = initial;
    a->speed  = speed > 0 ? speed : 12.f;   /* default ~80ms to converge */
}

void ui_anim_set(UiAnim *a, float target) {
    a->target = target < 0 ? 0 : (target > 1 ? 1 : target);
}

BOOL ui_anim_tick(UiAnim *a, float dt) {
    float before = a->value;
    /* Exponential chase: value += (target - value) * (1 - exp(-speed*dt)).
       Gives smooth ease-out with no overshoot, zero state besides speed. */
    float k = 1.f - (float)exp(-a->speed * dt);
    a->value += (a->target - a->value) * k;
    if (fabsf(a->target - a->value) < 0.001f) a->value = a->target;
    return fabsf(a->value - before) > 0.0005f;
}
