// ============================================================
// ModMenu for The Slormancer (Aurie + YYToolkit)
//
// Adds an "M" button to the game's Options menu. It opens a MODS menu with one tile per
// registered mod; clicking a tile opens that mod's settings page.
//
// Mods register through the "SlormModMenu" Aurie interface (see api/ModMenu/ModMenuAPI.hpp).
// The interface is created in ModulePreinitialize, so Aurie loads this module before the mods
// that register with it in their ModuleInitialize.
//
// Settings, in ModMenu.ini next to the DLL (both optional):
//   [ModMenu]
//   Language=en   menu language: en (default) or fr, also switched from the MODS menu
//   Debug=1       menu logs + gallery of the game's UI sprites (End key)
// ============================================================

#include <YYToolkit/YYTK_Shared.hpp>
#include <ModMenu/ModMenuAPI.hpp>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace Aurie;
using namespace YYTK;

// ============================================================
// Constants
// ============================================================

static constexpr int KEY_GALLERY = VK_END;    // Debug: gallery of the game's UI sprites (page by page)

// Text size of the M button (multiplied by screen height / 900).
static constexpr double UI_TEXT_SCALE = 1.0;

// Maximum number of mods shown in the MODS menu.
static constexpr size_t kMaxMods = 6;

// Settings page grid, in native window coordinates (850 x 419).
static constexpr double kPageColumnX[] = { 75.0, 468.0 };       // checkbox centers, per column
static constexpr double kPageRowY[] = { 138.0, 228.0, 318.0 };

// ============================================================
// Globals
// ============================================================

static YYTKInterface* g_Yytk = nullptr;
static bool g_Debug = false;

// Menu language (ModMenu::LANG_*), saved in ModMenu.ini and switched from the MODS menu.
static int g_Language = ModMenu::kLangEnglish;
static char g_IniPath[MAX_PATH] = {};

// French accented letters as explicit UTF-8, so the texts do not depend on the source file
// encoding. Always concatenated ("Port" FR_E_ACUTE "e") so a following letter is never read
// as part of the escape.
#define FR_E_ACUTE    "\xC3\xA9"
#define FR_E_ACUTE_UP "\xC3\x89"
#define FR_E_CIRC     "\xC3\xAA"
#define FR_E_GRAVE    "\xC3\xA8"
#define FR_I_CIRC     "\xC3\xAE"
#define FR_A_GRAVE    "\xC3\xA0"

// Picks the text for the current language.
static const char* Tr(const char* English, const char* French)
{
    return (g_Language == ModMenu::kLangFrench) ? French : English;
}

static PFUNC_YYGMLScript g_OriginalSettings = nullptr;   // scr_ui_menus_settings
static PFUNC_YYGMLScript g_OriginalManager = nullptr;   // scr_ui_menus_manager
static PFUNC_YYGMLScript g_OriginalHud = nullptr;   // scr_ui_draw_gold_essence

struct RegisteredMod
{
    std::string name;
    std::string title;
    std::string help;
    bool (*IsActive)(void*) = nullptr;
    void (*DrawPage)(ModMenu::IModMenu*, void*) = nullptr;
    void (*OnFrame)(void*) = nullptr;
    void* user = nullptr;
};

static std::vector<RegisteredMod> g_Mods;

// Sprite gallery (Debug)
static int g_GalleryPage = 0;

static const char* kGallerySmall[] = {
    "spr_radio_btn", "spr_radio_btn_v2", "spr_radio_btn_v3", "spr_radio_cross_v2",
    "spr_radio_small_btn", "spr_radio_small_btn_filled",
    "spr_selector_btn_l", "spr_selector_btn_r", "spr_selector_btn_l_v2", "spr_selector_btn_r_v2",
    "spr_menu_toggler", "spr_menu_toggler_right",
    "spr_ui_hud_settings_button_v2", "spr_ui_hud_map_button_v2", "spr_ui_hud_inventory_button_v2",
    "spr_menu_cancel_button", "spr_menu_escape_button",
    "spr_menu_increase_button", "spr_menu_decrease_button",
    "spr_plus_button_mini", "spr_minus_button_mini",
    "spr_menu_choice_square", "spr_menu_bottom_part",
    "spr_menu_nav_left", "spr_menu_nav_right", "spr_selector_block_square",
    "spr_trait_button_plus", "spr_trait_button_minus", "spr_menu_tooltip_button"
};

static const char* kGalleryBig[] = {
    "spr_settings_mono", "spr_settings_mono_large", "spr_settings_multi",
    "spr_settings_leave", "spr_settings_separator", "spr_ach_slider"
};

static constexpr int kGallerySmallCount = static_cast<int>(sizeof(kGallerySmall) / sizeof(kGallerySmall[0]));
static constexpr int kGalleryBigCount = static_cast<int>(sizeof(kGalleryBig) / sizeof(kGalleryBig[0]));
static constexpr int kGalleryPages = 1 + kGalleryBigCount;      // page 1 = small sprites, pages 2.. = large ones

// UI state
static bool g_PanelOpen = false;             // our window (mods menu or settings) is open
static int  g_ModPage = 0;                   // 0 = mods menu, N = settings of mod number N - 1
static ULONGLONG g_PanelOpenedAt = 0;        // when it was opened
static bool g_OptionsOpen = false;           // the game's Options menu is fully shown
static bool g_HoverThisCall = false;         // one of our buttons is hovered
static bool g_CursorIsHover = false;         // the game cursor is in "hover" mode

// ============================================================
// GameMaker helpers
// ============================================================

// Converts a numeric GameMaker value to a double (through its text form).
static double ReadNumber(RValue Value)
{
    std::string s(Value.ToString());
    return std::atof(s.c_str());
}

// True if the GameMaker value is truthy (true, 1, or any non-zero number).
static bool IsTrue(RValue Value)
{
    std::string s(Value.ToString());
    if (s == "true" || s == "True") return true;
    return std::atof(s.c_str()) != 0.0;
}

// True if the foreground window belongs to the game (avoids reacting to keys
// typed in another application).
static bool GameHasFocus()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// ============================================================
// Mouse and drawing
// ============================================================

struct MouseState
{
    double x = 0.0;           // position according to GameMaker (GUI coordinates)
    double y = 0.0;
    double wx = 0.0;          // position according to Windows, converted to GUI coordinates
    double wy = 0.0;
    bool hasWin = false;      // the Windows coordinates are valid
    bool pressed = false;     // left button just pressed
    bool down = false;        // left button currently held
    bool focus = false;       // the game window is in the foreground
};

static MouseState g_LastMouse;            // last known mouse position (M button hover)

static RValue Real(double V) { return RValue(V); }

static void GmColor(int R, int G, int B)
{
    RValue c = g_Yytk->CallBuiltin("make_color_rgb", { Real(R), Real(G), Real(B) });
    g_Yytk->CallBuiltin("draw_set_color", { c });
}

