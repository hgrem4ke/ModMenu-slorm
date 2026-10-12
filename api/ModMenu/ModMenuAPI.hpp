// ============================================================
// API du menu des mods pour The Slormancer (ModMenu.dll)
//
// ModMenu.dll ajoute le bouton "M" au menu Options du jeu et affiche un encadre par mod
// enregistre. Un mod s'y ajoute depuis son ModuleInitialize :
//
//   AurieInterfaceBase* base = nullptr;
//   if (AurieSuccess(ObGetInterface(ModMenu::kInterfaceName, base)) && base)
//       static_cast<ModMenu::IModMenu*>(base)->RegisterMod(entry);
//
// Si ModMenu.dll n'est pas installe, ObGetInterface echoue : le mod doit continuer sans menu.
// ============================================================

#pragma once
#include <Aurie/shared.hpp>

namespace ModMenu
{
    static constexpr const char* kInterfaceName = "SlormModMenu";

    struct IModMenu;

    // Description d'un mod. Les textes sont copies par RegisterMod.
    struct ModEntry
    {
        const char* Name;       // texte de l'encadre dans le menu MODS (ex : "AUTOLOOT")
        const char* Title;      // titre de la page de reglages (ex : "MOD  -  AUTOLOOT")
        const char* Help;       // ligne d'aide en bas de la page (facultatif)

        // Etat affiche dans l'encadre : ACTIF / INACTIF. nullptr = pas d'etat.
        bool (*IsActive)(void* User);

        // Dessine la page de reglages, une fois par image tant qu'elle est ouverte.
        // Menu->Toggle / Menu->Selector ne sont utilisables que pendant cet appel.
        void (*DrawPage)(IModMenu* Menu, void* User);

        // Appele une fois par image, menu ouvert ou non (facultatif).
        void (*OnFrame)(void* User);

        void* User;             // renvoye tel quel aux fonctions ci-dessus
    };

    // La page de reglages est une grille : 2 colonnes (0 = gauche, 1 = droite) x 3 lignes (0 a 2).
    struct IModMenu : Aurie::AurieInterfaceBase
    {
        // Ajoute un mod au menu (ou met a jour celui qui porte deja ce nom).
        virtual bool RegisterMod(const ModEntry& Entry) = 0;

        // Case a cocher en losange avec son libelle. Renvoie vrai au clic.
        virtual bool Toggle(int Column, int Row, const char* Label, bool On) = 0;

        // Libelle + selecteur  <  valeur  >. Renvoie -1 (fleche gauche), +1 (fleche droite) ou 0.
        virtual int Selector(int Column, int Row, const char* Label, const char* Value) = 0;
    };
}
