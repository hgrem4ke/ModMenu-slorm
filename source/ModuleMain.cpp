// ============================================================
// ModMenu pour The Slormancer (Aurie + YYToolkit)
//
// Ajoute un bouton "M" dans le menu Options du jeu. Il ouvre un menu MODS avec un encadre par
// mod enregistre ; un clic sur un encadre ouvre la page de reglages de ce mod.
//
// Les mods s'enregistrent via l'interface Aurie "SlormModMenu" (voir api/ModMenu/ModMenuAPI.hpp).
// L'interface est creee des ModulePreinitialize : Aurie charge donc ce module avant les mods
// qui s'y enregistrent dans leur ModuleInitialize.
//
// Reglage facultatif, dans ModMenu.ini a cote de la DLL :
//   [ModMenu]
//   Debug=1     logs du menu + galerie des sprites d'interface (touche Fin)
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
// Constantes
// ============================================================

static constexpr int KEY_GALLERY = VK_END;    // Debug : galerie des sprites d'interface du jeu (pages successives)

// Taille du texte du bouton M (multiplie par la hauteur de l'ecran / 900).
static constexpr double UI_TEXT_SCALE = 1.0;

// Nombre maximum de mods affiches dans le menu MODS.
static constexpr size_t kMaxMods = 6;

// Grille de la page de reglages, en coordonnees de la fenetre d'origine (850 x 419).
static constexpr double kPageColumnX[] = { 75.0, 468.0 };       // centre des cases, par colonne
static constexpr double kPageRowY[] = { 138.0, 228.0, 318.0 };

// ============================================================
// Globales
// ============================================================

static YYTKInterface* g_Yytk = nullptr;
static bool g_Debug = false;

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

// Galerie de sprites (Debug)
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
static constexpr int kGalleryPages = 1 + kGalleryBigCount;      // page 1 = petits sprites, pages 2.. = grands

// Etat de l'interface
static bool g_PanelOpen = false;             // notre fenetre (menu des mods ou reglages) est ouverte
static int  g_ModPage = 0;                   // 0 = menu des mods, N = reglages du mod numero N - 1
static ULONGLONG g_PanelOpenedAt = 0;        // moment de son ouverture
static bool g_OptionsOpen = false;           // le menu Options du jeu est completement affiche
static bool g_HoverThisCall = false;         // un de nos boutons est survole
static bool g_CursorIsHover = false;         // le curseur du jeu est en mode "survol"

// ============================================================
// Utilitaires GameMaker
// ============================================================

// Convertit une valeur GameMaker numerique en double (via sa representation texte).
static double ReadNumber(RValue Value)
{
    std::string s(Value.ToString());
    return std::atof(s.c_str());
}

// Vrai si la valeur GameMaker est "vraie" (true, 1, ou tout nombre non nul).
static bool IsTrue(RValue Value)
{
    std::string s(Value.ToString());
    if (s == "true" || s == "True") return true;
    return std::atof(s.c_str()) != 0.0;
}

// Vrai si la fenetre au premier plan appartient au jeu (evite de reagir aux touches
// tapees dans une autre application).
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
// Souris et dessin
// ============================================================

struct MouseState
{
    double x = 0.0;           // position selon GameMaker (coordonnees GUI)
    double y = 0.0;
    double wx = 0.0;          // position selon Windows, convertie en coordonnees GUI
    double wy = 0.0;
    bool hasWin = false;      // les coordonnees Windows sont valides
    bool pressed = false;     // clic gauche tout juste enfonce
    bool down = false;        // bouton gauche actuellement maintenu
    bool focus = false;       // la fenetre du jeu est au premier plan
};

static MouseState g_LastMouse;            // derniere position de souris connue (survol du M)

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

static void GmAlign(int H, int V)   // 0 = gauche/haut, 1 = centre, 2 = droite/bas
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

