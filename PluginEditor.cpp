#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;
using namespace QmixUi;
using QmixMenu::fnt;

//==============================================================================
//  Couleurs
//==============================================================================
namespace
{
    #define cBg        (QmixUi::theme().bg)
    #define cGrid      (QmixUi::theme().grid)
    #define cGridMajor (QmixUi::theme().gridMajor)
    #define cLabel     (QmixUi::theme().label)
    #define cText      (QmixUi::theme().text)
    #define cDim       (QmixUi::theme().dim)
    #define cAccent    (QmixUi::theme().accent)
    #define cFace      (QmixUi::theme().face)

    double nowS() { return Time::getMillisecondCounterHiRes() * 0.001; }

    String hzStr (double f)
    {
        if (f >= 10000.0) return String (f / 1000.0, 1) + " kHz";
        if (f >= 1000.0)  return String (f / 1000.0, 2) + " kHz";
        if (f >= 100.0)   return String ((int) std::round (f)) + " Hz";
        return String (f, 1) + " Hz";
    }
    String dbStr (double d) { return (d > 0.0049 ? "+" : "") + String (d, std::abs (d) < 10.0 ? 2 : 1) + " dB"; }
    String qStr (double q)  { return "Q " + String (q, q < 10.0 ? 2 : 1); }

    // lumière d'ambiance de la façade : source sur la diagonale haut gauche (45°)
    void paintFacade (Graphics& g, Rectangle<float> r, bool overImage)
    {
        if (! overImage) { g.setColour (cBg); g.fillRect (r); }
        const float d = r.getWidth() + r.getHeight();
        ColourGradient lit (Colours::white.withAlpha (0.045f), r.getX(), r.getY(),
                            Colours::black.withAlpha (0.12f), r.getX() + d * 0.5f, r.getY() + d * 0.5f, false);
        g.setGradientFill (lit);
        g.fillRect (r);
        // léger reflet satiné, bande perpendiculaire à la lumière (x + y = cte)
        const float c = d * 0.36f;
        ColourGradient sat (Colours::transparentWhite, r.getX() + c * 0.5f - d * 0.08f, r.getY() + c * 0.5f - d * 0.08f,
                            Colours::transparentWhite, r.getX() + c * 0.5f + d * 0.08f, r.getY() + c * 0.5f + d * 0.08f, false);
        sat.addColour (0.5, Colours::white.withAlpha (0.03f));
        g.setGradientFill (sat);
        g.fillRect (r);
    }
}

static const QmixUi::Theme kThemes[] = {
    { "Graphite",
      Colour (0xff0c0f13), Colour (0xff080a0e), Colour (0xff040507), Colour (0xff161b22), Colour (0xff1f2630), Colour (0xff5d6675), Colour (0xffe9eef4), Colour (0xff8d96a4), Colour (0xff4fc3f7),
      Colour (0xff050608), Colour (0xff2a313b), Colour (0xf0151a21), Colour (0xf00b0e12), Colour (0xff2a313b), Colour (0xff0a0d10), Colour (0xff2a313b),
      Colour (0xffeaf4ff), Colour (0xff2f6f93), Colour (0xff12303f), Colour (0xff8cc6e6), Colour (0xff7d8796),
      Colour (0xff4a525d), Colour (0xff181c22), Colour (0xff3b424c), Colour (0xff12161b), Colour (0xff1d232c), Colour (0xfff4f7fb), Colour (0xff0a0c10),
      Colour (0xffe9eef4), Colour (0xff8d96a4), Colour (0xff07090c), Colour (0xff252b34),
      false, false,
      { 0xffff5a5f, 0xffff9f43, 0xfffeca57, 0xffa3e048, 0xff2ed573, 0xff1dd1a1, 0xff4fc3f7, 0xff5f8cff, 0xff8a6cff, 0xffc56cf0, 0xffff6bcb, 0xffff7f9f } },
};
static int gTheme = 0;
const QmixUi::Theme& QmixUi::theme() { return kThemes[gTheme]; }
void QmixUi::setTheme (int i) { gTheme = jlimit (0, numThemes() - 1, i); }
int QmixUi::numThemes() { return (int) (sizeof (kThemes) / sizeof (kThemes[0])); }

Colour QmixUi::bandColour (int b) { return Colour (theme().palette[(b * 5) % 12]); }

//==============================================================================
//  Potard
//==============================================================================
Knob::Knob (QmixEditor& e, String l) : label (std::move (l)), ed (e) {}

void Knob::attach (RangedAudioParameter* p, Colour c, bool bip)
{
    if (p == param && att != nullptr) { colour = c; bipolar = bip; repaint(); return; }
    att.reset();
    param = p; colour = c; bipolar = bip;
    if (p == nullptr) return;
    att = std::make_unique<ParameterAttachment> (*p, [this] (float v) { norm = param->convertTo0to1 (v); repaint(); });
    att->sendInitialUpdate();
    shownAngle = prevAngle = angleOf (norm);
}

void Knob::detach() { att.reset(); param = nullptr; }

float Knob::angleOf (float v) const { return MathConstants<float>::pi * (-0.75f + 1.5f * v); }

void Knob::tick()
{
    const float a = angleOf (norm);
    shownAngle = a;
    const float tooth = MathConstants<float>::twoPi / 32.0f;
    const float t = std::abs (shownAngle - prevAngle) / tooth;
    prevAngle = shownAngle;
    const float lvl = jlimit (0.0f, 1.6f, (t - 0.15f) / 0.6f);
    const float nb = lvl > blur ? lvl : blur * 0.6f;
    if (std::abs (nb - blur) > 0.002f) { blur = nb < 0.01f ? 0.0f : nb; repaint(); }
}