static void GmAlpha(double A)
{
    g_Yytk->CallBuiltin("draw_set_alpha", { Real(A) });
}

static void GmAlign(int H, int V)   // 0 = left/top, 1 = center, 2 = right/bottom
{
    g_Yytk->CallBuiltin("draw_set_halign", { Real(H) });
    g_Yytk->CallBuiltin("draw_set_valign", { Real(V) });
}

static void GmRect(double X1, double Y1, double X2, double Y2, bool Outline)
{
    g_Yytk->CallBuiltin("draw_rectangle",
        { Real(X1), Real(Y1), Real(X2), Real(Y2), Real(Outline ? 1.0 : 0.0) });
}

static void GmText(double X, double Y, const char* Text, double Scale)
{
    g_Yytk->CallBuiltin("draw_text_transformed",
        { Real(X), Real(Y), Text, Real(Scale), Real(Scale), Real(0.0) });
}

static void GmResetState()
{
    GmColor(255, 255, 255);
    GmAlpha(1.0);
    GmAlign(0, 0);
}

static bool Inside(const MouseState& M, double X1, double Y1, double X2, double Y2)
{
    if (M.x >= X1 && M.x <= X2 && M.y >= Y1 && M.y <= Y2)
        return true;

    return M.hasWin && M.wx >= X1 && M.wx <= X2 && M.wy >= Y1 && M.wy <= Y2;
}

// Game window (titled "The Slormancer"), otherwise the process's foreground window.
static HWND GetGameWindow()
{
    static HWND cached = nullptr;

    if (cached && IsWindow(cached))
        return cached;

    cached = FindWindowA(nullptr, "The Slormancer");
    if (cached)
        return cached;

    HWND fg = GetForegroundWindow();
    if (fg)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId())
            return fg;
    }

    return nullptr;
}

static MouseState ReadMouse(double W, double H)
{
    MouseState m;

    // Position according to GameMaker.
    m.x = ReadNumber(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { Real(0) }));
    m.y = ReadNumber(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { Real(0) }));

    HWND hwnd = GetGameWindow();
    if (hwnd)
    {
        // Position according to Windows, mapped to GUI coordinates.
        POINT p;
        RECT rc;
        if (GetCursorPos(&p) && ScreenToClient(hwnd, &p) && GetClientRect(hwnd, &rc))
        {
            const double cw = static_cast<double>(rc.right - rc.left);
            const double ch = static_cast<double>(rc.bottom - rc.top);
            if (cw > 0.0 && ch > 0.0)
            {
                m.wx = p.x * W / cw;
                m.wy = p.y * H / ch;
                m.hasWin = true;
            }
        }

        // Left click: rising edge, only while the game window is in the foreground.
        static bool wasDown = false;
        m.focus = (GetForegroundWindow() == hwnd);
        const bool down = m.focus && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        m.down = down;
        m.pressed = down && !wasDown;
        wasDown = down;
    }
    else
    {
        // No window found: fall back to GameMaker input.
        m.pressed = IsTrue(g_Yytk->CallBuiltin("mouse_check_button_pressed", { Real(1) }));  // 1 = mb_left
    }

    return m;
}

// ------------------------------------------------------------
// Game sprites: we reuse the Options window's elements so our pages share the same
// style (frame, diamond checkboxes, selector arrows, close button).
// ------------------------------------------------------------

struct GmSprite
{
    bool   ok = false;
    double index = -1.0;
    double w = 0.0;     // native size in pixels
    double h = 0.0;
    double xo = 0.0;     // sprite origin
    double yo = 0.0;
};

static const GmSprite& GetSprite(const char* Name)
{
    static std::map<std::string, GmSprite> cache;

    auto it = cache.find(Name);
    if (it != cache.end())
        return it->second;

    GmSprite sp;
    RValue idx = g_Yytk->CallBuiltin("asset_get_index", { Name });
    sp.index = ReadNumber(idx);
    if (sp.index >= 0.0)
    {
        sp.w = ReadNumber(g_Yytk->CallBuiltin("sprite_get_width", { idx }));
        sp.h = ReadNumber(g_Yytk->CallBuiltin("sprite_get_height", { idx }));
        sp.xo = ReadNumber(g_Yytk->CallBuiltin("sprite_get_xoffset", { idx }));
        sp.yo = ReadNumber(g_Yytk->CallBuiltin("sprite_get_yoffset", { idx }));
        sp.ok = sp.w > 0.0 && sp.h > 0.0;
    }

    return cache.emplace(Name, sp).first->second;
}

// Draws a sprite with its top-left corner at (X, Y).
static void DrawSpriteTL(const GmSprite& Sp, int Frame, double X, double Y, double XScale, double YScale,
    double Color = 16777215.0)
{
    if (!Sp.ok)
        return;

    g_Yytk->CallBuiltin("draw_sprite_ext",
        { Real(Sp.index), Real(Frame), Real(X + Sp.xo * XScale), Real(Y + Sp.yo * YScale),
          Real(XScale), Real(YScale), Real(0.0), Real(Color), Real(1.0) });
}

// Draws a sprite centered on (Cx, Cy).
static void DrawSpriteCentered(const GmSprite& Sp, int Frame, double Cx, double Cy, double Scale)
{
    DrawSpriteTL(Sp, Frame, Cx - Sp.w * 0.5 * Scale, Cy - Sp.h * 0.5 * Scale, Scale, Scale);
}

static double TextWidth(const char* Text)
{
    return ReadNumber(g_Yytk->CallBuiltin("string_width", { Text }));
}

// ============================================================
// UI elements
// ============================================================

// Size of the orange check mark relative to the diamond (tune to taste).
static constexpr double kToggleCrossScale = 1.6;

// Diamond checkbox + label, like the game's options. Returns true when clicked.
static bool UiToggle(const MouseState& M, double Cx, double Cy, double Scale,
    double TextScale, const char* Label, bool On)
{
    const GmSprite& box = GetSprite("spr_radio_btn_v3");
    const double half = (box.ok ? box.w : 44.0) * 0.5 * Scale;
    const double labelX = Cx + half + 14.0 * Scale;
    const double textW = TextWidth(Label) * TextScale;

    const bool hover = Inside(M, Cx - half, Cy - half, labelX + textW, Cy + half);
    if (hover)
        g_HoverThisCall = true;

    DrawSpriteCentered(box, hover ? 3 : 0, Cx, Cy, Scale);
    if (On)
        DrawSpriteCentered(GetSprite("spr_radio_cross_v2"), 0, Cx, Cy, Scale * kToggleCrossScale);

    GmColor(255, 255, 255);
    GmAlign(0, 1);
    GmText(labelX, Cy, Label, TextScale);
    GmAlign(0, 0);

    return hover && M.pressed;
}

