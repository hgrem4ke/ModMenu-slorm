# ModMenu pour The Slormancer

Ajoute un bouton **M** dans le menu Options du jeu. Il ouvre un menu **MODS** avec un encadré par
mod installé ; un clic sur un encadré ouvre la page de réglages de ce mod, dans le style des menus
du jeu.

ModMenu ne fait rien tout seul : il sert de menu commun aux mods qui s'y enregistrent.

Mods connus :

- [AutoLoot](https://github.com/hgrem4ke/Autoloot-slorm) : s'enregistre lui-même.
- [SlormReforger](https://github.com/wistb/SlormReforger) : détecté automatiquement, avec une page
  d'explication (ses réglages restent dans sa propre fenêtre).

## Installation

Il faut [Aurie Framework](https://github.com/AurieFramework/Aurie) et
[YYToolkit](https://github.com/AurieFramework/YYToolkit) **v5.0.0c** déjà installés dans le jeu.

Copie `x64/Release/ModMenu.dll` dans `The Slormancer\mods\Aurie\`.

Réglage facultatif, dans un fichier `ModMenu.ini` à côté de la DLL :

    [ModMenu]
    Debug=1

`Debug=1` affiche des messages détaillés et active une galerie des sprites d'interface du jeu
(touche Fin).

## Ajouter son mod au menu

Copie [api/ModMenu/ModMenuAPI.hpp](api/ModMenu/ModMenuAPI.hpp) dans ton projet, puis enregistre-toi
depuis ton `ModuleInitialize` :

```cpp
#include <ModMenu/ModMenuAPI.hpp>

static bool g_Enabled = true;

static bool IsActive(void*) { return g_Enabled; }

static void DrawPage(ModMenu::IModMenu* Menu, void*)
{
    // Grille de 2 colonnes (0 = gauche, 1 = droite) x 3 lignes (0 à 2).
    if (Menu->Toggle(0, 0, "Mon mod actif", g_Enabled))
        g_Enabled = !g_Enabled;
}

static void RegisterInModMenu()
{
    Aurie::AurieInterfaceBase* base = nullptr;
    if (!Aurie::AurieSuccess(Aurie::ObGetInterface(ModMenu::kInterfaceName, base)) || !base)
        return;                         // ModMenu.dll n'est pas installé : on continue sans menu

    ModMenu::ModEntry entry = {};
    entry.Name = "MON MOD";             // texte de l'encadré
    entry.Title = "MOD  -  MON MOD";    // titre de la page
    entry.IsActive = IsActive;          // affiche ACTIF / INACTIF dans l'encadré (facultatif)
    entry.DrawPage = DrawPage;

    static_cast<ModMenu::IModMenu*>(base)->RegisterMod(entry);
}
```

Éléments disponibles dans une page : `Toggle` (case à cocher) et `Selector` (valeur avec flèches
gauche et droite). Le menu affiche jusqu'à 6 mods.

ModMenu crée son interface dès `ModulePreinitialize` : Aurie le charge donc avant les mods qui
s'enregistrent dans leur `ModuleInitialize`.

## Compilation

Visual Studio 2022 ou plus récent, avec « Développement Desktop en C++ ». Ouvre `ModMenu.vcxproj`
et compile en **Release / x64**. Les en-têtes d'Aurie et de YYToolkit v5.0.0c sont dans `include/`.

## Licence

AGPL-3.0, voir [LICENSE](LICENSE). Le dossier `include/` contient les en-têtes d'Aurie et de
YYToolkit (AGPL-3.0, AurieFramework).

Mod non officiel, sans lien avec les développeurs du jeu.
