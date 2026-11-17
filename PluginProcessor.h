#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "EqMath.h"
#include "ConsoleColour.h"

//==============================================================================
//  QMix (NINE HANDS | DSP) : égaliseur paramétrique 24 bandes
//  Zero Latency / Analog / Linear Phase, bandes dynamiques
//==============================================================================
class QmixProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    QmixProcessor();
    ~QmixProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //------------------------------------------------------------------ paramètres
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String bandId (int band, const char* what) { return "b" + juce::String (band + 1) + "_" + what; }

    struct BandParams
    {
        std::atomic<float>* used = nullptr, *on = nullptr, *type = nullptr, *freq = nullptr, *gain = nullptr,
                          *q = nullptr, *slope = nullptr, *place = nullptr, *dyn = nullptr, *thresh = nullptr, *range = nullptr;
    };
    BandParams bandParam[qmix::kNumBands];
    std::atomic<float>* outParam = nullptr, *powerParam = nullptr, *scaleParam = nullptr, *colorParam = nullptr;
    bool colourActive() const { return phaseMode.load() == qmix::analog && analogFlavour.load() != qmix::flavourClean && colorParam->load() > 0.05f; }

    // valeur réelle d'une bande (lue par l'interface)
    struct BandValues
    {
        bool used = false, on = true, dyn = false; int type = 0, slope = 3, place = 0;
        float freq = 1000, gain = 0, q = 1, thresh = -24, range = -6;
    };
    BandValues bandValues (int b) const;
    void setBandParam (int band, const char* what, float realValue);       // thread de messages uniquement
    int  createBand (float freq, float gain, int type);                     // renvoie l'index de la bande, -1 si plus de place
    void deleteBand (int band);

    //------------------------------------------------------------------ mode de traitement (enregistré avec le projet)
    std::atomic<int> phaseMode { qmix::analog };     // Zero Latency / Analog / Linear Phase
    std::atomic<int> lpQuality { 1 };                // Low / Medium / High
    std::atomic<int> analogFlavour { qmix::flavourClean };   // caractère du mode Analog : Clean / SSL 4000 G / Neve VR
    static int firLength (int quality) { static const int n[3] = { 4096, 8192, 16384 }; return n[juce::jlimit (0, 2, quality)]; }
    int latencyFor (int mode, int quality) const { return mode == qmix::linearPhase ? firLength (quality) / 2 : 0; }

    //------------------------------------------------------------------ bandes dynamiques : gain dynamique actuel (dB), lu par l'interface
    std::atomic<float> dynDb[qmix::kNumBands];

    //------------------------------------------------------------------ analyseur (thread audio -> interface, sans verrou)
    static constexpr int kFifoSize = 1 << 15;
    struct Fifo
    {
        juce::AbstractFifo fifo { kFifoSize };
        std::vector<float> data = std::vector<float> ((size_t) kFifoSize, 0.0f);
        void push (const float* x, int n);
        int  pull (float* dst, int n);
    };
    Fifo preFifo, postFifo;
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool> firReady { false }, prepared { false };   // phase linéaire : RIF chargés / moteur prêt

    //------------------------------------------------------------------ réglages de l'interface (enregistrés avec le projet)
    std::atomic<int> uiSizeIdx { -1 };      // -1 = taille par défaut (60 %)
    std::atomic<int> analyzerMode { 1 };    // 0 off, 1 post, 2 pre + post
    std::atomic<int> rangeIdx { 1 };        // ±6 / ±12 / ±24 / ±30 dB
    std::atomic<int> glowIdx { 2 };         // 25 / 50 / 75 / 100 %
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> presetDirty { false };
    juce::String presetName { "Flat" };

    //------------------------------------------------------------------ presets
    struct FactoryBand { int type; float freq, gain, q; int slope = 3; int place = 0; bool dyn = false; float thresh = -24.0f, range = -6.0f; };
    struct FactoryPreset { const char* name; std::vector<FactoryBand> bands; float out = 0.0f; };
    static const std::vector<FactoryPreset>& factoryPresets();
    void loadFactoryPreset (int index);
    juce::ValueTree savePresetTree() const;
    void loadPresetTree (const juce::ValueTree& t, const juce::String& name);

    //------------------------------------------------------------------ MIDI Learn (aucun CC par défaut)
    static constexpr int kMaxParams = 320;
    std::atomic<int> learnParam { -1 };
    std::atomic<int> ccMap[128];
    std::atomic<int> lastLearnedCc { -1 };
    int  paramIndexOf (const juce::String& id) const;
    int  ccForParam (int paramIndex) const;
    void clearMidiFor (int paramIndex);
    std::atomic<int> midiActivity { 0 };

private:
    void timerCallback() override;          // file MIDI -> setValueNotifyingHost ; latence (thread de messages)
    std::atomic<float> pendingValue[kMaxParams];
    std::atomic<bool>  pendingSet[kMaxParams];
    std::atomic<int>   ccMin[128], ccMax[128];

    //------------------------------------------------------------------ moteur IIR (Zero Latency / Analog / bandes dynamiques)
    struct BandDsp
    {
        qmix::SectionCoeffs c[qmix::kMaxSections];
        int n = 0;
        qmix::SvfState st[2][qmix::kMaxSections];
        int lastType = -1, lastSlope = -1, lastMode = -1;
        double sFreq = 1000, sGain = 0, sQ = 1, amt = 0;   // valeurs lissées
        double dFreq = -1, dGain = 0, dQ = 0, dAmt = -1;   // valeurs de la dernière conception
        int kinds[qmix::kMaxSections];                     // Analog : façon d'apparier chaque section (choix réutilisé)
        double kFreq = -1, kGain = 0, kQ = 0;              // valeurs du dernier choix
        bool primed = false;
        // détecteur de la bande dynamique
        qmix::SectionCoeffs det;
        qmix::SvfState detSt[2];
        double env = 0.0, dynGain = 0.0;
        void reset() { for (auto& ch : st) for (auto& s : ch) s.reset(); detSt[0].reset(); detSt[1].reset(); }
    };
    BandDsp dsp[qmix::kNumBands];
    void runBands (float* L, float* R, int n, bool dynamicOnly, int mode);

    //------------------------------------------------------------------ phase linéaire : matrice 2 x 2 de convolutions (L/R + Mid/Side)
    class FirDesigner;
    std::unique_ptr<FirDesigner> designer;
    std::unique_ptr<juce::dsp::Convolution> conv[4];   // LL, LR, RL, RR
    std::atomic<bool> crossNeeded { false }; // termes croisés (bandes Mid / Side)
    juce::AudioBuffer<float> lpIn, lpTmp;
    juce::AudioBuffer<float> dryDelay;       // chemin sec retardé (Power) en phase linéaire
    int dryPos = 0;
    int activeMode = -1, activeLatency = 0, fadeIn = 0;

    qmix::ConsoleColour colour;
    double colorSmooth = 0.5;
    juce::AudioBuffer<float> dryBuf;
    double outSmooth = 1.0, powerSmooth = 1.0;
    double sr = 48000.0;
    int maxBlock = 512;
    std::vector<float> monoTmp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QmixProcessor)
};