void Knob::paint (Graphics& g)
{
    const float s = ed.sc();
    const float d = jmin ((float) getWidth(), (float) getHeight() - 26.0f * s);
    const float r = d * 0.5f - 3.5f * s;
    const Point<float> c (getWidth() * 0.5f, d * 0.5f);
    const float a = shownAngle;
    const float alphaAll = enabledLook ? 1.0f : 0.35f;
    auto P = [&] (float ang, float rad) { return c + Point<float> (std::sin (ang), -std::cos (ang)) * rad; };

    // anneau de valeur
    {
        Path track; track.addCentredArc (c.x, c.y, r + 2.0f * s, r + 2.0f * s, 0.0f, angleOf (0.0f), angleOf (1.0f), true);
        g.setColour (theme().track.withMultipliedAlpha (alphaAll));
        g.strokePath (track, PathStrokeType (2.2f * s, PathStrokeType::curved, PathStrokeType::rounded));
        const float from = bipolar ? 0.0f : angleOf (0.0f);
        if (std::abs (a - from) > 0.01f)
        {
            Path v; v.addCentredArc (c.x, c.y, r + 2.0f * s, r + 2.0f * s, 0.0f, jmin (from, a), jmax (from, a), true);
            g.setColour (colour.withAlpha (0.18f * ed.glow() * alphaAll));
            g.strokePath (v, PathStrokeType (5.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (colour.withMultipliedAlpha (alphaAll));
            g.strokePath (v, PathStrokeType (2.2f * s, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    const float rc = r * 0.84f, rcap = r * 0.64f;
    // ombre portée (vers le bas à droite, lumière en haut à gauche à 45°)
    {
        const auto sc = c + Point<float> (1.0f, 1.0f) * (0.10f * r);
        ColourGradient sh (Colours::black.withAlpha (0.6f * alphaAll), sc.x, sc.y, Colours::transparentBlack, sc.x + rc * 1.18f, sc.y, true);
        sh.addColour (0.72, Colours::black.withAlpha (0.35f * alphaAll));
        g.setGradientFill (sh);
        g.fillEllipse (Rectangle<float> (rc * 2.36f, rc * 2.36f).withCentre (sc));
    }
    // couronne crantée
    {
        ColourGradient cg (theme().crownLight, c.x - rc * 0.7f, c.y - rc * 0.7f, theme().crownDark, c.x + rc * 0.7f, c.y + rc * 0.7f, false);
        g.setGradientFill (cg);
        g.setOpacity (alphaAll);
        g.fillEllipse (Rectangle<float> (rc * 2, rc * 2).withCentre (c));
        g.setOpacity (1.0f);
        constexpr int nTeeth = 32;
        const float tooth = MathConstants<float>::twoPi / nTeeth;
        const float bl = jmin (blur, 1.0f);                     // 1 = un cran entier parcouru : moyenne uniforme
        const int copies = blur > 0.01f ? 1 + (int) std::ceil (bl * 10.0f) : 1;
        const float spread = bl * tooth;
        for (int k = 0; k < copies; ++k)
        {
            const float off = copies > 1 ? -spread * (float) k / (float) (copies - 1) : 0.0f;
            const float al = (1.0f / (float) copies) * (blur > 1.0f ? jmax (0.4f, 1.0f - (blur - 1.0f)) : 1.0f) * alphaAll;
            for (int i = 0; i < nTeeth; ++i)
            {
                const float ang = a + off + tooth * (float) i;
                // éclairage fixe : les crans tournés vers le haut gauche sont plus clairs
                const float lit = 0.5f + 0.5f * std::cos (ang - (-MathConstants<float>::pi * 0.25f));
                g.setColour (Colour::greyLevel (0.16f + 0.42f * lit).withAlpha (0.85f * al));
                g.drawLine (Line<float> (P (ang, rc * 0.80f), P (ang, rc * 0.985f)), 1.3f * s);
                g.setColour (Colours::black.withAlpha (0.55f * al));
                g.drawLine (Line<float> (P (ang + tooth * 0.42f, rc * 0.80f), P (ang + tooth * 0.42f, rc * 0.985f)), 1.0f * s);
            }
        }
        g.setColour (Colours::black.withAlpha (0.6f * alphaAll));
        g.drawEllipse (Rectangle<float> (rc * 2, rc * 2).withCentre (c), 0.8f * s);
    }
    // chapeau
    {
        ColourGradient cap (theme().capLight, c.x - rcap * 0.75f, c.y - rcap * 0.75f, theme().capDark, c.x + rcap * 0.8f, c.y + rcap * 0.8f, false);
        g.setGradientFill (cap);
        g.setOpacity (alphaAll);
        g.fillEllipse (Rectangle<float> (rcap * 2, rcap * 2).withCentre (c));
        g.setOpacity (1.0f);
        // ombre du chapeau sur la couronne
        g.setColour (Colours::black.withAlpha (0.45f * alphaAll));
        Path sh; sh.addCentredArc (c.x, c.y, rcap + 0.6f * s, rcap + 0.6f * s, 0.0f, MathConstants<float>::pi * 0.15f, MathConstants<float>::pi * 1.35f, true);
        g.strokePath (sh, PathStrokeType (1.6f * s));
        // liseré de lumière en haut à gauche (fixe)
        Path hl; hl.addCentredArc (c.x, c.y, rcap - 0.8f * s, rcap - 0.8f * s, 0.0f, -MathConstants<float>::pi * 0.62f, MathConstants<float>::pi * 0.12f, true);
        g.setColour (Colours::white.withAlpha (0.20f * alphaAll));
        g.strokePath (hl, PathStrokeType (1.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
        // reflet satiné fixe
        const auto hc = c + Point<float> (-0.32f, -0.34f) * rcap;
        ColourGradient spec (Colours::white.withAlpha (0.14f * alphaAll), hc.x, hc.y, Colours::transparentWhite, hc.x + rcap * 0.75f, hc.y, true);
        g.setGradientFill (spec);
        g.fillEllipse (Rectangle<float> (rcap * 2, rcap * 2).withCentre (c));
    }
    // marqueur
    {
        g.setColour (colour.withAlpha (0.25f * ed.glow() * alphaAll));
        g.drawLine (Line<float> (P (a, rcap * 0.28f), P (a, rcap * 0.92f)), 4.0f * s);
        g.setColour (theme().marker.withAlpha (0.95f * alphaAll));
        g.drawLine (Line<float> (P (a, rcap * 0.28f), P (a, rcap * 0.92f)), 1.9f * s);
    }
    // valeur + nom
    const auto tr = Rectangle<float> (0.0f, d + 1.0f * s, (float) getWidth(), 13.0f * s);
    g.setFont (ed.font (1, 11.5f * s));
    g.setColour ((onFacade ? theme().facadeText : cText).withAlpha (alphaAll));
    g.drawText (param != nullptr && format ? format (param->convertFrom0to1 (norm)) : String(), tr, Justification::centred, false);
    g.setFont (ed.font (2, 8.6f * s).withExtraKerningFactor (0.14f));
    g.setColour ((onFacade ? theme().facadeDim : cDim).withAlpha (alphaAll));
    g.drawText (label, tr.translated (0.0f, 12.5f * s), Justification::centred, false);
}

void Knob::mouseDown (const MouseEvent& e)
{
    if (param == nullptr) return;
    if (e.mods.isPopupMenu()) { ed.showParamMidiMenu (param, param->getName (64)); return; }
    dragging = true; dragStart = norm;
    att->beginGesture();
    e.source.enableUnboundedMouseMovement (true);
}

void Knob::mouseDrag (const MouseEvent& e)
{
    if (! dragging || param == nullptr) return;
    const float k = e.mods.isShiftDown() ? 0.0008f : 0.0045f;
    const float v = jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() * k / jmax (0.6f, ed.sc()));
    att->setValueAsPartOfGesture (param->convertFrom0to1 (v));
}

void Knob::mouseUp (const MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    att->endGesture();
    e.source.enableUnboundedMouseMovement (false);
}

void Knob::mouseDoubleClick (const MouseEvent&)
{
    if (param == nullptr) return;
    att->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void Knob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float step = (e.mods.isShiftDown() ? 0.01f : 0.05f) * (w.deltaY >= 0 ? 1.0f : -1.0f) * jmin (1.0f, std::abs (w.deltaY) * 4.0f);
    att->setValueAsCompleteGesture (param->convertFrom0to1 (jlimit (0.0f, 1.0f, norm + step)));
}

//==============================================================================
//  Panneau de la bande sélectionnée
//==============================================================================
BandPanel::BandPanel (QmixEditor& e) : freq (e, "FREQ"), gain (e, "GAIN"), q (e, "Q"), thresh (e, "THRESHOLD"), range (e, "RANGE"), ed (e)
{
    for (auto* k : { &freq, &gain, &q, &thresh, &range }) addAndMakeVisible (k);
    freq.format   = [] (float v) { return hzStr (v); };
    gain.format   = [] (float v) { return dbStr (v); };
    q.format      = [] (float v) { return String (v, v < 10.0f ? 2 : 1); };
    thresh.format = [] (float v) { return String (v, 1) + " dB"; };
    range.format  = [] (float v) { return dbStr (v); };
}

bool BandPanel::dynShown() const
{
    if (current < 0) return false;
    const auto v = ed.proc.bandValues (current);
    return v.dyn && qmix::canBeDynamic (v.type);
}

void BandPanel::setBand (int b)
{
    current = b;
    if (b < 0) { for (auto* k : { &freq, &gain, &q, &thresh, &range }) k->detach(); setVisible (false); return; }
    auto& ap = ed.proc.apvts;
    const auto c = bandColour (b);
    freq.attach   (ap.getParameter (QmixProcessor::bandId (b, "freq")), c, false);
    gain.attach   (ap.getParameter (QmixProcessor::bandId (b, "gain")), c, true);
    q.attach      (ap.getParameter (QmixProcessor::bandId (b, "q")), c, false);
    thresh.attach (ap.getParameter (QmixProcessor::bandId (b, "thresh")), c, false);
    range.attach  (ap.getParameter (QmixProcessor::bandId (b, "range")), c, true);
    setVisible (true);
    resized();
    repaint();
}

void BandPanel::resized()
{
    const float s = ed.sc();
    layoutDyn = dynShown();
    const int kw = (int) (78 * s), kh = (int) (72 * s);
    int i = 0;
    for (auto* k : { &freq, &gain, &q, &thresh, &range })
    {
        const bool show = i < 3 || layoutDyn;
        k->setVisible (show);
        k->setBounds ((int) ((158 + 86 * i) * s), (int) (8 * s), kw, kh);
        ++i;
    }
}

void BandPanel::tick()
{
    if (current < 0) return;
    const auto v = ed.proc.bandValues (current);
    const bool g = qmix::hasGain (v.type);
    if (gain.enabledLook != g) { gain.enabledLook = g; gain.repaint(); }
    for (auto* k : { &freq, &gain, &q, &thresh, &range }) k->tick();
}

void BandPanel::paint (Graphics& g)
{
    if (current < 0) return;
    const float s = ed.sc();
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto v = ed.proc.bandValues (current);
    const auto c = bandColour (current);
    // fond : verre fumé
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (2.0f * s, 3.0f * s), 12.0f * s);
    ColourGradient bg (theme().panelTop, r.getX(), r.getY(), theme().panelBottom, r.getRight(), r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 12.0f * s);
    g.setColour (theme().panelBorder);
    g.drawRoundedRectangle (r, 12.0f * s, 1.0f);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawLine (r.getX() + 12 * s, r.getY() + 1.0f, r.getRight() - 12 * s, r.getY() + 1.0f, 1.0f);

    // en-tête : pastille + numéro
    const float x0 = r.getX() + 14.0f * s;
    g.setColour (c.withAlpha (0.25f * ed.glow()));
    g.fillEllipse (Rectangle<float> (16.0f * s, 16.0f * s).withCentre ({ x0 + 5.0f * s, r.getY() + 18.0f * s }));
    g.setColour (v.on ? c : c.withSaturation (0.0f).darker (0.4f));
    g.fillEllipse (Rectangle<float> (9.0f * s, 9.0f * s).withCentre ({ x0 + 5.0f * s, r.getY() + 18.0f * s }));
    g.setColour (cText);
    g.setFont (ed.font (2, 13.0f * s));
    g.drawText ("BAND " + String (current + 1), Rectangle<float> (x0 + 16.0f * s, r.getY() + 9.0f * s, 90.0f * s, 18.0f * s), Justification::centredLeft, false);

    auto chip = [&] (Rectangle<float> b, const String& t, bool active, bool hl)
    {
        g.setColour (hl ? c.withAlpha (0.16f) : theme().chipBg);
        g.fillRoundedRectangle (b, 5.0f * s);
        g.setColour (hl ? c.withAlpha (0.6f) : theme().chipBorder);
        g.drawRoundedRectangle (b.reduced (0.5f), 5.0f * s, 1.0f);
        g.setColour (active ? cText : cDim.withAlpha (0.5f));
        g.setFont (ed.font (1, 10.5f * s));
        g.drawFittedText (t, b.toNearestInt(), Justification::centred, 1, 0.8f);
    };
    rType  = { x0, r.getY() + 32.0f * s, 128.0f * s, 20.0f * s };
    rSlope = { x0, r.getY() + 56.0f * s, 40.0f * s, 20.0f * s };
    rPlace = { x0 + 43.0f * s, r.getY() + 56.0f * s, 42.0f * s, 20.0f * s };
    rDyn   = { x0 + 88.0f * s, r.getY() + 56.0f * s, 40.0f * s, 20.0f * s };
    chip (rType, String (qmix::typeShort (v.type)) + "  " + String::charToString (0x25BE), true, true);
    chip (rSlope, qmix::hasSlope (v.type) ? String (qmix::slopeDb (v.slope)) + " dB" : String ("--"), qmix::hasSlope (v.type), false);
    chip (rPlace, qmix::placementName (v.place), true, v.place != 0);
    chip (rDyn, "DYN", qmix::canBeDynamic (v.type), v.dyn && qmix::canBeDynamic (v.type));

    // séparateur entre les réglages statiques et dynamiques
    if (layoutDyn)
    {
        const float xs = (158 + 86 * 3 - 4) * s;
        g.setColour (theme().panelBorder);
        g.fillRect (Rectangle<float> (xs, r.getY() + 14 * s, 1.0f, r.getHeight() - 28 * s));
    }

    // marche / arrêt et suppression (colonne de droite)
    const float xr = r.getRight() - 26.0f * s;
    rOn  = Rectangle<float> (22.0f * s, 22.0f * s).withCentre ({ xr, r.getY() + 24.0f * s });
    rDel = Rectangle<float> (22.0f * s, 22.0f * s).withCentre ({ xr, r.getY() + 58.0f * s });
    if (v.on)
    {
        g.setColour (c.withAlpha (0.22f * ed.glow()));
        g.fillEllipse (rOn.expanded (3.0f * s));
    }
    QmixMenu::drawIco (g, QmixMenu::icoBypass, rOn.reduced (3.0f * s), v.on ? c : cDim);
    QmixMenu::drawIco (g, QmixMenu::icoClear, rDel.reduced (5.0f * s), cDim);
}

void BandPanel::mouseDown (const MouseEvent& e)
{
    if (current < 0) return;
    const auto p = e.position;
    const auto v = ed.proc.bandValues (current);
    if (e.mods.isPopupMenu()) { ed.showBandMenu (current); return; }
    if (rType.contains (p))  ed.showTypeMenu (current);
    else if (rSlope.contains (p) && qmix::hasSlope (v.type)) ed.showSlopeMenu (current);
    else if (rPlace.contains (p)) ed.showPlaceMenu (current);
    else if (rDyn.contains (p) && qmix::canBeDynamic (v.type)) ed.proc.setBandParam (current, "dyn", v.dyn ? 0.0f : 1.0f);
    else if (rOn.contains (p))  ed.proc.setBandParam (current, "on", v.on ? 0.0f : 1.0f);
    else if (rDel.contains (p)) { ed.proc.deleteBand (current); ed.selectBand (-1); }
}

//==============================================================================
//  Affichage
//==============================================================================
Display::Display (QmixEditor& e) : ed (e)
{
    freqs.resize (kPts);
    for (int i = 0; i < kPts; ++i) freqs[(size_t) i] = (float) (10.0 * std::pow (3000.0, (double) i / (kPts - 1)));
    for (auto& v : bandDb) v.assign (kPts, 0.0f);
    for (auto& v : rangeDb) v.assign (kPts, 0.0f);
    sumDb.assign (kPts, 0.0f);
    ringPost.assign (kFftSize, 0.0f); ringPre.assign (kFftSize, 0.0f);
    fftBuf.assign (kFftSize * 2, 0.0f); scratch.assign (4096, 0.0f);
    specPost.assign (kPts, -120.0f); specPre.assign (kPts, -120.0f);
    setOpaque (true);
}

Rectangle<float> Display::plot() const
{
    const float s = ed.sc();
    return getLocalBounds().toFloat().withTrimmedRight (30.0f * s).withTrimmedBottom (18.0f * s).withTrimmedTop (4.0f * s);
}

float Display::xOf (double hz) const { const auto p = plot(); return p.getX() + p.getWidth() * (float) (std::log (hz / 10.0) / std::log (3000.0)); }
double Display::hzAt (float x) const { const auto p = plot(); return 10.0 * std::pow (3000.0, (double) ((x - p.getX()) / p.getWidth())); }
float Display::yOf (double db) const { const auto p = plot(); const double R = ed.rangeDb() * 1.1; return p.getCentreY() - (float) (db / R) * p.getHeight() * 0.5f; }
double Display::dbAt (float y) const { const auto p = plot(); const double R = ed.rangeDb() * 1.1; return (double) (p.getCentreY() - y) / (p.getHeight() * 0.5) * R; }

void Display::resized() {}

void Display::rebuildCurves()
{
    const double fs = ed.proc.currentRate.load();
    const double scale = ed.proc.scaleParam->load() * 0.01;
    const int mode = ed.proc.phaseMode.load();
    std::fill (sumDb.begin(), sumDb.end(), 0.0f);
    auto curve = [&] (const QmixProcessor::BandValues& v, double gainDb, bool dynBand, std::vector<float>& out)
    {
        // phase linéaire : module analogique exact ; bandes dynamiques : IIR « Analog » ; sinon : moteur du mode
        if (mode == qmix::linearPhase && ! dynBand)
        {
            const auto d = qmix::designAnalog (v.type, v.freq, gainDb, v.q, v.slope);
            for (int i = 0; i < kPts; ++i) out[(size_t) i] = (float) qmix::analogDb (d, freqs[(size_t) i]);
            return;
        }
        const int flav = (mode == qmix::analog) ? ed.proc.analogFlavour.load() : (int) qmix::flavourClean;
        const auto des = qmix::design (v.type, v.freq, gainDb, v.q, v.slope, fs, mode == qmix::zeroLatency ? qmix::zeroLatency : qmix::analog, nullptr, false, flav);
        for (int i = 0; i < kPts; ++i)
        {
            const double f = freqs[(size_t) i];
            out[(size_t) i] = f < fs * 0.5 ? (float) qmix::responseDb (des, f, fs) : out[(size_t) jmax (0, i - 1)];
        }
    };
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        const auto v = ed.proc.bandValues (b);
        isDyn[(size_t) b] = false;
        if (! v.used) continue;
        const bool dynBand = v.dyn && qmix::canBeDynamic (v.type);
        isDyn[(size_t) b] = dynBand;
        const double live = v.gain * scale + (dynBand && v.on ? (double) ed.proc.dynDb[b].load() : 0.0);
        curve (v, live, dynBand, bandDb[(size_t) b]);
        if (dynBand) curve (v, (v.gain + v.range) * scale, true, rangeDb[(size_t) b]);
        if (v.on) for (int i = 0; i < kPts; ++i) sumDb[(size_t) i] += bandDb[(size_t) b][(size_t) i];
    }
    repaint();
}

void Display::analyse (std::vector<float>& ring, int ringPos, std::vector<float>& spec, bool fast)
{
    for (int i = 0; i < kFftSize; ++i) fftBuf[(size_t) i] = ring[(size_t) ((ringPos + i) % kFftSize)];
    std::fill (fftBuf.begin() + kFftSize, fftBuf.end(), 0.0f);
    window.multiplyWithWindowingTable (fftBuf.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    const double fs = ed.proc.currentRate.load();
    const double binHz = fs / kFftSize;
    const float norm = 2.0f / ((float) kFftSize * 0.35875f);
    for (int i = 0; i < kPts; ++i)
    {
        const double f = freqs[(size_t) i];
        const double f0 = f * std::pow (3000.0, -0.5 / (kPts - 1)), f1 = f * std::pow (3000.0, 0.5 / (kPts - 1));
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        float m = 0.0f;
        if (b1 - b0 < 1.0)
        {
            const double bf = f / binHz;
            const int k = jlimit (0, kFftSize / 2 - 2, (int) bf);
            const float t = (float) (bf - k);
            m = fftBuf[(size_t) k] * (1.0f - t) + fftBuf[(size_t) k + 1] * t;
        }
        else
            for (int k = jmax (0, (int) b0); k <= jmin (kFftSize / 2 - 1, (int) b1); ++k) m = jmax (m, fftBuf[(size_t) k]);
        float db = Decibels::gainToDecibels (m * norm, -150.0f) + 4.5f * (float) std::log2 (f / 1000.0);   // pente 4,5 dB/oct
        if (f > fs * 0.5) db = -150.0f;
        auto& o = spec[(size_t) i];
        o += (db - o) * (db > o ? (fast ? 0.75f : 0.6f) : 0.16f);
    }
}

void Display::updateSpectrum()
{
    const int mode = ed.proc.analyzerMode.load();
    auto pull = [&] (QmixProcessor::Fifo& fifo, std::vector<float>& ring, int& pos)
    {
        for (;;)
        {
            const int got = fifo.pull (scratch.data(), (int) scratch.size());
            if (got <= 0) break;
            for (int i = 0; i < got; ++i) { ring[(size_t) pos] = scratch[(size_t) i]; pos = (pos + 1) % kFftSize; }
        }
    };
    pull (ed.proc.postFifo, ringPost, ringPosPost);
    pull (ed.proc.preFifo, ringPre, ringPosPre);
    if (mode >= 1) analyse (ringPost, ringPosPost, specPost, false);
    if (mode == 2) analyse (ringPre, ringPosPre, specPre, false);
    if (mode > 0) ed.askRepaint (getBounds());
}

Point<float> Display::nodePos (int b) const
{
    const auto v = ed.proc.bandValues (b);
    const double scale = ed.proc.scaleParam->load() * 0.01;
    double db = 0.0;
    if (qmix::hasGain (v.type)) db = v.gain * scale;
    else if (qmix::hasSlope (v.type))
    {
        const double fs = ed.proc.currentRate.load();
        db = jlimit (-ed.rangeDb(), ed.rangeDb(), qmix::responseDb (qmix::design (v.type, v.freq, 0, v.q, v.slope, fs), v.freq, fs));
    }
    return { xOf (v.freq), yOf (jlimit (-ed.rangeDb() * 1.08, ed.rangeDb() * 1.08, db)) };
}

int Display::nodeAt (Point<float> p) const
{
    const float s = ed.sc();
    int best = -1; float bd = 11.0f * s;
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        if (ed.proc.bandParam[b].used->load() < 0.5f) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bd || (b == ed.selected && d < bd + 2.0f * s)) { bd = d; best = b; }
    }
    return best;
}

void Display::paint (Graphics& g)
{
    const float s = ed.sc();
    const auto full = getLocalBounds().toFloat();
    const auto p = plot();
    const bool power = ed.proc.powerParam->load() > 0.5f;

    // fond de l'écran
    g.fillAll (cBg);
    ColourGradient bg (theme().displayTop, 0, full.getY(), theme().displayBottom, 0, full.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (full, 10.0f * s);

    // grille : fréquences
    g.setFont (ed.font (1, 9.5f * s));
    for (double dec = 10.0; dec < 30000.0; dec *= 10.0)
        for (int m = 1; m < 10; ++m)
        {
            const double f = dec * m;
            if (f < 10.0 || f > 30000.0) continue;
            const float x = xOf (f);
            g.setColour (m == 1 ? cGridMajor : cGrid);
            g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
            const bool lab = (m == 2 || m == 5 || m == 1) && f >= 20.0 && f <= 20000.0;
            if (lab)
            {
                g.setColour (cLabel);
                const String t = f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f);
                g.drawText (t, Rectangle<float> (x - 20 * s, p.getBottom() + 2 * s, 40 * s, 14 * s), Justification::centred, false);
            }
        }
    // grille : gain
    const double R = ed.rangeDb();
    const double step = R <= 6 ? 2 : R <= 12 ? 3 : 6;
    for (double d = -R; d <= R + 0.01; d += step)
    {
        const float y = yOf (d);
        g.setColour (std::abs (d) < 0.01 ? cGridMajor.brighter (0.25f) : cGrid);
        g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
        g.setColour (std::abs (d) < 0.01 ? cDim : cLabel);
        g.drawText ((d > 0 ? "+" : "") + String ((int) d), Rectangle<float> (p.getRight() + 2 * s, y - 7 * s, 26 * s, 14 * s), Justification::centredLeft, false);
    }

    g.saveState();
    g.reduceClipRegion (p.toNearestInt());

    // analyseur
    const int mode = ed.proc.analyzerMode.load();
    auto specY = [&] (float db) { return p.getY() + p.getHeight() * jlimit (0.0f, 1.2f, (-db - 6.0f) / 90.0f); };
    auto specPath = [&] (const std::vector<float>& sp, bool close)
    {
        Path path;
        const double nyq = ed.proc.currentRate.load() * 0.5;
        float lastX = p.getX();
        for (int i = 0; i < kPts; ++i)
        {
            if (freqs[(size_t) i] > nyq) break;                     // rien au-delà de Nyquist
            const float x = p.getX() + p.getWidth() * (float) i / (kPts - 1);
            const float y = specY (sp[(size_t) i]);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            lastX = x;
        }
        if (close) { path.lineTo (lastX, p.getBottom() + 4); path.lineTo (p.getX(), p.getBottom() + 4); path.closeSubPath(); }
        return path;
    };
    if (mode == 2)
    {
        g.setColour (theme().preLine.withAlpha (0.35f));
        g.strokePath (specPath (specPre, false), PathStrokeType (1.0f * s));
    }
    if (mode >= 1)
    {
        auto fill = specPath (specPost, true);
        ColourGradient sg (theme().specTop.withAlpha (0.42f), 0, p.getY() + p.getHeight() * 0.2f, theme().specBottom.withAlpha (0.10f), 0, p.getBottom(), false);
        g.setGradientFill (sg);
        g.fillPath (fill);
        g.setColour (theme().specLine.withAlpha (0.45f));
        g.strokePath (specPath (specPost, false), PathStrokeType (1.0f * s));
    }

    // courbes des bandes
    auto curvePath = [&] (const std::vector<float>& db, bool close)
    {
        Path path;
        for (int i = 0; i < kPts; ++i)
        {
            const float x = p.getX() + p.getWidth() * (float) i / (kPts - 1);
            const float y = yOf (jlimit (-R * 1.6, R * 1.6, (double) db[(size_t) i]));
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        if (close) { path.lineTo (p.getRight(), yOf (0)); path.lineTo (p.getX(), yOf (0)); path.closeSubPath(); }
        return path;
    };
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        const auto v = ed.proc.bandValues (b);
        if (! v.used) continue;
        const bool focus = b == ed.selected || b == hover;
        const auto c = bandColour (b).withMultipliedSaturation (v.on && power ? 1.0f : 0.0f);
        g.setColour (c.withAlpha (focus ? 0.20f : (qmix::hasSlope (v.type) ? 0.035f : 0.07f)));
        g.fillPath (curvePath (bandDb[(size_t) b], true));
        if (focus)
        {
            g.setColour (c.withAlpha (0.75f));
            g.strokePath (curvePath (bandDb[(size_t) b], false), PathStrokeType (1.3f * s));
        }
        if (isDyn[(size_t) b])                                    // bande dynamique : limite de la plage en pointillés
        {
            Path dashed;
            const float dl[2] = { 4.0f * s, 3.0f * s };
            PathStrokeType (1.1f * s).createDashedStroke (dashed, curvePath (rangeDb[(size_t) b], false), dl, 2);
            g.setColour (c.withAlpha (focus ? 0.8f : 0.45f));
            g.fillPath (dashed);
        }
    }

    // courbe totale (halo selon Glow)
    {
        const auto path = curvePath (sumDb, false);
        const auto c = power ? theme().curve : Colour (0xff6c7480);
        const float gl = ed.glow();
        if (power)
        {
            g.setColour (cAccent.withAlpha (0.05f * gl)); g.strokePath (path, PathStrokeType (10.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (cAccent.withAlpha (0.09f * gl)); g.strokePath (path, PathStrokeType (5.5f * s, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (path, PathStrokeType (2.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.restoreState();

    // points des bandes
    bool any = false;
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        const auto v = ed.proc.bandValues (b);
        if (! v.used) continue;
        any = true;
        const auto np = nodePos (b);
        const bool sel = b == ed.selected, hov = b == hover;
        const auto c = bandColour (b);
        const float rr = (sel || hov ? 7.5f : 6.5f) * s;
        if (v.on && power)
        {
            ColourGradient halo (c.withAlpha ((sel ? 0.45f : 0.22f) * ed.glow()), np.x, np.y, c.withAlpha (0.0f), np.x + rr * 3.2f, np.y, true);
            g.setGradientFill (halo);
            g.fillEllipse (Rectangle<float> (rr * 6.4f, rr * 6.4f).withCentre (np));
        }
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillEllipse (Rectangle<float> (rr * 2.0f + 2.0f * s, rr * 2.0f + 2.0f * s).withCentre (np.translated (0.8f * s, 1.0f * s)));
        if (v.on)
        {
            ColourGradient fillc (c.brighter (0.35f), np.x - rr * 0.6f, np.y - rr * 0.6f, c.darker (0.25f), np.x + rr * 0.7f, np.y + rr * 0.7f, false);
            g.setGradientFill (fillc);
            g.fillEllipse (Rectangle<float> (rr * 2, rr * 2).withCentre (np));
        }
        else
        {
            g.setColour (theme().displayTop);
            g.fillEllipse (Rectangle<float> (rr * 2, rr * 2).withCentre (np));
            g.setColour (c.withAlpha (0.7f));
            g.drawEllipse (Rectangle<float> (rr * 2, rr * 2).withCentre (np).reduced (0.7f * s), 1.4f * s);
        }
        if (isDyn[(size_t) b] && v.on)
        {
            const double scale = ed.proc.scaleParam->load() * 0.01;
            const float y0 = np.y, y1 = yOf (jlimit (-ed.rangeDb() * 1.08, ed.rangeDb() * 1.08, (double) (v.gain + v.range) * scale));
            g.setColour (c.withAlpha (0.55f));
            g.drawLine (np.x, y0 + (y1 > y0 ? rr : -rr), np.x, y1, 1.6f * s);
            g.fillEllipse (Rectangle<float> (4.0f * s, 4.0f * s).withCentre ({ np.x, y1 }));
            const float amt = std::abs (v.range) > 0.01f ? jlimit (0.0f, 1.0f, std::abs (ed.proc.dynDb[b].load()) / (std::abs (v.range) * (float) scale + 1.0e-6f)) : 0.0f;
            Path arc; arc.addCentredArc (np.x, np.y, rr + 4.0f * s, rr + 4.0f * s, 0.0f, 0.0f, MathConstants<float>::twoPi * jmax (0.02f, amt), true);
            g.setColour (c.withAlpha (0.25f)); g.drawEllipse (Rectangle<float> (rr * 2 + 8 * s, rr * 2 + 8 * s).withCentre (np), 1.4f * s);
            g.setColour (c); g.strokePath (arc, PathStrokeType (2.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (sel)
        {
            g.setColour (Colours::white.withAlpha (0.9f));
            g.drawEllipse (Rectangle<float> (rr * 2 + 5 * s, rr * 2 + 5 * s).withCentre (np), 1.4f * s);
        }
        g.setColour (v.on ? theme().nodeText : c);
        g.setFont (ed.font (2, 8.8f * s));
        g.drawText (String (b + 1), Rectangle<float> (rr * 2, rr * 2).withCentre (np.translated (0, 0.4f * s)), Justification::centred, false);

        if (hov && dragBand < 0)                                  // bulle de lecture
        {
            String t = hzStr (v.freq) + (qmix::hasGain (v.type) ? "   " + dbStr (v.gain) : String()) + "   " + qStr (v.q);
            if (isDyn[(size_t) b]) t << "   DYN " << dbStr (ed.proc.dynDb[b].load());
            g.setFont (ed.font (1, 10.5f * s));
            const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 16 * s;
            auto br = Rectangle<float> (tw, 20 * s).withCentre (np.translated (0, -24 * s));
            br = br.withX (jlimit (p.getX(), p.getRight() - tw, br.getX()));
            if (br.getY() < p.getY()) br = br.withY (np.y + 14 * s);
            g.setColour (theme().panelTop.withAlpha (0.93f)); g.fillRoundedRectangle (br, 6 * s);
            g.setColour (c.withAlpha (0.6f)); g.drawRoundedRectangle (br, 6 * s, 1.0f);
            g.setColour (cText); g.drawText (t, br, Justification::centred, false);
        }
    }
    if (! any)
    {
        const auto hr = p.withSizeKeepingCentre (230 * s, 26 * s).translated (0, 52 * s);
        g.setColour (theme().displayTop.withAlpha (0.88f)); g.fillRoundedRectangle (hr, 13 * s);
        g.setColour (theme().panelBorder); g.drawRoundedRectangle (hr, 13 * s, 1.0f);
        g.setColour (cText.withAlpha (0.85f));
        g.setFont (ed.font (1, 12.0f * s));
        g.drawText ("Double-click to add a band", hr, Justification::centred, false);
    }

    // cadre en creux (ombre intérieure côté lumière)
    g.setColour (Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (full.reduced (0.5f), 10.0f * s, 1.2f);
    g.setColour (Colours::white.withAlpha (0.045f));
    g.drawLine (full.getX() + 10 * s, full.getBottom() - 0.5f, full.getRight() - 10 * s, full.getBottom() - 0.5f, 1.0f);
}

void Display::mouseMove (const MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}

void Display::mouseExit (const MouseEvent&) { if (hover >= 0) { hover = -1; repaint(); } }

void Display::mouseDown (const MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (n >= 0) { ed.selectBand (n); ed.showBandMenu (n); }
        else ed.showMainMenu();
        return;
    }
    if (n < 0) { ed.selectBand (-1); return; }
    ed.selectBand (n);
    dragBand = n;
    dragOrigin = e.position;
    const auto v = ed.proc.bandValues (n);
    dragF0 = v.freq; dragG0 = v.gain;
    for (auto* id : { "freq", "gain" })
        if (auto* p = ed.proc.apvts.getParameter (QmixProcessor::bandId (n, id))) p->beginChangeGesture();
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (dragBand < 0) return;
    const float k = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const auto d = (e.position - dragOrigin) * k;
    const double f = jlimit (10.0, 30000.0, hzAt (xOf (dragF0) + d.x));
    const auto v = ed.proc.bandValues (dragBand);
    auto* pf = ed.proc.apvts.getParameter (QmixProcessor::bandId (dragBand, "freq"));
    pf->setValueNotifyingHost (pf->convertTo0to1 ((float) f));
    if (qmix::hasGain (v.type))
    {
        const double scale = jmax (0.01, ed.proc.scaleParam->load() * 0.01);
        const double gdb = jlimit (-30.0, 30.0, (dbAt (yOf (dragG0 * scale) + d.y)) / scale);
        auto* pg = ed.proc.apvts.getParameter (QmixProcessor::bandId (dragBand, "gain"));
        pg->setValueNotifyingHost (pg->convertTo0to1 ((float) gdb));
    }
    ed.proc.presetDirty = true;
}

void Display::mouseUp (const MouseEvent&)
{
    if (dragBand < 0) return;
    for (auto* id : { "freq", "gain" })
        if (auto* p = ed.proc.apvts.getParameter (QmixProcessor::bandId (dragBand, id))) p->endChangeGesture();
    dragBand = -1;
    repaint();
}

void Display::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int n = nodeAt (e.position);
    if (n >= 0)
    {
        const auto v = ed.proc.bandValues (n);
        ed.proc.setBandParam (n, "on", v.on ? 0.0f : 1.0f);
        return;
    }
    if (! plot().contains (e.position)) return;
    const double f = jlimit (10.0, 30000.0, hzAt (e.position.x));
    const int type = f < 30.0 ? qmix::lowCut : f > 16000.0 ? qmix::highCut : qmix::bell;
    const double scale = jmax (0.01, ed.proc.scaleParam->load() * 0.01);
    const int b = ed.proc.createBand ((float) f, (float) jlimit (-30.0, 30.0, dbAt (e.position.y) / scale), type);
    if (b >= 0) ed.selectBand (b);
    else ed.showStatus ("All 24 bands are in use");
}

void Display::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int b = nodeAt (e.position) >= 0 ? nodeAt (e.position) : ed.selected;
    if (b < 0) return;
    auto* pq = ed.proc.apvts.getParameter (QmixProcessor::bandId (b, "q"));
    const float step = (e.mods.isShiftDown() ? 0.01f : 0.04f) * (w.deltaY >= 0 ? 1.0f : -1.0f) * jmin (1.0f, std::abs (w.deltaY) * 4.0f);
    pq->beginChangeGesture();
    pq->setValueNotifyingHost (jlimit (0.0f, 1.0f, pq->getValue() + step));
    pq->endChangeGesture();
    ed.proc.presetDirty = true;
}

//==============================================================================
//  Fenêtre
//==============================================================================
QmixEditor::QmixEditor (QmixProcessor& p)
    : AudioProcessorEditor (p), proc (p), display (*this), panel (*this), outKnob (*this, "OUTPUT"), scaleKnob (*this, "GAIN SCALE"), colorKnob (*this, "COLOR")
{
    fonts.reg  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
    fonts.med  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
    fonts.bold = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    menuLnf.f = fonts;
    logo = Drawable::createFromImageData (BinaryData::NineHandsLogoWhite_svg, BinaryData::NineHandsLogoWhite_svgSize);
    outKnob.onFacade = scaleKnob.onFacade = true;

    setOpaque (true);
    addAndMakeVisible (display);
    addChildComponent (panel);
    addAndMakeVisible (outKnob);
    addAndMakeVisible (scaleKnob);
    addAndMakeVisible (colorKnob);
    colorKnob.attach (proc.apvts.getParameter ("color"), cAccent, false);
    colorKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    colorKnob.onFacade = true;
    outKnob.attach (proc.apvts.getParameter ("output"), cAccent, true);
    outKnob.format = [] (float v) { return dbStr (v); };
    scaleKnob.attach (proc.apvts.getParameter ("scale"), cAccent, false);
    scaleKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };

    setResizable (true, true);
    setResizeLimits (kW, kH, kMaxW, kMaxH);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setScaleIndex (proc.uiSizeIdx.load());

    for (int b = 0; b < qmix::kNumBands; ++b) lastVals[(size_t) b] = proc.bandValues (b);
    lastOut = proc.outParam->load(); lastScale = proc.scaleParam->load(); lastPower = proc.powerParam->load() > 0.5f;
    powerGlow = lastPower ? 1.0f : 0.0f;
    lastMidiAct = proc.midiActivity.load();
    display.rebuildCurves();
    startTimerHz (30);
}

QmixEditor::~QmixEditor() { stopTimer(); hostLock.detach(); }

Font QmixEditor::font (int w, float h) const { return fnt (w == 0 ? fonts.reg : w == 1 ? fonts.med : fonts.bold, h); }

void QmixEditor::setScaleIndex (int idx)
{
    static const int ws[3] = { kW, 1200, 1600 };
    setSize (ws[jlimit (0, 2, idx)], ws[jlimit (0, 2, idx)] * kH / kW);
}

void QmixEditor::resized()
{
    const float s = sc();
    display.setBounds (Rectangle<float> (10 * s, 46 * s, 940 * s, 424 * s).toNearestInt());
    panel.setBounds (Rectangle<float> (0, 0, panel.desiredWidth() * s, 90 * s).withCentre ({ 480 * s, (470 - 14 - 45) * s }).toNearestInt());
    panel.resized();
    colorKnob.setBounds (Rectangle<float> (690 * s, 474 * s, 64 * s, 64 * s).toNearestInt());
    scaleKnob.setBounds (Rectangle<float> (772 * s, 474 * s, 64 * s, 64 * s).toNearestInt());
    outKnob.setBounds   (Rectangle<float> (860 * s, 474 * s, 64 * s, 64 * s).toNearestInt());
    if (! (hostLock.hostMaximized()))
        hostLock.setMinimumSize (kW, kH);
}

// ------------------------------------------------------------------ zones de la façade
Rectangle<float> QmixEditor::pill() const     { const float s = sc(); return Rectangle<float> (236 * s, 24 * s).withCentre ({ 480 * s, 23 * s }); }
Rectangle<float> QmixEditor::pillPrev() const { const auto p = pill(); return p.withWidth (p.getHeight() * 0.9f); }
Rectangle<float> QmixEditor::pillNext() const { const auto p = pill(); return p.withTrimmedLeft (p.getWidth() - p.getHeight() * 0.9f); }
Rectangle<float> QmixEditor::powerRect() const { const float s = sc(); return Rectangle<float> (24 * s, 24 * s).withCentre ({ 930 * s, 23 * s }); }
Rectangle<float> QmixEditor::segAnalyzer (int i) const { const float s = sc(); return { (24 + 54 * i) * s, 498 * s, 52 * s, 22 * s }; }
Rectangle<float> QmixEditor::segMode (int i) const     { const float s = sc(); return { (676 + 72 * i) * s, 12 * s, 70 * s, 22 * s }; }
Rectangle<float> QmixEditor::segRange (int i) const    { const float s = sc(); return { (226 + 40 * i) * s, 498 * s, 38 * s, 22 * s }; }

void QmixEditor::paint (Graphics& g)
{
    lastPaint = nowS(); pendingSince = 0;
    const float s = sc();
    g.fillAll (cBg);
    paintFacade (g, getLocalBounds().toFloat(), false);

    // logo NINE HANDS | DSP + nom
    if (auto* lg = logo.get())
        lg->drawWithin (g, Rectangle<float> (18 * s, 17 * s, 132 * s, 12 * s), RectanglePlacement::xLeft | RectanglePlacement::yMid, 0.92f);
    g.setColour (theme().facadeText.withAlpha (0.18f));
    g.fillRect (Rectangle<float> (162 * s, 14 * s, 1.0f, 18 * s));
    g.setColour (theme().facadeText);
    g.setFont (font (2, 18 * s));
    g.drawText ("QMix", Rectangle<float> (174 * s, 12 * s, 70 * s, 22 * s), Justification::centredLeft, false);
    g.setColour (theme().facadeDim);
    g.setFont (font (1, 8.6f * s).withExtraKerningFactor (0.16f));
    g.drawText ("PARAMETRIC EQ", Rectangle<float> (228 * s, 15 * s, 120 * s, 18 * s), Justification::centredLeft, false);

    // pastille presets / ligne d'état
    {
        const auto p = pill();
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (p.translated (0, 1.0f), p.getHeight() * 0.5f);         // arête éclairée (bas)
        g.setColour (theme().pillBorder.darker (0.3f));
        g.fillRoundedRectangle (p.expanded (1.0f), p.getHeight() * 0.5f + 1.0f);
        g.setColour (cFace);
        g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
        g.setColour (theme().pillBorder);
        g.drawRoundedRectangle (p.reduced (0.5f), p.getHeight() * 0.5f, 1.0f);
        const float a = 1.0f - statusAlpha;
        g.setFont (font (1, 12.6f * s));
        if (a > 0.01f)
        {
            g.setColour (cText.withAlpha (a));
            const String name = proc.presetName + (proc.presetDirty.load() ? " *" : "");
            g.drawFittedText (name, p.reduced (p.getHeight() * 0.95f, 0).toNearestInt(), Justification::centred, 1, 0.8f);
            for (int side = 0; side < 2; ++side)
            {
                const auto r = side == 0 ? pillPrev() : pillNext();
                const bool hl = hoverZone == (side == 0 ? 1 : 2);
                Path t;
                const float cx = r.getCentreX() + (side == 0 ? 2.0f : -2.0f) * s, cy = r.getCentreY(), h = 5.5f * 0.548f * s * 1.75f;
                if (side == 0) t.addTriangle (cx + h * 0.6f, cy - h, cx + h * 0.6f, cy + h, cx - h * 0.8f, cy);
                else           t.addTriangle (cx - h * 0.6f, cy - h, cx - h * 0.6f, cy + h, cx + h * 0.8f, cy);
                g.setColour ((hl ? Colours::white : cText.withAlpha (0.6f)).withMultipliedAlpha (a));
                g.fillPath (t);
            }
        }
        if (statusAlpha > 0.01f)
        {
            g.setColour (cAccent.withAlpha (statusAlpha));
            g.drawFittedText (status, p.reduced (p.getHeight() * 0.5f, 0).toNearestInt(), Justification::centred, 1, 0.6f);
        }
    }


    // interrupteur général
    {
        const auto r = powerRect();
        const auto c = cAccent;
        if (powerGlow > 0.01f)
        {
            ColourGradient halo (c.withAlpha (0.35f * glow() * powerGlow), r.getCentreX(), r.getCentreY(), c.withAlpha (0.0f), r.getCentreX() + r.getWidth(), r.getCentreY(), true);
            g.setGradientFill (halo);
            g.fillEllipse (r.expanded (r.getWidth() * 0.5f));
        }
        g.setColour (cFace);
        g.fillEllipse (r);
        g.setColour (theme().pillBorder);
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        QmixMenu::drawIco (g, QmixMenu::icoBypass, r.reduced (5.5f * s), c.interpolatedWith (cDim.withAlpha (0.6f), 1.0f - powerGlow));
    }

    // barre du bas : analyseur + échelle ; en haut : mode de traitement
    auto seg = [&] (Rectangle<float> r, const String& t, bool on, bool first, bool last)
    {
        Path p; p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6 * s, 6 * s, first, last, first, last);
        g.setColour (theme().segBg);
        g.fillPath (p);
        if (on) { g.setColour (cAccent.withAlpha (0.18f)); g.fillPath (p); }
        g.setColour (on ? cAccent.withAlpha (0.8f) : theme().segBorder);
        g.strokePath (p, PathStrokeType (1.0f));
        g.setColour (on ? Colours::white : theme().facadeDim);
        g.setFont (font (1, 10.5f * s));
        g.drawText (t, r, Justification::centred, false);
    };
    g.setFont (font (2, 8.6f * s).withExtraKerningFactor (0.16f));
    g.setColour (theme().facadeDim);
    g.drawText ("ANALYZER", Rectangle<float> (24 * s, 482 * s, 160 * s, 14 * s), Justification::centredLeft, false);
    g.drawText ("RANGE", Rectangle<float> (226 * s, 482 * s, 160 * s, 14 * s), Justification::centredLeft, false);
    static const char* an[3] = { "OFF", "POST", "PRE+POST" };
    for (int i = 0; i < 3; ++i) seg (segAnalyzer (i).withWidth (i == 2 ? 70 * s : 52 * s).withX (segAnalyzer (i).getX() + (i == 2 ? 0 : 0)), an[i], proc.analyzerMode.load() == i, i == 0, i == 2);
    const String md[3] = { "ZERO LAT", qmix::flavourShort (proc.analogFlavour.load()), "LINEAR" };
    for (int i = 0; i < 3; ++i) seg (segMode (i), md[i], proc.phaseMode.load() == i, i == 0, i == 2);
    static const char* rn[4] = { "6", "12", "24", "30" };
    for (int i = 0; i < 4; ++i) seg (segRange (i), String (rn[i]) + " dB", proc.rangeIdx.load() == i, i == 0, i == 3);

    // nombre de bandes utilisées
    int used = 0;
    for (int b = 0; b < qmix::kNumBands; ++b) used += proc.bandParam[b].used->load() > 0.5f ? 1 : 0;
    g.setFont (font (1, 10.5f * s));
    g.setColour (theme().facadeDim);
    g.drawText (String (used) + " / 24 BANDS", Rectangle<float> (420 * s, 498 * s, 120 * s, 22 * s), Justification::centredLeft, false);
    const int lat = proc.latencyFor (proc.phaseMode.load(), proc.lpQuality.load());
    const double ms = 1000.0 * lat / jmax (1.0, proc.currentRate.load());
    g.drawText ("LATENCY  " + (lat == 0 ? String ("0 ms") : String (ms, 1) + " ms"), Rectangle<float> (540 * s, 498 * s, 160 * s, 22 * s), Justification::centredLeft, false);
}

void QmixEditor::mouseMove (const MouseEvent& e)
{
    const int z = pillPrev().contains (e.position) ? 1 : pillNext().contains (e.position) ? 2 : 0;
    if (z != hoverZone) { hoverZone = z; repaint (pill().expanded (4).toNearestInt()); }
}

void QmixEditor::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    if (e.mods.isPopupMenu())
    {
        if (segMode (qmix::linearPhase).contains (p)) showQualityMenu();
        else if (segMode (qmix::analog).contains (p)) showFlavourMenu();
        else showMainMenu();
        return;
    }
    if (pill().contains (p))
    {
        if (statusAlpha > 0.05f) { statusTime = -10.0; return; }       // la ligne d'état s'efface, la barre revient
        if (pillPrev().contains (p)) prevNextPreset (-1);
        else if (pillNext().contains (p)) prevNextPreset (1);
        else
        {
            PopupMenu m; m.setLookAndFeel (&menuLnf);
            QmixMenu::addHeader (m, fonts, QmixMenu::icoPreset, cAccent, "Presets", proc.presetName);
            const auto& fp = QmixProcessor::factoryPresets();
            for (int i = 0; i < (int) fp.size(); ++i)
                m.addItem (i + 1, fp[(size_t) i].name, true, proc.currentPreset.load() == i && ! proc.presetDirty.load());
            m.addSeparator();
            m.addItem (900, "Save preset...");
            m.addItem (901, "Load preset...");
            m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (pill().toNearestInt())), [this] (int r)
            {
                if (r >= 1 && r < 900) { proc.loadFactoryPreset (r - 1); selectBand (-1); }
                else if (r == 900) savePreset();
                else if (r == 901) loadPreset();
                repaint();
            });
        }
        return;
    }
    if (powerRect().contains (p))
    {
        auto* pw = proc.apvts.getParameter ("power");
        pw->setValueNotifyingHost (pw->getValue() > 0.5f ? 0.0f : 1.0f);
        return;
    }
    for (int i = 0; i < 3; ++i) if (segAnalyzer (i).withWidth (i == 2 ? 70 * sc() : 52 * sc()).contains (p)) { proc.analyzerMode = i; repaint(); display.repaint(); return; }
    for (int i = 0; i < 4; ++i) if (segRange (i).contains (p)) { proc.rangeIdx = i; display.rebuildCurves(); repaint(); return; }
    for (int i = 0; i < 3; ++i) if (segMode (i).contains (p))
    {
        if (e.mods.isPopupMenu() || (i == qmix::linearPhase && proc.phaseMode.load() == qmix::linearPhase)) { showQualityMenu(); return; }
        if (i == qmix::analog && proc.phaseMode.load() == qmix::analog) { showFlavourMenu(); return; }
        proc.phaseMode = i; display.rebuildCurves(); repaint(); showStatus (String (qmix::modeName (i)).toUpperCase());
        return;
    }
    selectBand (-1);
}

void QmixEditor::prevNextPreset (int dir)
{
    const int n = (int) QmixProcessor::factoryPresets().size();
    const int cur = proc.currentPreset.load();
    proc.loadFactoryPreset (((cur < 0 ? 0 : cur) + dir + n) % n);
    selectBand (-1);
    repaint();
}

static File presetFolder()
{
    auto f = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Nine Hands DSP").getChildFile ("QMix");
    f.createDirectory();
    return f;
}

void QmixEditor::savePreset()
{
    chooser = std::make_unique<FileChooser> ("Save QMix preset", presetFolder().getChildFile (proc.presetName + ".qmix"), "*.qmix");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        f = f.withFileExtension ("qmix");
        if (auto xml = proc.savePresetTree().createXml()) xml->writeTo (f);
        proc.presetName = f.getFileNameWithoutExtension();
        proc.presetDirty = false; proc.currentPreset = -1;
        repaint();
    });
}

void QmixEditor::loadPreset()
{
    chooser = std::make_unique<FileChooser> ("Load QMix preset", presetFolder(), "*.qmix");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        if (auto xml = XmlDocument::parse (f))
            proc.loadPresetTree (ValueTree::fromXml (*xml), f.getFileNameWithoutExtension());
        selectBand (-1);
        repaint();
    });
}

void QmixEditor::selectBand (int b)
{
    if (b >= 0 && proc.bandParam[b].used->load() < 0.5f) b = -1;
    selected = b;
    panel.setBand (b);
    resized();
    display.repaint();
}

void QmixEditor::showStatus (const String& s) { status = s; statusTime = nowS(); }

String QmixEditor::bandSummary (int b) const
{
    const auto v = proc.bandValues (b);
    String t = "BAND " + String (b + 1) + "  " + qmix::typeShort (v.type) + "  " + hzStr (v.freq);
    if (qmix::hasGain (v.type)) t << "  " << dbStr (v.gain);
    if (qmix::hasSlope (v.type)) t << "  " << qmix::slopeDb (v.slope) << " dB/oct";
    t << "  " << qStr (v.q);
    if (v.place != 0) t << "  " << qmix::placementShort (v.place);
    if (v.dyn && qmix::canBeDynamic (v.type)) t << "  DYN " << String (v.thresh, 1) << " / " << dbStr (v.range);
    if (! v.on) t << "  OFF";
    return t;
}

// ------------------------------------------------------------------ menus (toujours ouverts là où on clique)
PopupMenu::Options QmixEditor::menuAt() const { return PopupMenu::Options().withMousePosition(); }

void QmixEditor::showTypeMenu (int band)
{
    const auto v = proc.bandValues (band);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoShape, bandColour (band), "Band " + String (band + 1), "Shape");
    for (int t = 0; t < qmix::numTypes; ++t) m.addItem (t + 1, qmix::typeName (t), true, v.type == t);
    m.showMenuAsync (menuAt(), [this, band] (int r) { if (r > 0) proc.setBandParam (band, "type", (float) (r - 1)); });
}

void QmixEditor::showSlopeMenu (int band)
{
    const auto v = proc.bandValues (band);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoSlope, bandColour (band), "Band " + String (band + 1), "Slope");
    for (int i = 0; i < qmix::kNumSlopes; ++i) m.addItem (i + 1, String (qmix::slopeDb (i)) + " dB/oct", true, v.slope == i);
    m.showMenuAsync (menuAt(), [this, band] (int r) { if (r > 0) proc.setBandParam (band, "slope", (float) (r - 1)); });
}

void QmixEditor::showPlaceMenu (int band)
{
    const auto v = proc.bandValues (band);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoStereo, bandColour (band), "Band " + String (band + 1), "Stereo placement");
    for (int i = 0; i < qmix::numPlacements; ++i) m.addItem (i + 1, qmix::placementName (i), true, v.place == i);
    m.showMenuAsync (menuAt(), [this, band] (int r) { if (r > 0) proc.setBandParam (band, "place", (float) (r - 1)); });
}

