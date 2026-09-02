// GDI+ anti-alias drawing wrapper (called from pure C)
// Compiled as C++ (-fno-exceptions -fno-rtti); exposes C interface (extern "C").
#include <windows.h>
#include <gdiplus.h>
#include <stdlib.h>

using namespace Gdiplus;

// Pure-C build has no C++ runtime; provide minimal operator new/delete for GDI+ header inline code
void *operator new(size_t n) { return malloc(n); }
void operator delete(void *p) { free(p); }
void *operator new[](size_t n) { return malloc(n); }
void operator delete[](void *p) { free(p); }

static ULONG_PTR g_gdiplus_token;

extern "C" void gdiplus_init(void) {
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplus_token, &input, NULL);
}

extern "C" void gdiplus_shutdown(void) {
    GdiplusShutdown(g_gdiplus_token);
}

static void build_round_rect(GraphicsPath &path, REAL x, REAL y, REAL w, REAL h, REAL r) {
    REAL d = r * 2;
    path.StartFigure();
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

extern "C" void ui_fill_round_rect(HDC hdc, RECT r, int radius,
                                   COLORREF top, COLORREF bottom,
                                   BOOL gradient, BOOL border, COLORREF border_col) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    GraphicsPath path;
    build_round_rect(path, (REAL)r.left, (REAL)r.top,
                     (REAL)(r.right - r.left), (REAL)(r.bottom - r.top), (REAL)radius);
    Color ctop((BYTE)255, GetRValue(top), GetGValue(top), GetBValue(top));
    Color cbot((BYTE)255, GetRValue(bottom), GetGValue(bottom), GetBValue(bottom));
    if (gradient) {
        LinearGradientBrush lb(PointF(0.0f, (REAL)r.top), PointF(0.0f, (REAL)r.bottom), ctop, cbot);
        g.FillPath(&lb, &path);
    } else {
        SolidBrush b(ctop);
        g.FillPath(&b, &path);
    }
    if (border) {
        Color cbd((BYTE)255, GetRValue(border_col), GetGValue(border_col), GetBValue(border_col));
        Pen p(cbd, 1.0f);
        g.DrawPath(&p, &path);
    }
}

extern "C" void ui_fill_ellipse(HDC hdc, RECT r, COLORREF color) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Color c((BYTE)255, GetRValue(color), GetGValue(color), GetBValue(color));
    SolidBrush b(c);
    g.FillEllipse(&b, (REAL)r.left, (REAL)r.top,
                  (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}