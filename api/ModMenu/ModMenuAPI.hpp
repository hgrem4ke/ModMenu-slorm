// ============================================================
// Mods menu API for The Slormancer (ModMenu.dll)
//
// ModMenu.dll adds the "M" button to the game's Options menu and shows one tile per
// registered mod. A mod adds itself from its ModuleInitialize:
//
//   AurieInterfaceBase* base = nullptr;
//   if (AurieSuccess(ObGetInterface(ModMenu::kInterfaceName, base)) && base)
//       static_cast<ModMenu::IModMenu*>(base)->RegisterMod(entry);
//
// If ModMenu.dll is not installed, ObGetInterface fails: the mod must carry on without a menu.
// ============================================================

#pragma once
#include <Aurie/shared.hpp>

namespace ModMenu
{
    static constexpr const char* kInterfaceName = "SlormModMenu";

    struct IModMenu;

    // Describes a mod. The strings are copied by RegisterMod.
    struct ModEntry
    {
        const char* Name;       // tile text in the MODS menu (e.g. "AUTOLOOT")
        const char* Title;      // settings page title (e.g. "MOD  -  AUTOLOOT")
        const char* Help;       // help line at the bottom of the page (optional)

        // Status shown in the tile: ACTIVE / INACTIVE. nullptr = no status.
        bool (*IsActive)(void* User);

        // Draws the settings page, once per frame while it is open.
        // Menu->Toggle / Menu->Selector are only usable during this call.
        void (*DrawPage)(IModMenu* Menu, void* User);

        // Called once per frame, whether the menu is open or not (optional).
        void (*OnFrame)(void* User);

        void* User;             // passed back as-is to the functions above
    };

    // The settings page is a grid: 2 columns (0 = left, 1 = right) x 3 rows (0 to 2).
    struct IModMenu : Aurie::AurieInterfaceBase
    {
        // Adds a mod to the menu (or updates the one that already has this name).
        virtual bool RegisterMod(const ModEntry& Entry) = 0;

        // Diamond checkbox with its label. Returns true when clicked.
        virtual bool Toggle(int Column, int Row, const char* Label, bool On) = 0;

        // Label + selector  <  value  >. Returns -1 (left arrow), +1 (right arrow) or 0.
        virtual int Selector(int Column, int Row, const char* Label, const char* Value) = 0;
    };
}