void QmixEditor::showFlavourMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoEq, cAccent, "Analog", "Console character");
    static const char* sub[qmix::numFlavours] = { "Clean (constant Q)", "SSL 4000 G  (292 Pink Knob)", "Neve VR Legend" };
    for (int i = 0; i < qmix::numFlavours; ++i) m.addItem (1 + i, sub[i], true, proc.analogFlavour.load() == i);
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r <= 0) return;
        proc.analogFlavour = r - 1; proc.phaseMode = qmix::analog;
        showStatus ("ANALOG  " + String (qmix::flavourName (r - 1)).toUpperCase());
        display.rebuildCurves(); repaint();
    });
}

void QmixEditor::showQualityMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoEq, cAccent, "Linear Phase", "Quality / latency");
    static const char* qn[3] = { "Low", "Medium", "High" };
    const double fs = jmax (1.0, proc.currentRate.load());
    for (int i = 0; i < 3; ++i)
        m.addItem (1 + i, String (qn[i]) + "  (" + String (1000.0 * QmixProcessor::firLength (i) / 2 / fs, 1) + " ms)", true, proc.lpQuality.load() == i);
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r > 0) { proc.lpQuality = r - 1; proc.phaseMode = qmix::linearPhase; display.rebuildCurves(); repaint(); }
    });
}