// Selector arrow (red triangle). Returns true when clicked.
static bool UiArrow(const MouseState& M, double Cx, double Cy, double Scale, bool Left)
{
    const GmSprite& sp = GetSprite(Left ? "spr_selector_btn_l_v2" : "spr_selector_btn_r_v2");
    const double halfW = (sp.ok ? sp.w : 24.0) * 0.5 * Scale + 6.0 * Scale;
    const double halfH = (sp.ok ? sp.h : 36.0) * 0.5 * Scale + 4.0 * Scale;

    const bool hover = Inside(M, Cx - halfW, Cy - halfH, Cx + halfW, Cy + halfH);
    if (hover)
        g_HoverThisCall = true;

    DrawSpriteCentered(sp, hover ? 3 : 0, Cx, Cy, Scale);
    return hover && M.pressed;
}

// Close button (the game's red cross). Returns true when clicked.
static bool UiCloseButton(const MouseState& M, double Cx, double Cy, double Scale)
{
    const GmSprite& sp = GetSprite("spr_menu_escape_button");
    const double half = (sp.ok ? sp.w : 52.0) * 0.5 * Scale;

    const bool hover = Inside(M, Cx - half, Cy - half, Cx + half, Cy + half);
    if (hover)
        g_HoverThisCall = true;

    DrawSpriteCentered(sp, hover ? 1 : 0, Cx, Cy, Scale);
    return hover && M.pressed;
}

// A clickable tile (the game's golden bar with an arrow): label on the left, status on the right.
static bool UiTile(const MouseState& M, double X, double Y, double Scale, double TextScale,
    const char* Label, const char* Status, int Sr, int Sg, int Sb)
{
    const GmSprite& sp = GetSprite("spr_ach_slider");
    const double w = (sp.ok ? sp.w : 468.0) * Scale;
    const double h = (sp.ok ? sp.h : 104.0) * Scale;

    const bool hover = Inside(M, X, Y, X + w, Y + h);
    if (hover)
        g_HoverThisCall = true;

    if (sp.ok)
    {
        // Slightly dimmed at rest, full brightness on hover.
        DrawSpriteTL(sp, 0, X, Y, Scale, Scale, hover ? 16777215.0 : 11842740.0);
    }
    else
    {
        GmColor(20, 20, 24);
        GmRect(X, Y, X + w, Y + h, false);
        GmColor(hover ? 246 : 190, hover ? 206 : 150, hover ? 96 : 60);
        GmRect(X, Y, X + w, Y + h, true);
    }

    if (hover) GmColor(255, 226, 140); else GmColor(255, 255, 255);
    GmAlign(0, 1);
    GmText(X + 0.07 * w, Y + h * 0.5, Label, TextScale * 1.25);

    GmColor(Sr, Sg, Sb);
    GmAlign(2, 1);                                  // right-aligned, before the golden arrow
    GmText(X + 0.80 * w, Y + h * 0.5, Status, TextScale * 0.9);
    GmAlign(0, 0);

    return hover && M.pressed;
}

// ============================================================
// Window shared by the MODS menu and the settings pages: same frame as the game's menus
// (title band + body), centered on screen.
// ============================================================

struct WindowContext
{
    MouseState mouse;
    double wx = 0.0;          // top-left corner of the window on screen
    double wy = 0.0;
    double ws = 1.0;          // window scale
    double ts = 1.0;          // text scale
    double nw = 850.0;        // native frame size (850 x 419)
    double nh = 419.0;
    bool   inModPage = false; // a mod is currently drawing its page
    std::string help;         // help line set by the mod for this frame (empty = its default one)
};

static WindowContext g_Win;

// Converts native window coordinates to screen coordinates.
static double WinX(double Nx) { return g_Win.wx + Nx * g_Win.ws; }
static double WinY(double Ny) { return g_Win.wy + Ny * g_Win.ws; }

// Dims the game, draws the frame and fills g_Win. WidthRatio = window width
// relative to the screen width. The caller sets the text scale (g_Win.ts).
static void BeginWindow(double W, double H, double WidthRatio, const MouseState& M)
{
    GmColor(0, 0, 0);
    GmAlpha(0.78);
    GmRect(0, 0, W, H, false);
    GmAlpha(1.0);

    const GmSprite& win = GetSprite("spr_settings_leave");

    g_Win.mouse = M;
    g_Win.nw = win.ok ? win.w : 850.0;
    g_Win.nh = win.ok ? win.h : 419.0;
    g_Win.ws = (WidthRatio * W) / g_Win.nw;
    g_Win.wx = (W - g_Win.nw * g_Win.ws) * 0.5;
    g_Win.wy = (H - g_Win.nh * g_Win.ws) * 0.5;

    if (win.ok)
    {
        DrawSpriteTL(win, 0, g_Win.wx, g_Win.wy, g_Win.ws, g_Win.ws);
    }
    else
    {
        GmColor(12, 12, 12);
        GmRect(WinX(0.0), WinY(0.0), WinX(g_Win.nw), WinY(g_Win.nh), false);
        GmColor(80, 76, 60);
        GmRect(WinX(0.0), WinY(0.0), WinX(g_Win.nw), WinY(g_Win.nh), true);
    }
}

// Title in the band.
static void DrawWindowTitle(const char* Title)
{
    GmColor(255, 255, 255);
    GmAlign(1, 1);
    GmText(WinX(g_Win.nw * 0.5), WinY(43.0), Title, g_Win.ts * 1.15);
    GmAlign(0, 0);
}

// Help line, at the bottom of the window.
static void DrawWindowHelp(const char* Help)
{
    GmColor(150, 140, 126);
    GmAlign(1, 1);
    GmText(WinX(g_Win.nw * 0.5), WinY(384.0), Help, g_Win.ts * 0.62);
    GmAlign(0, 0);
}

// The game's cross in the band (Esc is handled by DrawModPanel). Returns true when clicked.
static bool DrawWindowCloseButton()
{
    return UiCloseButton(g_Win.mouse, WinX(g_Win.nw - 44.0), WinY(43.0), g_Win.ws * 0.75);
}

// ============================================================
// Aurie interface offered to mods
// ============================================================

struct ModMenuInterface final : ModMenu::IModMenu
{
    AurieStatus Create() override { return AURIE_SUCCESS; }
    void Destroy() override {}

    void QueryVersion(OUT short& Major, OUT short& Minor, OUT short& Patch) override
    {
        Major = 1;
        Minor = 1;
        Patch = 0;
    }

    bool RegisterMod(const ModMenu::ModEntry& Entry) override
    {
        if (!Entry.Name || !Entry.DrawPage)
            return false;

        RegisteredMod mod;
        mod.name = Entry.Name;
        mod.title = Entry.Title ? Entry.Title : Entry.Name;
        mod.help = Entry.Help ? Entry.Help : "";
        mod.IsActive = Entry.IsActive;
        mod.DrawPage = Entry.DrawPage;
        mod.OnFrame = Entry.OnFrame;
        mod.user = Entry.User;

        for (RegisteredMod& existing : g_Mods)
        {
            if (existing.name == mod.name)
            {
                existing = mod;
                return true;
            }
        }

        if (g_Mods.size() >= kMaxMods)
            return false;

        g_Mods.push_back(mod);
        return true;
    }

