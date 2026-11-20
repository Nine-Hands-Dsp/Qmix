#pragma once
#include <juce_core/juce_core.h>

//==============================================================================
// NINE HANDS | DSP : taille par defaut des nouvelles fenetres (plugins rack), comme sur les pedales.
// Clic droit > Plugin size > Default size : choix commun a toutes les instances du plugin, enregistre dans
//   Windows : %APPDATA%\Nine Hands DSP\<Plugin>\settings.xml
//   macOS   : ~/Library/Application Support/Nine Hands DSP/<Plugin>/settings.xml
// (meme fichier et meme attribut « defaultSize » que WigaWindowSize.h des pedales).
// Chaque instance garde la taille choisie dans son menu (indice 0..n-1) ; -1 = suit la taille par defaut.
//==============================================================================
namespace nhsize
{
    constexpr int idDefault = 7021;      // 7021..7023 : sous-menu « Default size » (plage 7000-7099 reservee)

    inline juce::File file (const juce::String& plugin)
    {
        const auto uad = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        const auto base = uad.getChildFile ("Application Support");
       #else
        const auto base = uad;
       #endif
        return base.getChildFile ("Nine Hands DSP").getChildFile (plugin).getChildFile ("settings.xml");
    }

    inline int defaultIndex (const juce::String& plugin, int numSizes)
    {
        const auto f = file (plugin);
        if (f.existsAsFile())
            if (auto x = juce::XmlDocument::parse (f))
                return juce::jlimit (0, juce::jmax (0, numSizes - 1), x->getIntAttribute ("defaultSize", 0));
        return 0;
    }

    inline void setDefaultIndex (const juce::String& plugin, int idx)
    {
        const auto f = file (plugin);
        juce::XmlElement x ("NineHandsSettings");
        if (f.existsAsFile())
            if (auto old = juce::XmlDocument::parse (f))
                x = *old;
        x.setAttribute ("defaultSize", juce::jmax (0, idx));
        f.getParentDirectory().createDirectory();
        x.writeTo (f);
    }

    // taille d'ouverture : celle de l'instance, sinon la taille par defaut du plugin
    inline int openIndex (const juce::String& plugin, int instanceIdx, int numSizes)
    {
        return (instanceIdx >= 0 && instanceIdx < numSizes) ? instanceIdx : defaultIndex (plugin, numSizes);
    }
}