void QmixEditor::showBandMenu (int band)
{
    const auto v = proc.bandValues (band);
    const auto c = bandColour (band);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoEq, c, "Band " + String (band + 1), String (qmix::typeName (v.type)) + "  " + hzStr (v.freq));
    PopupMenu types, slopes, places, midi;
    for (int t = 0; t < qmix::numTypes; ++t) types.addItem (100 + t, qmix::typeName (t), true, v.type == t);
    for (int i = 0; i < qmix::kNumSlopes; ++i) slopes.addItem (200 + i, String (qmix::slopeDb (i)) + " dB/oct", true, v.slope == i);
    for (int i = 0; i < qmix::numPlacements; ++i) places.addItem (300 + i, qmix::placementName (i), true, v.place == i);
    const char* ids[5] = { "freq", "gain", "q", "thresh", "range" };
    const char* nm[5] = { "Frequency", "Gain", "Q", "Threshold", "Dynamic range" };
    for (int i = 0; i < (v.dyn ? 5 : 3); ++i)
    {
        const int pi = proc.paramIndexOf (QmixProcessor::bandId (band, ids[i]));
        const int cc = proc.ccForParam (pi);
        midi.addItem (400 + i, "MIDI Learn: " + String (nm[i]) + (cc >= 0 ? "  (CC " + String (cc) + ")" : String()), i != 1 || qmix::hasGain (v.type), proc.learnParam.load() == pi);
    }
    m.addSubMenu ("Shape", types);
    m.addSubMenu ("Slope", slopes, qmix::hasSlope (v.type));
    m.addSubMenu ("Stereo placement", places, getAudioProcessor()->getTotalNumOutputChannels() > 1);
    m.addItem (500, "Enabled", true, v.on);
    m.addItem (502, "Dynamic", qmix::canBeDynamic (v.type), v.dyn);
    m.addSeparator();
    m.addSubMenu ("MIDI", midi);
    m.addSeparator();
    m.addItem (501, "Delete band");
    m.showMenuAsync (menuAt(), [this, band, v, ids] (int r)
    {
        if (r >= 100 && r < 200) proc.setBandParam (band, "type", (float) (r - 100));
        else if (r >= 200 && r < 300) proc.setBandParam (band, "slope", (float) (r - 200));
        else if (r >= 300 && r < 400) proc.setBandParam (band, "place", (float) (r - 300));
        else if (r >= 400 && r < 405)
        {
            const int pi = proc.paramIndexOf (QmixProcessor::bandId (band, ids[r - 400]));
            proc.learnParam = pi;
        }
        else if (r == 500) proc.setBandParam (band, "on", v.on ? 0.0f : 1.0f);
        else if (r == 501) { proc.deleteBand (band); selectBand (-1); }
        else if (r == 502) { proc.setBandParam (band, "dyn", v.dyn ? 0.0f : 1.0f); panel.resized(); resized(); }
    });
}

