#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

//==============================================================================
//  NINE HANDS | DSP : potard de la série rack (QMix, LMix, CompMix, PreMix, MasterMix)
//
//  Aluminium tourné anodisé noir, trait blanc gravé, anneau de valeur lumineux dans la couleur du module,
//  valeur affichée dans une pastille au bout de l'anneau. Lumière en haut à gauche à 45° : les reflets de la face
//  et du flanc sont fixes (ils ne tournent pas avec le bouton) ; le trait, la face et l'ombre suivent la rotation,
//  avec un flou de mouvement du trait quand le bouton tourne vite.
//
//  Fichier identique dans les cinq dépôts : le modifier partout à la fois.
//==============================================================================
namespace nhrack
{
    using namespace juce;

    // face d'alu tourné anodisé noir : stries concentriques (invariantes par rotation) et reflets anisotropes fixes
    inline Image makeAnodisedFace (int px)
    {
        Image im (Image::ARGB, px, px, true);
        Image::BitmapData bd (im, Image::BitmapData::writeOnly);
        const float c = (px - 1) * 0.5f, R = px * 0.5f;
        const Colour dark (0xff0b0c0f), base (0xff1b1e23), light (0xff4e5560);
        const float lightDir = -MathConstants<float>::pi * 0.25f;            // haut gauche (depuis midi, sens horaire)
        for (int y = 0; y < px; ++y)
            for (int x = 0; x < px; ++x)
            {
                const float dx = (x - c) / R, dy = (y - c) / R, rr = std::sqrt (dx * dx + dy * dy);
                if (rr > 1.0f) continue;
                const float phi = std::atan2 (dx, -dy), dlt = phi - lightDir;
                float t = 0.45f + 0.36f * std::cos (2.0f * dlt) + 0.12f * std::cos (dlt);   // deux lobes, le plus fort côté lumière
                t *= 0.82f + 0.18f * jmin (1.0f, rr * 1.6f);                                 // moyeu un peu plus sombre
                const uint32 h = (uint32) (int) (rr * px * 0.9f) * 2654435761u;
                t += ((float) ((h >> 8) & 0xff) / 255.0f - 0.5f) * 0.07f;                   // stries de tournage
                t = jlimit (0.0f, 1.0f, t);
                Colour col = t < 0.5f ? dark.interpolatedWith (base, t * 2.0f) : base.interpolatedWith (light, (t - 0.5f) * 2.0f);
                if (rr > 0.94f) col = col.darker ((rr - 0.94f) * 6.0f);                    // chanfrein
                bd.setPixelColour (x, y, col.withMultipliedAlpha (jlimit (0.0f, 1.0f, (1.0f - rr) * R + 0.5f)));
            }
        return im;
    }

    struct Look
    {
        Point<float> c;          // centre
        float ring = 30;         // rayon de l'anneau de valeur
        float angle = 0;         // angle du trait (radians, 0 = midi, sens horaire)
        float a0 = -2.356f, a1 = 2.356f;   // butées
        float from = -2.356f;    // départ de l'anneau (a0, ou 0 pour un réglage bipolaire)
        float s = 1;             // échelle de l'interface
        float glow = 0.75f, alpha = 1, blur = 0, blurDir = 1;
        Colour colour { 0xffa78bfa };
        const Image* face = nullptr;
    };

    inline Point<float> polar (Point<float> c, float a, float r) { return c + Point<float> (std::sin (a), -std::cos (a)) * r; }
    inline float faceRadius (float ring) { return ring * 0.80f * 0.92f; }

