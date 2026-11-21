#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Menus clic droit de Qmix : même design que les menus d'Octopus (fond sombre arrondi, icônes,
// coche bleue, chevron des sous-menus, en-tête à pastille). Toujours ouverts là où on clique.
namespace QmixMenu
{
    using namespace juce;

    const Colour kBg (0xff13171d), kBorder (0xff262c36), kHover (0xff1c222b), kSep (0xff20252e),
                 kText (0xffeef1f5), kDim (0xff8d96a4), kFaint (0xff3a414d), kAccent (0xff4fc3f7);

    struct Fonts { Typeface::Ptr reg, med, bold; };
    inline Font fnt (const Typeface::Ptr& tf, float h) { return Font (FontOptions (tf).withHeight (h)); }

    enum Ico { icoNone = 0, icoSave, icoLoad, icoClear, icoMidi, icoSize, icoPreset, icoGlow, icoEq, icoAnalyzer, icoRange,
               icoShape, icoSlope, icoStereo, icoBypass, icoRemove, icoAdd,
               // formes de bande : dessin de la courbe correspondante
               icoBell, icoLowShelf, icoHighShelf, icoLowCut, icoHighCut, icoNotch, icoBandPass, icoTilt };
    inline bool isShapeIco (int i) { return i >= icoBell && i <= icoTilt; }
    inline int shapeIco (int type) { return icoBell + std::clamp (type, 0, 7); }   // même ordre que qmix::BandType

    inline int iconFor (const String& t)
    {
        const auto s = t.toLowerCase();
        if (s == "bell") return icoBell;
        if (s == "low shelf") return icoLowShelf;
        if (s == "high shelf") return icoHighShelf;
        if (s == "low cut") return icoLowCut;
        if (s == "high cut") return icoHighCut;
        if (s == "notch") return icoNotch;
        if (s == "band pass") return icoBandPass;
        if (s == "tilt shelf") return icoTilt;
        if (s.startsWith ("save")) return icoSave;
        if (s.startsWith ("load")) return icoLoad;
        if (s.contains ("clear") || s.startsWith ("reset") || s.startsWith ("cancel")) return icoClear;
        if (s.startsWith ("midi") || s.startsWith ("learn")) return icoMidi;
        if (s.startsWith ("glow")) return icoGlow;
        if (s.contains ("size") || s.endsWith ("%")) return icoSize;
        if (s.startsWith ("preset") || s.startsWith ("factory")) return icoPreset;
        if (s.startsWith ("analy")) return icoAnalyzer;
        if (s.startsWith ("display range") || s.startsWith ("range")) return icoRange;
        if (s.startsWith ("shape")) return icoShape;
        if (s.startsWith ("slope")) return icoSlope;
        if (s.startsWith ("stereo")) return icoStereo;
        if (s.startsWith ("enable") || s.startsWith ("bypass") || s.startsWith ("power")) return icoBypass;
        if (s.startsWith ("delete") || s.startsWith ("remove")) return icoRemove;
        if (s.startsWith ("add") || s.startsWith ("new")) return icoAdd;
        return icoNone;
    }

