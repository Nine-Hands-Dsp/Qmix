#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
static NormalisableRange<float> logRange (float lo, float hi)
{
    return NormalisableRange<float> (lo, hi,
        [] (float a, float b, float v) { return a * std::pow (b / a, v); },
        [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); });
}

static String hzText (float v, int)
{
    if (v >= 1000.0f) return String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
    return String (v, v < 100.0f ? 1 : 0) + " Hz";
}

AudioProcessorValueTreeState::ParameterLayout QmixProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray types, slopes, places;
    for (int t = 0; t < qmix::numTypes; ++t) types.add (qmix::typeName (t));
    for (int s = 0; s < qmix::kNumSlopes; ++s) slopes.add (String (qmix::slopeDb (s)) + " dB/oct");
    for (int p = 0; p < qmix::numPlacements; ++p) places.add (qmix::placementName (p));
    const auto dB = AudioParameterFloatAttributes().withLabel ("dB");

    l.add (std::make_unique<AudioParameterBool> (ParameterID { "power", 1 }, "Power", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-36.0f, 36.0f, 0.01f), 0.0f, dB));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "scale", 1 }, "Gain Scale", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), "|");
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandId (b, "used"), 1 }, n + "Used", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandId (b, "on"), 1 }, n + "Enabled", true));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "type"), 1 }, n + "Shape", types, 0));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "freq"), 1 }, n + "Frequency", logRange (10.0f, 30000.0f), 1000.0f,
                                                              AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "gain"), 1 }, n + "Gain", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, dB));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "q"), 1 }, n + "Q", logRange (0.025f, 40.0f), 1.0f));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "slope"), 1 }, n + "Slope", slopes, 3));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "place"), 1 }, n + "Stereo Placement", places, 0));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandId (b, "dyn"), 1 }, n + "Dynamic", false));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "thresh"), 1 }, n + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f, dB));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "range"), 1 }, n + "Dynamic Range", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), -6.0f, dB));
        l.add (std::move (grp));
    }
    return l;
}

//==============================================================================
//  Phase linéaire : conception des RIF sur un fil d'arrière-plan
//==============================================================================
class QmixProcessor::FirDesigner : public Thread
{
public:
    explicit FirDesigner (QmixProcessor& p) : Thread ("Qmix FIR designer"), proc (p) {}
    ~FirDesigner() override { stopThread (2000); }
    void invalidate() { lastSig = -1.0; }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (proc.phaseMode.load() == qmix::linearPhase && proc.prepared.load())
            {
                const double sig = signature();
                if (sig != lastSig) { lastSig = sig; build(); }
            }
            wait (15);
        }
    }