    inline void paintKnob (Graphics& g, const Look& k)
    {
        const float s = k.s, al = k.alpha, a = k.angle;
        const float ringW = jmax (2.0f * s, k.ring * 0.075f);
        // anneau : piste sombre puis valeur lumineuse (halo dosé par Glow) et point blanc au bout
        {
            Path track; track.addCentredArc (k.c.x, k.c.y, k.ring, k.ring, 0.0f, k.a0, k.a1, true);
            g.setColour (Colour (0xff1d232c).withMultipliedAlpha (al));
            g.strokePath (track, PathStrokeType (ringW, PathStrokeType::curved, PathStrokeType::rounded));
            if (std::abs (a - k.from) > 0.005f)
            {
                Path v; v.addCentredArc (k.c.x, k.c.y, k.ring, k.ring, 0.0f, jmin (k.from, a), jmax (k.from, a), true);
                g.setColour (k.colour.withAlpha (0.10f * k.glow * al));
                g.strokePath (v, PathStrokeType (ringW * 4.2f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (k.colour.withAlpha (0.22f * k.glow * al));
                g.strokePath (v, PathStrokeType (ringW * 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (k.colour.withMultipliedAlpha (al));
                g.strokePath (v, PathStrokeType (ringW, PathStrokeType::curved, PathStrokeType::rounded));
            }
            const auto e = polar (k.c, a, k.ring);
            const float dr = ringW * 0.95f;
            g.setColour (k.colour.withAlpha (0.35f * k.glow * al)); g.fillEllipse (Rectangle<float> (dr * 3.2f, dr * 3.2f).withCentre (e));
            g.setColour (Colours::white.withAlpha (al)); g.fillEllipse (Rectangle<float> (dr * 1.8f, dr * 1.8f).withCentre (e));
        }
        const float rb = k.ring * 0.80f, rf = faceRadius (k.ring);
        // ombre portée vers le bas à droite
        {
            const auto sc = k.c + Point<float> (1.0f, 1.0f) * (0.07f * rb);
            ColourGradient sh (Colours::black.withAlpha (0.7f * al), sc.x, sc.y, Colours::transparentBlack, sc.x + rb * 1.22f, sc.y, true);
            sh.addColour (0.70, Colours::black.withAlpha (0.42f * al));
            g.setGradientFill (sh);
            g.fillEllipse (Rectangle<float> (rb * 2.44f, rb * 2.44f).withCentre (sc));
        }
        // flanc biseauté (clair en haut à gauche)
        {
            ColourGradient bev (Colour (0xff3a3f47), k.c.x - rb * 0.7f, k.c.y - rb * 0.7f, Colour (0xff0e1014), k.c.x + rb * 0.7f, k.c.y + rb * 0.7f, false);
            g.setGradientFill (bev); g.setOpacity (al);
            g.fillEllipse (Rectangle<float> (rb * 2, rb * 2).withCentre (k.c));
            g.setOpacity (1.0f);
            Path hl; hl.addCentredArc (k.c.x, k.c.y, rb - 0.6f * s, rb - 0.6f * s, 0.0f, -MathConstants<float>::pi * 0.75f, MathConstants<float>::pi * 0.05f, true);
            g.setColour (Colours::white.withAlpha (0.16f * al));
            g.strokePath (hl, PathStrokeType (0.9f * s, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // face anodisée
        const auto fr = Rectangle<float> (rf * 2, rf * 2).withCentre (k.c);
        if (k.face != nullptr && k.face->isValid()) { g.setOpacity (al); g.drawImage (*k.face, fr, RectanglePlacement::stretchToFit); g.setOpacity (1.0f); }
        else { g.setColour (Colour (0xff1b1e23).withMultipliedAlpha (al)); g.fillEllipse (fr); }
        g.setColour (Colours::black.withAlpha (0.55f * al));
        g.drawEllipse (fr, 0.8f * s);
        // trait gravé blanc, flou de mouvement le long du déplacement
        {
            const float mw = jmax (1.6f * s, rf * 0.055f);
            const float bl = jmin (k.blur, 1.6f);
            const int copies = bl > 0.01f ? 1 + (int) std::ceil (bl * 6.0f) : 1;
            const float spread = bl * MathConstants<float>::twoPi / 40.0f;
            for (int i = 0; i < copies; ++i)
            {
                const float off = copies > 1 ? k.blurDir * spread * (float) i / (float) (copies - 1) : 0.0f;
                const float aa = (copies > 1 ? (i == 0 ? 0.7f : 0.45f / (float) copies) : 1.0f) * al;
                g.setColour (Colours::black.withAlpha (0.45f * aa));
                g.drawLine (Line<float> (polar (k.c, a + off, rf * 0.28f), polar (k.c, a + off, rf * 0.90f)), mw * 1.9f);
                g.setColour (Colour (0xfff4f7fb).withAlpha (aa));
                g.drawLine (Line<float> (polar (k.c, a + off, rf * 0.28f), polar (k.c, a + off, rf * 0.90f)), mw);
            }
        }
    }

    // pastille de valeur au bout de l'anneau (à l'extérieur, sans le toucher)
    inline Rectangle<float> pillRect (Point<float> c, float ring, float angle, const String& text, const Font& f, float s)
    {
        const float w = GlyphArrangement::getStringWidth (f, text) + 10.0f * s, h = f.getHeight() + 4.0f * s;
        const float d = ring + 3.5f * s + std::abs (std::sin (angle)) * w * 0.5f + std::abs (std::cos (angle)) * h * 0.5f;
        const auto p = polar (c, angle, d);
        return Rectangle<float> (w, h).withCentre (p);
    }

    inline void paintPill (Graphics& g, Rectangle<float> r, const String& text, const Font& f, Colour c, float alpha)
    {
        g.setColour (Colour (0xeb090b0f).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (c.withAlpha (0.55f * alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
        g.setColour (Colours::white.withAlpha (alpha));
        g.setFont (f);
        g.drawText (text, r, Justification::centred, false);
    }

    //------------------------------------------------------------------ calque des pastilles (au-dessus de toute l'interface)
    // Chaque potard s'inscrit avec une fonction qui donne sa pastille (dans ses propres coordonnées) ;
    // le calque ne reçoit aucun clic et redessine seulement les zones qui changent.
    class PillOverlay : public Component
    {
    public:
        struct Pill { Rectangle<float> r, ringBox, labelBox; Point<float> c; float ring = 0, angle = 0, gap = 3; String text; Colour colour; float alpha = 1; bool visible = false; };
        PillOverlay() { setInterceptsMouseClicks (false, false); setPaintingIsUnclipped (false); }
        void add (Component* knob, std::function<Pill()> fn, std::function<Font()> font) { items.push_back ({ knob, std::move (fn), std::move (font) }); }
        void remove (Component* knob) { items.erase (std::remove_if (items.begin(), items.end(), [knob] (auto& i) { return i.knob == knob; }), items.end()); }
        bool shown (Component* k) const
        {
            auto* top = getParentComponent();
            if (k == nullptr || top == nullptr) return false;
            for (auto* c = k; c != nullptr && c != top; c = c->getParentComponent())
                if (! c->isVisible()) return false;
            return top->isParentOf (k);
        }
        // place les pastilles (coordonnées du calque) en évitant qu'elles se chevauchent
        std::vector<std::pair<Pill, Rectangle<float>>> layout() const
        {
            std::vector<std::pair<Pill, Rectangle<float>>> out;
            out.reserve (items.size());
            for (auto& i : items)
            {
                Pill p; Rectangle<float> r;
                if (shown (i.knob)) { p = i.fn(); if (p.visible) r = getLocalArea (i.knob, p.r); }
                out.push_back ({ p, r });
            }
            // obstacles : les potards voisins (leur anneau) et les pastilles déjà placées
            std::vector<Rectangle<float>> knobsR, labelsR;
            for (auto& i : items)
            {
                Rectangle<float> kr, lr;
                if (shown (i.knob)) { auto p = i.fn(); if (p.visible) { kr = getLocalArea (i.knob, p.ringBox); if (! p.labelBox.isEmpty()) lr = getLocalArea (i.knob, p.labelBox); } }
                knobsR.push_back (kr); labelsR.push_back (lr);
            }
            auto hits = [&] (size_t n, Rectangle<float> r, Rectangle<float>& o)
            {
                for (size_t m = 0; m < knobsR.size(); ++m)
                {
                    if (m != n && ! knobsR[m].isEmpty() && knobsR[m].intersects (r)) { o = knobsR[m]; return true; }
                    if (! labelsR[m].isEmpty() && labelsR[m].intersects (r)) { o = labelsR[m]; return true; }
                }
                for (size_t m = 0; m < n; ++m)
                    if (out[m].first.visible && out[m].second.expanded (1.5f).intersects (r)) { o = out[m].second; return true; }
                return false;
            };
            // on fait glisser la pastille le long de son propre anneau (puis un peu plus loin) jusqu'à trouver une place libre
            for (size_t n = 0; n < out.size(); ++n)
            {
                auto& [p, r] = out[n];
                if (! p.visible) continue;
                const auto c = getLocalPoint (items[n].knob, p.c);
                Rectangle<float> o;
                if (! hits (n, r, o)) continue;
                const float w = r.getWidth(), h = r.getHeight();
                bool found = false;
                for (float extra : { 0.0f, 0.35f, 0.7f })
                {
                    for (int step = 1; step <= 10 && ! found; ++step)
                        for (float sg : { 1.0f, -1.0f })
                        {
                            const float a = p.angle + sg * (float) step * 0.20f;
                            const float d = p.ring + p.gap + extra * h * 2.0f + std::abs (std::sin (a)) * w * 0.5f + std::abs (std::cos (a)) * h * 0.5f;
                            const auto cand = Rectangle<float> (w, h).withCentre (c + Point<float> (std::sin (a), -std::cos (a)) * d);
                            if (! hits (n, cand, o)) { r = cand; found = true; break; }
                        }
                    if (found) break;
                }
            }
            return out;
        }
        void update()
        {
            auto now = layout();
            if (now.size() != last.size()) { repaint(); last.clear(); }
            for (size_t n = 0; n < now.size(); ++n)
            {
                const auto& [p, r] = now[n];
                if (n < last.size())
                {
                    const auto& [lp, lr] = last[n];
                    if (lp.visible == p.visible && lr == r && lp.text == p.text && lp.alpha == p.alpha && lp.colour == p.colour) continue;
                    if (lp.visible) repaint (lr.getSmallestIntegerContainer().expanded (2));
                }
                if (p.visible) repaint (r.getSmallestIntegerContainer().expanded (2));
            }
            last = std::move (now);
        }
        void paint (Graphics& g) override
        {
            const auto now = layout();
            for (size_t n = 0; n < now.size() && n < items.size(); ++n)
                if (now[n].first.visible)
                    paintPill (g, now[n].second, now[n].first.text, items[n].font(), now[n].first.colour, now[n].first.alpha);
        }
    private:
        struct Item { Component* knob; std::function<Pill()> fn; std::function<Font()> font; };
        std::vector<Item> items;
        std::vector<std::pair<Pill, Rectangle<float>>> last;
    };
}