    bool Toggle(int Column, int Row, const char* Label, bool On) override
    {
        if (!g_Win.inModPage || !Label || Column < 0 || Column > 1 || Row < 0 || Row > 2)
            return false;

        return UiToggle(g_Win.mouse, WinX(kPageColumnX[Column]), WinY(kPageRowY[Row]),
            g_Win.ws * 0.85, g_Win.ts, Label, On);
    }

    int Selector(int Column, int Row, const char* Label, const char* Value) override
    {
        if (!g_Win.inModPage || !Label || !Value || Column < 0 || Column > 1 || Row < 0 || Row > 2)
            return 0;

        const double col = kPageColumnX[Column];
        const double row = kPageRowY[Row];

        GmColor(255, 255, 255);
        GmAlign(0, 1);
        GmText(WinX(col - 28.0), WinY(row - 24.0), Label, g_Win.ts);
        GmAlign(0, 0);

        const double selY = WinY(row + 16.0);
        const double boxX1 = WinX(col + 20.0), boxX2 = WinX(col + 235.0);
        const double boxH = 34.0 * g_Win.ws;

        GmColor(24, 24, 24);
        GmRect(boxX1, selY - boxH * 0.5, boxX2, selY + boxH * 0.5, false);
        GmColor(52, 50, 44);
        GmRect(boxX1, selY - boxH * 0.5, boxX2, selY + boxH * 0.5, true);
        GmColor(200, 200, 200);
        GmAlign(1, 1);
        GmText((boxX1 + boxX2) * 0.5, selY, Value, g_Win.ts);
        GmAlign(0, 0);

        int step = 0;
        if (UiArrow(g_Win.mouse, WinX(col - 4.0), selY, g_Win.ws * 0.8, true))
            step = -1;
        if (UiArrow(g_Win.mouse, WinX(col + 259.0), selY, g_Win.ws * 0.8, false))
            step = 1;

        return step;
    }

    int GetLanguage() override
    {
        return g_Language;
    }

    void HelpLine(const char* Text) override
    {
        if (g_Win.inModPage && Text)
            g_Win.help = Text;
    }
};

static ModMenuInterface g_Interface;

// ============================================================
// External mods: mods that do not know about this menu. We give them a tile with an
// information page, without touching their code.
//
// SlormReforger (https://github.com/wistb/SlormReforger) has its own settings window;
// its hotkey is read from SlormReforger.txt, next to its DLL.
// ============================================================

static char g_ModsFolder[MAX_PATH] = {};      // folder of our DLL (mods/Aurie), with the trailing separator
static int g_ReforgerKey = VK_F6;             // SlormReforger's hotkey (F6 by default)

// One line of text in a mod's page, in native window coordinates.
static void PageText(double Nx, double Ny, const char* Text, int R, int G, int B)
{
    GmColor(R, G, B);
    GmAlign(0, 1);
    GmText(WinX(Nx), WinY(Ny), Text, g_Win.ts * 0.8);
    GmAlign(0, 0);
}

static void ReforgerReadKey()
{
    int key = VK_F6;

    FILE* f = nullptr;
    const std::string path = std::string(g_ModsFolder) + "SlormReforger.txt";
    if (fopen_s(&f, path.c_str(), "r") == 0 && f)
    {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
        {
            int value = 0;
            if (sscanf_s(line, "hotkey %d", &value) == 1 && value >= 8 && value <= 254)
                key = value;
        }
        std::fclose(f);
    }

    g_ReforgerKey = key;
}

static void ReforgerDrawPage(ModMenu::IModMenu* Menu, void*)
{
    const double left = kPageColumnX[0] - 28.0;
    const double right = kPageColumnX[1] - 28.0;

    // --- Left column: how to use it ---
    PageText(left, 138.0, Tr("Automatic reforging at Friedrich's",
        "Reforge automatique chez Friedrich"), 255, 226, 140);
    PageText(left, 198.0, Tr("1. Talk to Friedrich, the blacksmith",
        "1. Parle " FR_A_GRAVE " Friedrich, le forgeron"), 255, 255, 255);
    PageText(left, 243.0, Tr("2. Open Reforge Equipment",
        "2. Ouvre la reforge d'" FR_E_ACUTE "quipement"), 255, 255, 255);
    PageText(left, 288.0, Tr("3. Put an item in the slot",
        "3. Place un objet dans l'emplacement"), 255, 255, 255);
    PageText(left, 333.0, Tr("The SlormReforger window appears",
        "La fen" FR_E_CIRC "tre SlormReforger appara" FR_I_CIRC "t"), 200, 200, 200);

    // --- Right column: hotkey and settings ---
    char keyText[48];
    if (g_ReforgerKey >= VK_F1 && g_ReforgerKey <= VK_F24)
        std::snprintf(keyText, sizeof(keyText), Tr("Hotkey: F%d", "Touche : F%d"), g_ReforgerKey - VK_F1 + 1);
    else
        std::snprintf(keyText, sizeof(keyText), Tr("Hotkey: key code %d", "Touche : code %d"), g_ReforgerKey);

    PageText(right, 138.0, keyText, 255, 226, 140);
    PageText(right, 183.0, Tr("Shows or hides its window",
        "Affiche ou cache sa fen" FR_E_CIRC "tre"), 200, 200, 200);
    PageText(right, 258.0, Tr("Settings", "R" FR_E_ACUTE "glages"), 255, 226, 140);
    PageText(right, 303.0, Tr("Options tab of its window",
        "Onglet Options de sa fen" FR_E_CIRC "tre"), 200, 200, 200);

    Menu->HelpLine(Tr(
        "Mod by wistb  -  its settings live in its own window  -  Esc: back",
        "Mod de wistb  -  ses r" FR_E_ACUTE "glages se font dans sa propre fen" FR_E_CIRC "tre  -  " FR_E_ACUTE_UP "chap : retour"));
}

// SkillAlpha sets the strength of skill visuals per kind, from rows it adds to the game's own
// Settings (Gameplay page). Its values are read from SkillAlpha.ini, next to its DLL, and only
// shown here: it reads that file once at launch and rewrites it on every change.

static constexpr int kSkillAlphaKinds = 6;
static const char* kSkillAlphaKeys[kSkillAlphaKinds] = { "Skills", "Ancestral", "Auras", "Seals", "Minions", "Enemies" };
static int g_SkillAlphaStrength[kSkillAlphaKinds] = { 100, 100, 100, 100, 100, 100 };

static void SkillAlphaReadValues()
{
    const std::string path = std::string(g_ModsFolder) + "SkillAlpha.ini";
    for (int k = 0; k < kSkillAlphaKinds; ++k)
        g_SkillAlphaStrength[k] = static_cast<int>(GetPrivateProfileIntA("Strength", kSkillAlphaKeys[k], 100, path.c_str()));
}