// Fenetre du jeu (titre "The Slormancer"), sinon la fenetre au premier plan du processus.
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

    // Position selon GameMaker.
    m.x = ReadNumber(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { Real(0) }));
    m.y = ReadNumber(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { Real(0) }));

    HWND hwnd = GetGameWindow();
    if (hwnd)
    {
        // Position selon Windows, ramenee aux coordonnees GUI.
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

        // Clic gauche : front montant, uniquement si la fenetre du jeu est au premier plan.
        static bool wasDown = false;
        m.focus = (GetForegroundWindow() == hwnd);
        const bool down = m.focus && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        m.down = down;
        m.pressed = down && !wasDown;
        wasDown = down;
    }
    else
    {
        // Pas de fenetre trouvee : on se rabat sur l'entree GameMaker.
        m.pressed = IsTrue(g_Yytk->CallBuiltin("mouse_check_button_pressed", { Real(1) }));  // 1 = mb_left
    }

    return m;
}

// ------------------------------------------------------------
// Sprites du jeu : on reutilise les elements de la fenetre Options pour que nos pages aient le meme
// style (cadre, cases en losange, fleches de selection, bouton de fermeture).
// ------------------------------------------------------------

struct GmSprite
{
    bool   ok = false;
    double index = -1.0;
    double w = 0.0;     // taille d'origine en pixels
    double h = 0.0;
    double xo = 0.0;     // point d'origine du sprite
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

// Dessine un sprite dont le coin haut-gauche est en (X, Y).
static void DrawSpriteTL(const GmSprite& Sp, int Frame, double X, double Y, double XScale, double YScale,
    double Color = 16777215.0)
{
    if (!Sp.ok)
        return;

    g_Yytk->CallBuiltin("draw_sprite_ext",
        { Real(Sp.index), Real(Frame), Real(X + Sp.xo * XScale), Real(Y + Sp.yo * YScale),
          Real(XScale), Real(YScale), Real(0.0), Real(Color), Real(1.0) });
}

// Dessine un sprite centre sur (Cx, Cy).
static void DrawSpriteCentered(const GmSprite& Sp, int Frame, double Cx, double Cy, double Scale)
{
    DrawSpriteTL(Sp, Frame, Cx - Sp.w * 0.5 * Scale, Cy - Sp.h * 0.5 * Scale, Scale, Scale);
}

static double TextWidth(const char* Text)
{
    return ReadNumber(g_Yytk->CallBuiltin("string_width", { Text }));
}

// ============================================================
// Elements d'interface
// ============================================================

// Taille de la coche orange par rapport au losange (a ajuster au gout).
static constexpr double kToggleCrossScale = 1.6;

// Case en losange + libelle, comme les options du jeu. Renvoie vrai au clic.
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

// Fleche de selection (triangle rouge). Renvoie vrai au clic.
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

// Bouton de fermeture (croix rouge du jeu). Renvoie vrai au clic.
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

// Un encadre cliquable (barre doree du jeu avec fleche) : libelle a gauche, etat a droite.
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
        // Un peu assombri au repos, pleine luminosite au survol.
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
    GmAlign(2, 1);                                  // aligne a droite, avant la fleche doree
    GmText(X + 0.80 * w, Y + h * 0.5, Status, TextScale * 0.9);
    GmAlign(0, 0);

    return hover && M.pressed;
}

// ============================================================
// Fenetre commune au menu MODS et aux pages de reglages : meme cadre que les menus du jeu
// (bandeau de titre + corps), centree a l'ecran.
// ============================================================

struct WindowContext
{
    MouseState mouse;
    double wx = 0.0;          // coin haut-gauche de la fenetre a l'ecran
    double wy = 0.0;
    double ws = 1.0;          // echelle de la fenetre
    double ts = 1.0;          // echelle du texte
    double nw = 850.0;        // dimensions d'origine du cadre (850 x 419)
    double nh = 419.0;
    bool   inModPage = false; // un mod est en train de dessiner sa page
};

