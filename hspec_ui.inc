// ===========================================================================
// Shell UI v2
// ---------------------------------------------------------------------------
// 1024x760 的主窗口：52dp 标题栏 + 200dp 侧栏 + 可滚动内容区。
// 与 hw-ui/index.html 的几何对齐；数据全部来自 BuildModel / TargetInfo，
// 这里只负责画和命中测试。旧的 SettingsDraw 系列保留在 hspec.cpp 里作对照。
// ===========================================================================

static void SettingsInvalidate(void);
static void OpenSettings(void);
static void OpenSettingsPage(int page);
static void HideSettingsToPill(void);
static void ShowPill(void);
static void HidePill(void);
static void ApplyWindowVisibility(void);
static bool SavePanelPng(const wchar_t* path);
static void CopyVersionToClipboard(void);
static void Render(void);
static bool AutoStartEnabled(void);
static void SetAutoStart(bool on);

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

struct UiStyle
{
    bool dark;
    Gdiplus::Color page;        // 窗口底
    Gdiplus::Color card;        // 卡片
    Gdiplus::Color cardHover;
    Gdiplus::Color primary;     // 强调色
    Gdiplus::Color btnContent;  // 图标 / 次级强调文字
    Gdiplus::Color btnRegular;  // 控件底
    Gdiplus::Color btnHover;
    Gdiplus::Color deepText;    // 主文字
    Gdiplus::Color titleActive; // 选中项文字
    Gdiplus::Color t90, t75, t60, t50, t40;
    Gdiplus::Color divider;
    Gdiplus::Color line;
};

static Gdiplus::Color UiClr(const Gdiplus::Color& c, int a)
{
    return Gdiplus::Color((BYTE)ClampInt(a, 0, 255), c.GetR(), c.GetG(), c.GetB());
}

static UiStyle MakeUiStyle(bool dark)
{
    double h = (double)HueDeg();
    UiStyle u;
    u.dark = dark;
    if (dark) {
        u.primary = OklchColor(0.75, 0.14, h, 255);
        u.page = OklchColor(0.16, 0.014, h, 255);
        u.card = OklchColor(0.23, 0.015, h, 255);
        u.cardHover = OklchColor(0.30, 0.03, h, 255);
        u.btnContent = OklchColor(0.75, 0.10, h, 255);
        u.btnRegular = OklchColor(0.33, 0.035, h, 255);
        u.btnHover = OklchColor(0.38, 0.04, h, 255);
        u.deepText = OklchColor(0.85, 0.02, h, 255);
        u.titleActive = OklchColor(0.75, 0.10, h, 255);
        u.t90 = Gdiplus::Color(235, 255, 255, 255);
        u.t75 = Gdiplus::Color(194, 255, 255, 255);
        u.t60 = Gdiplus::Color(158, 255, 255, 255);
        u.t50 = Gdiplus::Color(128, 255, 255, 255);
        u.t40 = Gdiplus::Color(97, 255, 255, 255);
        u.divider = Gdiplus::Color(20, 255, 255, 255);
        u.line = Gdiplus::Color(28, 255, 255, 255);
    } else {
        u.primary = OklchColor(0.70, 0.14, h, 255);
        u.page = OklchColor(0.95, 0.01, h, 255);
        u.card = OklchColor(1.0, 0.0, h, 255);
        u.cardHover = OklchColor(0.98, 0.005, h, 255);
        u.btnContent = OklchColor(0.55, 0.12, h, 255);
        u.btnRegular = OklchColor(0.95, 0.025, h, 255);
        u.btnHover = OklchColor(0.90, 0.05, h, 255);
        u.deepText = OklchColor(0.25, 0.02, h, 255);
        u.titleActive = OklchColor(0.60, 0.10, h, 255);
        u.t90 = Gdiplus::Color(230, 0, 0, 0);
        u.t75 = Gdiplus::Color(191, 0, 0, 0);
        u.t60 = Gdiplus::Color(153, 0, 0, 0);
        u.t50 = Gdiplus::Color(128, 0, 0, 0);
        u.t40 = Gdiplus::Color(102, 0, 0, 0);
        u.divider = Gdiplus::Color(20, 0, 0, 0);
        u.line = Gdiplus::Color(26, 0, 0, 0);
    }
    return u;
}

// ---------------------------------------------------------------------------
// Icon set: 24-unit grid, 1.5 stroke, round caps. Same paths as the mockup.
// ---------------------------------------------------------------------------

enum UiIcon
{
    UI_CPU = 0, UI_BOARD, UI_RAM, UI_DISK, UI_GPU, UI_NET, UI_NPU, UI_VGA,
    UI_MONITOR, UI_BATTERY, UI_AUDIO, UI_PLUG, UI_REFRESH, UI_COPY, UI_DOC,
    UI_GEAR, UI_BACK, UI_RING, UI_WINDOW, UI_EYE_OFF, UI_MIN, UI_CLOSE,
    UI_CHEVRON, UI_CARET, UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT, UI_CHECK,
    UI_IMAGE, UI_CODE, UI_INFO, UI_CLOCK, UI_EXPORT, UI_COUNT_
};