static void SkillAlphaDrawPage(ModMenu::IModMenu* Menu, void*)
{
    const double left = kPageColumnX[0] - 28.0;
    const double right = kPageColumnX[1] - 28.0;

    // --- Left column: where it is set ---
    PageText(left, 138.0, Tr("Skill visuals strength, per kind",
        "Intensit" FR_E_ACUTE " des effets, par type"), 255, 226, 140);
    PageText(left, 198.0, Tr("In the game's Options, Gameplay page,",
        "Dans les Options du jeu, page Gameplay,"), 255, 255, 255);
    PageText(left, 243.0, Tr("under Map Opacity:",
        "sous l'opacit" FR_E_ACUTE " de la carte :"), 255, 255, 255);
    PageText(left, 288.0, "Effect Strength For / Effect Strength", 255, 255, 255);
    PageText(left, 333.0, Tr("0 to 500%  -  Shift = steps of 50",
        "0 " FR_A_GRAVE " 500 %  -  Maj = pas de 50"), 200, 200, 200);

    // --- Right column: current values ---
    static const char* kNamesEn[kSkillAlphaKinds] = {
        "Skills", "Ancestral Legacy", "Auras & Shields", "Seals", "Minions", "Enemy Effects" };
    static const char* kNamesFr[kSkillAlphaKinds] = {
        "Comp" FR_E_ACUTE "tences", "H" FR_E_ACUTE "ritage ancestral", "Auras et boucliers",
        "Sceaux", "Serviteurs", "Effets ennemis" };

    PageText(right, 138.0, Tr("Current strength", "Intensit" FR_E_ACUTE " actuelle"), 255, 226, 140);
    for (int k = 0; k < kSkillAlphaKinds; ++k)
    {
        char line[96];
        if (g_Language == ModMenu::kLangFrench)
            std::snprintf(line, sizeof(line), "%s : %d %%", kNamesFr[k], g_SkillAlphaStrength[k]);
        else
            std::snprintf(line, sizeof(line), "%s: %d%%", kNamesEn[k], g_SkillAlphaStrength[k]);

        const bool changed = g_SkillAlphaStrength[k] != 100;
        PageText(right, 180.0 + k * 33.0, line, changed ? 255 : 200, changed ? 255 : 200, changed ? 255 : 200);
    }

    Menu->HelpLine(Tr(
        "Mod by surficialruby  -  its settings live in the game's Options  -  Esc: back",
        "Mod de surficialruby  -  ses r" FR_E_ACUTE "glages sont dans les Options du jeu  -  " FR_E_ACUTE_UP "chap : retour"));
}

// Adds the external mods loaded in the game to the menu. Called every time the menu opens:
// those mods may be loaded after us, and their settings files may have changed.
static void DetectExternalMods()
{
    static bool reforgerAdded = false;
    static bool skillAlphaAdded = false;

    if (GetModuleHandleA("SlormReforger.dll"))
    {
        ReforgerReadKey();

        if (!reforgerAdded)
        {
            ModMenu::ModEntry entry = {};
            entry.Name = "SLORMREFORGER";
            entry.Title = "MOD  -  SLORMREFORGER";
            entry.DrawPage = ReforgerDrawPage;
            reforgerAdded = g_Interface.RegisterMod(entry);
        }
    }

    if (GetModuleHandleA("SkillAlpha.dll"))
    {
        SkillAlphaReadValues();

        if (!skillAlphaAdded)
        {
            ModMenu::ModEntry entry = {};
            entry.Name = "SKILLALPHA";
            entry.Title = "MOD  -  SKILLALPHA";
            entry.DrawPage = SkillAlphaDrawPage;
            skillAlphaAdded = g_Interface.RegisterMod(entry);
        }
    }
}

// ============================================================
// Pages: MODS menu and a mod's settings
// ============================================================

// Language switch "EN / FR" on the left of the title band: the current language is highlighted.
// A click switches to the other language and saves it in ModMenu.ini.
static void DrawLanguageSwitch()
{
    const double scale = g_Win.ts * 0.8;
    const double x = WinX(34.0);
    const double cy = WinY(43.0);

    const double wEn = TextWidth("EN") * scale;
    const double wSep = TextWidth("  /  ") * scale;
    const double wFr = TextWidth("FR") * scale;
    const double halfH = ReadNumber(g_Yytk->CallBuiltin("string_height", { "EN" })) * scale * 0.5;
    const double pad = 8.0 * g_Win.ws;

    const bool hover = Inside(g_Win.mouse, x - pad, cy - halfH - pad, x + wEn + wSep + wFr + pad, cy + halfH + pad);
    if (hover)
        g_HoverThisCall = true;

    const bool french = (g_Language == ModMenu::kLangFrench);

    GmAlign(0, 1);
    if (!french) GmColor(255, 226, 140); else GmColor(130, 124, 112);
    GmText(x, cy, "EN", scale);
    GmColor(130, 124, 112);
    GmText(x + wEn, cy, "  /  ", scale);
    if (french) GmColor(255, 226, 140); else GmColor(130, 124, 112);
    GmText(x + wEn + wSep, cy, "FR", scale);
    GmAlign(0, 0);

    if (hover && g_Win.mouse.pressed)
    {
        g_Language = french ? ModMenu::kLangEnglish : ModMenu::kLangFrench;
        WritePrivateProfileStringA("ModMenu", "Language", french ? "en" : "fr", g_IniPath);
    }
}

// Page 0: mods menu (one tile per mod). Returns true if the player asks to close the menu.
static bool DrawModsMenu(double W, double H, const MouseState& M)
{
    BeginWindow(W, H, 0.40, M);
    g_Win.ts = (H / 1440.0) * 1.05;          // same text size as the settings pages

    DrawWindowTitle("MODS");
    DrawLanguageSwitch();

    const int count = static_cast<int>(g_Mods.size());

    if (count == 0)
    {
        GmColor(200, 200, 200);
        GmAlign(1, 1);
        GmText(WinX(g_Win.nw * 0.5), WinY(215.0), Tr("No mod installed", "Aucun mod install" FR_E_ACUTE), g_Win.ts);
        GmAlign(0, 0);
    }

    // One or two mods: large tiles stacked vertically. More: a 2-column grid.
    const bool grid = count > 2;
    const double tileScale = grid ? 0.85 : 1.25;
    const double tileText = g_Win.ts * (tileScale / 1.25);

    for (int i = 0; i < count; ++i)
    {
        const RegisteredMod& mod = g_Mods[i];

        const double nx = grid ? 22.0 + (i % 2) * 410.0 : 40.0;
        const double ny = grid ? 104.0 + (i / 2) * 94.0 : 112.0 + i * 138.0;

        const char* status = "";
        int sr = 255, sg = 255, sb = 255;
        if (mod.IsActive)
        {
            const bool active = mod.IsActive(mod.user);
            status = active ? Tr("ACTIVE", "ACTIF") : Tr("INACTIVE", "INACTIF");
            sr = active ? 130 : 214;
            sg = active ? 205 : 96;
            sb = active ? 120 : 72;
        }

        // A click opens the mod's settings.
        if (UiTile(M, WinX(nx), WinY(ny), g_Win.ws * tileScale, tileText, mod.name.c_str(), status, sr, sg, sb))
        {
            g_ModPage = i + 1;
            g_PanelOpenedAt = GetTickCount64();
        }
    }

    DrawWindowHelp(Tr(
        "Click a mod to open its settings  -  Esc: back",
        "Clique sur un mod pour ouvrir ses r" FR_E_ACUTE "glages  -  " FR_E_ACUTE_UP "chap : retour"));

    return DrawWindowCloseButton();
}