static WindowContext g_Win;

// Conversion des coordonnees de la fenetre d'origine vers l'ecran.
static double WinX(double Nx) { return g_Win.wx + Nx * g_Win.ws; }
static double WinY(double Ny) { return g_Win.wy + Ny * g_Win.ws; }

// Assombrit le jeu, dessine le cadre et renseigne g_Win. WidthRatio = largeur de la fenetre
// par rapport a celle de l'ecran. L'echelle du texte (g_Win.ts) est a fixer par l'appelant.
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

// Titre dans le bandeau.
static void DrawWindowTitle(const char* Title)
{
    GmColor(255, 255, 255);
    GmAlign(1, 1);
    GmText(WinX(g_Win.nw * 0.5), WinY(43.0), Title, g_Win.ts * 1.15);
    GmAlign(0, 0);
}

// Ligne d'aide, en bas de la fenetre.
static void DrawWindowHelp(const char* Help)
{
    GmColor(150, 140, 126);
    GmAlign(1, 1);
    GmText(WinX(g_Win.nw * 0.5), WinY(384.0), Help, g_Win.ts * 0.62);
    GmAlign(0, 0);
}

// Croix du jeu dans le bandeau (Echap est gere par DrawModPanel). Renvoie vrai au clic.
static bool DrawWindowCloseButton()
{
    return UiCloseButton(g_Win.mouse, WinX(g_Win.nw - 44.0), WinY(43.0), g_Win.ws * 0.75);
}

// ============================================================
// Interface Aurie offerte aux mods
// ============================================================

struct ModMenuInterface final : ModMenu::IModMenu
{
    AurieStatus Create() override { return AURIE_SUCCESS; }
    void Destroy() override {}