    inline void drawIco (Graphics& g, int ico, Rectangle<float> r, Colour c)
    {
        if (ico == icoNone) return;
        const float k = r.getWidth() / 16.0f;
        auto P = [&] (float x, float y) { return Point<float> (r.getX() + x * k, r.getY() + y * k); };
        Path p;
        auto line = [&] (float x1, float y1, float x2, float y2) { p.startNewSubPath (P (x1, y1)); p.lineTo (P (x2, y2)); };
        auto circ = [&] (float cx, float cy, float rad) { p.addEllipse (P (cx - rad, cy - rad).x, P (cx - rad, cy - rad).y, rad * 2 * k, rad * 2 * k); };
        auto rect = [&] (float x, float y, float w, float h, float rr) { p.addRoundedRectangle (P (x, y).x, P (x, y).y, w * k, h * k, rr * k); };
        bool fillDots = false;
        switch (ico)
        {
            case icoSave:   p.startNewSubPath (P (2.5f, 2.5f)); p.lineTo (P (11, 2.5f)); p.lineTo (P (13.5f, 5)); p.lineTo (P (13.5f, 13.5f)); p.lineTo (P (2.5f, 13.5f)); p.closeSubPath();
                            p.startNewSubPath (P (5, 2.5f)); p.lineTo (P (5, 6)); p.lineTo (P (10, 6)); p.lineTo (P (10, 2.5f)); break;
            case icoLoad:   line (8, 2, 8, 10); line (4.5f, 6.5f, 8, 10); line (11.5f, 6.5f, 8, 10); line (3, 13.5f, 13, 13.5f); break;
            case icoClear:  line (4, 4, 12, 12); line (12, 4, 4, 12); break;
            case icoMidi:   circ (8, 8, 6.0f); fillDots = true; break;
            case icoSize:   line (3, 3, 13, 13); line (3, 3, 3, 7); line (3, 3, 7, 3); line (13, 13, 13, 9); line (13, 13, 9, 13); break;
            case icoPreset: rect (3, 2.5f, 10, 11, 1.5f); line (5.5f, 6, 10.5f, 6); line (5.5f, 9, 9, 9); break;
            case icoGlow:   circ (8, 8, 2.6f);
                            for (int i = 0; i < 8; ++i) { const float a = (float) i * MathConstants<float>::pi / 4.0f;
                                                          line (8 + 4.4f * std::cos (a), 8 + 4.4f * std::sin (a), 8 + 6.3f * std::cos (a), 8 + 6.3f * std::sin (a)); }
                            break;
            case icoEq:     p.startNewSubPath (P (1.5f, 10)); p.cubicTo (P (4, 10), P (5, 4), P (7.5f, 4)); p.cubicTo (P (10, 4), P (10.5f, 11), P (12.5f, 11)); p.lineTo (P (14.5f, 11)); break;
            case icoAnalyzer: line (3, 13, 3, 9); line (6, 13, 6, 5); line (9, 13, 9, 7); line (12, 13, 12, 3); break;
            case icoRange:  line (8, 2, 8, 14); line (5, 5, 8, 2); line (11, 5, 8, 2); line (5, 11, 8, 14); line (11, 11, 8, 14); break;
            case icoShape:  p.startNewSubPath (P (1.5f, 11)); p.quadraticTo (P (8, -1), P (14.5f, 11)); break;
            case icoSlope:  line (2, 4, 7, 4); p.startNewSubPath (P (7, 4)); p.quadraticTo (P (10, 4), P (13, 13)); break;
            case icoStereo: circ (6, 8, 4.0f); circ (10, 8, 4.0f); break;
            case icoBypass: p.addCentredArc (P (8, 8.5f).x, P (8, 8.5f).y, 5.5f * k, 5.5f * k, 0.0f, 0.75f, MathConstants<float>::twoPi - 0.75f, true); line (8, 2, 8, 8); break;
            case icoRemove: line (3, 4.5f, 13, 4.5f); p.startNewSubPath (P (4.5f, 4.5f)); p.lineTo (P (5.5f, 13.5f)); p.lineTo (P (10.5f, 13.5f)); p.lineTo (P (11.5f, 4.5f)); line (6.5f, 2.5f, 9.5f, 2.5f); break;
            case icoAdd:    line (8, 3, 8, 13); line (3, 8, 13, 8); break;
            // formes (0 dB = y 9 ; le haut de l'icône = accentuation)
            case icoBell:      p.startNewSubPath (P (0.5f, 10.5f)); p.lineTo (P (3.5f, 10.5f)); p.cubicTo (P (6.0f, 10.5f), P (6.3f, 2.5f), P (8.0f, 2.5f));
                               p.cubicTo (P (9.7f, 2.5f), P (10.0f, 10.5f), P (12.5f, 10.5f)); p.lineTo (P (15.5f, 10.5f)); break;
            case icoLowShelf:  p.startNewSubPath (P (0.5f, 4.0f)); p.lineTo (P (4.5f, 4.0f)); p.cubicTo (P (7.5f, 4.0f), P (8.5f, 11.5f), P (11.5f, 11.5f)); p.lineTo (P (15.5f, 11.5f)); break;
            case icoHighShelf: p.startNewSubPath (P (0.5f, 11.5f)); p.lineTo (P (4.5f, 11.5f)); p.cubicTo (P (7.5f, 11.5f), P (8.5f, 4.0f), P (11.5f, 4.0f)); p.lineTo (P (15.5f, 4.0f)); break;
            case icoLowCut:    p.startNewSubPath (P (2.5f, 15.0f)); p.cubicTo (P (4.0f, 8.0f), P (5.5f, 5.5f), P (8.5f, 5.5f)); p.lineTo (P (15.5f, 5.5f)); break;
            case icoHighCut:   p.startNewSubPath (P (0.5f, 5.5f)); p.lineTo (P (7.5f, 5.5f)); p.cubicTo (P (10.5f, 5.5f), P (12.0f, 8.0f), P (13.5f, 15.0f)); break;
            case icoNotch:     p.startNewSubPath (P (0.5f, 5.5f)); p.lineTo (P (5.5f, 5.5f)); p.cubicTo (P (7.0f, 5.5f), P (7.4f, 14.5f), P (8.0f, 14.5f));
                               p.cubicTo (P (8.6f, 14.5f), P (9.0f, 5.5f), P (10.5f, 5.5f)); p.lineTo (P (15.5f, 5.5f)); break;
            case icoBandPass:  p.startNewSubPath (P (1.0f, 15.0f)); p.cubicTo (P (4.5f, 13.0f), P (6.0f, 4.0f), P (8.0f, 4.0f));
                               p.cubicTo (P (10.0f, 4.0f), P (11.5f, 13.0f), P (15.0f, 15.0f)); break;
            case icoTilt:      p.startNewSubPath (P (0.5f, 12.0f)); p.lineTo (P (2.5f, 12.0f)); p.cubicTo (P (6.0f, 12.0f), P (10.0f, 4.5f), P (13.5f, 4.5f)); p.lineTo (P (15.5f, 4.5f));
                               fillDots = false; break;
            default: break;
        }
        g.setColour (c);
        g.strokePath (p, PathStrokeType (1.5f * k, PathStrokeType::curved, PathStrokeType::rounded));
        if (fillDots)
            for (auto d : { Point<float> (5, 7), Point<float> (11, 7), Point<float> (6.3f, 10.3f), Point<float> (9.7f, 10.3f), Point<float> (8, 4.7f) })
                g.fillEllipse (Rectangle<float> (1.8f * k, 1.8f * k).withCentre (P (d.x, d.y)));
    }