// A mod's settings page: the frame is ours, the content is drawn by the mod.
// Returns true if the player asks to close the page.
static bool DrawModPage(double W, double H, const MouseState& M, const RegisteredMod& Mod)
{
    BeginWindow(W, H, 0.62, M);
    g_Win.ts = g_Win.ws * 0.55;

    DrawWindowTitle(Mod.title.c_str());

    // Central separator line.
    DrawSpriteTL(GetSprite("spr_settings_separator"), 0, WinX(403.0), WinY(98.0),
        g_Win.ws, g_Win.ws * (265.0 / 531.0));

    g_Win.help.clear();
    g_Win.inModPage = true;
    Mod.DrawPage(&g_Interface, Mod.user);
    g_Win.inModPage = false;

    // The mod's HelpLine call for this frame wins over the help text it registered with.
    const std::string& help = g_Win.help.empty() ? Mod.help : g_Win.help;
    if (!help.empty())
        DrawWindowHelp(help.c_str());

    return DrawWindowCloseButton();
}

// Original values of the Options menu, saved to be restored as they were.
static double g_SavedMenuY = 0.0;
static double g_SavedPanelY = 1100.0;

// Menu values when it is fully closed (read while Options is closed). Used to
// put the menu back in order if the game closes it while our page is open.
static double g_ClosedMenuY = -1440.0;
static double g_ClosedPanelY = -100.0;
static bool   g_HaveClosedValues = false;

// Moves the Options menu off screen while our page is open, then puts it back:
//  - menu_y_pos  : the window itself (0 = shown, -1440 = hidden)
//  - panel_y_pos : the band (1100 = shown, -1500 = off screen)
static void SetOptionsWindowHidden(bool Hidden)
{
    RValue obj = g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" });
    RValue count = g_Yytk->CallBuiltin("instance_number", { obj });
    if (ReadNumber(count) < 1.0)
        return;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { obj, Real(0) });

    if (Hidden)
    {
        g_SavedMenuY = ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { inst, "menu_y_pos" }));
        g_SavedPanelY = ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { inst, "panel_y_pos" }));

        if (g_Debug)
            DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] page opened: menu_y_pos=%.0f panel_y_pos=%.0f (values read)",
                g_SavedMenuY, g_SavedPanelY);

        // If they already read as "hidden", keep the values of an open menu.
        if (g_SavedMenuY < -20.0)  g_SavedMenuY = 0.0;
        if (g_SavedPanelY < 200.0) g_SavedPanelY = 1100.0;

        g_Yytk->CallBuiltin("variable_instance_set", { inst, "menu_y_pos",  Real(-1440.0) });
        g_Yytk->CallBuiltin("variable_instance_set", { inst, "panel_y_pos", Real(-1500.0) });
    }
    else
    {
        g_Yytk->CallBuiltin("variable_instance_set", { inst, "menu_y_pos",  Real(g_SavedMenuY) });
        g_Yytk->CallBuiltin("variable_instance_set", { inst, "panel_y_pos", Real(g_SavedPanelY) });

        if (g_Debug)
            DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] page closed: menu_y_pos=%.0f panel_y_pos=%.0f (values restored)",
                g_SavedMenuY, g_SavedPanelY);
    }
}

// The game closed Options while our page was open: put the menu back in its
// "closed" state (not the "open" state saved when the page was opened).
static void RestoreClosedMenuState()
{
    RValue obj = g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" });
    RValue count = g_Yytk->CallBuiltin("instance_number", { obj });
    if (ReadNumber(count) < 1.0)
        return;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { obj, Real(0) });
    g_Yytk->CallBuiltin("variable_instance_set", { inst, "menu_y_pos",  Real(g_ClosedMenuY) });
    g_Yytk->CallBuiltin("variable_instance_set", { inst, "panel_y_pos", Real(g_ClosedPanelY) });

    if (g_Debug)
        DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] menu closed by the game while the page was open: menu_y_pos=%.0f panel_y_pos=%.0f reset to closed state",
            g_ClosedMenuY, g_ClosedPanelY);
}

// Mods window: routes to the mods menu or the settings, and handles closing.
static void DrawModPanel(double W, double H, const MouseState& M)
{
    if (g_ModPage < 0 || g_ModPage > static_cast<int>(g_Mods.size()))
        g_ModPage = 0;

    bool back = (g_ModPage == 0) ? DrawModsMenu(W, H, M) : DrawModPage(W, H, M, g_Mods[g_ModPage - 1]);

    // Esc closes too (rising edge read through Windows).
    static bool wasEsc = false;
    const bool escDown = M.focus && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !wasEsc)
    {
        back = true;
    }
    wasEsc = escDown;

    // Guard: do not close right after opening or changing page.
    const bool justChanged = (GetTickCount64() - g_PanelOpenedAt) < 400;

    if (back && !justChanged)
    {
        if (g_ModPage != 0)
        {
            // From the settings: back to the mods menu.
            g_ModPage = 0;
            g_PanelOpenedAt = GetTickCount64();
        }
        else
        {
            // From the mods menu: back to the game's Options menu.
            g_PanelOpen = false;
            SetOptionsWindowHidden(false);
        }
    }
}

// ============================================================
// "M" button of the Options menu
// ============================================================

static void ModButtonRect(double W, double H, double& X1, double& Y1, double& X2, double& Y2)
{
    // Original spot, next to the HUD arrow (see the mock-up).
    const double size = 0.045 * H;
    const double cx = 0.839 * W;
    const double cy = 0.927 * H;
    X1 = cx - size * 0.5;
    Y1 = cy - size * 0.5;
    X2 = cx + size * 0.5;
    Y2 = cy + size * 0.5;
}

// The M is DRAWN by the Options menu script (SettingsMenuHook), in the same buffer as the
// window. Here we only handle hover and click, once per frame.
static void HandleModButtonClick(double W, double H, const MouseState& M)
{
    double x1, y1, x2, y2;
    ModButtonRect(W, H, x1, y1, x2, y2);

    const bool hover = Inside(M, x1, y1, x2, y2);
    if (hover)
        g_HoverThisCall = true;

    if (hover && M.pressed)
    {
        DetectExternalMods();
        g_PanelOpen = true;
        g_ModPage = 0;
        SetOptionsWindowHidden(true);
        g_PanelOpenedAt = GetTickCount64();
    }
}