    void QueryVersion(OUT short& Major, OUT short& Minor, OUT short& Patch) override
    {
        Major = 1;
        Minor = 0;
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
};

static ModMenuInterface g_Interface;

// ============================================================
// Mods externes : des mods qui ne connaissent pas ce menu. On leur ajoute un encadre avec une
// page d'explication, sans toucher a leur code.
//
// SlormReforger (https://github.com/wistb/SlormReforger) a sa propre fenetre de reglages ;
// sa touche est lue dans SlormReforger.txt, a cote de sa DLL.
// ============================================================

static char g_ModsFolder[MAX_PATH] = {};      // dossier de notre DLL (mods\Aurie\), avec le \ final
static char g_ReforgerKeyText[48] = "Touche : F6";

// Une ligne de texte dans la page d'un mod, en coordonnees de la fenetre d'origine.
static void PageText(double Nx, double Ny, const char* Text, int R, int G, int B)
{
    GmColor(R, G, B);
    GmAlign(0, 1);
    GmText(WinX(Nx), WinY(Ny), Text, g_Win.ts * 0.8);
    GmAlign(0, 0);
}

static void ReforgerReadKey()
{
    int key = VK_F6;                          // touche par defaut de SlormReforger

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

    if (key >= VK_F1 && key <= VK_F24)
        std::snprintf(g_ReforgerKeyText, sizeof(g_ReforgerKeyText), "Touche : F%d", key - VK_F1 + 1);
    else
        std::snprintf(g_ReforgerKeyText, sizeof(g_ReforgerKeyText), "Touche : code %d", key);
}

static void ReforgerDrawPage(ModMenu::IModMenu*, void*)
{
    const double left = kPageColumnX[0] - 28.0;
    const double right = kPageColumnX[1] - 28.0;

    // --- Colonne gauche : comment s'en servir ---
    PageText(left, 138.0, "Reforge automatique chez Friedrich", 255, 226, 140);
    PageText(left, 198.0, "1. Parle a Friedrich, le forgeron", 255, 255, 255);
    PageText(left, 243.0, "2. Ouvre la reforge d'equipement", 255, 255, 255);
    PageText(left, 288.0, "3. Place un objet dans l'emplacement", 255, 255, 255);
    PageText(left, 333.0, "La fenetre SlormReforger apparait", 200, 200, 200);

    // --- Colonne droite : touche et reglages ---
    PageText(right, 138.0, g_ReforgerKeyText, 255, 226, 140);
    PageText(right, 183.0, "Affiche ou cache sa fenetre", 200, 200, 200);
    PageText(right, 258.0, "Reglages", 255, 226, 140);
    PageText(right, 303.0, "Onglet Options de sa fenetre", 200, 200, 200);
}

// Ajoute au menu les mods externes charges dans le jeu. Appele a chaque ouverture du menu :
// ces mods peuvent etre charges apres nous.
static void DetectExternalMods()
{
    static bool reforgerAdded = false;

    if (!GetModuleHandleA("SlormReforger.dll"))
        return;

    ReforgerReadKey();

    if (reforgerAdded)
        return;

    ModMenu::ModEntry entry = {};
    entry.Name = "SLORMREFORGER";
    entry.Title = "MOD  -  SLORMREFORGER";
    entry.Help = "Mod de wistb  -  ses reglages se font dans sa propre fenetre  -  Echap : retour";
    entry.DrawPage = ReforgerDrawPage;

    reforgerAdded = g_Interface.RegisterMod(entry);
}

// ============================================================
// Pages : menu MODS et reglages d'un mod
// ============================================================

// Page 0 : menu des mods (un encadre par mod). Renvoie vrai si le joueur demande a fermer le menu.
static bool DrawModsMenu(double W, double H, const MouseState& M)
{
    BeginWindow(W, H, 0.40, M);
    g_Win.ts = (H / 1440.0) * 1.05;          // meme taille de texte que les pages de reglages

    DrawWindowTitle("MODS");

    const int count = static_cast<int>(g_Mods.size());

    if (count == 0)
    {
        GmColor(200, 200, 200);
        GmAlign(1, 1);
        GmText(WinX(g_Win.nw * 0.5), WinY(215.0), "Aucun mod installe", g_Win.ts);
        GmAlign(0, 0);
    }

    // Un ou deux mods : grands encadres l'un sous l'autre. Au-dela : grille de 2 colonnes.
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
            status = active ? "ACTIF" : "INACTIF";
            sr = active ? 130 : 214;
            sg = active ? 205 : 96;
            sb = active ? 120 : 72;
        }

        // Un clic ouvre les reglages du mod.
        if (UiTile(M, WinX(nx), WinY(ny), g_Win.ws * tileScale, tileText, mod.name.c_str(), status, sr, sg, sb))
        {
            g_ModPage = i + 1;
            g_PanelOpenedAt = GetTickCount64();
        }
    }

    DrawWindowHelp("Clique sur un mod pour ouvrir ses reglages  -  Echap : retour");

    return DrawWindowCloseButton();
}

// Page de reglages d'un mod : le cadre est le notre, le contenu est dessine par le mod.
// Renvoie vrai si le joueur demande a fermer la page.
static bool DrawModPage(double W, double H, const MouseState& M, const RegisteredMod& Mod)
{
    BeginWindow(W, H, 0.62, M);
    g_Win.ts = g_Win.ws * 0.55;

    DrawWindowTitle(Mod.title.c_str());

    // Trait de separation central.
    DrawSpriteTL(GetSprite("spr_settings_separator"), 0, WinX(403.0), WinY(98.0),
        g_Win.ws, g_Win.ws * (265.0 / 531.0));

    g_Win.inModPage = true;
    Mod.DrawPage(&g_Interface, Mod.user);
    g_Win.inModPage = false;

    if (!Mod.help.empty())
        DrawWindowHelp(Mod.help.c_str());

    return DrawWindowCloseButton();
}

// Valeurs d'origine du menu Options, memorisees pour etre restaurees telles quelles.
static double g_SavedMenuY = 0.0;
static double g_SavedPanelY = 1100.0;