private:
    QmixProcessor& proc;
    std::atomic<double> lastSig { -1.0 };

    double signature() const
    {
        double h = proc.currentRate.load() * 1.0e-3 + proc.lpQuality.load() * 7.0 + proc.scaleParam->load() * 0.013;
        for (int b = 0; b < qmix::kNumBands; ++b)
        {
            const auto v = proc.bandValues (b);
            if (! v.used || ! v.on || (v.dyn && qmix::canBeDynamic (v.type))) continue;
            h += (b + 1) * (v.type * 1.7 + v.freq * 0.0137 + v.gain * 0.311 + v.q * 5.13 + v.slope * 0.77 + v.place * 2.9) * 1.0001;
            h += std::sin (v.freq + v.gain * 3.0 + v.q * 11.0 + b);
        }
        return h;
    }

    void build()
    {
        const double fs = proc.currentRate.load();
        const int N = firLength (proc.lpQuality.load());
        const double scale = proc.scaleParam->load() * 0.01;
        // modules analogiques par bin : Hl, Hr (stéréo + gauche / droite), Hm, Hs (Mid / Side)
        std::vector<double> Hl ((size_t) N / 2 + 1, 1.0), Hr = Hl, Hm = Hl, Hs = Hl;
        bool ms = false;
        const bool stereoIo = proc.getTotalNumOutputChannels() > 1;
        for (int b = 0; b < qmix::kNumBands; ++b)
        {
            const auto v = proc.bandValues (b);
            if (! v.used || ! v.on || (v.dyn && qmix::canBeDynamic (v.type))) continue;   // les bandes dynamiques restent en IIR « Analog »
            const auto d = qmix::designAnalog (v.type, v.freq, v.gain * scale, v.q, v.slope);
            const int place = stereoIo ? v.place : qmix::stereo;
            if (place == qmix::mid || place == qmix::side) ms = true;
            for (int k = 0; k <= N / 2; ++k)
            {
                const double f = jmax (1.0e-3, (double) k * fs / N);
                const double m = std::sqrt (qmix::analogMag2 (d, f));
                switch (place)
                {
                    case qmix::left:  Hl[(size_t) k] *= m; break;
                    case qmix::right: Hr[(size_t) k] *= m; break;
                    case qmix::mid:   Hm[(size_t) k] *= m; break;
                    case qmix::side:  Hs[(size_t) k] *= m; break;
                    default:          Hl[(size_t) k] *= m; Hr[(size_t) k] *= m; break;
                }
            }
            if (threadShouldExit()) return;
        }
        // matrice : LL = ½(Hm+Hs)Hl, LR = ½(Hm−Hs)Hr, RL = ½(Hm−Hs)Hl, RR = ½(Hm+Hs)Hr
        auto make = [&] (auto fn)
        {
            const int order = (int) std::round (std::log2 ((double) N));
            dsp::FFT fft (order);
            std::vector<float> buf ((size_t) N * 2, 0.0f);
            for (int k = 0; k <= N / 2; ++k)
            {
                const double m = fn (k);
                // phase linéaire : retard de N/2 échantillons -> (-1)^k
                buf[(size_t) (2 * k)] = (float) (m * ((k & 1) ? -1.0 : 1.0));
                buf[(size_t) (2 * k + 1)] = 0.0f;
            }
            fft.performRealOnlyInverseTransform (buf.data());
            AudioBuffer<float> ir (1, N);
            for (int n = 0; n < N; ++n)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * qmix::kPi * n / N);            // fenêtre de Hann centrée sur N/2
                ir.setSample (0, n, (float) (buf[(size_t) n] * w));
            }
            return ir;
        };
        if (! ms)
        {
            proc.conv[0]->loadImpulseResponse (make ([&] (int k) { return Hl[(size_t) k]; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
            proc.conv[3]->loadImpulseResponse (make ([&] (int k) { return Hr[(size_t) k]; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
            if (proc.crossNeeded.load())
            {
                proc.conv[1]->loadImpulseResponse (make ([] (int) { return 0.0; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
                proc.conv[2]->loadImpulseResponse (make ([] (int) { return 0.0; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
                crossOffAt = Time::getMillisecondCounter() + 400;       // fondu des termes croisés avant de les couper
            }
            else crossOffAt = 0;
        }
        else
        {
            proc.conv[0]->loadImpulseResponse (make ([&] (int k) { return 0.5 * (Hm[(size_t) k] + Hs[(size_t) k]) * Hl[(size_t) k]; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
            proc.conv[1]->loadImpulseResponse (make ([&] (int k) { return 0.5 * (Hm[(size_t) k] - Hs[(size_t) k]) * Hr[(size_t) k]; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
            proc.conv[2]->loadImpulseResponse (make ([&] (int k) { return 0.5 * (Hm[(size_t) k] - Hs[(size_t) k]) * Hl[(size_t) k]; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
            proc.conv[3]->loadImpulseResponse (make ([&] (int k) { return 0.5 * (Hm[(size_t) k] + Hs[(size_t) k]) * Hr[(size_t) k]; }), fs, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
            proc.crossNeeded = true;
            crossOffAt = 0;
        }
        proc.firReady = true;
    }
public:
    uint32 crossOffAt = 0;
};

//==============================================================================
QmixProcessor::QmixProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QMIX", createLayout())
{
    for (auto& c : conv) c = std::make_unique<dsp::Convolution> (dsp::Convolution::NonUniform { 512 });
    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        auto& p = bandParam[b];
        p.used   = apvts.getRawParameterValue (bandId (b, "used"));
        p.on     = apvts.getRawParameterValue (bandId (b, "on"));
        p.type   = apvts.getRawParameterValue (bandId (b, "type"));
        p.freq   = apvts.getRawParameterValue (bandId (b, "freq"));
        p.gain   = apvts.getRawParameterValue (bandId (b, "gain"));
        p.q      = apvts.getRawParameterValue (bandId (b, "q"));
        p.slope  = apvts.getRawParameterValue (bandId (b, "slope"));
        p.place  = apvts.getRawParameterValue (bandId (b, "place"));
        p.dyn    = apvts.getRawParameterValue (bandId (b, "dyn"));
        p.thresh = apvts.getRawParameterValue (bandId (b, "thresh"));
        p.range  = apvts.getRawParameterValue (bandId (b, "range"));
        dynDb[b] = 0.0f;
    }
    outParam   = apvts.getRawParameterValue ("output");
    powerParam = apvts.getRawParameterValue ("power");
    scaleParam = apvts.getRawParameterValue ("scale");

    for (int i = 0; i < 128; ++i) { ccMap[i] = -1; ccMin[i] = 127; ccMax[i] = 0; }
    for (int i = 0; i < kMaxParams; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; }
    jassert (getParameters().size() <= kMaxParams);
    designer = std::make_unique<FirDesigner> (*this);
    designer->startThread (Thread::Priority::low);
    startTimerHz (60);
}

QmixProcessor::~QmixProcessor() { stopTimer(); designer.reset(); }

bool QmixProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

double QmixProcessor::getTailLengthSeconds() const
{
    return phaseMode.load() == qmix::linearPhase ? (double) firLength (lpQuality.load()) / jmax (1.0, sr) : 0.0;
}

void QmixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = false;
    sr = sampleRate;
    maxBlock = jmax (64, samplesPerBlock);
    currentRate = sampleRate;
    dryBuf.setSize (2, maxBlock * 2);
    lpIn.setSize (2, maxBlock * 2);
    lpTmp.setSize (4, maxBlock * 2);
    dryDelay.setSize (2, firLength (2) + maxBlock * 2 + 8);
    dryDelay.clear(); dryPos = 0;
    monoTmp.assign ((size_t) maxBlock * 2, 0.0f);
    for (auto& d : dsp) { d.reset(); d.primed = false; d.amt = 0.0; d.lastType = -1; d.env = 0.0; d.dynGain = 0.0; }
    outSmooth = Decibels::decibelsToGain ((double) outParam->load());
    powerSmooth = powerParam->load() > 0.5f ? 1.0 : 0.0;
    for (auto& c : conv)
    {
        c->prepare ({ sampleRate, (uint32) (maxBlock * 2), 1 });
        c->reset();
    }
    crossNeeded = false; firReady = false;
    if (designer != nullptr) designer->invalidate();
    activeMode = -1;
    activeLatency = latencyFor (phaseMode.load(), lpQuality.load());
    setLatencySamples (activeLatency);
    prepared = true;
}

//==============================================================================
QmixProcessor::BandValues QmixProcessor::bandValues (int b) const
{
    const auto& p = bandParam[b];
    BandValues v;
    v.used = p.used->load() > 0.5f; v.on = p.on->load() > 0.5f; v.dyn = p.dyn->load() > 0.5f;
    v.type = (int) p.type->load(); v.slope = (int) p.slope->load(); v.place = (int) p.place->load();
    v.freq = p.freq->load(); v.gain = p.gain->load(); v.q = p.q->load();
    v.thresh = p.thresh->load(); v.range = p.range->load();
    return v;
}

void QmixProcessor::setBandParam (int band, const char* what, float realValue)
{
    if (auto* p = apvts.getParameter (bandId (band, what)))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
    presetDirty = true;
}

int QmixProcessor::createBand (float freq, float gain, int type)
{
    static const char* ids[] = { "type", "freq", "gain", "q", "slope", "place", "on", "dyn", "thresh", "range" };
    for (int b = 0; b < qmix::kNumBands; ++b)
        if (bandParam[b].used->load() < 0.5f)
        {
            for (auto* id : ids) if (auto* p = apvts.getParameter (bandId (b, id))) p->beginChangeGesture();
            setBandParam (b, "type", (float) type);
            setBandParam (b, "freq", freq);
            setBandParam (b, "gain", qmix::hasGain (type) ? gain : 0.0f);
            setBandParam (b, "q", qmix::hasSlope (type) ? 0.7071f : 1.0f);
            setBandParam (b, "slope", 3.0f);
            setBandParam (b, "place", 0.0f);
            setBandParam (b, "on", 1.0f);
            setBandParam (b, "dyn", 0.0f);
            setBandParam (b, "thresh", -24.0f);
            setBandParam (b, "range", -6.0f);
            setBandParam (b, "used", 1.0f);
            for (auto* id : ids) if (auto* p = apvts.getParameter (bandId (b, id))) p->endChangeGesture();
            return b;
        }
    return -1;
}

void QmixProcessor::deleteBand (int band)
{
    if (band < 0 || band >= qmix::kNumBands) return;
    setBandParam (band, "used", 0.0f);
}

//==============================================================================
void QmixProcessor::Fifo::push (const float* x, int n)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) std::copy (x, x + n1, data.begin() + s1);
    if (n2 > 0) std::copy (x + n1, x + n1 + n2, data.begin() + s2);
    fifo.finishedWrite (n1 + n2);
}

int QmixProcessor::Fifo::pull (float* dst, int n)
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (jmin (n, fifo.getNumReady()), s1, n1, s2, n2);
    if (n1 > 0) std::copy (data.begin() + s1, data.begin() + s1 + n1, dst);
    if (n2 > 0) std::copy (data.begin() + s2, data.begin() + s2 + n2, dst + n1);
    fifo.finishedRead (n1 + n2);
    return n1 + n2;
}

//==============================================================================
//  Bandes IIR (Zero Latency / Analog) ; en phase linéaire, seulement les bandes dynamiques
//==============================================================================
void QmixProcessor::runBands (float* L, float* R, int n, bool dynamicOnly, int mode)
{
    const double scale = scaleParam->load() * 0.01;
    constexpr int kSub = 32;
    const double aSm  = 1.0 - std::exp (-(double) kSub / (0.025 * sr));   // ~25 ms
    const double aAmt = 1.0 - std::exp (-(double) kSub / (0.012 * sr));   // ~12 ms
    const double atk  = 1.0 - std::exp (-1.0 / (0.004 * sr));             // détecteur : 4 ms
    const double rel  = 1.0 - std::exp (-1.0 / (0.120 * sr));             //             120 ms
    const int designMode = mode == qmix::zeroLatency ? qmix::zeroLatency : qmix::analog;

    for (int b = 0; b < qmix::kNumBands; ++b)
    {
        auto& p = bandParam[b];
        auto& d = dsp[b];
        const int type  = (int) p.type->load();
        const bool isDyn = p.dyn->load() > 0.5f && qmix::canBeDynamic (type);
        const bool active = p.used->load() > 0.5f && p.on->load() > 0.5f && (! dynamicOnly || isDyn);
        const int slope = (int) p.slope->load();
        const int place = R != nullptr ? (int) p.place->load() : 0;
        const double tf = (double) p.freq->load(), tg = (double) p.gain->load() * scale, tq = (double) p.q->load();
        const double thr = (double) p.thresh->load(), rng = (double) p.range->load() * scale;

        if (! active && d.amt < 1.0e-5)
        {
            if (d.primed) { d.reset(); d.primed = false; }
            d.amt = 0.0; d.lastType = -1; d.env = 0.0; d.dynGain = 0.0; dynDb[b] = 0.0f;
            continue;
        }
        if (! d.primed) { d.sFreq = tf; d.sGain = tg; d.sQ = tq; d.primed = true; d.reset(); d.lastType = type; d.lastSlope = slope; d.lastMode = designMode; d.dFreq = -1; d.kFreq = -1; }

        const bool shapeChange = type != d.lastType || slope != d.lastSlope || designMode != d.lastMode;
        const double targetAmt = (active && ! shapeChange) ? 1.0 : 0.0;

        // détecteur : passe-bande à la fréquence de la bande (plateaux : Q large)
        if (isDyn)
        {
            qmix::Section ds; ds.fn = tf; ds.k = 1.0 / (qmix::hasGain (type) && type != qmix::bell ? 0.7 : jlimit (0.3, 8.0, tq)); ds.m0 = 0; ds.m1 = ds.k; ds.m2 = 0;
            ds.g = qmix::prewarp (tf, sr);
            d.det.from (ds);
        }

        for (int start = 0; start < n; start += kSub)
        {
            const int len = jmin (kSub, n - start);
            d.amt   += (targetAmt - d.amt) * aAmt;
            if (shapeChange && d.amt < 1.0e-3) { d.lastType = type; d.lastSlope = slope; d.lastMode = designMode; d.reset(); d.amt = 0.0; d.dFreq = -1; d.kFreq = -1; }
            d.sFreq *= std::pow (tf / d.sFreq, aSm);
            d.sQ    *= std::pow (tq / d.sQ, aSm);
            d.sGain += (tg - d.sGain) * aSm;

            // bande dynamique : niveau dans la bande -> gain ajouté (2:1 au-dessus du seuil, genou doux de 6 dB, jusqu'à RANGE)
            double dyn = 0.0;
            if (isDyn)
            {
                for (int i = start; i < start + len; ++i)
                {
                    double x;
                    switch (place)
                    {
                        case qmix::left:  x = std::abs (d.det.tick (L[i], d.detSt[0])); break;
                        case qmix::right: x = std::abs (d.det.tick (R[i], d.detSt[1])); break;
                        case qmix::mid:   x = std::abs (d.det.tick (0.5 * (L[i] + R[i]), d.detSt[0])); break;
                        case qmix::side:  x = std::abs (d.det.tick (0.5 * (L[i] - R[i]), d.detSt[1])); break;
                        default:          x = std::abs (d.det.tick (L[i], d.detSt[0]));
                                          if (R != nullptr) x = jmax (x, std::abs (d.det.tick (R[i], d.detSt[1])));
                                          break;
                    }
                    d.env += (x - d.env) * (x > d.env ? atk : rel);
                }
                const double lev = 20.0 * std::log10 (d.env + 1.0e-9);
                const double over = lev - thr;
                const double knee = 6.0;
                double o = over <= -knee * 0.5 ? 0.0 : over >= knee * 0.5 ? over : (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
                dyn = jlimit (-std::abs (rng), std::abs (rng), (rng >= 0 ? 1.0 : -1.0) * o * 0.5);
                d.dynGain = dyn;
                dynDb[b] = (float) dyn;
            }
            else if (d.dynGain != 0.0) { d.dynGain = 0.0; dynDb[b] = 0.0f; }

            const bool gainType = qmix::hasGain (d.lastType);
            const double gNow = gainType ? (d.sGain + dyn) * d.amt : d.sGain;
            if (std::abs (d.sFreq - d.dFreq) > 1.0e-6 * d.sFreq || std::abs (gNow - d.dGain) > 1.0e-5 || std::abs (d.sQ - d.dQ) > 1.0e-6 * d.sQ
                || (! gainType && std::abs (d.amt - d.dAmt) > 1.0e-6))
            {
                // Analog : le choix de la meilleure section n'est refait que si la bande a nettement bougé
                const bool reselect = d.kFreq < 0 || std::abs (std::log (d.sFreq / d.kFreq)) > 0.03 || std::abs (gNow - d.kGain) > 0.75
                                   || std::abs (std::log (d.sQ / d.kQ)) > 0.05;
                if (reselect) { d.kFreq = d.sFreq; d.kGain = gNow; d.kQ = d.sQ; }
                const auto des = qmix::design (d.lastType, d.sFreq, gNow, d.sQ, d.lastSlope, sr, designMode, d.kinds, ! reselect);
                d.n = des.n;
                for (int s = 0; s < des.n; ++s) d.c[s].from (des.s[s]);
                d.dFreq = d.sFreq; d.dGain = gNow; d.dQ = d.sQ; d.dAmt = d.amt;
            }
            const double wet = gainType ? 1.0 : d.amt;
            const double dry = 1.0 - wet;

            auto run = [&] (double x, int ch) noexcept
            {
                double y = x;
                for (int s = 0; s < d.n; ++s) y = d.c[s].tick (y, d.st[ch][s]);
                return dry * x + wet * y;
            };

            for (int i = start; i < start + len; ++i)
            {
                switch (place)
                {
                    case qmix::left:  L[i] = (float) run (L[i], 0); break;
                    case qmix::right: R[i] = (float) run (R[i], 1); break;
                    case qmix::mid:
                    case qmix::side:
                    {
                        double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]);
                        if (place == qmix::mid) m = run (m, 0); else s = run (s, 1);
                        L[i] = (float) (m + s); R[i] = (float) (m - s);
                        break;
                    }
                    default:
                        L[i] = (float) run (L[i], 0);
                        if (R != nullptr) R[i] = (float) run (R[i], 1);
                        break;
                }
            }
        }
    }
}

//==============================================================================
void QmixProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numCh = jmin (2, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (n == 0 || numCh == 0) return;
    if (n > maxBlock * 2)                                       // bloc plus grand qu'annoncé : découpé, jamais d'allocation
    {
        for (int s0 = 0; s0 < n; s0 += maxBlock)
        {
            AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), s0, jmin (maxBlock, n - s0));
            processBlock (part, midi);
        }
        return;
    }

    //-------------------------------------------------------------- MIDI : Learn + CC appris (file vers le thread de messages)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController()) continue;
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        const int lp = learnParam.load();
        if (lp >= 0)
        {
            for (auto& x : ccMap) if (x.load() == lp) x = -1;
            ccMap[cc] = lp; learnParam = -1; lastLearnedCc = cc;
            ccMin[cc] = v; ccMax[cc] = v;
        }
        const int pi = ccMap[cc].load();
        if (pi < 0 || pi >= kMaxParams) continue;
        if (v < ccMin[cc].load()) ccMin[cc] = v;
        if (v > ccMax[cc].load()) ccMax[cc] = v;
        const int lo = ccMin[cc].load(), hi = ccMax[cc].load();
        float x = (float) v / 127.0f;
        if (hi - lo >= 64) x = jlimit (0.0f, 1.0f, ((float) (v - lo) - 1.0f) / jmax (1.0f, (float) (hi - lo) - 2.0f));
        pendingValue[pi] = x;
        pendingSet[pi] = true;
        midiActivity.fetch_add (1);
    }
    midi.clear();

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    //-------------------------------------------------------------- analyseur : entrée
    const int am = analyzerMode.load();
    if (am == 2)
    {
        for (int i = 0; i < n; ++i) monoTmp[(size_t) i] = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        preFifo.push (monoTmp.data(), n);
    }

    const int mode = phaseMode.load();
    if (mode != activeMode)
    {
        if (activeMode >= 0) fadeIn = (int) (0.04 * sr);       // changement de mode : court fondu d'entrée
        activeMode = mode;
        for (auto& d : dsp) { d.reset(); d.dFreq = -1; }
    }
    const int latency = latencyFor (mode, lpQuality.load());

    // chemin sec (Power), retardé de la latence en phase linéaire
    for (int c = 0; c < numCh; ++c) dryBuf.copyFrom (c, 0, buffer, c, 0, n);
    {
        const int len = dryDelay.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const int rd = (dryPos - latency + len) % len;
            for (int c = 0; c < numCh; ++c)
            {
                dryDelay.setSample (c, dryPos, dryBuf.getSample (c, i));
                dryBuf.setSample (c, i, dryDelay.getSample (c, rd));
            }
            dryPos = (dryPos + 1) % len;
        }
    }

    //-------------------------------------------------------------- égalisation
    if (mode == qmix::linearPhase)
    {
        const bool cross = R != nullptr && crossNeeded.load();
        for (int c = 0; c < numCh; ++c) lpIn.copyFrom (c, 0, buffer, c, 0, n);
        auto runConv = [&] (int idx, int srcCh, int tmpCh)
        {
            lpTmp.copyFrom (tmpCh, 0, lpIn, srcCh, 0, n);
            float* ch[1] = { lpTmp.getWritePointer (tmpCh) };
            dsp::AudioBlock<float> blk (ch, 1, (size_t) n);
            conv[idx]->process (dsp::ProcessContextReplacing<float> (blk));
        };
        runConv (0, 0, 0);
        if (R != nullptr) runConv (3, 1, 3);
        if (cross) { runConv (1, 1, 1); runConv (2, 0, 2); }
        for (int i = 0; i < n; ++i)
        {
            L[i] = lpTmp.getSample (0, i) + (cross ? lpTmp.getSample (1, i) : 0.0f);
            if (R != nullptr) R[i] = lpTmp.getSample (3, i) + (cross ? lpTmp.getSample (2, i) : 0.0f);
        }
        runBands (L, R, n, true, qmix::analog);                // bandes dynamiques : IIR « Analog » après le RIF
    }
    else
        runBands (L, R, n, false, mode);

    //-------------------------------------------------------------- sortie + Power (fondu, sans clic)
    const double outT = Decibels::decibelsToGain ((double) outParam->load());
    const double pT = powerParam->load() > 0.5f ? 1.0 : 0.0;
    const double aO = 1.0 - std::exp (-1.0 / (0.02 * sr));
    const int fadeLen = jmax (1, (int) (0.04 * sr));
    for (int i = 0; i < n; ++i)
    {
        outSmooth += (outT - outSmooth) * aO;
        powerSmooth += (pT - powerSmooth) * aO;
        double fade = 1.0;
        if (fadeIn > 0) { fade = 1.0 - (double) fadeIn / fadeLen; --fadeIn; }
        for (int c = 0; c < numCh; ++c)
        {
            auto* x = buffer.getWritePointer (c);
            const double dryv = (double) dryBuf.getSample (c, i);
            x[i] = (float) (fade * (dryv + powerSmooth * ((double) x[i] * outSmooth - dryv)));
        }
    }

    //-------------------------------------------------------------- analyseur : sortie
    if (am > 0)
    {
        for (int i = 0; i < n; ++i) monoTmp[(size_t) i] = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        postFifo.push (monoTmp.data(), n);
    }
}

//==============================================================================
void QmixProcessor::timerCallback()
{
    auto& params = getParameters();
    for (int i = 0; i < params.size() && i < kMaxParams; ++i)
        if (pendingSet[i].exchange (false))
        {
            auto* p = params[i];
            p->beginChangeGesture();
            p->setValueNotifyingHost (pendingValue[i].load());
            p->endChangeGesture();
        }
    // latence annoncée à l'hôte (thread de messages)
    const int lat = latencyFor (phaseMode.load(), lpQuality.load());
    if (lat != getLatencySamples()) setLatencySamples (lat);
    // termes croisés coupés après leur fondu vers zéro
    if (designer != nullptr && designer->crossOffAt != 0 && Time::getMillisecondCounter() > designer->crossOffAt)
    {
        designer->crossOffAt = 0;
        crossNeeded = false;
    }
}

int QmixProcessor::paramIndexOf (const String& id) const
{
    auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (params[i]))
            if (wp->paramID == id) return i;
    return -1;
}

int QmixProcessor::ccForParam (int pi) const
{
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() == pi) return c;
    return -1;
}

void QmixProcessor::clearMidiFor (int pi)
{
    for (auto& x : ccMap) if (x.load() == pi) x = -1;
    if (learnParam.load() == pi) learnParam = -1;
}

//==============================================================================
const std::vector<QmixProcessor::FactoryPreset>& QmixProcessor::factoryPresets()
{
    using namespace qmix;
    static const std::vector<FactoryPreset> p = {
        { "Flat", {} },
        { "Vocal Presence",   { { lowCut, 90, 0, 0.7071f, 3 }, { bell, 250, -2.5f, 1.2f }, { bell, 3200, 2.5f, 0.9f }, { highShelf, 11000, 2.0f, 0.7f } } },
        { "Vocal De-Ess",     { { lowCut, 80, 0, 0.7071f, 3 }, { bell, 6500, 0.0f, 3.0f, 3, 0, true, -30.0f, -8.0f }, { highShelf, 10000, 1.5f, 0.7f } } },
        { "Guitar Cab Tamer", { { lowCut, 80, 0, 0.7071f, 3 }, { bell, 420, -2.0f, 1.4f }, { bell, 3800, 0.0f, 2.2f, 3, 0, true, -28.0f, -5.0f }, { highCut, 9500, 0, 0.7071f, 1 } } },
        { "Tight Bass",       { { lowCut, 35, 0, 0.7071f, 3 }, { bell, 90, 2.0f, 1.0f }, { bell, 280, -3.0f, 1.3f }, { bell, 1800, 1.5f, 1.0f } } },
        { "Mud Cut",          { { bell, 300, -4.0f, 1.6f } } },
        { "Air",              { { highShelf, 12000, 3.0f, 0.7f } } },
        { "Telephone",        { { lowCut, 400, 0, 0.7071f, 3 }, { highCut, 3400, 0, 0.7071f, 3 }, { bell, 1500, 4.0f, 1.0f } } },
        { "Mid / Side Wide",  { { lowCut, 120, 0, 0.7071f, 1, side }, { highShelf, 8000, 2.5f, 0.7f, 3, side }, { bell, 3000, -1.0f, 1.0f, 3, mid } } },
        { "Warm Tilt",        { { tiltShelf, 900, -3.0f, 0.7071f } } },
    };
    return p;
}

int QmixProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const String QmixProcessor::getProgramName (int i) { return isPositiveAndBelow (i, (int) factoryPresets().size()) ? String (factoryPresets()[(size_t) i].name) : String(); }
void QmixProcessor::setCurrentProgram (int i) { if (isPositiveAndBelow (i, getNumPrograms())) loadFactoryPreset (i); }

void QmixProcessor::loadFactoryPreset (int index)
{
    const auto& fp = factoryPresets()[(size_t) jlimit (0, (int) factoryPresets().size() - 1, index)];
    for (int b = 0; b < qmix::kNumBands; ++b) setBandParam (b, "used", 0.0f);
    int b = 0;
    for (const auto& x : fp.bands)
    {
        setBandParam (b, "type", (float) x.type); setBandParam (b, "freq", x.freq); setBandParam (b, "gain", x.gain);
        setBandParam (b, "q", x.q); setBandParam (b, "slope", (float) x.slope); setBandParam (b, "place", (float) x.place);
        setBandParam (b, "dyn", x.dyn ? 1.0f : 0.0f); setBandParam (b, "thresh", x.thresh); setBandParam (b, "range", x.range);
        setBandParam (b, "on", 1.0f); setBandParam (b, "used", 1.0f);
        ++b;
    }
    if (auto* o = apvts.getParameter ("output")) o->setValueNotifyingHost (o->convertTo0to1 (fp.out));
    currentPreset = index;
    presetName = fp.name;
    presetDirty = false;
}

ValueTree QmixProcessor::savePresetTree() const
{
    ValueTree t ("QMIXPRESET");
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (wp->paramID != "power")
                t.setProperty (wp->paramID, p->getValue(), nullptr);
    t.setProperty ("mode", phaseMode.load(), nullptr);
    return t;
}

void QmixProcessor::loadPresetTree (const ValueTree& t, const String& name)
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (t.hasProperty (wp->paramID))
                p->setValueNotifyingHost ((float) t[wp->paramID]);
    if (t.hasProperty ("mode")) phaseMode = jlimit (0, qmix::numModes - 1, (int) t["mode"]);
    presetName = name;
    currentPreset = -1;
    presetDirty = false;
}

//==============================================================================
void QmixProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiSize", uiSizeIdx.load(), nullptr);
    state.setProperty ("analyzer", analyzerMode.load(), nullptr);
    state.setProperty ("range", rangeIdx.load(), nullptr);
    state.setProperty ("glow", glowIdx.load(), nullptr);
    state.setProperty ("mode", phaseMode.load(), nullptr);
    state.setProperty ("lpQuality", lpQuality.load(), nullptr);
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("presetDirty", presetDirty.load(), nullptr);
    String map;
    for (int c = 0; c < 128; ++c)
        if (ccMap[c].load() >= 0)
            if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (getParameters()[ccMap[c].load()]))
                map << c << "=" << wp->paramID << ";";
    state.setProperty ("midiMap", map, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void QmixProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto t = ValueTree::fromXml (*xml);
        if (! t.isValid()) return;
        apvts.replaceState (t);
        uiSizeIdx    = (int) t.getProperty ("uiSize", -1);
        analyzerMode = (int) t.getProperty ("analyzer", 1);
        rangeIdx     = (int) t.getProperty ("range", 1);
        glowIdx      = (int) t.getProperty ("glow", 2);
        phaseMode    = jlimit (0, qmix::numModes - 1, (int) t.getProperty ("mode", (int) qmix::analog));
        lpQuality    = jlimit (0, 2, (int) t.getProperty ("lpQuality", 1));
        currentPreset = (int) t.getProperty ("preset", 0);
        presetName   = t.getProperty ("presetName", "Flat").toString();
        presetDirty  = (bool) t.getProperty ("presetDirty", false);
        for (auto& x : ccMap) x = -1;
        for (auto& e : StringArray::fromTokens (t.getProperty ("midiMap").toString(), ";", ""))
        {
            const int c = e.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const int pi = paramIndexOf (e.fromFirstOccurrenceOf ("=", false, false));
            if (isPositiveAndBelow (c, 128) && pi >= 0) ccMap[c] = pi;
        }
    }
}

AudioProcessorEditor* QmixProcessor::createEditor() { return new QmixEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new QmixProcessor(); }