void QmixEditor::showParamMidiMenu (RangedAudioParameter* p, const String& title)
{
    const int pi = proc.paramIndexOf (dynamic_cast<AudioProcessorParameterWithID*> (p) != nullptr ? dynamic_cast<AudioProcessorParameterWithID*> (p)->paramID : String());
    if (pi < 0) return;
    const int cc = proc.ccForParam (pi);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoMidi, cAccent, title, cc >= 0 ? "MIDI CC " + String (cc) : String ("No MIDI CC"));
    m.addItem (1, "MIDI Learn", true, proc.learnParam.load() == pi);
    if (cc >= 0) m.addItem (2, "Clear MIDI");
    if (proc.learnParam.load() >= 0) m.addItem (3, "Cancel MIDI learn");
    m.showMenuAsync (menuAt(), [this, pi] (int r)
    {
        if (r == 1) proc.learnParam = pi;
        else if (r == 2) proc.clearMidiFor (pi);
        else if (r == 3) proc.learnParam = -1;
    });
}

void QmixEditor::showMainMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    QmixMenu::addHeader (m, fonts, QmixMenu::icoEq, cAccent, "QMix", "Parametric EQ  v" + String (JucePlugin_VersionString));
    PopupMenu pres, an, rg, midi, size, gl;
    const auto& fp = QmixProcessor::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) pres.addItem (1 + i, fp[(size_t) i].name, true, proc.currentPreset.load() == i && ! proc.presetDirty.load());
    pres.addSeparator();
    pres.addItem (90, "Save preset...");
    pres.addItem (91, "Load preset...");
    static const char* an3[3] = { "Off", "Post EQ", "Pre + Post EQ" };
    for (int i = 0; i < 3; ++i) an.addItem (100 + i, an3[i], true, proc.analyzerMode.load() == i);
    static const char* rg4[4] = { "6 dB", "12 dB", "24 dB", "30 dB" };
    for (int i = 0; i < 4; ++i) rg.addItem (110 + i, String::charToString (0xB1) + " " + rg4[i], true, proc.rangeIdx.load() == i);
    midi.addItem (120, "Right-click a knob to learn", false, false);
    if (proc.learnParam.load() >= 0) midi.addItem (121, "Cancel MIDI learn");
    midi.addItem (122, "Clear all MIDI");
    static const char* sz[3] = { "60 %", "75 %", "100 %" };
    const int cur = jmax (0, proc.uiSizeIdx.load());
    for (int i = 0; i < 3; ++i) size.addItem (130 + i, sz[i], true, cur == i);
    for (int i = 0; i < 4; ++i) gl.addItem (140 + i, String ((i + 1) * 25) + " %", true, proc.glowIdx.load() == i);
    PopupMenu modes, qual;
    for (int i = 0; i < qmix::numModes; ++i) modes.addItem (150 + i, qmix::modeName (i), true, proc.phaseMode.load() == i);
    static const char* qn[3] = { "Low", "Medium", "High" };
    for (int i = 0; i < 3; ++i) qual.addItem (160 + i, qn[i], true, proc.lpQuality.load() == i);
    m.addSubMenu ("Presets", pres);
    m.addSubMenu ("Processing mode", modes);
    PopupMenu flav;
    for (int i = 0; i < qmix::numFlavours; ++i) flav.addItem (170 + i, qmix::flavourName (i), true, proc.analogFlavour.load() == i);
    m.addSubMenu ("Analog character", flav);
    m.addSubMenu ("Linear phase quality", qual);
    m.addSubMenu ("Analyzer", an);
    m.addSubMenu ("Display range", rg);
    m.addSeparator();
    m.addSubMenu ("MIDI", midi);
    m.addSubMenu ("Plugin size", size);
    m.addSubMenu ("Glow intensity", gl);
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r >= 1 && r < 90) { proc.loadFactoryPreset (r - 1); selectBand (-1); }
        else if (r == 90) savePreset();
        else if (r == 91) loadPreset();
        else if (r >= 100 && r < 103) proc.analyzerMode = r - 100;
        else if (r >= 110 && r < 114) { proc.rangeIdx = r - 110; display.rebuildCurves(); }
        else if (r == 121) proc.learnParam = -1;
        else if (r == 122) for (auto& x : proc.ccMap) x = -1;
        else if (r >= 130 && r < 133) { proc.uiSizeIdx = r == 130 ? -1 : r - 130; setScaleIndex (r - 130); }
        else if (r >= 140 && r < 144) proc.glowIdx = r - 140;
        else if (r >= 150 && r < 153) { proc.phaseMode = r - 150; display.rebuildCurves(); }
        else if (r >= 160 && r < 163) proc.lpQuality = r - 160;
        else if (r >= 170 && r < 170 + qmix::numFlavours) { proc.analogFlavour = r - 170; proc.phaseMode = qmix::analog; display.rebuildCurves(); }
        repaint(); display.repaint();
    });
}