// Valeurs du menu quand il est completement ferme (lues tant qu'Options est ferme). Servent a
// remettre le menu en ordre si le jeu le ferme alors que notre page est ouverte.
static double g_ClosedMenuY = -1440.0;
static double g_ClosedPanelY = -100.0;
static bool   g_HaveClosedValues = false;

// Ecarte le menu Options de l'ecran pendant que notre page est ouverte, puis le remet en place :
//  - menu_y_pos  : la fenetre elle-meme (0 = affichee, -1440 = cachee)
//  - panel_y_pos : le bandeau (1100 = affiche, -1500 = hors ecran)
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
            DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] page ouverte : menu_y_pos=%.0f panel_y_pos=%.0f (valeurs lues)",
                g_SavedMenuY, g_SavedPanelY);

        // Si on les lit deja "cachees", on garde les valeurs d'un menu ouvert.
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
            DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] page fermee : menu_y_pos=%.0f panel_y_pos=%.0f (valeurs restaurees)",
                g_SavedMenuY, g_SavedPanelY);
    }
}

// Le jeu a ferme Options alors que notre page etait ouverte : on remet le menu dans son etat
// "ferme" (et non dans l'etat "ouvert" memorise a l'ouverture de la page).
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
        DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] menu ferme par le jeu pendant la page : menu_y_pos=%.0f panel_y_pos=%.0f remis en etat ferme",
            g_ClosedMenuY, g_ClosedPanelY);
}

// Fenetre des mods : aiguille vers le menu des mods ou les reglages, et gere la fermeture.
static void DrawModPanel(double W, double H, const MouseState& M)
{
    if (g_ModPage < 0 || g_ModPage > static_cast<int>(g_Mods.size()))
        g_ModPage = 0;

    bool back = (g_ModPage == 0) ? DrawModsMenu(W, H, M) : DrawModPage(W, H, M, g_Mods[g_ModPage - 1]);

    // Echap ferme aussi (front montant lu via Windows).
    static bool wasEsc = false;
    const bool escDown = M.focus && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !wasEsc)
    {
        back = true;
    }
    wasEsc = escDown;

    // Garde : on ne ferme pas juste apres une ouverture ou un changement de page.
    const bool justChanged = (GetTickCount64() - g_PanelOpenedAt) < 400;

    if (back && !justChanged)
    {
        if (g_ModPage != 0)
        {
            // Des reglages : retour au menu des mods.
            g_ModPage = 0;
            g_PanelOpenedAt = GetTickCount64();
        }
        else
        {
            // Du menu des mods : retour au menu Options du jeu.
            g_PanelOpen = false;
            SetOptionsWindowHidden(false);
        }
    }
}

// ============================================================
// Bouton "M" du menu Options
// ============================================================

static void ModButtonRect(double W, double H, double& X1, double& Y1, double& X2, double& Y2)
{
    // Emplacement d'origine, a cote de la fleche du HUD (voir la maquette).
    const double size = 0.045 * H;
    const double cx = 0.839 * W;
    const double cy = 0.927 * H;
    X1 = cx - size * 0.5;
    Y1 = cy - size * 0.5;
    X2 = cx + size * 0.5;
    Y2 = cy + size * 0.5;
}

// Le M est DESSINE par le script du menu Options (SettingsMenuHook), dans le meme tampon que la
// fenetre. Ici on ne gere que le survol et le clic, une fois par image.
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