// One of the game's diamonds with the letter M inside.
// Drawn in the Options window's buffer; the click is handled elsewhere.
static void DrawModButton(const MouseState& M, double W, double H)
{
    double x1, y1, x2, y2;
    ModButtonRect(W, H, x1, y1, x2, y2);

    const double cx = (x1 + x2) * 0.5;
    const double cy = (y1 + y2) * 0.5;
    const double size = y2 - y1;

    const bool hover = Inside(M, x1, y1, x2, y2);
    if (hover)
        g_HoverThisCall = true;

    const GmSprite& box = GetSprite("spr_radio_btn_v3");
    if (box.ok)
    {
        DrawSpriteCentered(box, hover ? 3 : 0, cx, cy, (size * 1.35) / box.w);
    }
    else
    {
        GmColor(hover ? 120 : 62, hover ? 45 : 22, hover ? 22 : 14);
        GmRect(x1, y1, x2, y2, false);
        GmColor(214, 90, 30);
        GmRect(x1, y1, x2, y2, true);
    }

    if (hover) GmColor(246, 206, 96); else GmColor(226, 112, 38);
    GmAlign(1, 1);
    GmText(cx, cy, "M", (H / 900.0) * UI_TEXT_SCALE);
    GmAlign(0, 0);
}

// ------------------------------------------------------------
// Sprite gallery (Debug=1, End key): shows the game's UI sprites with their names,
// so their look can be reused in our own windows. Page 0 = closed.
// ------------------------------------------------------------

static void HandleGalleryKey()
{
    static bool wasDown = false;

    const bool down = (GetAsyncKeyState(KEY_GALLERY) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;

    if (pressed && g_Debug && GameHasFocus())
        g_GalleryPage = (g_GalleryPage + 1) % (kGalleryPages + 1);
}

static void GalleryDrawSprite(RValue Sprite, int Frame, double X, double Y, double Scale)
{
    const double ox = ReadNumber(g_Yytk->CallBuiltin("sprite_get_xoffset", { Sprite }));
    const double oy = ReadNumber(g_Yytk->CallBuiltin("sprite_get_yoffset", { Sprite }));

    g_Yytk->CallBuiltin("draw_sprite_ext",
        { Sprite, Real(Frame), Real(X + ox * Scale), Real(Y + oy * Scale),
          Real(Scale), Real(Scale), Real(0.0), Real(16777215.0), Real(1.0) });
}

static void DrawSpriteGallery(double W, double H)
{
    // Solid background: sprites with a transparent background stay readable.
    GmAlpha(0.95);
    GmColor(34, 34, 42);
    GmRect(0, 0, W, H, false);
    GmAlpha(1.0);
    GmColor(255, 255, 255);
    GmAlign(0, 0);

    char title[96];
    std::snprintf(title, sizeof(title), "Sprite gallery - page %d / %d (End key = next page)",
        g_GalleryPage, kGalleryPages);
    GmText(20, 8, title, 0.7);

    if (g_GalleryPage == 1)
    {
        const double scale = 2.0;
        const double gap = 14.0;
        double x = 20.0, y = 50.0, rowH = 0.0;

        for (int i = 0; i < kGallerySmallCount; ++i)
        {
            RValue spr = g_Yytk->CallBuiltin("asset_get_index", { kGallerySmall[i] });
            const double w = ReadNumber(g_Yytk->CallBuiltin("sprite_get_width", { spr }));
            const double h = ReadNumber(g_Yytk->CallBuiltin("sprite_get_height", { spr }));
            const int frames = static_cast<int>(ReadNumber(g_Yytk->CallBuiltin("sprite_get_number", { spr })));
            if (w <= 0.0 || frames < 1)
                continue;

            const int shown = (frames > 6) ? 6 : frames;
            const double blockW = shown * (w * scale + gap);
            if (x + blockW > W - 20.0 && x > 20.0)
            {
                x = 20.0;
                y += rowH + 36.0;
                rowH = 0.0;
            }

            char label[96];
            std::snprintf(label, sizeof(label), "%s  %.0fx%.0f  (%d img)", kGallerySmall[i], w, h, frames);
            GmColor(255, 220, 140);
            GmText(x, y, label, 0.5);

            double fx = x;
            for (int f = 0; f < shown; ++f)
            {
                GalleryDrawSprite(spr, f, fx, y + 18.0, scale);
                fx += w * scale + gap;
            }

            if (h * scale > rowH) rowH = h * scale;
            x += blockW + gap;
        }
    }
    else
    {
        const int bigIndex = g_GalleryPage - 2;
        if (bigIndex >= 0 && bigIndex < kGalleryBigCount)
        {
            RValue spr = g_Yytk->CallBuiltin("asset_get_index", { kGalleryBig[bigIndex] });
            const double w = ReadNumber(g_Yytk->CallBuiltin("sprite_get_width", { spr }));
            const double h = ReadNumber(g_Yytk->CallBuiltin("sprite_get_height", { spr }));

            if (w > 0.0 && h > 0.0)
            {
                double scale = ((W - 40.0) / w < (H - 90.0) / h) ? (W - 40.0) / w : (H - 90.0) / h;
                if (scale > 2.0) scale = 2.0;

                char label[96];
                std::snprintf(label, sizeof(label), "%s  %.0fx%.0f  (shown at x%.2f)", kGalleryBig[bigIndex], w, h, scale);
                GmColor(255, 220, 140);
                GmText(20, 34, label, 0.6);

                GalleryDrawSprite(spr, 0, 20.0, 70.0, scale);
            }
        }
    }

    GmResetState();
}

// ============================================================
// Options menu state and hooks
//
// - SettingsMenuHook: draws the M in the same buffer as the Options window.
// - HudDrawHook (every frame): reads the mouse, handles the click on the M and draws our pages.
// - MenusManagerHook: suspends the game's menu handling while our page is open,
//   so our clicks do not reach the original menu.
// - While our page is open, the Options window is moved off screen.
// ============================================================

// Switches the game cursor to "hover" mode (spr_cursor_hover) when one of our buttons
// is hovered, like the game's own buttons. Only acts when the state changes.
static void ApplyCursorHover()
{
    if (g_HoverThisCall == g_CursorIsHover)
        return;

    g_CursorIsHover = g_HoverThisCall;

    RValue cursorObj = g_Yytk->CallBuiltin("asset_get_index", { "obj_cursor" });
    RValue count = g_Yytk->CallBuiltin("instance_number", { cursorObj });
    if (ReadNumber(count) < 1.0)
        return;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { cursorObj, Real(0) });
    RValue spr = g_Yytk->CallBuiltin("asset_get_index",
        { g_CursorIsHover ? "spr_cursor_hover" : "spr_cursor" });
    g_Yytk->CallBuiltin("variable_instance_set", { inst, "sprite_index", spr });
}