// ------------------------------------------------------------------ anti-gel v2
void QmixEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if ((void*) peer != lastPeer) { lastPeer = (void*) peer; repaint(); }
    lastRendererCheck = nowS();
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw) peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void QmixEditor::trackHostWindow()
{
    if (auto* peer = getPeer())
    {
        void* handle = peer->getNativeHandle();
        if (handle != nullptr && ! hostLock.isAttachedTo (handle))
        {
            Component::SafePointer<Component> top (&peer->getComponent());
            hostLock.attach (handle, [top] { return top != nullptr ? top->getWidth() : 0; });
            hostLock.setMinimumSize (kW, kH);
            hostLock.setMaximumSize (kMaxW, kMaxH);
            hostLock.enforceNow();
        }
        if (getWidth() < kW - 2) hostLock.enforceNow();
    }
    else hostLock.detach();
}

void QmixEditor::parentHierarchyChanged() { useSoftwareRenderer(); trackHostWindow(); }
void QmixEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

void QmixEditor::askRepaint (Rectangle<int> r)
{
    if (pendingSince <= 0.0) pendingSince = nowS();
    repaint (r);
}

// ------------------------------------------------------------------ 30 Hz
void QmixEditor::timerCallback()
{
    const double t = nowS();
    if ((void*) getPeer() != lastPeer || t - lastRendererCheck > 1.0) { useSoftwareRenderer(); trackHostWindow(); }
    if (pendingSince > 0.0 && t - pendingSince > 1.5 && isShowing())                 // chien de garde
    {
        lastPeer = nullptr; useSoftwareRenderer(); repaint(); pendingSince = 0.0;
    }

    // changements de réglages (souris, automation, MIDI) -> courbes + ligne d'état
    bool changed = false;
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        const auto v = proc.bandValues (b);
        auto& o = lastVals[(size_t) b];
        const bool diff = v.used != o.used || v.on != o.on || v.type != o.type || v.slope != o.slope || v.place != o.place
                       || v.dyn != o.dyn || std::abs (v.thresh - o.thresh) > 0.01f || std::abs (v.range - o.range) > 0.005f
                       || std::abs (v.freq - o.freq) > 0.0005f * o.freq || std::abs (v.gain - o.gain) > 0.002f || std::abs (v.q - o.q) > 0.0005f * o.q;
        if (diff)
        {
            changed = true;
            if (v.used) showStatus (bandSummary (b));
            if (! v.used && b == selected) selectBand (-1);
            o = v;
        }
    }
    const float out = proc.outParam->load(), scl = proc.scaleParam->load();
    if (std::abs (out - lastOut) > 0.005f) { lastOut = out; showStatus ("OUTPUT  " + dbStr (out)); }
    const float col = proc.colorParam->load();
    const bool colLook = proc.phaseMode.load() == qmix::analog && proc.analogFlavour.load() != qmix::flavourClean;
    if (colorKnob.enabledLook != colLook) { colorKnob.enabledLook = colLook; colorKnob.repaint(); }
    if (lastColor >= 0 && std::abs (col - lastColor) > 0.05f)
        showStatus ("CONSOLE COLOR  " + String ((int) std::round (col)) + " %" + (colLook ? "  " + String (qmix::flavourName (proc.analogFlavour.load())).toUpperCase() : String ("  (ANALOG SSL G / NEVE VR)")));
    lastColor = col;
    if (std::abs (scl - lastScale) > 0.05f) { lastScale = scl; changed = true; showStatus ("GAIN SCALE  " + String ((int) std::round (scl)) + " %"); }
    const bool pw = proc.powerParam->load() > 0.5f;
    if (pw != lastPower) { lastPower = pw; changed = true; showStatus (pw ? "QMIX ON" : "QMIX BYPASSED"); }
    if (proc.glowIdx.load() != lastGlow || proc.analyzerMode.load() != lastAnalyzer || proc.rangeIdx.load() != lastRange)
    {
        lastGlow = proc.glowIdx.load(); lastAnalyzer = proc.analyzerMode.load(); lastRange = proc.rangeIdx.load();
        changed = true; repaint();
    }
    // bandes dynamiques : la courbe suit le gain dynamique en direct
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        const float dd = proc.dynDb[b].load();
        if (std::abs (dd - lastDyn[(size_t) b]) > 0.05f) { lastDyn[(size_t) b] = dd; changed = true; }
    }
    const int modeNow = proc.phaseMode.load() * 10 + proc.analogFlavour.load();
    if (modeNow != lastMode) { lastMode = modeNow; changed = true; repaint(); }
    if (changed) { display.rebuildCurves(); panel.repaint(); if (panel.band() >= 0 && panel.wantsWidthChange()) resized(); askRepaint (getLocalBounds().removeFromBottom ((int) (70 * sc()))); }

    // MIDI Learn
    const int lp = proc.learnParam.load();
    if (lp >= 0)
        showStatus ("MIDI LEARN  " + proc.getParameters()[lp]->getName (40));
    static int lastLearned = -1;
    const int ll = proc.lastLearnedCc.exchange (-1);
    if (ll >= 0 && ll != lastLearned)
    {
        const int pi = proc.ccMap[ll].load();
        if (pi >= 0) showStatus ("CC " + String (ll) + "  " + proc.getParameters()[pi]->getName (40));
    }

    // fondus : ligne d'état et interrupteur
    const float target = (t - statusTime < 2.5) ? 1.0f : 0.0f;
    const float na = target > statusAlpha ? jmin (1.0f, statusAlpha + 0.28f) : jmax (0.0f, statusAlpha - 0.11f);
    if (std::abs (na - statusAlpha) > 0.001f) { statusAlpha = na; askRepaint (pill().expanded (4).toNearestInt()); }
    const float pg = pw ? jmin (1.0f, powerGlow + 0.45f) : jmax (0.0f, powerGlow - 0.28f);
    if (std::abs (pg - powerGlow) > 0.001f) { powerGlow = pg; askRepaint (powerRect().expanded (powerRect().getWidth()).toNearestInt()); }

    display.updateSpectrum();
    panel.tick();
    outKnob.tick(); scaleKnob.tick(); colorKnob.tick();
}