    inline void drawCheck (Graphics& g, Rectangle<float> r, Colour c, float w)
    {
        Path p;
        p.startNewSubPath (r.getX() + r.getWidth() * 0.18f, r.getY() + r.getHeight() * 0.52f);
        p.lineTo (r.getX() + r.getWidth() * 0.42f, r.getY() + r.getHeight() * 0.76f);
        p.lineTo (r.getX() + r.getWidth() * 0.84f, r.getY() + r.getHeight() * 0.26f);
        g.setColour (c);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    }

    struct MenuLnf : public LookAndFeel_V4
    {
        Fonts f;
        MenuLnf()
        {
            setColour (PopupMenu::backgroundColourId, Colours::transparentBlack);
            setColour (PopupMenu::textColourId, kText);
            setColour (PopupMenu::headerTextColourId, kDim);
            setColour (PopupMenu::highlightedBackgroundColourId, kHover);
            setColour (PopupMenu::highlightedTextColourId, Colours::white);
        }
        Font getPopupMenuFont() override { return fnt (f.med, 15.0f); }
        int getPopupMenuBorderSize() override { return 7; }
        void drawPopupMenuBackground (Graphics& g, int w, int h) override
        {
            const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
            g.setColour (kBg);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (kBorder);
            g.drawRoundedRectangle (r, 10.0f, 1.0f);
        }
        void getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardHeight, int& w, int& h) override
        {
            if (isSeparator) { w = 50; h = 11; return; }
            h = standardHeight > 0 ? standardHeight : 32;
            w = (int) std::ceil (GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 34 + 40;
        }
        void drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name) override
        {
            g.setFont (fnt (f.bold, 11.5f).withExtraKerningFactor (0.14f));
            g.setColour (kDim);
            g.drawFittedText (name.toUpperCase(), area.withTrimmedLeft (14).withTrimmedRight (10).withTrimmedTop (6), Justification::centredLeft, 1);
        }
        void drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                bool hasSubMenu, const String& text, const String& shortcut, const Drawable*, const Colour* textColour) override
        {
            const auto a = area.toFloat();
            if (isSeparator)
            {
                g.setColour (kSep);
                g.fillRect (a.withSizeKeepingCentre (a.getWidth() - 20.0f, 1.0f));
                return;
            }
            const bool hi = isHighlighted && isActive;
            if (hi)
            {
                g.setColour (kHover);
                g.fillRoundedRectangle (a.reduced (5.0f, 1.0f), 7.0f);
            }
            const auto ib = Rectangle<float> (15.0f, 15.0f).withCentre ({ a.getX() + 22.0f, a.getCentreY() });
            const int ico = shortcut == "ico:glow" ? (int) icoGlow : iconFor (text);
            const bool shape = isShapeIco (ico);
            if (shape)
            {
                // forme de bande : la courbe reste dessinée, la coche passe à droite
                drawIco (g, ico, ib.expanded (1.5f), ! isActive ? kFaint : (isTicked || hi) ? kAccent : kDim);
                if (isTicked) drawCheck (g, Rectangle<float> (14.0f, 14.0f).withCentre ({ a.getRight() - 20.0f, a.getCentreY() }), kAccent, 2.0f);
            }
            else if (isTicked) drawCheck (g, ib.expanded (1.0f), kAccent, 2.0f);
            else drawIco (g, ico, ib, ! isActive ? kFaint : hi ? kAccent : kDim);
            auto tr = a.withTrimmedLeft (38.0f).withTrimmedRight (hasSubMenu ? 30.0f : (shape ? 34.0f : 14.0f));
            g.setFont (getPopupMenuFont());
            g.setColour (textColour != nullptr ? *textColour : ! isActive ? kDim.withAlpha (0.55f) : hi ? Colours::white : kText);
            g.drawFittedText (text, tr.toNearestInt(), Justification::centredLeft, 1, 0.9f);
            if (hasSubMenu)
            {
                const float cx = a.getRight() - 18.0f, cy = a.getCentreY();
                Path p; p.startNewSubPath (cx - 2.5f, cy - 5.0f); p.lineTo (cx + 2.5f, cy); p.lineTo (cx - 2.5f, cy + 5.0f);
                g.setColour (hi ? Colours::white : kDim);
                g.strokePath (p, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
    };

    // en-tête : pastille teintée + titre + sous-titre (comme Octopus)
    struct Header : public PopupMenu::CustomComponent
    {
        Fonts f; int ico; Colour col; String title, sub;
        Header (Fonts fo, int icon, Colour c, String t, String s) : CustomComponent (false), f (fo), ico (icon), col (c), title (t), sub (s) {}
        void getIdealSize (int& w, int& h) override
        {
            w = jmax (240, (int) std::ceil (jmax (GlyphArrangement::getStringWidth (fnt (f.bold, 15.5f), title), GlyphArrangement::getStringWidth (fnt (f.reg, 12.5f), sub))) + 72);
            h = 52;
        }
        void paint (Graphics& g) override
        {
            const auto b = Rectangle<float> (30.0f, 30.0f).withPosition (12.0f, 10.0f);
            g.setColour (col.withAlpha (0.16f));
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (col.withAlpha (0.7f));
            g.drawRoundedRectangle (b.reduced (0.6f), 8.0f, 1.2f);
            drawIco (g, ico, b.reduced (7.0f), col);
            g.setColour (kText);
            g.setFont (fnt (f.bold, 15.5f));
            g.drawText (title, Rectangle<float> (52.0f, sub.isEmpty() ? 15.0f : 8.0f, (float) getWidth() - 60.0f, 19.0f), Justification::centredLeft, true);
            if (sub.isNotEmpty())
            {
                g.setColour (kDim);
                g.setFont (fnt (f.reg, 12.5f));
                g.drawText (sub, Rectangle<float> (52.0f, 27.0f, (float) getWidth() - 60.0f, 16.0f), Justification::centredLeft, true);
            }
        }
    };

    inline void addHeader (PopupMenu& m, Fonts f, int ico, Colour c, const String& title, const String& sub)
    {
        m.addCustomItem (-1, std::make_unique<Header> (f, ico, c, title, sub), nullptr, title);
        m.addSeparator();
    }
}