static void UiRoundRectPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r)
{
    if (w <= 0 || h <= 0) return;
    if (r <= 0.01f) { p.AddRectangle(Gdiplus::RectF(x, y, w, h)); return; }
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    p.AddArc(x, y, d, d, 180.0f, 90.0f);
    p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

static void DrawUiIcon(Gdiplus::Graphics& g, int icon, float cx, float cy, float size,
    const Gdiplus::Color& col, float strokeGrid = 1.5f)
{
    if (size <= 1) return;
    const float k = size / 24.0f;
    const float ox = cx - size * 0.5f;
    const float oy = cy - size * 0.5f;
    auto X = [&](float v) { return ox + v * k; };
    auto Y = [&](float v) { return oy + v * k; };

    Gdiplus::Pen pen(col, strokeGrid * k);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    auto Line = [&](float x1, float y1, float x2, float y2) {
        g.DrawLine(&pen, X(x1), Y(y1), X(x2), Y(y2));
    };
    auto Rect = [&](float x, float y, float w, float h, float r) {
        Gdiplus::GraphicsPath p;
        UiRoundRectPath(p, X(x), Y(y), w * k, h * k, r * k);
        g.DrawPath(&pen, &p);
    };
    auto Ell = [&](float x, float y, float r) {
        g.DrawEllipse(&pen, X(x - r), Y(y - r), 2 * r * k, 2 * r * k);
    };
    auto Poly = [&](const float* pts, int n) {
        if (n < 2) return;
        Gdiplus::GraphicsPath p;
        Gdiplus::PointF prev(X(pts[0]), Y(pts[1]));
        p.StartFigure();
        for (int i = 1; i < n; i++) {
            Gdiplus::PointF cur(X(pts[i * 2]), Y(pts[i * 2 + 1]));
            p.AddLine(prev, cur);
            prev = cur;
        }
        g.DrawPath(&pen, &p);
    };

    switch (icon) {
    case UI_CPU:
        // mockup i-cpu：芯片方框 + 内核方框 + 每边三根引脚
        Rect(5, 5, 14, 14, 2);
        Rect(9.5f, 9.5f, 5, 5, 0.6f);
        Line(8, 2, 8, 5); Line(12, 2, 12, 5); Line(16, 2, 16, 5);
        Line(8, 19, 8, 22); Line(12, 19, 12, 22); Line(16, 19, 16, 22);
        Line(2, 8, 5, 8); Line(2, 12, 5, 12); Line(2, 16, 5, 16);
        Line(19, 8, 22, 8); Line(19, 12, 22, 12); Line(19, 16, 22, 16);
        break;
    case UI_BOARD:
        Rect(2.5f, 4, 19, 16, 2);
        Rect(6, 8, 6, 5, 0.8f);
        Line(15, 8, 15, 16); Line(18, 8, 18, 16);
        Line(6, 16.4f, 12, 16.4f);
        break;    case UI_RAM:
        // mockup i-ram：内存条 + 两条缺口 + 五根引脚
        Rect(2, 7, 20, 10, 1.5f);
        Line(8.5f, 12, 8.5f, 17); Line(14.5f, 12, 14.5f, 17);
        Line(4, 17, 4, 19); Line(8, 17, 8, 19); Line(12, 17, 12, 19);
        Line(16, 17, 16, 19); Line(20, 17, 20, 19);
        break;
    case UI_DISK:
        Rect(2, 5, 20, 14, 2);
        Ell(9, 12, 3);
        Line(15, 9.4f, 19, 9.4f); Line(15, 13, 19, 13);
        break;
    case UI_GPU:
        Rect(2, 4, 20, 13, 2);
        Line(12, 17, 12, 20); Line(8, 20, 16, 20);
        break;
    case UI_NET:
        Ell(12, 12, 9);
        Line(3, 12, 21, 12);
        g.DrawEllipse(&pen, X(8), Y(3), 8 * k, 18 * k);
        break;
    case UI_NPU:
        Ell(12, 12, 2.8f);
        Line(12, 9.2f, 12, 4.6f); Line(14.4f, 13.7f, 18.4f, 16.4f);
        Line(9.6f, 13.7f, 5.6f, 16.4f);
        Ell(12, 4.6f, 1.8f); Ell(18.4f, 16.4f, 1.8f); Ell(5.6f, 16.4f, 1.8f);
        break;
    case UI_VGA:
        Rect(2.2f, 5, 19.6f, 11, 2);
        Ell(7.8f, 10.5f, 2.5f);
        Line(12.6f, 8.6f, 18.8f, 8.6f); Line(12.6f, 12.4f, 18.8f, 12.4f);
        { float pts[] = { 8, 16, 8, 18.4f, 16, 18.4f, 16, 16 }; Poly(pts, 4); }
        break;
    case UI_MONITOR:
        Rect(2, 4, 20, 13, 2);
        Line(12, 17, 12, 20); Line(8, 20, 16, 20);
        break;
    case UI_BATTERY:
        Rect(2.4f, 7.2f, 16, 9.6f, 2.2f);
        Line(20.6f, 10.6f, 20.6f, 13.4f);
        Line(5.6f, 10.4f, 5.6f, 13.6f); Line(9.2f, 10.4f, 9.2f, 13.6f);
        break;
    case UI_AUDIO: {
        // 喇叭 + 两道声波，和面板上的声卡图标同一套形状。
        Gdiplus::GraphicsPath p;
        p.StartFigure();
        p.AddLine(Gdiplus::PointF(X(4.0f), Y(9.4f)), Gdiplus::PointF(X(7.4f), Y(9.4f)));
        p.AddLine(Gdiplus::PointF(X(7.4f), Y(9.4f)), Gdiplus::PointF(X(12.2f), Y(5.2f)));
        p.AddLine(Gdiplus::PointF(X(12.2f), Y(5.2f)), Gdiplus::PointF(X(12.2f), Y(18.8f)));
        p.AddLine(Gdiplus::PointF(X(12.2f), Y(18.8f)), Gdiplus::PointF(X(7.4f), Y(14.6f)));
        p.AddLine(Gdiplus::PointF(X(7.4f), Y(14.6f)), Gdiplus::PointF(X(4.0f), Y(14.6f)));
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        g.DrawArc(&pen, X(13.0f), Y(8.6f), 4.4f * k, 6.8f * k, -55, 110);
        g.DrawArc(&pen, X(13.0f), Y(6.4f), 8.2f * k, 11.2f * k, -50, 100);
        break;
    }
    case UI_PLUG:
        Line(9, 3, 9, 7); Line(15, 3, 15, 7);
        { float pts[] = { 6.6f, 7, 17.4f, 7, 17.4f, 10.4f };
          Gdiplus::GraphicsPath p;
          p.AddLine(Gdiplus::PointF(X(6.6f), Y(7)), Gdiplus::PointF(X(17.4f), Y(7)));
          p.AddLine(Gdiplus::PointF(X(17.4f), Y(7)), Gdiplus::PointF(X(17.4f), Y(10.4f)));
          p.AddArc(X(6.6f), Y(5.0f), 10.8f * k, 10.8f * k, 0, 180);
          p.AddLine(Gdiplus::PointF(X(6.6f), Y(10.4f)), Gdiplus::PointF(X(6.6f), Y(7)));
          g.DrawPath(&pen, &p);
          (void)pts; }
        Line(12, 15.8f, 12, 21);
        break;
    case UI_REFRESH: {
        // mockup i-refresh：从右侧 (20,12) 逆时针画到右上 (17.4,6.1)，再补一个方角箭头。
        g.DrawArc(&pen, X(4), Y(4), 16 * k, 16 * k, 0, 322.6f);
        { float pts[] = { 20, 4.4f, 20, 10, 14.4f, 10 }; Poly(pts, 3); }
        break;
    }
    case UI_COPY:
        Rect(8.6f, 8.6f, 10.8f, 10.8f, 2.4f);
        { Gdiplus::GraphicsPath p;
          p.AddLine(Gdiplus::PointF(X(15.4f), Y(8.6f)), Gdiplus::PointF(X(15.4f), Y(6.4f)));
          p.AddArc(X(4), Y(4), 4.8f * k, 4.8f * k, -90, 90);
          p.AddLine(Gdiplus::PointF(X(6.4f), Y(4)), Gdiplus::PointF(X(13), Y(4)));
          p.AddArc(X(10.6f), Y(4), 4.8f * k, 4.8f * k, -90, 90);
          p.AddLine(Gdiplus::PointF(X(15.4f), Y(6.4f)), Gdiplus::PointF(X(15.4f), Y(13)));
          p.AddArc(X(10.6f), Y(10.6f), 4.8f * k, 4.8f * k, 0, 90);
          p.AddLine(Gdiplus::PointF(X(13), Y(15.4f)), Gdiplus::PointF(X(8.6f), Y(15.4f)));
          g.DrawPath(&pen, &p); }
        break;
    case UI_DOC: {
        Gdiplus::GraphicsPath p;
        float pts[] = { 5, 3.6f, 13.4f, 3.6f, 19, 9.2f, 19, 20.4f, 5, 20.4f, 5, 3.6f };
        Gdiplus::PointF prev(X(pts[0]), Y(pts[1]));
        p.StartFigure();
        for (int i = 1; i < 6; i++) {
            Gdiplus::PointF cur(X(pts[i * 2]), Y(pts[i * 2 + 1]));
            p.AddLine(prev, cur);
            prev = cur;
        }
        g.DrawPath(&pen, &p);
        Line(13.4f, 3.6f, 13.4f, 9.2f); Line(13.4f, 9.2f, 19, 9.2f);
        Line(8.2f, 13, 15.8f, 13); Line(8.2f, 16.6f, 13.4f, 16.6f);
        break;
    }
    case UI_GEAR:
        // mockup i-gear：圆 + 八根向外的辐条 + 内圈
        Ell(12, 12, 6.4f);
        Ell(12, 12, 1.6f);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265358979 / 4.0;
            Line((float)(12 + cos(a) * 6.4), (float)(12 + sin(a) * 6.4),
                 (float)(12 + cos(a) * 8.6), (float)(12 + sin(a) * 8.6));
        }
        break;
    case UI_BACK:
        Line(20, 12, 4.4f, 12);
        { float pts[] = { 4.4f, 12, 11, 5.4f }; Poly(pts, 2); }
        { float pts[] = { 4.4f, 12, 11, 18.6f }; Poly(pts, 2); }
        break;
    case UI_CLOCK:
        Ell(12, 12, 8.4f);
        Line(12, 7, 12, 12);
        Line(12, 12, 15.4f, 13.6f);
        break;
    case UI_RING:
        // mockup i-ring：圆环 + 顶端的实心点
        Ell(12, 12, 6.6f);
        { Gdiplus::SolidBrush b(col); g.FillEllipse(&b, X(10.2f), Y(2.4f), 3.6f * k, 3.6f * k); }
        break;
    case UI_WINDOW:
        Rect(3, 4, 18, 16, 2.6f);
        Line(3, 9, 21, 9);
        break;
    case UI_EYE_OFF: {
        Gdiplus::GraphicsPath p;
        p.AddBezier(Gdiplus::PointF(X(3), Y(12)), Gdiplus::PointF(X(6.4f), Y(6)),
            Gdiplus::PointF(X(17.6f), Y(6)), Gdiplus::PointF(X(21), Y(12)));
        p.AddBezier(Gdiplus::PointF(X(21), Y(12)), Gdiplus::PointF(X(17.6f), Y(18)),
            Gdiplus::PointF(X(6.4f), Y(18)), Gdiplus::PointF(X(3), Y(12)));
        g.DrawPath(&pen, &p);
        Ell(12, 12, 2.6f);
        Line(4, 4, 20, 20);
        break;
    }
    case UI_MIN:
        Line(5, 12, 19, 12);
        break;
    case UI_CLOSE:
        Line(6.5f, 6.5f, 17.5f, 17.5f);
        Line(17.5f, 6.5f, 6.5f, 17.5f);
        break;
    case UI_CHEVRON:
        { float pts[] = { 6.5f, 9.5f, 12, 15, 17.5f, 9.5f }; Poly(pts, 3); }
        break;
    case UI_EXPORT: {
        // 托盘 + 向上箭头：导出。两段圆弧收角，16px 下也分得清。
        Gdiplus::GraphicsPath p;
        p.AddLine(Gdiplus::PointF(X(5.2f), Y(13.2f)), Gdiplus::PointF(X(5.2f), Y(17.6f)));
        p.AddArc(X(5.2f), Y(15.6f), 4 * k, 4 * k, 90, 90);
        p.AddLine(Gdiplus::PointF(X(7.2f), Y(19.6f)), Gdiplus::PointF(X(16.8f), Y(19.6f)));
        p.AddArc(X(14.8f), Y(15.6f), 4 * k, 4 * k, 0, 90);
        p.AddLine(Gdiplus::PointF(X(18.8f), Y(17.6f)), Gdiplus::PointF(X(18.8f), Y(13.2f)));
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, X(12), Y(4.4f), X(12), Y(14.2f));
        { float pts[] = { 7.8f, 8.6f, 12, 4.4f, 16.2f, 8.6f }; Poly(pts, 3); }
        break;
    }
    case UI_CARET:
        { float pts[] = { 6.5f, 9.5f, 12, 15, 17.5f, 9.5f }; Poly(pts, 3); }
        break;
    case UI_UP:
        { float pts[] = { 6, 14.4f, 12, 8.4f, 18, 14.4f }; Poly(pts, 3); }
        break;
    case UI_DOWN:
        { float pts[] = { 6, 9.6f, 12, 15.6f, 18, 9.6f }; Poly(pts, 3); }
        break;
    case UI_LEFT:
        { float pts[] = { 14.4f, 6, 8.4f, 12, 14.4f, 18 }; Poly(pts, 3); }
        break;
    case UI_RIGHT:
        { float pts[] = { 9.6f, 6, 15.6f, 12, 9.6f, 18 }; Poly(pts, 3); }
        break;
    case UI_CHECK:
        { float pts[] = { 5, 12.4f, 10, 17.2f, 19, 6.8f }; Poly(pts, 3); }
        break;
    case UI_IMAGE:
        Rect(3, 4.4f, 18, 15.2f, 2.4f);
        Ell(8.2f, 9.4f, 1.6f);
        { float pts[] = { 3.4f, 16.6f, 9.4f, 11.4f, 13.6f, 15.6f, 16.4f, 13, 20.6f, 16.8f }; Poly(pts, 5); }
        break;
    case UI_CODE:
        { float pts[] = { 8.4f, 7.6f, 3.6f, 12, 8.4f, 16.4f }; Poly(pts, 3); }
        { float pts[] = { 15.6f, 7.6f, 20.4f, 12, 15.6f, 16.4f }; Poly(pts, 3); }
        break;
    case UI_INFO:
        Ell(12, 12, 8.6f);
        Line(12, 11, 12, 16.2f);
        { Gdiplus::SolidBrush b(col); g.FillEllipse(&b, X(11.1f), Y(7.1f), 1.8f * k, 1.8f * k); }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Small shared primitives
// ---------------------------------------------------------------------------

static int g_uiScroll = 0;
static int g_uiMaxScroll = 0;

static bool g_uiHitsOff = false;

static void UiHit(float x, float y, float w, float h, int act)
{
    if (g_uiHitsOff || act == ACT_NONE) return;
    HitAdd((int)x, (int)(y - (float)g_uiScroll), (int)w, (int)h, act);
}

static void UiHitScreen(float x, float y, float w, float h, int act)
{
    if (g_uiHitsOff || act == ACT_NONE) return;
    HitAdd((int)x, (int)y, (int)w, (int)h, act);
}

static float UiCX(const Gdiplus::RectF& r) { return r.X + r.Width * 0.5f; }
static float UiCY(const Gdiplus::RectF& r) { return r.Y + r.Height * 0.5f; }

static float UiTextW(const wchar_t* t, float dp)
{
    if (!t || !t[0]) return 0;
    HwMeasurerBegin((int)(dp * SettingsScale() + 0.5));
    return MeasureHw(t);
}

// ICON_*（数据层）到 UiIcon（绘制层）的映射：两个枚举从 ICON_MONITOR 起错位，
// 直接强转会把显示器画成显卡、电池画成显示器。
static UiIcon UiIconForRow(int icon)
{
    switch (icon) {
    case ICON_CPU: return UI_CPU;
    case ICON_BOARD: return UI_BOARD;
    case ICON_RAM: return UI_RAM;
    case ICON_DISK: return UI_DISK;
    case ICON_GPU: return UI_VGA;
    case ICON_NET: return UI_NET;
    case ICON_NPU: return UI_NPU;
    case ICON_MONITOR: return UI_MONITOR;
    case ICON_BATTERY: return UI_BATTERY;
    case ICON_AUDIO: return UI_AUDIO;
    case ICON_PLUG: return UI_PLUG;
    case ICON_CLOCK: return UI_CLOCK;
    default: return UI_INFO;
    }
}

static void UiChip(Gdiplus::Graphics& g, const UiStyle& u, float x, float y,
    float box, float r, int icon, float iconSize)
{
    FillRound(g, MRect(x, y, box, box), r, u.btnRegular);
    DrawUiIcon(g, icon, x + box * 0.5f, y + box * 0.5f, iconSize, u.btnContent);
}

static void UiSwitch(Gdiplus::Graphics& g, const UiStyle& u, const Gdiplus::RectF& rc,
    bool on, bool disabled, int act)
{
    Gdiplus::Color track = on ? u.primary : u.btnRegular;
    if (disabled) track = UiClr(track, 110);
    FillRound(g, rc, rc.Height * 0.5f, track);
    float k = rc.Height - 6;
    float kx = on ? rc.GetRight() - k - 3 : rc.X + 3;
    Gdiplus::SolidBrush knob(disabled ? Gdiplus::Color(160, 255, 255, 255)
                                      : Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&knob, kx, rc.Y + 3, k, k);
    if (!disabled) UiHit(rc.X - 4, rc.Y - 4, rc.Width + 8, rc.Height + 8, act);
}

static void UiSegmented(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float h, const wchar_t* const* labels, int count,
    int active, int actBase)
{
    FillRound(g, MRect(x, y, w, h), 10.0f * (float)SettingsScale(), u.btnRegular);
    float s = (float)SettingsScale();
    float pad = 3 * s;
    float bw = (w - pad * 2) / (float)count;
    for (int i = 0; i < count; i++) {
        Gdiplus::RectF b(x + pad + bw * i, y + pad, bw, h - pad * 2);
        bool on = (i == active);
        if (on) {
            FillRound(g, b, 8 * s, u.card);
            Gdiplus::Pen pen(u.primary, 1.0f);
            Gdiplus::GraphicsPath p;
            UiRoundRectPath(p, b.X, b.Y, b.Width, b.Height, 8 * s);
            g.DrawPath(&pen, &p);
        }
        SText(g, labels[i], 12.5f, FW_NORMAL, on ? u.deepText : u.t60, light,
            b.X, b.Y, b.Width, b.Height,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        UiHit(b.X, b.Y, b.Width, b.Height, actBase + i);
    }
}

static bool UiButton(Gdiplus::Graphics& g, const UiStyle& u, bool light, float x, float y,
    float w, float h, const wchar_t* label, int icon, bool primary, bool hover, int act,
    bool disabled = false, float trailing = 0)
{
    float s = (float)SettingsScale();
    Gdiplus::RectF rc(x, y, w, h);
    if (primary) {
        FillRound(g, rc, 10 * s, disabled ? UiClr(u.primary, 110) : u.primary);
    } else {
        FillRound(g, rc, 10 * s, hover && !disabled ? u.btnRegular : Gdiplus::Color(0, 0, 0, 0));
        Gdiplus::Pen pen(u.line, 1.0f);
        Gdiplus::GraphicsPath p;
        UiRoundRectPath(p, rc.X, rc.Y, rc.Width, rc.Height, 10 * s);
        g.DrawPath(&pen, &p);
    }
    Gdiplus::Color fg = primary ? Gdiplus::Color(255, 255, 255, 255)
                                : (disabled ? u.t40 : (hover ? u.deepText : u.t75));
    if (icon >= 0) {
        float iw = UiTextW(label, 13);
        float total = 16 * s + 7 * s + iw + trailing;
        float tx = rc.X + (rc.Width - total) * 0.5f;
        DrawUiIcon(g, icon, tx + 8 * s, rc.Y + rc.Height * 0.5f, 16 * s, fg);
        SText(g, label, 13, FW_NORMAL, fg, light, tx + 23 * s, rc.Y,
            iw + 2 * s, rc.Height);
        if (trailing > 0) {
            DrawUiIcon(g, UI_CARET, tx + 16 * s + 7 * s + iw + 6 * s,
                rc.Y + rc.Height * 0.5f, 11 * s, disabled ? u.t40 : u.t60);
        }
    } else {
        SText(g, label, 13, FW_NORMAL, fg, light, rc.X, rc.Y, rc.Width, rc.Height,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (!disabled) UiHit(rc.X, rc.Y, rc.Width, rc.Height, act);
    return !disabled;
}

static void UiIconButton(Gdiplus::Graphics& g, const UiStyle& u, float x, float y,
    float w, float h, int icon, float iconSize, bool hover, int act,
    const Gdiplus::Color& hoverBg, const Gdiplus::Color& fg)
{
    float s = (float)SettingsScale();
    if (hover) FillRound(g, MRect(x, y, w, h), 8 * s, hoverBg);
    DrawUiIcon(g, icon, x + w * 0.5f, y + h * 0.5f, iconSize, fg);
    UiHit(x, y, w, h, act);
}

// 一行 KV：标签列 + 1px 竖线 + 值。标签列宽按本节最长标签收缩（上限 72dp），
// 这样短标签（核心 / 线程）不会在自己和竖线之间留下半截空白。
static const float kUiKeyW = 72.0f;
static const float kUiKeyGap = 10.0f;
static const float kUiCellGap = 24.0f;
static const float kUiLineH = 21.0f;

static float UiKeyWidthFor(const wchar_t* const* labels, int n, float s)
{
    float maxPx = 0;
    for (int i = 0; i < n; i++) {
        if (!labels[i] || !labels[i][0]) continue;
        float px = (float)UiTextW(labels[i], 12.0f);
        if (px > maxPx) maxPx = px;
    }
    float dp = maxPx / (s > 0 ? s : 1) + 14.0f;
    if (dp < 32.0f) dp = 32.0f;
    if (dp > kUiKeyW) dp = kUiKeyW;
    return dp;
}

static float UiKvLabelW(const KvPair& kv, float s)
{
    if (!kv.label[0]) return 0;
    float px = (float)UiTextW(kv.label, 12.0f) / (s > 0 ? s : 1) + 6.0f;
    if (px > kUiKeyW) px = kUiKeyW;
    return px;
}

static float UiKvWidth(const KvPair& kv, float s, float keyW)
{
    (void)keyW;
    if (kv.label[0])
        return (UiKvLabelW(kv, s) + 4 + 1 + kUiKeyGap) * s + UiTextW(kv.value, 12.0f);
    return UiTextW(kv.value, 12.0f);
}

static float UiSectionKeyW(const UiRow& row, float s)
{
    const wchar_t* labels[24];
    int n = 0;
    KvPair parts[12];
    for (int d = 0; d < row.detailCount && n < 24; d++) {
        int m = SplitDetail(row.details[d], parts, 12);
        for (int i = 0; i < m && n < 24; i++) labels[n++] = parts[i].label;
    }
    return UiKeyWidthFor(labels, n, s);
}

static int UiKvLines(const UiRow& row, float rowW, float s, float keyW,
    int* wideOut = NULL)
{
    int lines = 0;
    int wide = 0;
    float used = 0;
    KvPair parts[12];
    for (int d = 0; d < row.detailCount + row.extraCount; d++) {
        const wchar_t* text = (d < row.detailCount) ? row.details[d]
                                                    : row.extra[d - row.detailCount];
        int n = SplitDetail(text, parts, 12);
        for (int i = 0; i < n; i++) {
            float pw = UiKvWidth(parts[i], s, keyW);
            if (parts[i].label[0] == 0) {
                wide++;
                if (used > 0) { lines++; used = 0; }
                lines++;
            } else if (used > 0 && used + kUiCellGap * s + pw > rowW) {
                lines++;
                used = pw;
            } else {
                used += (used > 0 ? kUiCellGap * s : 0) + pw;
            }
        }
    }
    if (used > 0) lines++;
    if (wideOut) *wideOut = wide;
    return lines;
}

// 分区标题右侧的小徽标：线程数 / 适配器数 / 健康度 / 接口数
static void UiSectionTag(const UiRow& row, int diskCount, int netCount,
    wchar_t* out, int cch)
{
    out[0] = 0;
    KvPair parts[12];
    if (row.icon == ICON_CPU) {
        for (int d = 0; d < row.detailCount; d++) {
            int n = SplitDetail(row.details[d], parts, 12);
            for (int i = 0; i < n; i++)
                if (wcscmp(parts[i].label, L"线程") == 0)
                    WPrintf(out, cch, L"%s 线程", parts[i].value);
        }
    } else if (row.icon == ICON_GPU && wcscmp(row.title, L"显卡") == 0) {
        if (row.detailCount > 0) WPrintf(out, cch, L"%d 个适配器", row.detailCount);
    } else if (row.icon == ICON_DISK && diskCount > 1) {
        WPrintf(out, cch, L"%d 块", diskCount);
    } else if (row.icon == ICON_BATTERY) {
        // 标题已经带当前电量，徽标就别再只写一个百分数，容易和电量混淆。
        for (int d = 0; d < row.detailCount; d++) {
            int n = SplitDetail(row.details[d], parts, 12);
            for (int i = 0; i < n; i++)
                if (wcscmp(parts[i].label, L"健康度") == 0)
                    WPrintf(out, cch, L"健康度 %s", parts[i].value);
        }
    } else if (row.icon == ICON_NET && netCount > 1) {
        WPrintf(out, cch, L"%d 个适配器", netCount);
    } else if (row.icon == ICON_PLUG) {
        int total = 0;
        for (int d = 0; d < row.detailCount; d++)
            total += SplitDetail(row.details[d], parts, 12);
        if (total > 0) WPrintf(out, cch, L"%d 类", total);
    }
}

// ---------------------------------------------------------------------------
// Shell state
// ---------------------------------------------------------------------------

static int g_uiMenu = 0;              // 0 none / 1 export / 2 system picker
static Gdiplus::RectF g_uiMenuAnchor;
static int g_uiDialog = 0;                 // 1 = 统一提示框打开
static wchar_t g_uiDlgTitle[96];
static wchar_t g_uiDlgBody[768];

// 由页面代码调用：打开统一提示框并重绘设置窗口。
static void UiShowDialog(const wchar_t* title, const wchar_t* body)
{
    wcsncpy_s(g_uiDlgTitle, 96, title ? title : L"", _TRUNCATE);
    wcsncpy_s(g_uiDlgBody, 768, body ? body : L"", _TRUNCATE);
    g_uiDialog = 1;
    SettingsInvalidate();
}
static ULONGLONG g_toastUntil = 0;
static wchar_t g_toastText[128] = { 0 };

static void UiToast(const wchar_t* text)
{
    wcsncpy_s(g_toastText, 128, text ? text : L"", _TRUNCATE);
    g_toastUntil = GetTickCount64() + 2000;
    if (g_setHwnd) SetTimer(g_setHwnd, 9, 200, NULL);
}

static void UiSetContentEnd(float endY)
{
    float s = (float)SettingsScale();
    float limit = (float)g_setH - 22 * s;
    int m = (int)(endY - limit + 0.5f);
    g_uiMaxScroll = m > 0 ? m : 0;
    if (g_uiScroll > g_uiMaxScroll) g_uiScroll = g_uiMaxScroll;
    if (g_uiScroll < 0) g_uiScroll = 0;
}

// ---------------------------------------------------------------------------
// Page: hardware
// ---------------------------------------------------------------------------

static float UiSectionHeight(int lines)
{
    float s = (float)SettingsScale();
    return (10 + 32 + 7 + 9) * s + lines * kUiLineH * s;
}

static float UiDrawSection(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    const UiRow& row, const wchar_t* tag, float x, float y, float w, bool divider)
{
    const float s = (float)SettingsScale();
    const float chip = 32 * s;

    if (divider) {
        Gdiplus::Pen pen(u.divider, 1.0f);
        g.DrawLine(&pen, x, y, x + w, y);
    }

    float hy = y + 10 * s;
    UiChip(g, u, x, hy, chip, 9 * s, UiIconForRow(row.icon), 20 * s);

    float tagW = 0;
    if (tag && tag[0]) tagW = UiTextW(tag, 11.5f) + 14 * s;
    SText(g, row.title, 14.5f, FW_NORMAL, u.deepText, light, x + 44 * s, hy,
        w - 44 * s - tagW - 8 * s, chip);
    if (tag && tag[0]) {
        Gdiplus::RectF tr(x + w - tagW, hy + (chip - 19 * s) * 0.5f, tagW, 19 * s);
        FillRound(g, tr, 6 * s, u.btnRegular);
        SText(g, tag, 11.5f, FW_NORMAL, u.btnContent, light, tr.X, tr.Y, tr.Width,
            tr.Height, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // 标签列固定宽度并右对齐：短标签（核心）紧贴竖线，长标签（单条容量）
    // 也能放下，而且每个分区的竖线都在同一个 x 上，整页节奏一致。
    float keyW = kUiKeyW;
    int lines = UiKvLines(row, w - 44 * s, s, keyW);
    float ry = hy + chip + 7 * s;
    float lineW = w - 44 * s;
    float x0 = x + 44 * s;
    float step = kUiLineH * s;

    KvPair parts[12];
    struct DrawItem { KvPair p; bool wide; };
    DrawItem items[64];
    int n = 0;
    for (int d = 0; d < row.detailCount && n < 63; d++) {
        int cnt = SplitDetail(row.details[d], parts, 12);
        for (int i = 0; i < cnt && n < 63; i++) {
            items[n].p = parts[i];
            items[n].wide = (parts[i].label[0] == 0);
            n++;
        }
    }
    // 面板不画的补充信息，只在设置页跟着同一行排开。
    for (int d = 0; d < row.extraCount && n < 63; d++) {
        int cnt = SplitDetail(row.extra[d], parts, 12);
        for (int i = 0; i < cnt && n < 63; i++) {
            items[n].p = parts[i];
            items[n].wide = (parts[i].label[0] == 0);
            n++;
        }
    }

    int line = 0;
    int i = 0;
    while (i < n) {
        float yy = ry + line * step;
        if (items[i].wide) {
            SText(g, items[i].p.value, 12, FW_NORMAL, u.t75, light, x0, yy, lineW,
                kUiLineH * s);
            i++;
            line++;
            continue;
        }

        // 一行放得下几组 KV，就放几组。组间距在 24dp 基础上最多各加 32dp，
        // 大致铺满卡片，又不会在短值后面拉出长条空白；值按自然宽度绘制，
        // 型号 / MAC 这类长值不会被截断。
        int j = i;
        float used = 0;
        while (j < n && !items[j].wide) {
            float pw = UiKvWidth(items[j].p, s, keyW);
            if (j > i && used + kUiCellGap * s + pw > lineW) break;
            used += (j > i ? kUiCellGap * s : 0) + pw;
            j++;
        }
        int count = j - i;
        float leftover = lineW - used;
        float add = 0;
        if (count > 1 && leftover > 0) {
            float share = leftover / (float)(count - 1);
            add = (share < 32 * s) ? share : 32 * s;
        }
        float cx = x0;
        for (int k = i; k < j; k++) {
            float pw = UiKvWidth(items[k].p, s, keyW);
            float lw = UiKvLabelW(items[k].p, s) * s;
            SText(g, items[k].p.label, 12, FW_NORMAL, u.t50, light, cx, yy,
                lw, kUiLineH * s,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            Gdiplus::Pen sep(u.line, 1.0f);
            float sx = cx + lw + 4 * s;
            g.DrawLine(&sep, sx, yy + 5 * s, sx, yy + kUiLineH * s - 5 * s);
            float vx = sx + (1 + kUiKeyGap) * s;
            float vw = cx + pw - vx;
            if (vx + vw > x0 + lineW) vw = x0 + lineW - vx;
            if (vw < 0) vw = 0;
            SText(g, items[k].p.value, 12, FW_NORMAL, u.t90, light, vx, yy,
                vw, kUiLineH * s);
            cx += pw + kUiCellGap * s + add;
        }
        i = j;
        line++;
    }

    return UiSectionHeight(lines);
}

static void PageHwUi(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float s)
{
    if (g_rowCount <= 0) return;

    if (!g_tiReady) { CollectTargetInfo(&g_ti); g_tiReady = true; }

    // 摘要三卡
    const float gap = 12 * s;
    const float sumH = 76 * s;
    const float chipSz = 40 * s;
    const float cw = (w - gap * 2) / 3;

    wchar_t model[256] = { 0 };
    {
        wchar_t mfr[128] = { 0 };
        wcsncpy_s(mfr, 128, g_smbios.sysManufacturer, _TRUNCATE);
        TranslateVendorText(mfr, 128);
        if (mfr[0] && g_smbios.sysProduct[0])
            WPrintf(model, 256, L"%s %s", mfr, g_smbios.sysProduct);
        else if (g_smbios.sysProduct[0])
            wcsncpy_s(model, 256, g_smbios.sysProduct, _TRUNCATE);
        else if (g_ti.model[0])
            wcsncpy_s(model, 256, g_ti.model, _TRUNCATE);
        else
            wcsncpy_s(model, 256, L"未知机型", _TRUNCATE);
    }
    wchar_t sysSub[128] = { 0 };
    if (g_ti.releaseId[0] && g_ti.build[0])
        WPrintf(sysSub, 128, L"%s · %s", g_ti.releaseId, g_ti.build);
    else if (g_ti.build[0])
        wcsncpy_s(sysSub, 128, g_ti.build, _TRUNCATE);

    wchar_t upText[64] = { 0 };
    {
        ULONGLONG ms = GetTickCount64();
        ULONGLONG days = ms / 86400000ULL;
        ULONGLONG hours = (ms / 3600000ULL) % 24ULL;
        ULONGLONG mins = (ms / 60000ULL) % 60ULL;
        if (days > 0) WPrintf(upText, 64, L"%llu 天 %llu 小时", days, hours);
        else WPrintf(upText, 64, L"%llu 小时 %llu 分", hours, mins);
    }

    struct { int icon; const wchar_t* cap; const wchar_t* value; const wchar_t* sub; } sums[3] = {
        { UI_VGA, L"机型型号", model, NULL },
        { UI_MONITOR, L"系统信息", g_ti.osName[0] ? g_ti.osName : L"Windows", sysSub },
        { UI_CLOCK, L"运行时间", upText, NULL },
    };

    for (int i = 0; i < 3; i++) {
        Gdiplus::RectF rc(x + (cw + gap) * i, y, cw, sumH);
        FillRound(g, rc, 16 * s, u.card);
        UiChip(g, u, rc.X + 16 * s, rc.Y + (sumH - chipSz) * 0.5f, chipSz, 11 * s,
            sums[i].icon, 22 * s);
        float tx = rc.X + 16 * s + chipSz + 12 * s;
        float tw = rc.GetRight() - 14 * s - tx;
        // 文字块整体垂直居中：有副标题时三行，没有时两行。
        bool hasSub = sums[i].sub && sums[i].sub[0];
        float blockH = hasSub ? 47 * s : 32 * s;
        float ty = rc.Y + (sumH - blockH) * 0.5f;
        SText(g, sums[i].cap, 11.5f, FW_NORMAL, u.t40, light, tx, ty, tw, 15 * s);
        SText(g, sums[i].value, 13.5f, FW_NORMAL, u.deepText, light, tx, ty + 15 * s, tw, 18 * s);
        if (hasSub)
            SText(g, sums[i].sub, 11, FW_NORMAL, u.t40, light, tx, ty + 33 * s, tw, 14 * s);
    }

    float y2 = y + sumH + 16 * s;
    SText(g, L"详细信息", 14.5f, FW_NORMAL, u.deepText, light, x + 2 * s, y2, w, 20 * s);
    y2 += 20 * s + 10 * s;

    int diskCount = 0, netCount = 0;
    for (int i = 0; i < g_rowCount; i++) {
        if (g_rows[i].icon == ICON_DISK) diskCount++;
        if (g_rows[i].icon == ICON_NET) netCount++;
    }

    const bool split = (g_cfg.theme == THEME_SPLIT);
    const bool plain = (g_cfg.theme == THEME_PLAIN);
    const float padX = split ? 22 * s : (plain ? 4 * s : 22 * s);
    const float padY = split ? 6 * s : (plain ? 0.0f : 6 * s);
    const float innerW = w - padX * 2;

    if (!split) {
        float total = padY * 2;
        for (int i = 0; i < g_rowCount; i++)
            total += UiSectionHeight(UiKvLines(g_rows[i], innerW - 44 * s, s, UiSectionKeyW(g_rows[i], s)));
        if (!plain) FillRound(g, MRect(x, y2, w, total), 16 * s, u.card);
        float cy = y2 + padY;
        for (int i = 0; i < g_rowCount; i++) {
            wchar_t tag[64];
            UiSectionTag(g_rows[i], diskCount, netCount, tag, 64);
            cy += UiDrawSection(g, u, light, g_rows[i], tag, x + padX, cy, innerW, i > 0);
        }
        y2 += total;
    } else {
        for (int i = 0; i < g_rowCount; i++) {
            wchar_t tag[64];
            UiSectionTag(g_rows[i], diskCount, netCount, tag, 64);
            float h = UiSectionHeight(UiKvLines(g_rows[i], innerW - 44 * s, s, UiSectionKeyW(g_rows[i], s)));
            FillRound(g, MRect(x, y2, w, h), 16 * s, u.card);
            UiDrawSection(g, u, light, g_rows[i], tag, x + padX, y2, innerW, false);
            y2 += h + 10 * s;
        }
        y2 -= 10 * s;
    }

    UiSetContentEnd(y2);
}

// ---------------------------------------------------------------------------
// Page: target system
// ---------------------------------------------------------------------------

static void UiKvRow(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    const wchar_t* k, const wchar_t* v, float x, float y, float w, float s,
    float keyW = kUiKeyW)
{
    SText(g, k, 12, FW_NORMAL, u.t50, light, x, y, keyW * s, 21 * s);
    Gdiplus::Pen sep(u.line, 1.0f);
    float sx = x + keyW * s;
    g.DrawLine(&sep, sx, y + 5 * s, sx, y + 16 * s);
    const bool empty = (!v || !v[0]);
    SText(g, empty ? L"—" : v, 12, FW_NORMAL, empty ? u.t40 : u.t90, light,
        sx + kUiKeyGap * s, y, w - (keyW + kUiKeyGap) * s, 21 * s);
}

static void UiSysTimeText(wchar_t* out, int cch)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    WPrintf(out, cch, L"%02d:%02d 检测", st.wHour, st.wMinute);
}

static void PageSysUi(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float s)
{
    if (!g_tiReady) { CollectTargetInfo(&g_ti); g_tiReady = true; }
    const TargetInfo& t = g_ti;

    wchar_t fs[32] = { 0 };
    {
        wchar_t root[8] = { 0 };
        wcsncpy_s(root, 8, t.systemDrive, _TRUNCATE);
        if (root[0]) {
            wcscat_s(root, 8, L"\\");
            wchar_t fsName[32] = { 0 };
            if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 32) && fsName[0])
                wcsncpy_s(fs, 32, fsName, _TRUNCATE);
        }
    }

    // ---- sysbar ----
    const float barH = 50 * s;
    FillRound(g, MRect(x, y, w, barH), 16 * s, u.card);
    UiChip(g, u, x + 14 * s, y + 10 * s, 30 * s, 9 * s, UI_DISK, 18 * s);

    {
        float selX = x + 56 * s;
        float selW = 300 * s;
        Gdiplus::RectF sel(selX, y + 8 * s, selW, 34 * s);
        FillRound(g, sel, 10 * s, u.btnRegular);
        wchar_t label[256];
        WPrintf(label, 256, L"%s  %s", t.systemDrive[0] ? t.systemDrive : L"C:",
            t.osName[0] ? t.osName : L"Windows");
        SText(g, label, 13, FW_NORMAL, u.deepText, light, sel.X + 13 * s, sel.Y,
            sel.Width - 40 * s, sel.Height);
        DrawUiIcon(g, UI_CHEVRON, sel.GetRight() - 17 * s, UiCY(sel), 13 * s, u.t50);
        UiHit(sel.X, sel.Y, sel.Width, sel.Height, ACT_SYS_PICK);

        SText(g, L"共检测到 1 个系统", 11.5f, FW_NORMAL, u.t40, light,
            sel.GetRight() + 14 * s, y + 8 * s, 200 * s, 34 * s);

        wchar_t when[64];
        UiSysTimeText(when, 64);
        SText(g, when, 11.5f, FW_NORMAL, u.t40, light, x + w - 140 * s, y + 8 * s,
            126 * s, 34 * s,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    float cy = y + barH + 14 * s;

    // ---- vendor ----
    const float vH = 148 * s;
    FillRound(g, MRect(x, cy, w, vH), 16 * s, u.card);
    {
        Gdiplus::RectF logo(x + 18 * s, cy + 16 * s, 168 * s, vH - 32 * s);
        FillRound(g, logo, 12 * s, Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Pen edge(u.line, 1.0f);
        Gdiplus::GraphicsPath p;
        UiRoundRectPath(p, logo.X, logo.Y, logo.Width, logo.Height, 12 * s);
        g.DrawPath(&edge, &p);

        bool drew = false;
        if (t.logoFile[0]) {
            Gdiplus::Bitmap bmp(t.logoFile);
            if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
                float iw = (float)bmp.GetWidth(), ih = (float)bmp.GetHeight();
                float maxW = logo.Width - 20 * s, maxH = logo.Height - 20 * s;
                float k = maxW / iw;
                if (ih * k > maxH) k = maxH / ih;
                float dw = iw * k, dh = ih * k;
                g.DrawImage(&bmp, Gdiplus::RectF(logo.X + (logo.Width - dw) * 0.5f,
                    logo.Y + (logo.Height - dh) * 0.5f, dw, dh));
                drew = true;
            }
        }
        if (!drew) {
            SText(g, t.manufacturer[0] ? t.manufacturer : L"OEM", 20, FW_BOLD,
                Gdiplus::Color(255, 0xE2, 0x23, 0x1A), false,
                logo.X + 8 * s, logo.Y, logo.Width - 16 * s, logo.Height,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        float fx = logo.GetRight() + 18 * s;
        float fw = x + w - 18 * s - fx;
        float colW = (fw - 26 * s) * 0.5f;
        float rowH = 21 * s;
        float fy = cy + 16 * s;
        static const wchar_t* const vendorKeys[6] = {
            L"制造商", L"型号", L"支持时间", L"电话", L"网址", L"Logo 位置"
        };
        float vKey = UiKeyWidthFor(vendorKeys, 6, s);

        UiKvRow(g, u, light, L"制造商", t.manufacturer, fx, fy, colW, s, vKey);
        UiKvRow(g, u, light, L"型号", t.model, fx + colW + 26 * s, fy, colW, s, vKey);
        fy += rowH;
        UiKvRow(g, u, light, L"支持时间", NULL, fx, fy, colW, s, vKey);
        UiKvRow(g, u, light, L"电话", t.phone, fx + colW + 26 * s, fy, colW, s, vKey);
        fy += rowH;
        UiKvRow(g, u, light, L"网址", t.supportUrl, fx, fy, fw, s, vKey);
        fy += rowH;
        UiKvRow(g, u, light, L"Logo 位置", t.logoFile, fx, fy, fw, s, vKey);
    }
    cy += vH + 12 * s;

    // ---- 两列表格 ----
    const float tH = 299 * s;
    FillRound(g, MRect(x, cy, w, tH), 16 * s, u.card);
    {
        wchar_t drive[64];
        WPrintf(drive, 64, L"%s（%s）", t.systemDrive[0] ? t.systemDrive : L"C:",
            fs[0] ? fs : L"—");
        struct KV { const wchar_t* k; const wchar_t* v; };
        const KV left[10] = {
            { L"系统盘符", drive },
            { L"计算机名", t.computerName },
            { L"用户名称", t.userName },
            { L"桌面路径", t.desktop },
            { L"下载路径", t.downloads },
            { L"收藏夹", t.favorites },
            { L"文档路径", t.documents },
            { L"图片路径", t.pictures },
            { L"音乐路径", t.music },
            { L"视频路径", t.videos },
        };
        const KV right[11] = {
            { L"系统名称", t.osName },
            { L"版本类别", t.edition },
            { L"系统位数", t.arch },
            { L"版本内核", t.kernel },
            { L"发行号", t.releaseId },
            { L"版本编号", t.build },
            { L"补丁号", t.ubr },
            { L"实验版本", t.lab },
            { L"安装日期", t.installDate },
            { L"注册用户", t.regUser },
            { L"注册组织", t.regOrg },
        };

        float innerX = x + 20 * s;
        float innerW = w - 40 * s;
        float colW = (innerW - 30 * s) * 0.5f;
        float rightX = innerX + colW + 30 * s;
        float ry = cy + 12 * s;

        const wchar_t* lk[10];
        const wchar_t* rk[11];
        for (int i = 0; i < 10; i++) lk[i] = left[i].k;
        for (int i = 0; i < 11; i++) rk[i] = right[i].k;
        float lKey = UiKeyWidthFor(lk, 10, s);
        float rKey = UiKeyWidthFor(rk, 11, s);

        for (int i = 0; i < 10; i++)
            UiKvRow(g, u, light, left[i].k, left[i].v, innerX, ry + i * 25 * s, colW, s, lKey);
        Gdiplus::Pen vline(u.divider, 1.0f);
        g.DrawLine(&vline, innerX + colW + 15 * s, cy + 12 * s,
            innerX + colW + 15 * s, cy + tH - 12 * s);
        for (int i = 0; i < 11; i++)
            UiKvRow(g, u, light, right[i].k, right[i].v, rightX, ry + i * 25 * s,
                colW, s, rKey);
    }
    cy += tH + 12 * s;

    // ---- 按钮 ----
    {
        const float bh = 32 * s;
        float bx = x;
        UiButton(g, u, light, bx, cy, 108 * s, bh, L"IP 地址查看", UI_NET, false,
            g_setHot == ACT_SHOWIP, ACT_SHOWIP);
        bx += 118 * s;
        UiButton(g, u, light, bx, cy, 120 * s, bh, L"保存所有信息", UI_DOC, false,
            g_setHot == ACT_SAVEINFO, ACT_SAVEINFO);
        bx += 130 * s;
        UiButton(g, u, light, bx, cy, 120 * s, bh, L"打开保存目录", UI_WINDOW, false,
            g_setHot == ACT_OPENFOLDER, ACT_OPENFOLDER);
        bx += 130 * s;
        UiButton(g, u, light, bx, cy, 96 * s, bh, L"使用说明", UI_INFO, false,
            g_setHot == ACT_HELP, ACT_HELP);
        cy += bh + 10 * s;
    }

    {
        wchar_t note[512];
        wchar_t boot[64] = { 0 };
        int fw = GetFirmwareKind();
        if (fw == 2) {
            int sb = GetSecureBootState();
            wcscpy_s(boot, 64, sb == 1 ? L"UEFI · Secure Boot 开"
                          : sb == 0 ? L"UEFI · Secure Boot 关" : L"UEFI");
        } else if (fw == 1) {
            wcscpy_s(boot, 64, L"Legacy BIOS");
        }
        // 这里说的是“系统”，不是机器：给系统名 + 版本 + Build。
        const wchar_t* sysName = t.osName[0] ? t.osName
            : (t.edition[0] ? t.edition : L"Windows");
        WPrintf(note, 512, L"当前系统：%s%s · Build %s%s%s",
            sysName, t.releaseId[0] ? L" " : L"", t.build[0] ? t.build : L"—",
            t.ubr[0] ? L"." : L"", t.ubr);
        SText(g, note, 11, FW_NORMAL, u.t40, light, x, cy, w - 220 * s, 18 * s);
        if (boot[0])
            SText(g, boot, 11, FW_NORMAL, u.t40, light, x + w - 210 * s, cy, 210 * s, 18 * s,
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        cy += 18 * s;
    }

    UiSetContentEnd(cy);
}

// ---------------------------------------------------------------------------
// Page: appearance
// ---------------------------------------------------------------------------

static void UiPreviewPanel(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    const Gdiplus::RectF& rc, float s)
{
    // 模拟桌面上那张面板卡片：白/深底 + 五行真实标题 + 蓝紫渐变描边
    Gdiplus::GraphicsPath clip;
    UiRoundRectPath(clip, rc.X, rc.Y, rc.Width, rc.Height, 12 * s);
    Gdiplus::Region saved;
    g.GetClip(&saved);
    g.SetClip(&clip);

    FillRound(g, rc, 12 * s, UiClr(u.card, 255));

    float ry = rc.Y + 9 * s;
    const float rowH = 20 * s;
    for (int i = 0; i < 5 && i < g_rowCount; i++) {
        Gdiplus::RectF chip(rc.X + 10 * s, ry + 2 * s, 14 * s, 14 * s);
        FillRound(g, chip, 4 * s, u.btnRegular);
        DrawUiIcon(g, UiIconForRow(g_rows[i].icon), UiCX(chip), UiCY(chip), 9.5f * s,
            u.btnContent, 1.8f);
        SText(g, g_rows[i].title, 8.6f, FW_NORMAL, u.deepText, light,
            chip.GetRight() + 5 * s, ry, rc.Width - 34 * s, rowH,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        ry += rowH;
    }

    g.SetClip(&saved);

    // 渐变描边（蓝 -> 紫 -> 粉），暗示面板颜色跟随壁纸
    {
        Gdiplus::GraphicsPath border;
        UiRoundRectPath(border, rc.X + 0.75f * s, rc.Y + 0.75f * s,
            rc.Width - 1.5f * s, rc.Height - 1.5f * s, 12 * s);
        Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(rc.X, rc.Y),
            Gdiplus::PointF(rc.GetRight(), rc.GetBottom()),
            OklchColor(0.70, 0.14, 250, 255), OklchColor(0.70, 0.14, 330, 255));
        Gdiplus::Color blend[3] = { OklchColor(0.70, 0.14, 250, 255),
            OklchColor(0.70, 0.14, 300, 255), OklchColor(0.70, 0.14, 345, 255) };
        Gdiplus::REAL pos[3] = { 0.0f, 0.55f, 1.0f };
        lg.SetInterpolationColors(blend, pos, 3);
        Gdiplus::Pen pen(&lg, 1.6f * s);
        g.DrawPath(&pen, &border);
    }
}

static void UiCard(Gdiplus::Graphics& g, const UiStyle& u, const Gdiplus::RectF& rc, float s)
{
    FillRound(g, rc, 16 * s, u.card);
}

static void PageLookUi(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float s)
{
    float cy = y;

    // ---- 面板样式 ----
    {
        float cardH = 172 * s;
        Gdiplus::RectF card(x, cy, w, cardH);
        UiCard(g, u, card, s);
        SText(g, L"面板样式", 14.5f, FW_NORMAL, u.deepText, light, x + 18 * s,
            y + 14 * s, 200 * s, 20 * s);

        float rowY = y + 46 * s;
        float pvW = 246 * s;
        float tilesW = w - 36 * s - pvW - 14 * s;
        float gap = 10 * s;
        float tileW = (tilesW - gap * 2) / 3;
        const wchar_t* names[3] = { L"纯文字", L"卡片", L"分离卡片" };
        Palette pal = MakePalette(light, 255);
        // 三个样式块跟右侧预览一样高，标签压到底部，跟预览底沿齐平。
        const float tileH = 118 * s;

        for (int i = 0; i < 3; i++) {
            Gdiplus::RectF tile(x + 18 * s + (tileW + gap) * i, rowY, tileW, tileH);
            bool on = (g_cfg.theme == i);
            FillRound(g, tile, 12 * s, on ? u.card : u.btnRegular);
            if (on) {
                Gdiplus::Pen pen(u.primary, 1.5f);
                Gdiplus::GraphicsPath p;
                UiRoundRectPath(p, tile.X, tile.Y, tile.Width, tile.Height, 12 * s);
                g.DrawPath(&pen, &p);
            }
            Gdiplus::RectF thumb(tile.X + 8 * s, tile.Y + 8 * s,
                tile.Width - 16 * s, tileH - 34 * s);
            FillRound(g, thumb, 8 * s, u.page);
            DrawThemeThumb(g, thumb, i, pal, s);
            SText(g, names[i], 11.5f, FW_NORMAL, on ? u.titleActive : u.t40, light,
                tile.X, tile.Y + tileH - 24 * s, tile.Width, 16 * s,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            UiHit(tile.X, tile.Y, tile.Width, tile.Height, ACT_THEME + i);
        }

        UiPreviewPanel(g, u, light, Gdiplus::RectF(x + w - 18 * s - pvW, rowY,
            pvW, tileH), s);
        cy += cardH + 12 * s;
    }

    // ---- 主题色 ----
    {
        float h = 214 * s;
        UiCard(g, u, Gdiplus::RectF(x, cy, w, h), s);
        SText(g, L"主题色", 14.5f, FW_NORMAL, u.deepText, light, x + 18 * s, cy + 14 * s,
            200 * s, 20 * s);

        float ry = cy + 44 * s;
        SText(g, L"明暗模式", 13, FW_NORMAL, u.t60, light, x + 18 * s, ry, 120 * s, 20 * s);
        SText(g, L"跟随系统时读注册表的 AppsUseLightTheme", 11.5f, FW_NORMAL, u.t40, light,
            x + 18 * s, ry + 21 * s, 300 * s, 16 * s);
        const wchar_t* modes[3] = { L"跟随系统", L"浅色", L"深色" };
        // 控件与「标题 + 说明」两行整体垂直居中，而不是顶着标题行。
        UiSegmented(g, u, light, x + w - 18 * s - 240 * s, ry + 5 * s, 240 * s, 30 * s,
            modes, 3, g_cfg.lightMode, ACT_LIGHT);

        ry += 52 * s;
        SText(g, L"跟随壁纸取色", 13, FW_NORMAL, u.t60, light, x + 18 * s, ry, 200 * s, 20 * s);
        SText(g, L"从壁纸采样主色，换算成 oklch 的 hue", 11.5f, FW_NORMAL, u.t40, light,
            x + 18 * s, ry + 21 * s, 300 * s, 16 * s);
        UiSwitch(g, u, Gdiplus::RectF(x + w - 18 * s - 44 * s, ry + 8 * s, 44 * s, 24 * s),
            g_cfg.hueFollow != 0, false, ACT_HUE_FOLLOW);

        ry += 52 * s;
        float stripW = w - 36 * s;
        Gdiplus::RectF strip(x + 18 * s, ry, stripW, 22 * s);
        {
            Gdiplus::GraphicsPath path;
            UiRoundRectPath(path, strip.X, strip.Y, strip.Width, strip.Height, 11 * s);
            Gdiplus::Region saved;
            g.GetClip(&saved);
            g.SetClip(&path);
            const int stops = 12;
            for (int i = 0; i < stops; i++) {
                float x0 = strip.X + strip.Width * i / stops;
                float x1 = strip.X + strip.Width * (i + 1) / stops;
                Gdiplus::Color c0 = OklchColor(u.dark ? 0.75 : 0.70, 0.14, i * 30.0, 255);
                Gdiplus::Color c1 = OklchColor(u.dark ? 0.75 : 0.70, 0.14, (i + 1) * 30.0, 255);
                Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(x0, strip.Y),
                    Gdiplus::PointF(x1, strip.Y), c0, c1);
                g.FillRectangle(&brush, x0, strip.Y, x1 - x0 + 1, strip.Height);
            }
            g.SetClip(&saved);
        }
        {
            int hue = HueDeg();
            float kx = strip.X + strip.Width * (hue / 360.0f);
            Gdiplus::SolidBrush kb(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&kb, kx - 10 * s, UiCY(strip) - 10 * s, 20 * s, 20 * s);
            Gdiplus::Pen ring(u.primary, 3.0f);
            g.DrawEllipse(&ring, kx - 7 * s, UiCY(strip) - 7 * s, 14 * s, 14 * s);
        }
        g_hueTrack.left = (LONG)strip.X;
        g_hueTrack.right = (LONG)(strip.X + strip.Width);
        UiHit(strip.X, strip.Y - 6 * s, strip.Width, strip.Height + 12 * s, ACT_HUE_STRIP);

        ry += 22 * s + 8 * s;
        {
            wchar_t meta[96];
            WPrintf(meta, 96, L"Hue %d · %s", HueDeg(),
                g_cfg.hueFollow ? L"取自壁纸" : L"手动");
            SText(g, meta, 11.5f, FW_NORMAL, u.t40, light, x + 18 * s, ry, 130 * s, 18 * s);
            SText(g, L"拖动或点选，整窗中性色跟着走", 11.5f, FW_NORMAL, u.t40, light,
                x + 148 * s, ry, 260 * s, 18 * s);
        }
        {
            static const int preset[5] = { 250, 190, 160, 300, 20 };
            for (int i = 0; i < 5; i++) {
                float px = x + w - 18 * s - (5 - i) * 27 * s;
                Gdiplus::RectF sw(px, ry, 15 * s, 15 * s);
                FillRound(g, sw, 4 * s, OklchColor(0.70, 0.14, preset[i], 255));
                bool on = (!g_cfg.hueFollow && g_cfg.hue == preset[i]);
                if (on) {
                    Gdiplus::Pen pen(u.primary, 2.0f * s);
                    g.DrawEllipse(&pen, sw.X - 3 * s, sw.Y - 3 * s, sw.Width + 6 * s,
                        sw.Height + 6 * s);
                }
                UiHit(sw.X - 4 * s, sw.Y - 4 * s, sw.Width + 8 * s, sw.Height + 8 * s,
                    ACT_SWATCH + 1 + i);
            }
        }
        cy += h + 12 * s;
    }

    // ---- 面板 ----
    {
        float h = 216 * s;
        UiCard(g, u, Gdiplus::RectF(x, cy, w, h), s);
        SText(g, L"面板", 14.5f, FW_NORMAL, u.deepText, light, x + 18 * s, cy + 14 * s,
            200 * s, 20 * s);

        float ry = cy + 46 * s;
        const float labelW = 90 * s;
        const float pillW = 56 * s;
        float sliderX = x + 18 * s + labelW;
        float sliderW = w - 36 * s - labelW - pillW - 14 * s;

        auto row = [&](const wchar_t* label, float t, int act, const wchar_t* val) {
            SText(g, label, 13, FW_NORMAL, u.t60, light, x + 18 * s, ry, labelW, 22 * s);
            Gdiplus::RectF track(sliderX, ry + 9 * s, sliderW, 4 * s);
            FillRound(g, track, 2 * s, u.btnRegular);
            float kx = track.X + (track.Width - 4 * s) * t;
            if (kx > track.X + 1) FillRound(g, MRect(track.X, track.Y, kx - track.X, 4 * s),
                2 * s, u.primary);
            Gdiplus::SolidBrush kb(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&kb, kx - 7.5f * s, UiCY(track) - 7.5f * s, 15 * s, 15 * s);
            Gdiplus::Pen ring(u.primary, 2.0f);
            g.DrawEllipse(&ring, kx - 6.5f * s, UiCY(track) - 6.5f * s, 13 * s, 13 * s);
            RECT* tr = (act == ACT_OPACITY) ? &g_opTrack
                     : (act == ACT_RADIUS ? &g_radTrack : &g_hueTrack);
            tr->left = (LONG)track.X;
            tr->right = (LONG)track.GetRight();
            UiHit(track.X - 6 * s, ry, track.Width + 12 * s, 22 * s, act);
            Gdiplus::RectF pill(x + w - 18 * s - pillW, ry - 2 * s, pillW, 26 * s);
            FillRound(g, pill, 8 * s, u.btnRegular);
            SText(g, val, 12, FW_NORMAL, u.t60, light, pill.X, pill.Y, pill.Width,
                pill.Height, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            ry += 34 * s;
        };

        wchar_t buf[32];
        WPrintf(buf, 32, L"%d%%", g_cfg.opacity);
        row(L"卡片不透明度", (g_cfg.opacity - 40) / 60.0f, ACT_OPACITY, buf);
        WPrintf(buf, 32, L"%d px", g_cfg.radius);
        row(L"圆角", g_cfg.radius / 24.0f, ACT_RADIUS, buf);

        SText(g, L"显示位置", 13, FW_NORMAL, u.t60, light, x + 18 * s, ry, labelW, 22 * s);
        {
            wchar_t pos[96];
            WPrintf(pos, 96, L"横向 %+d · 纵向 %+d", g_cfg.posDx, g_cfg.posDy);
            SText(g, pos, 12, FW_NORMAL, u.t90, light, sliderX, ry, 260 * s, 22 * s);
        }
        {
            float bx = x + w - 18 * s - 210 * s;
            const int acts[4] = { ACT_POS, ACT_POS + 1, ACT_POS + 2, ACT_POS + 3 };
            const int icons[4] = { UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT };
            for (int i = 0; i < 4; i++) {
                Gdiplus::RectF b(bx + i * 42 * s, ry - 2 * s, 38 * s, 28 * s);
                FillRound(g, b, 8 * s, u.btnRegular);
                DrawUiIcon(g, icons[i], UiCX(b), UiCY(b), 15 * s, u.t60);
                UiHit(b.X, b.Y, b.Width, b.Height, acts[i]);
            }
            UiButton(g, u, light, bx + 4 * 42 * s + 4 * s, ry - 2 * s, 54 * s, 28 * s,
                L"复位", -1, false, g_setHot == ACT_POS + 4, ACT_POS + 4);
        }
        ry += 34 * s;

        // 位置示意图：蓝点是面板在桌面上的真实落点
        {
            Gdiplus::RectF map(x + 18 * s, ry, 142 * s, 50 * s);
            Gdiplus::Pen dash(u.line, 1.0f);
            dash.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawRectangle(&dash, map.X, map.Y, map.Width, map.Height);

            float px = 50, py = 50;
            {
                RECT wr = { 0 };
                if (g_hwnd && GetWindowRect(g_hwnd, &wr)) {
                    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi = { sizeof(mi) };
                    if (GetMonitorInfoW(mon, &mi)) {
                        float monW = (float)(mi.rcMonitor.right - mi.rcMonitor.left);
                        float monH = (float)(mi.rcMonitor.bottom - mi.rcMonitor.top);
                        if (monW > 0 && monH > 0) {
                            px = ((wr.left + wr.right) * 0.5f - mi.rcMonitor.left) / monW * 100.0f;
                            py = ((wr.top + wr.bottom) * 0.5f - mi.rcMonitor.top) / monH * 100.0f;
                        }
                    }
                }
            }
            if (px < 5) px = 5;
            if (px > 95) px = 95;
            if (py < 6) py = 6;
            if (py > 94) py = 94;
            Gdiplus::RectF dot(map.X + map.Width * px / 100.0f - 13 * s,
                map.Y + map.Height * py / 100.0f - 3.5f * s, 26 * s, 7 * s);
            FillRound(g, dot, 3.5f * s, u.primary);

            SText(g, L"蓝点是面板落点，箭头每次移动 8 dp，复位回到设定的角；",
                11.5f, FW_NORMAL, u.t40, light, map.GetRight() + 16 * s, map.Y + 6 * s,
                w - (map.Width + 52 * s), 18 * s);
            SText(g, L"换分辨率后按比例还原，不会跑到屏幕外。",
                11.5f, FW_NORMAL, u.t40, light, map.GetRight() + 16 * s, map.Y + 26 * s,
                w - (map.Width + 52 * s), 18 * s);
        }
        cy += h + 12 * s;
    }

    UiSetContentEnd(cy);
}

// ---------------------------------------------------------------------------
// Page: content switches
// ---------------------------------------------------------------------------

static void UiMiniPreview(Gdiplus::Graphics& g, const UiStyle& u, float x, float y,
    float w, float h, int icon, bool on, float s)
{
    FillRound(g, MRect(x, y, w, h), 9 * s, UiClr(u.btnRegular, on ? 255 : 140));
    UiChip(g, u, x + 9 * s, y + (h - 18 * s) * 0.5f, 18 * s, 5 * s, icon, 11 * s);
    float tx = x + 33 * s;
    FillRound(g, MRect(tx, y + h * 0.5f - 4.5f * s, 24 * s, 3.0f * s), 1.5f * s,
        UiClr(u.deepText, on ? 200 : 90));
    FillRound(g, MRect(tx, y + h * 0.5f + 1.5f * s, 16 * s, 2.6f * s), 1.3f * s,
        UiClr(u.deepText, on ? 120 : 60));
}

static void PageContentUi(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float s)
{
    struct RowDef { int icon; int toggle; const wchar_t* title; const wchar_t* meta; };
    const RowDef defs[10] = {
        { UI_CPU, TOGGLE_CPU, L"处理器", L"核心 / 线程 / 缓存" },
        { UI_BOARD, TOGGLE_BOARD, L"主板与固件", L"厂商 / 固件版本 / 启动方式 / 证书" },
        { UI_RAM, TOGGLE_MEM, L"内存", L"品牌 / 频率 / 容量 / 类型" },
        { UI_VGA, TOGGLE_GPU, L"显卡", L"型号 / 显存（空表示仅显示检测到的显卡）" },
        { UI_NPU, TOGGLE_NPU, L"NPU", L"仅显示已检测到的加速器" },
        { UI_MONITOR, TOGGLE_MONITOR, L"显示器", L"尺寸 / 分辨率 / 刷新率" },
        { UI_DISK, TOGGLE_DISK, L"硬盘", L"容量 / 分区格式 / 总线" },
        { UI_BATTERY, TOGGLE_BATTERY, L"电池", L"设计容量 / 健康度 / 循环次数" },
        { UI_AUDIO, TOGGLE_AUDIO, L"声卡", L"设备名称 / 接口 / 采样率" },
        { UI_NET, TOGGLE_NET, L"网卡", L"名称 / MAC / IP / 速率 / SSID" },
    };

    bool hasNpu = false;
    for (int i = 0; i < g_rowCount; i++)
        if (g_rows[i].icon == ICON_NPU) hasNpu = true;

    const float rowH = 56 * s;
    const float cardH = rowH * 10 + 12 * s;
    FillRound(g, MRect(x, y, w, cardH), 16 * s, u.card);

    float cy = y + 6 * s;
    for (int i = 0; i < 10; i++) {
        const RowDef& d = defs[i];
        bool disabled = (d.toggle == TOGGLE_NPU && !hasNpu);
        bool on = ToggleValue(d.toggle) != 0;
        if (disabled) on = false;
        if (i > 0) {
            Gdiplus::Pen pen(u.divider, 1.0f);
            g.DrawLine(&pen, x + 22 * s, cy, x + w - 22 * s, cy);
        }
        float ry = cy + 8 * s;
        UiChip(g, u, x + 22 * s, ry, 34 * s, 10 * s, d.icon, 20 * s);
        float tx = x + 22 * s + 34 * s + 14 * s;
        SText(g, d.title, 13.5f, FW_NORMAL, disabled ? u.t40 : u.deepText, light,
            tx, ry + 3 * s, 240 * s, 18 * s);
        SText(g, d.meta, 11.5f, FW_NORMAL, u.t40, light, tx, ry + 22 * s, 340 * s, 16 * s);

        float swW = 44 * s;
        UiMiniPreview(g, u, x + w - 22 * s - swW - 14 * s - 68 * s, ry + 2 * s,
            68 * s, 40 * s, d.icon, on, s);
        UiSwitch(g, u, Gdiplus::RectF(x + w - 22 * s - swW, ry + 12 * s, swW, 24 * s),
            on, disabled, ACT_TOGGLE + d.toggle);
        cy += rowH;
    }

    float noteY = y + cardH + 12 * s;
    SText(g, L"这些开关只影响桌面面板；硬件信息页始终展示完整资料，缺什么就写「—」。",
        11.5f, FW_NORMAL, u.t40, light, x + 2 * s, noteY, w, 18 * s);
    SText(g, L"NPU 一行跟随硬件：本机没有加速器时保持关闭且不可切换。",
        11.5f, FW_NORMAL, u.t40, light, x + 2 * s, noteY + 18 * s, w, 18 * s);

    UiSetContentEnd(noteY + 40 * s);
}

// ---------------------------------------------------------------------------
// Page: window behaviour (hide switches)
// ---------------------------------------------------------------------------

static void UiSwitchRow(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, const wchar_t* title, const wchar_t* meta,
    bool on, bool disabled, int act, float s, bool divider)
{
    if (divider) {
        Gdiplus::Pen pen(u.divider, 1.0f);
        g.DrawLine(&pen, x + 22 * s, y, x + w - 22 * s, y);
    }
    SText(g, title, 13.5f, FW_NORMAL, disabled ? u.t40 : u.deepText, light,
        x + 22 * s, y + 8 * s, w - 130 * s, 18 * s);
    SText(g, meta, 11.5f, FW_NORMAL, u.t40, light, x + 22 * s, y + 28 * s,
        w - 130 * s, 16 * s);
    UiSwitch(g, u, Gdiplus::RectF(x + w - 22 * s - 44 * s, y + 19 * s, 44 * s, 24 * s),
        on, disabled, act);
}

static void PageWindowUi(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float s)
{
    float cy = y;

    // ---- 窗口 ----
    {
        float h = 44 * s + 4 * 64 * s + 8 * s;
        UiCard(g, u, Gdiplus::RectF(x, cy, w, h), s);
        SText(g, L"窗口", 14.5f, FW_NORMAL, u.deepText, light, x + 22 * s, cy + 14 * s,
            200 * s, 20 * s);
        SText(g, L"决定主窗口在哪里留下入口，设置窗口始终留一个回家的按钮", 11.5f, FW_NORMAL,
            u.t40, light, x + 68 * s, cy + 16 * s, 460 * s, 18 * s);

        float ry = cy + 44 * s;
        UiSwitchRow(g, u, light, x, ry, w, L"隐藏任务栏窗口",
            L"主窗口不进入任务栏，只从托盘或悬浮按钮唤回", g_cfg.hideTaskbar != 0,
            false, ACT_HIDE_TASKBAR, s, false);
        UiSwitchRow(g, u, light, x, ry + 64 * s, w, L"隐藏托盘图标",
            g_cfg.hideTaskbar ? L"任务栏入口已隐藏，托盘必须保留"
                              : L"关掉任务栏入口后才能隐藏托盘图标",
            g_cfg.hideTray != 0, g_cfg.hideTaskbar != 0, ACT_HIDE_TRAY, s, true);
        UiSwitchRow(g, u, light, x, ry + 128 * s, w, L"显示悬浮按钮",
            L"主窗口收起后在桌面右下角留一个小按钮，点一下重新打开设置",
            g_cfg.showPill != 0, false, ACT_SHOW_PILL, s, true);
        UiSwitchRow(g, u, light, x, ry + 192 * s, w, L"开机启动",
            L"登录 Windows 后自动在桌面层铺好面板",
            AutoStartEnabled(), false, ACT_AUTOSTART, s, true);
        cy += h + 12 * s;
    }

    // ---- 面板 ----
    {
        float h = 44 * s + 64 * s + 8 * s;
        UiCard(g, u, Gdiplus::RectF(x, cy, w, h), s);
        SText(g, L"面板", 14.5f, FW_NORMAL, u.deepText, light, x + 22 * s, cy + 14 * s,
            200 * s, 20 * s);
        SText(g, L"桌面层上的行为，只影响那张半透明卡片", 11.5f, FW_NORMAL, u.t40, light,
            x + 68 * s, cy + 16 * s, 400 * s, 18 * s);

        float ry = cy + 44 * s;
        UiSwitchRow(g, u, light, x, ry, w, L"全屏应用时隐藏面板",
            L"检测到全屏窗口在前台就收起面板，退出全屏后恢复", g_cfg.hideFull != 0, false,
            ACT_HIDE_FULL, s, false);
        cy += h + 12 * s;
    }

    UiSetContentEnd(cy + 28 * s);
}

// ---------------------------------------------------------------------------
// Page: about
// ---------------------------------------------------------------------------

static void PageAboutUi(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, float s)
{
    float cx = x + w * 0.5f;
    float cy = y + 44 * s;

    // 品牌块：无论有没有位图都先铺一层淡品牌色圆角底，
    // 这样关于页看起来是「应用图标」，而不是一枚漂在页面上的显示器。
    Gdiplus::RectF tile(cx - 37 * s, cy, 74 * s, 74 * s);
    FillRound(g, tile, 20 * s, UiClr(u.primary, 26));

    Gdiplus::Bitmap* logo = light ? g_logoDark : g_logoLight;
    if (logo && logo->GetLastStatus() == Gdiplus::Ok) {
        const float lz = 52 * s;
        g.DrawImage(logo, Gdiplus::RectF(cx - lz * 0.5f,
            cy + (74 * s - lz) * 0.5f, lz, lz));
    } else {
        DrawUiIcon(g, UI_GPU, UiCX(tile), UiCY(tile), 38 * s, u.primary);
    }
    cy += 74 * s + 16 * s;

    SText(g, L"轻谱 Hydrogen Spec", 20, FW_NORMAL, u.deepText, light, x, cy, w, 28 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    cy += 32 * s;
    SText(g, L"v1.0 · x64 · MIT License", 12, FW_NORMAL, u.t40, light, x, cy, w, 18 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    cy += 26 * s;
    SText(g, L"把机器的全部参数摊在桌面上——看得见，读得懂。", 12.5f, FW_NORMAL, u.t50,
        light, x, cy, w, 20 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    cy += 30 * s;

    float bw = 118 * s, bh = 34 * s, gap = 10 * s;
    float bx = cx - bw - gap * 0.5f;
    UiButton(g, u, light, bx, cy, bw, bh, L"GitHub 仓库", UI_CODE, false,
        g_setHot == ACT_GITHUB, ACT_GITHUB);
    UiButton(g, u, light, bx + bw + gap, cy, bw, bh, L"复制版本信息", UI_COPY, false,
        g_setHot == ACT_COPYVER, ACT_COPYVER);
    cy += bh + 30 * s;

    SText(g, L"MIT License · 单文件 · 不联网", 11, FW_NORMAL, u.t40, light, x, cy, w, 16 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    cy += 26 * s;

    // 信息卡
    float cardH = 3 * 30 * s + 20 * s;
    UiCard(g, u, Gdiplus::RectF(x + 40 * s, cy, w - 80 * s, cardH), s);
    struct KV { const wchar_t* k; const wchar_t* v; };
    const KV rows[3] = {
        { L"界面字体", L"MiSans Medium 子集 · 内嵌" },
        { L"数据来源", L"SMBIOS / UEFI 变量 / 注册表 / 物理磁盘 IOCTL" },
        { L"运行方式", L"单文件 · 静默 · 不联网" },
    };
    for (int i = 0; i < 3; i++) {
        float ry = cy + 10 * s + i * 30 * s;
        float labelX = x + 62 * s;
        float labelW = 0;
        for (int j = 0; j < 3; j++) {
            float pw = (float)UiTextW(rows[j].k, 12.0f);
            if (pw > labelW) labelW = pw;
        }
        labelW += 18 * s;
        SText(g, rows[i].k, 12, FW_NORMAL, u.t50, light, labelX, ry, labelW, 20 * s);
        SText(g, rows[i].v, 12, FW_NORMAL, u.t90, light, labelX + labelW, ry,
            w - 80 * s - (labelW + 42 * s), 20 * s);
    }
    cy += cardH;

    UiSetContentEnd(cy + 10 * s);
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

static void UiTitlebar(Gdiplus::Graphics& g, const UiStyle& u, bool light, float w, float s)
{
    const float h = 52 * s;
    if (g_logoLight) {
        Gdiplus::Bitmap* logo = light ? g_logoDark : g_logoLight;
        if (logo && logo->GetLastStatus() == Gdiplus::Ok)
            g.DrawImage(logo, Gdiplus::RectF(18 * s, (h - 20 * s) * 0.5f, 20 * s, 20 * s));
    } else {
        DrawUiIcon(g, UI_GPU, 28 * s, h * 0.5f, 20 * s, u.deepText);
    }

    SText(g, L"轻谱", 14.5f, FW_NORMAL, u.deepText, light, 47 * s, 0, 60 * s, h);
    {
        float vx = 47 * s + 34 * s;
        Gdiplus::Pen pen(u.line, 1.0f);
        g.DrawLine(&pen, vx, 20 * s, vx, 32 * s);
        SText(g, L"HSpec 1.0", 11, FW_NORMAL, u.t40, light, vx + 9 * s, 0, 100 * s, h);
    }

    float by = (h - 32 * s) * 0.5f;
    Gdiplus::RectF close(w - 8 * s - 38 * s, by, 38 * s, 32 * s);
    Gdiplus::RectF hide(close.X - 40 * s, by, 38 * s, 32 * s);
    Gdiplus::RectF mini(hide.X - 40 * s, by, 38 * s, 32 * s);

    // 从托盘菜单打开时给「最小化」；从悬浮球唤回时给「隐藏」，因为这时候它没有任务栏入口。
    if (g_settingsFromPill) {
        bool hot = (g_setHot == ACT_WIN_HIDE);
        if (hot) FillRound(g, hide, 8 * s, u.btnRegular);
        DrawUiIcon(g, UI_EYE_OFF, UiCX(hide), UiCY(hide), 17 * s,
            hot ? u.deepText : u.t60);
        UiHit(hide.X, hide.Y, hide.Width, hide.Height, ACT_WIN_HIDE);
    } else {
        bool hot = (g_setHot == ACT_MINIMIZE);
        if (hot) FillRound(g, mini, 8 * s, u.btnRegular);
        DrawUiIcon(g, UI_MIN, UiCX(mini), UiCY(mini), 15 * s,
            hot ? u.deepText : u.t60);
        UiHit(mini.X, mini.Y, mini.Width, mini.Height, ACT_MINIMIZE);
    }
    {
        bool hot = (g_setHot == ACT_CLOSE);
        Gdiplus::Color red = OklchColor(0.62, 0.20, 25.0, 255);
        if (hot) FillRound(g, close, 8 * s, red);
        DrawUiIcon(g, UI_CLOSE, UiCX(close), UiCY(close), 15 * s,
            hot ? Gdiplus::Color(255, 255, 255, 255) : u.t60);
        UiHit(close.X, close.Y, close.Width, close.Height, ACT_CLOSE);
    }
}

static void UiRailItem(Gdiplus::Graphics& g, const UiStyle& u, bool light, float x, float y,
    float w, float s, int icon, const wchar_t* label, bool active, bool hot, int act)
{
    float h = 46 * s;
    Gdiplus::RectF item(x, y, w, h);
    if (active) FillRound(g, item, 10 * s, u.btnRegular);
    else if (hot) FillRound(g, item, 10 * s, UiClr(u.btnHover, 150));

    Gdiplus::RectF chip(x + 8 * s, y + 8 * s, 30 * s, 30 * s);
    FillRound(g, chip, 9 * s, active ? u.card : u.btnRegular);
    DrawUiIcon(g, icon, UiCX(chip), UiCY(chip), 18 * s,
        active ? u.titleActive : u.btnContent);

    SText(g, label, 14.5f, FW_NORMAL, active ? u.titleActive : u.t60, light,
        x + 48 * s, y, w - 56 * s, h);
    UiHitScreen(item.X, item.Y, item.Width, item.Height, act);
}

static void UiRail(Gdiplus::Graphics& g, const UiStyle& u, bool light, float h, float s)
{
    const float railW = 200 * s;
    const float x = 12 * s;
    const float w = railW - 24 * s;
    float y = 52 * s + 4 * s;

    if (g_setMode == 0) {
        struct { int icon; const wchar_t* label; } items[2] = {
            { UI_GPU, L"硬件显示" }, { UI_DOC, L"目标系统信息" },
        };
        for (int i = 0; i < 2; i++) {
            UiRailItem(g, u, light, x, y, w, s, items[i].icon, items[i].label,
                g_mainTab == i, g_setHot == ACT_NAV + i, ACT_NAV + i);
            y += 48 * s;
        }
        UiRailItem(g, u, light, x, h - 56 * s - 46 * s - 10 * s, w, s, UI_GEAR, L"设置",
            false, g_setHot == ACT_OPEN_SET, ACT_OPEN_SET);
    } else {
        UiRailItem(g, u, light, x, y, w, s, UI_BACK, L"返回", false,
            g_setHot == ACT_BACK, ACT_BACK);
        y += 48 * s;
        struct { int icon; const wchar_t* label; } items[4] = {
            { UI_RING, L"外观" }, { UI_RAM, L"显示内容" }, { UI_WINDOW, L"窗口" },
            { UI_INFO, L"关于" },
        };
        for (int i = 0; i < 4; i++) {
            UiRailItem(g, u, light, x, y, w, s, items[i].icon, items[i].label,
                g_setPage == i, g_setHot == ACT_SETNAV + i, ACT_SETNAV + i);
            y += 48 * s;
        }
    }

    wchar_t foot[64];
    WPrintf(foot, 64, L"HSpec %s", kAppVersion);
    SText(g, foot, 11, FW_NORMAL, u.t40, light, 22 * s, h - 36 * s, 160 * s, 18 * s);
}

static void UiPageHead(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float x, float y, float w, const wchar_t* title, const wchar_t* sub, float s)
{
    FillRound(g, MRect(x, y + 3 * s, 4 * s, 40 * s), 2 * s, u.primary);
    SText(g, title, 20, FW_NORMAL, u.deepText, light, x + 16 * s, y - 2 * s,
        w - 220 * s, 28 * s);
    SText(g, sub, 12.5f, FW_NORMAL, u.t50, light, x + 16 * s, y + 26 * s,
        w - 220 * s, 18 * s);
}

static void UiDrawExportMenu(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float w, float h, float s)
{
    const float iw = 196 * s;
    const float ih = 36 * s;
    const float pad = 6 * s;
    float bh = 3 * ih + pad * 2;
    float mx = g_uiMenuAnchor.GetRight() - iw;
    if (mx < 8 * s) mx = 8 * s;
    if (mx + iw > w - 8 * s) mx = w - 8 * s - iw;
    float my = g_uiMenuAnchor.GetBottom() + 6 * s;
    if (my + bh > h - 8 * s) my = g_uiMenuAnchor.Y - bh - 6 * s;

    Gdiplus::RectF box(mx, my, iw, bh);
    FillRound(g, box, 13 * s, u.card);
    Gdiplus::Pen pen(u.line, 1.0f);
    Gdiplus::GraphicsPath p;
    UiRoundRectPath(p, box.X, box.Y, box.Width, box.Height, 13 * s);
    g.DrawPath(&pen, &p);

    struct { int icon; const wchar_t* label; int act; } items[3] = {
        { UI_IMAGE, L"截图", ACT_EXPORT_ITEM + 0 },
        { UI_COPY, L"复制纯文字", ACT_EXPORT_ITEM + 1 },
        { UI_CODE, L"复制 Markdown", ACT_EXPORT_ITEM + 2 },
    };
    for (int i = 0; i < 3; i++) {
        Gdiplus::RectF item(box.X + pad, box.Y + pad + ih * i, box.Width - pad * 2, ih);
        bool hot = (g_setHot == items[i].act);
        if (hot) FillRound(g, item, 8 * s, u.btnRegular);
        DrawUiIcon(g, items[i].icon, item.X + 18 * s, UiCY(item), 16 * s, u.t60);
        SText(g, items[i].label, 13, FW_NORMAL, hot ? u.deepText : u.t75, light,
            item.X + 34 * s, item.Y, item.Width - 40 * s, item.Height);
        UiHitScreen(item.X, item.Y, item.Width, item.Height, items[i].act);
    }
}

static void UiDrawSysPicker(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float w, float h, float s)
{
    const float iw = 360 * s;
    float mx = g_uiMenuAnchor.X;
    float my = g_uiMenuAnchor.GetBottom() + 6 * s;
    float bh = 60 * s;
    if (mx + iw > w - 8 * s) mx = w - 8 * s - iw;
    if (my + bh > h - 8 * s) my = g_uiMenuAnchor.Y - bh - 6 * s;

    Gdiplus::RectF box(mx, my, iw, bh);
    FillRound(g, box, 13 * s, u.card);
    Gdiplus::Pen pen(u.line, 1.0f);
    Gdiplus::GraphicsPath p;
    UiRoundRectPath(p, box.X, box.Y, box.Width, box.Height, 13 * s);
    g.DrawPath(&pen, &p);

    wchar_t label[256];
    WPrintf(label, 256, L"%s  %s（%s）",
        g_ti.systemDrive[0] ? g_ti.systemDrive : L"C:",
        g_ti.osName[0] ? g_ti.osName : L"Windows", g_ti.arch);
    Gdiplus::RectF item(box.X + 6 * s, box.Y + 6 * s, box.Width - 12 * s, 48 * s);
    bool hot = (g_setHot == ACT_SYS_OPT);
    if (hot) FillRound(g, item, 9 * s, u.btnRegular);
    Gdiplus::RectF badge(item.X + 8 * s, item.Y + 11 * s, 40 * s, 26 * s);
    FillRound(g, badge, 7 * s, u.btnRegular);
    SText(g, g_ti.systemDrive[0] ? g_ti.systemDrive : L"C:", 11, FW_NORMAL, u.titleActive,
        light, badge.X, badge.Y, badge.Width, badge.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SText(g, label, 13, FW_NORMAL, u.deepText, light, item.X + 56 * s, item.Y + 8 * s,
        item.Width - 64 * s, 18 * s);
    SText(g, L"当前系统 · 唯一检测到的安装", 11, FW_NORMAL, u.t40, light,
        item.X + 56 * s, item.Y + 26 * s, item.Width - 64 * s, 14 * s);
    UiHitScreen(item.X, item.Y, item.Width, item.Height, ACT_SYS_OPT);
}

static void UiDrawToast(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float w, float h, float s)
{
    if (!g_toastText[0] || GetTickCount64() > g_toastUntil) return;
    float tw = UiTextW(g_toastText, 12) + 32 * s;
    if (tw > w - 40 * s) tw = w - 40 * s;
    float th = 34 * s;
    Gdiplus::RectF box((w - tw) * 0.5f, h - 22 * s - th, tw, th);
    FillRound(g, box, 10 * s, UiClr(u.deepText, 235));
    SText(g, g_toastText, 12, FW_NORMAL, u.dark ? Gdiplus::Color(255, 20, 20, 24)
                                                : Gdiplus::Color(255, 255, 255, 255),
        u.dark, box.X, box.Y, box.Width, box.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

// 统一提示框：盖在设置窗口上的一层卡片，替代系统 MessageBox。
static void UiDrawDialog(Gdiplus::Graphics& g, const UiStyle& u, bool light,
    float w, float h, float s)
{
    if (!g_uiDialog) return;
    g_uiHitsOff = false;

    Gdiplus::SolidBrush scrim(Gdiplus::Color(110, 0, 0, 0));
    g.FillRectangle(&scrim, Gdiplus::RectF(0, 0, w, h));

    int lines = 1;
    for (const wchar_t* p = g_uiDlgBody; *p; p++) if (*p == L'\n') lines++;
    const float dw = 430 * s;
    float dh = (96 + lines * 20) * s;
    if (dh < 190 * s) dh = 190 * s;

    Gdiplus::RectF box((w - dw) * 0.5f, (h - dh) * 0.5f, dw, dh);
    FillRound(g, box, 16 * s, u.card);

    SText(g, g_uiDlgTitle, 15.5f, FW_NORMAL, u.deepText, light,
        box.X + 22 * s, box.Y + 18 * s, dw - 44 * s, 24 * s);
    SText(g, g_uiDlgBody, 12.5f, FW_NORMAL, u.t60, light,
        box.X + 22 * s, box.Y + 50 * s, dw - 44 * s, dh - 106 * s,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    Gdiplus::RectF ok(box.X + dw - 22 * s - 86 * s, box.Y + dh - 46 * s, 86 * s, 32 * s);
    bool hot = (g_setHot == ACT_UI_DLG_OK);
    FillRound(g, ok, 9 * s, hot ? u.primary : u.btnRegular);
    SText(g, L"确定", 12.5f, FW_NORMAL, u.btnContent, light,
        ok.X, ok.Y, ok.Width, ok.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    UiHitScreen(ok.X, ok.Y, ok.Width, ok.Height, ACT_UI_DLG_OK);
    UiHitScreen(0, 0, w, h, ACT_UI_BLOCKER);
}

static void SettingsDraw(Gdiplus::Graphics& g)
{
    // The window can end up at a different size than the one we asked for
    // (DPI change on show, work-area clamp).  Re-derive the scale from the
    // real client rect so drawing and hit testing always agree with it.
    SyncSettingsSize();
    double sd = SettingsScale();
    const float s = (float)sd;
    const bool darkPage = (g_cfg.lightMode == 1) ? false
                        : (g_cfg.lightMode == 2) ? true
                        : g_lightText;
    UiStyle u = MakeUiStyle(darkPage);
    const bool light = darkPage;
    const float W = (float)g_setW;
    const float H = (float)g_setH;

    g.Clear(u.page);

    const bool overlay = (g_uiMenu != 0 || g_uiDialog != 0);
    g_uiHitsOff = overlay;

    UiTitlebar(g, u, light, W, s);
    UiRail(g, u, light, H, s);

    const float mx = 200 * s;
    const float my = 52 * s;
    const float mw = W - mx;
    const float mh = H - my;
    const float x = mx + 24 * s;
    const float w = mw - 48 * s;
    float y = my + 4 * s;

    Gdiplus::GraphicsState st = g.Save();
    g.SetClip(Gdiplus::RectF(mx, my, mw, mh));
    g.TranslateTransform(0, -(float)g_uiScroll);

    const wchar_t* title = L"";
    const wchar_t* sub = L"";
    if (g_setMode == 0) {
        if (g_mainTab == 0) { title = L"硬件信息"; sub = L"汇总本机型号、系统和关键硬件参数"; }
        else { title = L"目标系统信息"; sub = L"查看并保存本机磁盘上每个系统的资料"; }
    } else if (g_setPage == 0) { title = L"外观"; sub = L"配色、明暗与圆角，改完立即生效"; }
    else if (g_setPage == 1) { title = L"显示内容"; sub = L"只决定面板上显示什么，硬件信息页始终是完整资料"; }
    else if (g_setPage == 2) { title = L"窗口"; sub = L"主窗口的入口与桌面层行为"; }
    else { title = L""; sub = L""; }

    if (title[0]) UiPageHead(g, u, light, x, y, w, title, sub, s);

    // 页头右侧动作
    if (g_setMode == 0 && g_mainTab == 0) {
        float by = y + 3 * s;
        float bh = 32 * s;
        float bw2 = (30 + 16 + 7 + 6 + 11) * s + UiTextW(L"导出", 13);
        float bw1 = (30 + 16 + 7) * s + UiTextW(L"刷新", 13);
        Gdiplus::RectF exportBtn(x + w - bw2, by, bw2, bh);
        UiButton(g, u, light, exportBtn.X, exportBtn.Y, exportBtn.Width, bh,
            L"导出", UI_EXPORT, false, g_setHot == ACT_EXPORT, ACT_EXPORT, false, 17 * s);
        UiButton(g, u, light, exportBtn.X - 8 * s - bw1, by, bw1, bh,
            L"刷新", UI_REFRESH, false, g_setHot == ACT_REFRESH, ACT_REFRESH);
        g_uiMenuAnchor = exportBtn;
    } else if (g_setMode == 0 && g_mainTab == 1) {
        float bw = (30 + 16 + 7) * s + UiTextW(L"重新检测", 13);
        UiButton(g, u, light, x + w - bw, y + 3 * s, bw, 32 * s, L"重新检测",
            UI_REFRESH, false, g_setHot == ACT_REDETECT, ACT_REDETECT);
    }

    float contentY = y + 52 * s;
    if (g_setMode == 0) {
        if (g_mainTab == 0) PageHwUi(g, u, light, x, contentY, w, s);
        else PageSysUi(g, u, light, x, contentY, w, s);
    } else {
        if (g_setPage == 0) PageLookUi(g, u, light, x, contentY, w, s);
        else if (g_setPage == 1) PageContentUi(g, u, light, x, contentY, w, s);
        else if (g_setPage == 2) PageWindowUi(g, u, light, x, contentY, w, s);
        else PageAboutUi(g, u, light, x, contentY, w, s);
    }

    g.Restore(st);

    // 滚动条
    if (g_uiMaxScroll > 0) {
        float trackY = my + 8 * s;
        float trackH = mh - 16 * s;
        float thumbH = trackH * trackH / (trackH + (float)g_uiMaxScroll);
        if (thumbH < 28 * s) thumbH = 28 * s;
        float t = (float)g_uiScroll / (float)g_uiMaxScroll;
        float ty = trackY + (trackH - thumbH) * t;
        FillRound(g, MRect(W - 10 * s, ty, 4 * s, thumbH), 2 * s, UiClr(u.t40, 90));
    }

    // 弹出层
    if (g_uiMenu == 1) {
        g_uiHitsOff = false;
        UiDrawExportMenu(g, u, light, W, H, s);
        HitAdd(0, 0, (int)W, (int)H, ACT_UI_BLOCKER);
    } else if (g_uiMenu == 2) {
        g_uiHitsOff = false;
        UiDrawSysPicker(g, u, light, W, H, s);
        HitAdd(0, 0, (int)W, (int)H, ACT_UI_BLOCKER);
    }

    if (g_uiDialog) UiDrawDialog(g, u, light, W, H, s);

    UiDrawToast(g, u, light, W, H, s);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

static void UiCopyText(const wchar_t* text)
{
    if (!text || !text[0]) return;
    if (!OpenClipboard(g_setHwnd)) return;
    EmptyClipboard();
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        if (dst) {
            memcpy(dst, text, bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

static void UiBuildInfoText(bool markdown, wchar_t* out, int cch)
{
    out[0] = 0;
    for (int i = 0; i < g_rowCount; i++) {
        const UiRow& row = g_rows[i];
        if (markdown) {
            wcscat_s(out, cch, L"### ");
            wcscat_s(out, cch, row.title);
            wcscat_s(out, cch, L"\r\n");
        } else {
            wcscat_s(out, cch, row.title);
            wcscat_s(out, cch, L"\r\n");
        }
        KvPair parts[12];
        for (int d = 0; d < row.detailCount; d++) {
            int n = SplitDetail(row.details[d], parts, 12);
            for (int k = 0; k < n; k++) {
                if (parts[k].label[0]) {
                    if (markdown) {
                        wcscat_s(out, cch, L"- ");
                        wcscat_s(out, cch, parts[k].label);
                        wcscat_s(out, cch, L"：");
                        wcscat_s(out, cch, parts[k].value);
                        wcscat_s(out, cch, L"\r\n");
                    } else {
                        wcscat_s(out, cch, L"  ");
                        wcscat_s(out, cch, parts[k].label);
                        wcscat_s(out, cch, L"：");
                        wcscat_s(out, cch, parts[k].value);
                        wcscat_s(out, cch, L"\r\n");
                    }
                } else {
                    wcscat_s(out, cch, L"  ");
                    wcscat_s(out, cch, parts[k].value);
                    wcscat_s(out, cch, L"\r\n");
                }
            }
        }
        wcscat_s(out, cch, L"\r\n");
    }
}

static void UiExportItem(int which)
{
    if (which == 0) {
        EnsureSaveDir();
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t path[MAX_PATH] = { 0 };
        WPrintf(path, MAX_PATH, L"%s\\面板_%04d%02d%02d_%02d%02d%02d.png",
            g_saveDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        if (SavePanelPng(path)) UiToast(L"截图已保存到「文档\\HSpec」");
        else UiToast(L"截图保存失败");
    } else {
        static wchar_t text[16384];
        UiBuildInfoText(which == 2, text, 16384);
        UiCopyText(text);
        UiToast(which == 2 ? L"已复制 Markdown" : L"已复制纯文字");
    }
}

static bool UiHandleAction(int act)
{
    if (act >= ACT_NAV && act < ACT_NAV + 2) {
        g_mainTab = act - ACT_NAV;
        g_setMode = 0;
        g_uiScroll = 0;
        return true;
    }
    if (act >= ACT_SETNAV && act < ACT_SETNAV + 4) {
        g_setPage = act - ACT_SETNAV;
        g_uiScroll = 0;
        return true;
    }
    if (act == ACT_OPEN_SET) {
        g_setMode = 1;
        g_setPage = 0;
        g_uiScroll = 0;
        return true;
    }
    if (act == ACT_BACK) {
        if (g_setMode) { g_setMode = 0; g_uiScroll = 0; }
        else { g_mainTab = 0; }
        return true;
    }
    if (act == ACT_WIN_HIDE) {
        HideSettingsToPill();
        return true;
    }
    if (act == ACT_MINIMIZE) {
        // 真正最小化到任务栏：先临时摘掉 TOOLWINDOW / 挂上 APPWINDOW，
        // 否则 toolwindow 的 SW_MINIMIZE 会让窗口直接不见。
        if (g_setHwnd) {
            LONG_PTR ex = GetWindowLongPtrW(g_setHwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(g_setHwnd, GWL_EXSTYLE,
                (ex | WS_EX_APPWINDOW) & ~WS_EX_TOOLWINDOW);
            ShowWindow(g_setHwnd, SW_MINIMIZE);
        }
        return true;
    }
    if (act == ACT_AUTOSTART) {
        SetAutoStart(!AutoStartEnabled());
        UiToast(AutoStartEnabled() ? L"已开启开机启动" : L"已关闭开机启动");
        return true;
    }
    if (act == ACT_CLOSE) {
        PostMessageW(g_setHwnd, WM_CLOSE, 0, 0);
        return true;
    }
    if (act == ACT_EXPORT) {
        g_uiMenu = (g_uiMenu == 1) ? 0 : 1;
        return true;
    }
    if (act >= ACT_EXPORT_ITEM && act < ACT_EXPORT_ITEM + 3) {
        int which = act - ACT_EXPORT_ITEM;
        g_uiMenu = 0;
        UiExportItem(which);
        return true;
    }
    if (act == ACT_SYS_PICK) {
        g_uiMenu = (g_uiMenu == 2) ? 0 : 2;
        return true;
    }
    if (act == ACT_SYS_OPT) {
        g_uiMenu = 0;
        return true;
    }
    if (act == ACT_UI_DLG_OK || act == ACT_UI_BLOCKER) {
        if (g_uiDialog) { g_uiDialog = 0; return true; }
    }

    if (act == ACT_UI_BLOCKER) {
        g_uiMenu = 0;
        return true;
    }
    if (act == ACT_REFRESH) {
        BuildModel();
        ComputeLayout();
        UpdateTheme();
        PositionWindow();
        Render();
        SendToDesktopLayer();
        UiToast(L"已刷新");
        return true;
    }
    if (act == ACT_REDETECT) {
        g_tiReady = false;
        CollectTargetInfo(&g_ti);
        g_tiReady = true;
        BuildModel();
        Render();
        UiToast(L"已重新检测");
        return true;
    }
    if (act == ACT_HIDE_TASKBAR) {
        g_cfg.hideTaskbar = !g_cfg.hideTaskbar;
        if (g_cfg.hideTaskbar) g_cfg.hideTray = 0;
        else if (g_cfg.hideTray) g_cfg.showPill = 1;
        SaveConfig();
        ApplyWindowVisibility();
        return true;
    }
    if (act == ACT_HIDE_TRAY) {
        if (g_cfg.hideTaskbar) { UiToast(L"先恢复任务栏入口，才能隐藏托盘图标"); return true; }
        g_cfg.hideTray = !g_cfg.hideTray;
        if (g_cfg.hideTray) g_cfg.showPill = 1;
        SaveConfig();
        ApplyWindowVisibility();
        if (!g_cfg.showPill) HidePill();
        return true;
    }
    if (act == ACT_SHOW_PILL) {
        g_cfg.showPill = !g_cfg.showPill;
        SaveConfig();
        if (!g_cfg.showPill) HidePill();
        else if (g_setHwnd && !IsWindowVisible(g_setHwnd)) ShowPill();
        return true;
    }
    if (act == ACT_HIDE_FULL) {
        g_cfg.hideFull = !g_cfg.hideFull;
        SaveConfig();
        return true;
    }
    if (act >= ACT_THEME && act < ACT_THEME + 3) {
        g_cfg.theme = act - ACT_THEME;
        PanelReload(false);
        return true;
    }
    if (act >= ACT_TOGGLE && act < ACT_TOGGLE + TOGGLE_COUNT) {
        ToggleFlip(act - ACT_TOGGLE);
        PanelReload(true);
        return true;
    }
    if (act == ACT_HUE_FOLLOW) {
        g_cfg.hueFollow = !g_cfg.hueFollow;
        PanelReload(false);
        return true;
    }
    if (act >= ACT_LIGHT && act < ACT_LIGHT + 3) {
        g_cfg.lightMode = act - ACT_LIGHT;
        PanelReload(false);
        return true;
    }
    if (act >= ACT_SWATCH && act < ACT_SWATCH + 6) {
        int idx = act - ACT_SWATCH;
        if (idx == 0) {
            g_cfg.hueFollow = 1;
        } else {
            static const int preset[5] = { 250, 190, 160, 300, 20 };
            g_cfg.hueFollow = 0;
            g_cfg.hue = preset[idx - 1];
        }
        PanelReload(false);
        return true;
    }
    if (act >= ACT_POS && act < ACT_POS + 5) {
        int step = 8;
        switch (act - ACT_POS) {
        case 0: g_cfg.posDy -= step; break;
        case 1: g_cfg.posDy += step; break;
        case 2: g_cfg.posDx -= step; break;
        case 3: g_cfg.posDx += step; break;
        default: g_cfg.posDx = g_cfg.posDy = 0; break;
        }
        g_cfg.posDx = ClampInt(g_cfg.posDx, -400, 400);
        g_cfg.posDy = ClampInt(g_cfg.posDy, -400, 400);
        PanelReload(false);
        return true;
    }
    if (act == ACT_GITHUB) {
        ShellExecuteW(NULL, L"open", L"https://github.com/PanDaTech/Hydrogen-Spec",
            NULL, NULL, SW_SHOWNORMAL);
        return true;
    }
    if (act == ACT_COPYVER) {
        CopyVersionToClipboard();
        UiToast(L"已复制版本信息");
        return true;
    }
    return false;
}
