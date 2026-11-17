#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "QmixMenu.h"
#include "WindowMinSize.h"

class QmixEditor;

namespace QmixUi
{
    using namespace juce;
    constexpr int kW = 960, kH = 540;            // taille par défaut, annoncée « 60 % »
    constexpr int kMaxW = 2400, kMaxH = 1350;    // 150 %
    Colour bandColour (int band);

    //------------------------------------------------------------------ thèmes visuels (aperçus de direction)
    struct Theme
    {
        const char* name;
        Colour bg, displayTop, displayBottom, grid, gridMajor, label, text, dim, accent;
        Colour face, pillBorder, panelTop, panelBottom, panelBorder, chipBg, chipBorder;
        Colour curve, specTop, specBottom, specLine, preLine;
        Colour capLight, capDark, crownLight, crownDark, track, marker, nodeText;
        Colour facadeText, facadeDim, segBg, segBorder;
        bool brushed, logoBlack;
        uint32 palette[12];
    };
    const Theme& theme();
    void setTheme (int i);
    int numThemes();

    //------------------------------------------------------------------ potard vectoriel (lumière en haut à gauche, 45°)
    // couronne crantée, chapeau, marqueur et ombre tournent ensemble ; flou de mouvement de la couronne selon la vitesse
    class Knob : public Component
    {
    public:
        Knob (QmixEditor& e, String labelText);
        void attach (RangedAudioParameter* p, Colour c, bool bipolar);
        void detach();
        void paint (Graphics&) override;
        void mouseDown (const MouseEvent&) override;
        void mouseDrag (const MouseEvent&) override;
        void mouseUp (const MouseEvent&) override;
        void mouseDoubleClick (const MouseEvent&) override;
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
        void tick();                              // 30 Hz : flou de mouvement
        RangedAudioParameter* param = nullptr;
        String label;
        Colour colour { QmixMenu::kAccent };
        bool bipolar = false, enabledLook = true, onFacade = false;
        std::function<String (float)> format;
    private:
        QmixEditor& ed;
        std::unique_ptr<ParameterAttachment> att;
        float norm = 0.0f, dragStart = 0.0f, shownAngle = 0.0f, prevAngle = 0.0f, blur = 0.0f;
        bool dragging = false;
        float angleOf (float v) const;
    };

    //------------------------------------------------------------------ panneau de la bande sélectionnée
    class BandPanel : public Component
    {
    public:
        explicit BandPanel (QmixEditor& e);
        void setBand (int b);
        int band() const { return current; }
        void paint (Graphics&) override;
        void resized() override;
        void mouseDown (const MouseEvent&) override;
        void tick();
        bool dynShown() const;
        bool wantsWidthChange() const { return dynShown() != layoutDyn; }
        float desiredWidth() const { return 158.0f + (dynShown() ? 5.0f : 3.0f) * 86.0f + 40.0f; }
        Knob freq, gain, q, thresh, range;
    private:
        QmixEditor& ed;
        int current = -1;
        bool layoutDyn = false;
        Rectangle<float> rType, rSlope, rPlace, rDyn, rOn, rDel;
    };

    //------------------------------------------------------------------ courbe, analyseur, points des bandes
    class Display : public Component
    {
    public:
        explicit Display (QmixEditor& e);
        void paint (Graphics&) override;
        void resized() override;
        void mouseMove (const MouseEvent&) override;
        void mouseExit (const MouseEvent&) override;
        void mouseDown (const MouseEvent&) override;
        void mouseDrag (const MouseEvent&) override;
        void mouseUp (const MouseEvent&) override;
        void mouseDoubleClick (const MouseEvent&) override;
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

        float xOf (double hz) const;
        double hzAt (float x) const;
        float yOf (double db) const;
        double dbAt (float y) const;
        Rectangle<float> plot() const;

        void rebuildCurves();                     // quand un réglage change
        void updateSpectrum();                    // 30 Hz
        static constexpr int kPts = 480;
        int hover = -1, dragBand = -1;
    private:
        QmixEditor& ed;
        int nodeAt (Point<float> p) const;
        Point<float> nodePos (int b) const;
        std::array<std::vector<float>, qmix::kNumBands> bandDb, rangeDb;
        std::array<bool, qmix::kNumBands> isDyn {};
        std::vector<float> sumDb, freqs;
        // analyseur
        static constexpr int kFftOrder = 13, kFftSize = 1 << kFftOrder;
        dsp::FFT fft { kFftOrder };
        dsp::WindowingFunction<float> window { (size_t) kFftSize, dsp::WindowingFunction<float>::blackmanHarris };
        std::vector<float> ringPost, ringPre, fftBuf, specPost, specPre, scratch;
        int ringPosPost = 0, ringPosPre = 0;
        void analyse (std::vector<float>& ring, int ringPos, std::vector<float>& spec, bool fast);
        Point<float> dragOrigin; double dragF0 = 0, dragG0 = 0; bool fine = false;
    };
}

//==============================================================================
class QmixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit QmixEditor (QmixProcessor&);
    ~QmixEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    QmixProcessor& proc;
    QmixMenu::Fonts fonts;
    QmixMenu::MenuLnf menuLnf;
    juce::Font font (int weight, float h) const;   // 0 regular, 1 medium, 2 bold
    float sc() const { return (float) getWidth() / (float) QmixUi::kW; }

    int selected = -1;
    void selectBand (int b);
    void showStatus (const juce::String& s);
    void showBandMenu (int band);
    void showMainMenu();
    void showParamMidiMenu (juce::RangedAudioParameter* p, const juce::String& title);
    void showTypeMenu (int band);
    void showSlopeMenu (int band);
    void showPlaceMenu (int band);
    void showQualityMenu();
    void showFlavourMenu();
    juce::PopupMenu::Options menuAt() const;
    juce::String bandSummary (int b) const;
    float glow() const { return 0.25f * (float) (juce::jlimit (0, 3, proc.glowIdx.load()) + 1); }
    double rangeDb() const { static const double r[4] = { 6, 12, 24, 30 }; return r[juce::jlimit (0, 3, proc.rangeIdx.load())]; }
    void askRepaint (juce::Rectangle<int> r);

    QmixUi::Display display;
    QmixUi::BandPanel panel;
    QmixUi::Knob outKnob, scaleKnob, colorKnob;

private:
    friend int main();                         // outil de captures (tools/render)
    void timerCallback() override;
    void setScaleIndex (int idx);
    void useSoftwareRenderer();
    void trackHostWindow();
    void prevNextPreset (int dir);
    void savePreset();
    void loadPreset();

    std::unique_ptr<juce::Drawable> logo;
    juce::ComponentBoundsConstrainer constrainer;
    wiga::WindowMinSize hostLock;
    void* lastPeer = nullptr;
    double lastRendererCheck = 0, lastPaint = 0, pendingSince = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    // barre de presets / ligne d'état (même pastille)
    juce::String status;
    double statusTime = -10.0;
    float statusAlpha = 0.0f;
    int lastMidiAct = 0;
    juce::Rectangle<float> pill() const, pillPrev() const, pillNext() const, powerRect() const;
    juce::Rectangle<float> segAnalyzer (int i) const, segRange (int i) const, segMode (int i) const;
    int hoverZone = 0;
    std::array<QmixProcessor::BandValues, qmix::kNumBands> lastVals;
    float lastOut = 0, lastScale = 0, lastColor = -1;
    bool lastPower = true;
    float powerGlow = 1.0f;
    int lastAnalyzer = -1, lastRange = -1, lastGlow = -1, lastMode = -1;
    std::array<float, qmix::kNumBands> lastDyn {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QmixEditor)
};
