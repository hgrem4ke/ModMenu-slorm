
# ModMenu for The Slormancer

Adds an **M** button to the game's Options menu. It opens a **MODS** menu with one tile per
installed mod; clicking a tile opens that mod's settings page, styled like the game's own menus.

ModMenu does nothing on its own: it is a shared menu for the mods that register with it.

https://jumpshare.com/s/g45QLO3nvuRfN3XOVPJb

Known mods:

- [AutoLoot](https://github.com/hgrem4ke/Autoloot-slorm): registers itself.
- [SlormReforger](https://github.com/wistb/SlormReforger): detected automatically, with an
  information page (its settings stay in its own window).

## Installation

You need [Aurie Framework](https://github.com/AurieFramework/Aurie) and
[YYToolkit](https://github.com/AurieFramework/YYToolkit) **v5.0.0c** already installed in the game.

Copy `x64/Release/ModMenu.dll` into `The Slormancer\mods\Aurie\`.

## Language

The menu is in English by default. Click **EN / FR** at the top left of the MODS menu to switch to
French; the choice is saved. Mods that support it (AutoLoot does) follow the same language.

## Settings

The `ModMenu.ini` file next to the DLL holds two optional settings:

    [ModMenu]
    Language=en
    Debug=0

`Language` is `en` or `fr`. `Debug=1` prints detailed messages and enables a gallery of the game's
UI sprites (End key).

## Adding your mod to the menu

Copy [api/ModMenu/ModMenuAPI.hpp](api/ModMenu/ModMenuAPI.hpp) into your project, then register
from your `ModuleInitialize`:

```cpp
#include <ModMenu/ModMenuAPI.hpp>

static bool g_Enabled = true;

static bool IsActive(void*) { return g_Enabled; }

static void DrawPage(ModMenu::IModMenu* Menu, void*)
{
    // Grid of 2 columns (0 = left, 1 = right) x 3 rows (0 to 2).
    if (Menu->Toggle(0, 0, "My mod enabled", g_Enabled))
        g_Enabled = !g_Enabled;
}

static void RegisterInModMenu()
{
    Aurie::AurieInterfaceBase* base = nullptr;
    if (!Aurie::AurieSuccess(Aurie::ObGetInterface(ModMenu::kInterfaceName, base)) || !base)
        return;                         // ModMenu.dll is not installed: carry on without a menu

    ModMenu::ModEntry entry = {};
    entry.Name = "MY MOD";              // tile text
    entry.Title = "MOD  -  MY MOD";     // page title
    entry.IsActive = IsActive;          // shows ACTIVE / INACTIVE in the tile (optional)
    entry.DrawPage = DrawPage;

    static_cast<ModMenu::IModMenu*>(base)->RegisterMod(entry);
}
```

Widgets available in a page: `Toggle` (checkbox) and `Selector` (value with left and right
arrows). The menu shows up to 6 mods.

To translate your page, call `Menu->GetLanguage()` in `DrawPage` (it returns `kLangEnglish` or
`kLangFrench`), and `Menu->HelpLine(text)` to set the help line at the bottom. Both were added in
interface version 1.1, so check `QueryVersion` first if your mod may run with an older ModMenu.

ModMenu creates its interface in `ModulePreinitialize`, so Aurie loads it before the mods that
register in their `ModuleInitialize`.

## Building

Visual Studio 2022 or newer, with "Desktop development with C++". Open `ModMenu.vcxproj` and build
**Release / x64**. The Aurie and YYToolkit v5.0.0c headers are in `include/`.

## License

AGPL-3.0, see [LICENSE](LICENSE). The `include/` folder contains the Aurie and YYToolkit headers
(AGPL-3.0, AurieFramework).

Unofficial mod, not affiliated with the game's developers.