// Un losange du jeu avec la lettre M dedans.
// Dessine dans le tampon de la fenetre Options ; le clic est gere ailleurs.
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
// Galerie de sprites (Debug=1, touche Fin) : affiche les sprites d'interface du jeu avec leur nom,
// pour pouvoir copier leur apparence dans nos propres fenetres. Page 0 = fermee.
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
    // Fond uni : les sprites a fond transparent restent lisibles.
    GmAlpha(0.95);
    GmColor(34, 34, 42);
    GmRect(0, 0, W, H, false);
    GmAlpha(1.0);
    GmColor(255, 255, 255);
    GmAlign(0, 0);

    char title[96];
    std::snprintf(title, sizeof(title), "Galerie de sprites - page %d / %d (touche Fin = page suivante)",
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
                std::snprintf(label, sizeof(label), "%s  %.0fx%.0f  (affiche a x%.2f)", kGalleryBig[bigIndex], w, h, scale);
                GmColor(255, 220, 140);
                GmText(20, 34, label, 0.6);

                GalleryDrawSprite(spr, 0, 20.0, 70.0, scale);
            }
        }
    }

    GmResetState();
}

// ============================================================
// Etat du menu Options et hooks
//
// - SettingsMenuHook : dessine le M dans le meme tampon que la fenetre Options.
// - HudDrawHook (chaque image) : lit la souris, gere le clic sur le M et dessine nos pages.
// - MenusManagerHook : suspend la gestion des menus du jeu tant que notre page est ouverte,
//   pour que nos clics ne touchent pas le menu d'origine.
// - Pendant que notre page est ouverte, la fenetre Options est ecartee de l'ecran.
// ============================================================

// Fait passer le curseur du jeu en mode "survol" (spr_cursor_hover) quand un de nos boutons
// est survole, comme pour les boutons du jeu. Ne fait quelque chose qu'au changement d'etat.
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

// Lit l'etat du menu dans obj_ui_next_gen : menu_show vaut "settings" quand Options est affiche,
// et menu_y_pos passe de -1440 (cache) a 0 (completement affiche) pendant l'animation.
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

    // Menu completement ferme : on memorise ses valeurs "fermees".
    if (showStr != "settings" && !g_PanelOpen && yPos <= -1400.0)
    {
        g_ClosedMenuY = yPos;
        g_ClosedPanelY = ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { inst, "panel_y_pos" }));
        g_HaveClosedValues = true;
    }

    // Pendant que notre page est ouverte, la fenetre est volontairement hors ecran.
    g_OptionsOpen = (showStr == "settings") && (g_PanelOpen || yPos > -20.0);
}

// Le M est dessine par le script du menu Options, dans le meme tampon que la fenetre.
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

        MouseState m = g_LastMouse;     // survol memorise par le hook de chaque image
        m.pressed = false;              // le clic est gere ailleurs

        DrawModButton(m, W, H);
        GmResetState();
    }

    return Ret;
}

// Gestion des menus du jeu (Step, une fois par image). Quand notre page est ouverte,
// on ne l'appelle pas : les clics sur nos boutons n'atteignent pas le menu d'origine.
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

// Une fois par image : suivi des mods, etat du menu, souris, pages.
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
        // Options s'est ferme sans passer par notre bouton Retour : on ne laisse pas le menu
        // coince dans l'etat "cache par le mod".
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

// Dessin de l'interface (Draw GUI, une fois par image).
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
// Initialisation du module
// ============================================================

// L'interface est creee ici, avant le ModuleInitialize des autres mods.
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
        OutputDebugStringA("[ModMenu] ERREUR: interface YYToolkit introuvable\n");
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    }

    // --- Reglage facultatif : ModMenu.ini a cote de la DLL ---
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

    g_Debug = GetPrivateProfileIntA("ModMenu", "Debug", 0, iniPath.c_str()) != 0;

    // --- Installe un hook sur un script GameMaker ---
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

    // Le menu a besoin des trois hooks : s'il en manque un, on retire les autres.
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

        DbgPrintEx(LOG_SEVERITY_ERROR, "[ModMenu] impossible d'installer les hooks du menu Options");
        return AURIE_OBJECT_NOT_FOUND;
    }

    DbgPrintEx(LOG_SEVERITY_INFO, "[ModMenu] pret : bouton M du menu Options actif");

    return AURIE_SUCCESS;
}