// Reads the menu state from obj_ui_next_gen: menu_show is "settings" when Options is shown,
// and menu_y_pos goes from -1440 (hidden) to 0 (fully shown) during the animation.
static void RefreshMenuState()
{
    g_OptionsOpen = false;

    RValue obj = g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" });
    RValue count = g_Yytk->CallBuiltin("instance_number", { obj });
    if (ReadNumber(count) < 1.0)
        return;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { obj, Real(0) });

    RValue show = g_Yytk->CallBuiltin("variable_instance_get", { inst, "menu_show" });
    const std::string showStr(show.ToString());

    RValue y = g_Yytk->CallBuiltin("variable_instance_get", { inst, "menu_y_pos" });
    const double yPos = ReadNumber(y);

    // Menu fully closed: remember its "closed" values.
    if (showStr != "settings" && !g_PanelOpen && yPos <= -1400.0)
    {
        g_ClosedMenuY = yPos;
        g_ClosedPanelY = ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { inst, "panel_y_pos" }));
        g_HaveClosedValues = true;
    }

    // While our page is open, the window is deliberately off screen.
    g_OptionsOpen = (showStr == "settings") && (g_PanelOpen || yPos > -20.0);
}

// The M is drawn by the Options menu script, in the same buffer as the window.
RValue& SettingsMenuHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    RValue& Ret = g_OriginalSettings(Self, Other, Result, ArgumentCount, Arguments);

    if (!g_PanelOpen)
    {
        const double W = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
        const double H = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_height", {}));

        MouseState m = g_LastMouse;     // hover state saved by the per-frame hook
        m.pressed = false;              // the click is handled elsewhere

        DrawModButton(m, W, H);
        GmResetState();
    }

    return Ret;
}

// The game's menu handling (Step, once per frame). While our page is open,
// we do not call it: clicks on our buttons never reach the original menu.
RValue& MenusManagerHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    if (g_PanelOpen)
        return Result;

    return g_OriginalManager(Self, Other, Result, ArgumentCount, Arguments);
}

// Once per frame: mod callbacks, menu state, mouse, pages.
static void RunOverlay()
{
    for (const RegisteredMod& mod : g_Mods)
    {
        if (mod.OnFrame)
            mod.OnFrame(mod.user);
    }

    HandleGalleryKey();

    if (g_Debug && g_GalleryPage > 0)
    {
        const double gw = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
        const double gh = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_height", {}));
        DrawSpriteGallery(gw, gh);
    }

    RefreshMenuState();

    if (!g_OptionsOpen)
    {
        // Options closed without going through our close button: do not leave the menu
        // stuck in the "hidden by the mod" state.
        if (g_PanelOpen && g_HaveClosedValues)
            RestoreClosedMenuState();

        g_PanelOpen = false;
        g_HoverThisCall = false;
        ApplyCursorHover();
        return;
    }

    const double W = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
    const double H = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_height", {}));
    const MouseState mouse = ReadMouse(W, H);

    g_HoverThisCall = false;
    g_LastMouse = mouse;

    if (g_PanelOpen)
        DrawModPanel(W, H, mouse);
    else
        HandleModButtonClick(W, H, mouse);

    GmResetState();
    ApplyCursorHover();
}

// UI drawing (Draw GUI, once per frame).
RValue& HudDrawHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    RValue& Ret = g_OriginalHud(Self, Other, Result, ArgumentCount, Arguments);
    RunOverlay();
    return Ret;
}

// ============================================================
// Module initialization
// ============================================================

// The interface is created here, before the other mods' ModuleInitialize.
EXPORTED AurieStatus ModulePreinitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
)
{
    UNREFERENCED_PARAMETER(ModulePath);

    return ObCreateInterface(Module, &g_Interface, ModMenu::kInterfaceName);
}

EXPORTED AurieStatus ModuleInitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
)
{
    UNREFERENCED_PARAMETER(ModulePath);

    g_Yytk = YYTK::GetInterface();

    if (!g_Yytk)
    {
        OutputDebugStringA("[ModMenu] ERROR: YYToolkit interface not found\n");
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    }

    // --- Optional setting: ModMenu.ini next to the DLL ---
    HMODULE hMod = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ModuleInitialize),
        &hMod
    );

    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(hMod, dllPath, MAX_PATH);

    std::string iniPath(dllPath);
    const size_t slash = iniPath.find_last_of("\\/");
    const std::string folder = (slash == std::string::npos) ? std::string() : iniPath.substr(0, slash + 1);
    strncpy_s(g_ModsFolder, folder.c_str(), _TRUNCATE);
    iniPath = folder + "ModMenu.ini";

    strncpy_s(g_IniPath, iniPath.c_str(), _TRUNCATE);

    g_Debug = GetPrivateProfileIntA("ModMenu", "Debug", 0, g_IniPath) != 0;

    char language[8] = {};
    GetPrivateProfileStringA("ModMenu", "Language", "en", language, sizeof(language), g_IniPath);
    g_Language = (_stricmp(language, "fr") == 0) ? ModMenu::kLangFrench : ModMenu::kLangEnglish;

    // --- Installs a hook on a GameMaker script ---
    auto installScriptHook = [&](const char* scriptName, const char* hookName,
        PVOID hookFunction, PFUNC_YYGMLScript& original) -> bool
        {
            CScript* script = nullptr;
            AurieStatus st = g_Yytk->GetNamedRoutinePointer(scriptName, reinterpret_cast<PVOID*>(&script));

            if (!AurieSuccess(st) || !script || !script->m_Functions || !script->m_Functions->m_ScriptFunction)
                return false;

            st = MmCreateHook(
                Module,
                hookName,
                reinterpret_cast<PVOID>(script->m_Functions->m_ScriptFunction),
                hookFunction,
                nullptr
            );

            if (!AurieSuccess(st))
                return false;

            original = reinterpret_cast<PFUNC_YYGMLScript>(MmGetHookTrampoline(Module, hookName));
            return original != nullptr;
        };

    // The menu needs all three hooks: if one is missing, remove the others.
    const bool okSettings = installScriptHook("gml_Script_scr_ui_menus_settings", "SettingsMenu",
        reinterpret_cast<PVOID>(SettingsMenuHook), g_OriginalSettings);
    const bool okManager = installScriptHook("gml_Script_scr_ui_menus_manager", "MenusManager",
        reinterpret_cast<PVOID>(MenusManagerHook), g_OriginalManager);
    const bool okHud = installScriptHook("gml_Script_scr_ui_draw_gold_essence", "HudDraw",
        reinterpret_cast<PVOID>(HudDrawHook), g_OriginalHud);

    if (!(okSettings && okManager && okHud))
    {
        MmRemoveHook(Module, "SettingsMenu");
        MmRemoveHook(Module, "MenusManager");
        MmRemoveHook(Module, "HudDraw");

        DbgPrintEx(LOG_SEVERITY_ERROR, "[ModMenu] could not install the Options menu hooks");
        return AURIE_OBJECT_NOT_FOUND;
    }

    DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] ready: M button of the Options menu enabled");

    return AURIE_SUCCESS;
}
