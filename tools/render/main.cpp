// Captures de l'interface de QMix (hors plugin) : tools/render
#include "PluginEditor.h"
#include <random>
using namespace juce;

static std::function<void(QmixEditor*)> gTick;
static void tickEditor (QmixEditor* ed) { gTick (ed); }
static void runAudio (QmixProcessor& p, QmixEditor* ed, int blocks)
{
    AudioBuffer<float> buf (2, 512); MidiBuffer midi;
    static std::mt19937 rng (7); std::normal_distribution<float> nd (0.0f, 1.0f);
    static float b0 = 0, b1 = 0, b2 = 0, ph = 0, ph2 = 0;
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float w = nd (rng) * 0.05f;                       // bruit rose (Kellet, simplifié)
            b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
            float x = (b0 + b1 + b2 + w * 0.1848f) * 0.6f;
            ph += 110.0f / 48000.0f; ph2 += 2450.0f / 48000.0f;
            x += 0.08f * std::sin (MathConstants<float>::twoPi * ph) + 0.02f * std::sin (MathConstants<float>::twoPi * ph2);
            buf.setSample (0, i, x); buf.setSample (1, i, x * 0.9f + nd (rng) * 0.004f);
        }
        p.processBlock (buf, midi);
        if (ed != nullptr && k % 2 == 0) tickEditor (ed);
    }
}

static void snap (Component& c, const String& name, float scale)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    File out = File::getCurrentWorkingDirectory().getChildFile (name);
    out.deleteFile();
    FileOutputStream os (out); PNGImageFormat png; png.writeImageToStream (img, os);
    std::cout << "ecrit " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
}

static double measureDb (const AudioBuffer<float>& y, int ch, double f, double fs)
{
    std::complex<double> h (0, 0);
    for (int n = 0; n < y.getNumSamples(); ++n) h += (double) y.getSample (ch, n) * std::polar (1.0, -2.0 * MathConstants<double>::pi * f * n / fs);
    return 20 * std::log10 (std::abs (h));
}

static AudioBuffer<float> impulseThrough (QmixProcessor& p, int N)
{
    AudioBuffer<float> warm (2, 512); MidiBuffer m;
    for (int k = 0; k < 60; ++k) { warm.clear(); p.processBlock (warm, m); }
    AudioBuffer<float> imp (2, N); imp.clear(); imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 1.0f);
    for (int s0 = 0; s0 < N; s0 += 512) { AudioBuffer<float> blk (imp.getArrayOfWritePointers(), 2, s0, 512); p.processBlock (blk, m); }
    return imp;
}

int main()
{
    gTick = [] (QmixEditor* e) { e->timerCallback(); };
    ScopedJuceInitialiser_GUI init;

    // 1. réponse analogique : écart au prototype analogique (bilinéaire vs apparié), jusqu'à 20 kHz
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        double worstB = 0, worstM = 0;
        struct C { int t; double f, g, q; int sl; };
        std::vector<C> cases;
        for (double f : { 1000.0, 5000.0, 10000.0, 14000.0, 18000.0 })
            for (double q : { 0.5, 1.0, 3.0 })
                for (double g : { -12.0, 6.0, 12.0 }) cases.push_back ({ qmix::bell, f, g, q, 3 });
        for (double f : { 3000.0, 8000.0, 12000.0 }) for (double g : { -9.0, 9.0 }) { cases.push_back ({ qmix::highShelf, f, g, 0.7, 3 }); cases.push_back ({ qmix::lowShelf, f, g, 0.7, 3 }); }
        for (double f : { 6000.0, 12000.0, 16000.0 }) for (int sl : { 1, 3, 6 }) cases.push_back ({ qmix::highCut, f, 0, 0.7071, sl });
        for (auto& c : cases)
        {
            const auto an = qmix::designAnalog (c.t, c.f, c.g, c.q, c.sl);
            const auto bi = qmix::design (c.t, c.f, c.g, c.q, c.sl, fs, qmix::zeroLatency);
            const auto ma = qmix::design (c.t, c.f, c.g, c.q, c.sl, fs, qmix::analog);
            for (double f = 20; f <= 20000; f *= 1.02)
            {
                const double ref = qmix::analogDb (an, f);
                if (ref < -40) continue;
                worstB = jmax (worstB, std::abs (qmix::responseDb (bi, f, fs) - ref));
                worstM = jmax (worstM, std::abs (qmix::responseDb (ma, f, fs) - ref));
            }
        }
        std::cout << "fs " << fs << " : ecart max au prototype analogique  bilineaire " << worstB << " dB  |  Analog " << worstM << " dB\n";
    }
    // pire cas détaillé : cloche 15 kHz +12 dB Q 2 à 44,1 kHz
    {
        const auto an = qmix::designAnalog (qmix::bell, 15000, 12, 2, 3);
        const auto bi = qmix::design (qmix::bell, 15000, 12, 2, 3, 44100.0, qmix::zeroLatency);
        const auto ma = qmix::design (qmix::bell, 15000, 12, 2, 3, 44100.0, qmix::analog);
        for (double f : { 10000.0, 13000.0, 15000.0, 17000.0, 20000.0 })
            std::cout << "  cloche 15k +12 Q2 @44.1k, " << f << " Hz : analogique " << qmix::analogDb (an, f) << "  bilineaire " << qmix::responseDb (bi, f, 44100.0) << "  Analog " << qmix::responseDb (ma, f, 44100.0) << "\n";
    }

    // 0. banc d'essai CPU (48 kHz, blocs de 512, stéréo, 10 s de bruit)
    {
        auto bench = [] (const char* name, std::function<void (QmixProcessor&)> setup)
        {
            QmixProcessor q; q.setPlayConfigDetails (2, 2, 48000.0, 512); q.prepareToPlay (48000.0, 512);
            setup (q);
            AudioBuffer<float> b (2, 512); MidiBuffer m; Random r (3);
            for (int k = 0; k < 100; ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, r.nextFloat() * 0.5f - 0.25f); q.processBlock (b, m); }
            Thread::sleep (400);
            const int blocks = 48000 * 10 / 512;
            const double t0 = Time::getMillisecondCounterHiRes();
            for (int k = 0; k < blocks; ++k)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, r.nextFloat() * 0.5f - 0.25f);
                if (k % 8 == 0) if (auto* pf = q.apvts.getParameter ("b3_freq")) pf->setValueNotifyingHost (0.3f + 0.4f * (float) ((k / 8) % 2));   // automation
                q.processBlock (b, m);
            }
            const double ms = Time::getMillisecondCounterHiRes() - t0;
            std::cout << "BENCH " << name << " : " << ms / 100.0 << " % d'un coeur (10 s d'audio en " << ms << " ms)\n";
        };
        auto bells = [] (QmixProcessor& q, int mode, int fl, bool dyn)
        {
            q.phaseMode = mode; q.analogFlavour = fl;
            for (int b = 0; b < 24; ++b) { const int i = q.createBand (40.0f * std::pow (1.3f, (float) b), (b % 2 ? 3.0f : -3.0f), qmix::bell);
                                           if (dyn) { q.setBandParam (i, "dyn", 1.0f); q.setBandParam (i, "thresh", -40.0f); } }
        };
        bench ("24 cloches Zero Latency", [&] (QmixProcessor& q) { bells (q, 0, 0, false); });
        bench ("24 cloches Analog Clean", [&] (QmixProcessor& q) { bells (q, 1, 0, false); });
        bench ("24 cloches Analog SSL + COLOR", [&] (QmixProcessor& q) { bells (q, 1, 1, false); });
        bench ("24 cloches dynamiques Analog", [&] (QmixProcessor& q) { bells (q, 1, 0, true); });
        bench ("4 coupes 96 dB/oct + 20 cloches M/S Analog", [&] (QmixProcessor& q) { bells (q, 1, 0, false); for (int b = 0; b < 4; ++b) { q.setBandParam (b, "type", (float) qmix::lowCut); q.setBandParam (b, "slope", 8.0f); }
                                                                                    for (int b = 4; b < 24; ++b) q.setBandParam (b, "place", (float) (b % 2 ? qmix::mid : qmix::side)); });
        bench ("24 cloches Linear Phase Medium", [&] (QmixProcessor& q) { bells (q, 2, 0, false); });
    }

    // 0 ter. sauts de réglages extrêmes : crête mesurée / gain théorique
    for (int noRamp = 0; noRamp < 1; ++noRamp)
        for (int mode : { 0, 1 })
            for (int type : { (int) qmix::bell, (int) qmix::lowCut, (int) qmix::highShelf, (int) qmix::notch })
            {
                QmixProcessor q; q.setPlayConfigDetails (2, 2, 44100.0, 256); q.prepareToPlay (44100.0, 256); q.phaseMode = mode;
                const int b = q.createBand (100.0f, 30.0f, type); q.setBandParam (b, "q", 40.0f); q.setBandParam (b, "slope", 8.0f);
                Random r (5); AudioBuffer<float> buf (2, 256); MidiBuffer m; double peak = 0;
                for (int k = 0; k < 44100 * 10 / 256; ++k)
                {
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buf.setSample (c, i, (r.nextFloat() - 0.5f) * 0.5f);
                    if (k % 20 == 0) { q.setBandParam (b, "freq", (k / 20) % 2 ? 10000.0f : 60.0f); q.setBandParam (b, "q", (k / 40) % 2 ? 40.0f : 0.3f); }
                    q.processBlock (buf, m);
                    for (int i = 0; i < 256; ++i) peak = jmax (peak, (double) std::abs (buf.getSample (0, i)));
                }
                double mx = -200;
                for (double qq : { 40.0, 0.3 }) for (double f = 20; f < 20000; f *= 1.005) for (double fc : { 60.0, 10000.0 })
                    mx = jmax (mx, qmix::analogDb (qmix::designAnalog (type, fc, 30.0, qq, 8), f));
                std::cout << "SAUTS " << "(rampe)" << " " << qmix::modeName (mode) << " " << qmix::typeName (type) << " : crete " << Decibels::gainToDecibels (peak) << " dBFS (gain max des reglages " << mx << " dB)\n";
            }

    // 0 bis. essai de robustesse : réglages aléatoires à chaque bloc, tous modes, 60 s de bruit
    for (int mode = 0; mode < 3; ++mode)
    {
        QmixProcessor q; q.setPlayConfigDetails (2, 2, 44100.0, 256); q.prepareToPlay (44100.0, 256);
        q.phaseMode = mode;
        Random r (11 + mode); AudioBuffer<float> b (2, 256); MidiBuffer m;
        for (int i = 0; i < 24; ++i) q.createBand (20.0f + r.nextFloat() * 20000.0f, 0.0f, r.nextInt (qmix::numTypes));
        double peak = 0; bool bad = false;
        for (int k = 0; k < 44100 * 60 / 256; ++k)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) b.setSample (c, i, (r.nextFloat() - 0.5f) * 0.5f);
            if (k % 3 == 0)
            {
                const int band = r.nextInt (24);
                for (auto* id : { "freq", "gain", "q", "slope", "place", "type", "dyn", "thresh", "range", "on" })
                    if (r.nextFloat() < 0.3f) if (auto* pp = q.apvts.getParameter (QmixProcessor::bandId (band, id))) pp->setValueNotifyingHost (r.nextFloat());
                if (k % 300 == 0) { q.analogFlavour = r.nextInt (3); if (auto* cp = q.apvts.getParameter ("color")) cp->setValueNotifyingHost (r.nextFloat()); }
            }
            q.processBlock (b, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) bad = true; peak = jmax (peak, (double) std::abs (v)); }
            if (mode == 2 && k % 50 == 0) Thread::sleep (1);
        }
        std::cout << "ROBUSTESSE mode " << qmix::modeName (mode) << " : " << (bad ? "NaN/Inf !" : "aucun NaN ni Inf") << " (crete " << Decibels::gainToDecibels (peak) << " dBFS avec gains et Q aleatoires jusqu'aux extremes)\n";
    }

    QmixProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    // 2. mode Analog : réponse mesurée = courbe affichée
    {
        p.phaseMode = qmix::analog;
        p.loadFactoryPreset (0);
        const int b = p.createBand (12000.0f, 8.0f, qmix::bell); p.setBandParam (b, "q", 2.0f);
        const int c = p.createBand (120.0f, 0.0f, qmix::lowCut); p.setBandParam (c, "slope", 4.0f); p.setBandParam (c, "q", 1.5f);
        auto y = impulseThrough (p, 1 << 15);
        double worst = 0;
        for (double f : { 60.0, 120.0, 1000.0, 8000.0, 12000.0, 16000.0, 20000.0 })
        {
            double expect = 0;
            for (int bb = 0; bb < qmix::kNumBands; ++bb) { auto v = p.bandValues (bb); if (v.used) expect += qmix::responseDb (qmix::design (v.type, v.freq, v.gain, v.q, v.slope, 48000.0, qmix::analog), f, 48000.0); }
            worst = jmax (worst, std::abs (measureDb (y, 0, f, 48000.0) - expect));
        }
        std::cout << "Analog : ecart mesure / courbe " << worst << " dB\n";
    }

    // 3. phase linéaire : latence, symétrie, module = prototype analogique (y compris Mid/Side)
    {
        p.phaseMode = qmix::linearPhase; p.lpQuality = 1;
        p.loadFactoryPreset (0);
        const int b = p.createBand (12000.0f, 8.0f, qmix::bell); p.setBandParam (b, "q", 2.0f);
        const int c = p.createBand (300.0f, -6.0f, qmix::bell); p.setBandParam (c, "place", (float) qmix::side);
        p.prepareToPlay (48000.0, 512);
        for (int i = 0; i < 100 && ! p.firReady.load(); ++i) Thread::sleep (20);
        Thread::sleep (300);
        AudioBuffer<float> warm (2, 512); MidiBuffer m;
        for (int k = 0; k < 200; ++k) { warm.clear(); p.processBlock (warm, m); Thread::sleep (1); }
        const int N = 1 << 15;
        AudioBuffer<float> imp (2, N); imp.clear(); imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 0.0f);   // impulsion à gauche seulement
        for (int s0 = 0; s0 < N; s0 += 512) { AudioBuffer<float> blk (imp.getArrayOfWritePointers(), 2, s0, 512); p.processBlock (blk, m); }
        int peak = 0; float pv = 0; for (int n = 0; n < N; ++n) if (std::abs (imp.getSample (0, n)) > pv) { pv = std::abs (imp.getSample (0, n)); peak = n; }
        double asym = 0; for (int k = 1; k < 3000; ++k) asym = jmax (asym, (double) std::abs (imp.getSample (0, peak + k) - imp.getSample (0, peak - k)));
        std::cout << "Linear Phase : latence annoncee " << p.getLatencySamples() << " (attendue " << QmixProcessor::firLength (1) / 2 << "), pic a " << peak << ", asymetrie max " << asym << "\n";
        // L in -> L out = ½(Hm+Hs)Hstereo ; L in -> R out = ½(Hm−Hs)Hstereo
        const auto bell = qmix::designAnalog (qmix::bell, 12000, 8, 2, 3), sideB = qmix::designAnalog (qmix::bell, 300, -6, 1, 3);
        double worst = 0;
        for (double f : { 100.0, 300.0, 1000.0, 8000.0, 12000.0, 16000.0 })
        {
            const double hs = std::sqrt (qmix::analogMag2 (sideB, f)), hb = std::sqrt (qmix::analogMag2 (bell, f));
            const double eLL = 20 * std::log10 (0.5 * (1 + hs) * hb), eRL = 20 * std::log10 (std::abs (0.5 * (1 - hs)) * hb + 1e-12);
            const double mLL = measureDb (imp, 0, f, 48000.0), mRL = measureDb (imp, 1, f, 48000.0);
            worst = jmax (worst, std::abs (mLL - eLL));
            std::cout << "  " << f << " Hz : L->L " << mLL << " (cible " << eLL << ")   L->R " << mRL << " (cible " << eRL << ")\n";
        }
        std::cout << "Linear Phase : ecart max L->L " << worst << " dB\n";
    }

    // 4. bande dynamique : sinus au-dessus du seuil -> réduction 2:1 jusqu'à RANGE
    {
        p.phaseMode = qmix::analog; p.prepareToPlay (48000.0, 512);
        p.loadFactoryPreset (0);
        const int b = p.createBand (3000.0f, 0.0f, qmix::bell);
        p.setBandParam (b, "dyn", 1.0f); p.setBandParam (b, "thresh", -30.0f); p.setBandParam (b, "range", -9.0f);
        MidiBuffer m;
        for (double lev : { -40.0, -26.0, -20.0, -6.0 })
        {
            AudioBuffer<float> x (2, 512); double ph = 0; double outRms = 0; int cnt = 0;
            const double a = Decibels::decibelsToGain (lev) * std::sqrt (2.0);
            for (int k = 0; k < 200; ++k)
            {
                for (int i = 0; i < 512; ++i) { const float v = (float) (a * std::sin (ph)); ph += 2 * MathConstants<double>::pi * 3000.0 / 48000.0; x.setSample (0, i, v); x.setSample (1, i, v); }
                p.processBlock (x, m);
                if (k > 150) for (int i = 0; i < 512; ++i) { outRms += x.getSample (0, i) * x.getSample (0, i); ++cnt; }
            }
            std::cout << "  dynamique : entree " << lev << " dBFS -> gain " << (10 * std::log10 (outRms / cnt) - lev) << " dB (dyn " << p.dynDb[b].load() << ")\n";
        }
    }

    // 4 bis. caractère du mode Analog : largeur des cloches selon le gain, plateaux, et mesure = courbe
    for (int fl = 0; fl < qmix::numFlavours; ++fl)
    {
        std::cout << "Analog " << qmix::flavourName (fl) << " :";
        for (double g : { 3.0, 9.0, 18.0 })
        {
            const auto d = qmix::design (qmix::bell, 1000, g, 1.0, 3, 48000.0, qmix::analog, nullptr, false, fl);
            double lo = 0, hi = 0;
            for (double f = 30; f < 20000; f *= 1.002) { const double v = qmix::responseDb (d, f, 48000.0); if (v > g * 0.5) { if (lo == 0) lo = f; hi = f; } }
            std::cout << "  cloche Q1 " << g << " dB -> " << std::log2 (hi / lo) << " oct";
        }
        const auto sh = qmix::design (qmix::lowShelf, 100, 12, 0.7071, 3, 48000.0, qmix::analog, nullptr, false, fl);
        double mn = 0; for (double f = 100; f < 3000; f *= 1.01) mn = jmin (mn, qmix::responseDb (sh, f, 48000.0));
        std::cout << "  | plateau bas +12 dB : creux " << mn << " dB\n";
    }
    {
        p.phaseMode = qmix::analog; p.prepareToPlay (48000.0, 512);
        if (auto* cp = p.apvts.getParameter ("color")) cp->setValueNotifyingHost (0.0f);       // courbes seules (couleur coupée)
        for (int fl : { 1, 2 })
        {
            p.analogFlavour = fl;
            p.loadFactoryPreset (1);
            auto y = impulseThrough (p, 1 << 15);
            double worst = 0;
            for (double f : { 60.0, 250.0, 1000.0, 3200.0, 11000.0, 16000.0 })
            {
                double expect = 0;
                for (int bb = 0; bb < qmix::kNumBands; ++bb) { auto v = p.bandValues (bb); if (v.used) expect += qmix::responseDb (qmix::design (v.type, v.freq, v.gain, v.q, v.slope, 48000.0, qmix::analog, nullptr, false, fl), f, 48000.0); }
                worst = jmax (worst, std::abs (measureDb (y, 0, f, 48000.0) - expect));
            }
            std::cout << "Analog " << qmix::flavourName (fl) << " : ecart mesure / courbe " << worst << " dB\n";
        }
        p.analogFlavour = 0;
    }

    // 4 ter. couleur de console : harmoniques (sinus pur, égaliseur à plat)
    {
        p.phaseMode = qmix::analog; p.prepareToPlay (48000.0, 512); p.loadFactoryPreset (0);
        MidiBuffer m;
        auto harm = [&] (const AudioBuffer<float>& y, int start, int len, double f, int h)
        {
            std::complex<double> acc (0, 0);
            for (int nn = 0; nn < len; ++nn)
            {
                const double w = 0.5 - 0.5 * std::cos (2 * MathConstants<double>::pi * nn / (len - 1));
                acc += w * (double) y.getSample (0, start + nn) * std::polar (1.0, -2 * MathConstants<double>::pi * f * h * nn / 48000.0);
            }
            return std::abs (acc);
        };
        for (int fl : { 0, 1, 2 })
            for (float col : { 50.0f, 100.0f })
                for (auto [f, lev] : std::vector<std::pair<double, double>> { { 1000.0, -18.0 }, { 1000.0, -6.0 }, { 60.0, -12.0 } })
                {
                    p.analogFlavour = fl;
                    if (auto* cp = p.apvts.getParameter ("color")) cp->setValueNotifyingHost (col / 100.0f);
                    const int N = 48000; AudioBuffer<float> y (2, N);
                    const double a = Decibels::decibelsToGain (lev);
                    for (int nn = 0; nn < N; ++nn) { const float v = (float) (a * std::sin (2 * MathConstants<double>::pi * f * nn / 48000.0)); y.setSample (0, nn, v); y.setSample (1, nn, v); }
                    for (int s0 = 0; s0 < N; s0 += 512) { AudioBuffer<float> blk (y.getArrayOfWritePointers(), 2, s0, jmin (512, N - s0)); p.processBlock (blk, m); }
                    const double h1 = harm (y, 24000, 24000, f, 1), h2 = harm (y, 24000, 24000, f, 2), h3 = harm (y, 24000, 24000, f, 3), h5 = harm (y, 24000, 24000, f, 5);
                    std::cout << "  couleur " << qmix::flavourName (fl) << " " << col << " %, " << f << " Hz " << lev << " dBFS : H2 " << 20 * std::log10 (h2 / h1 + 1e-12)
                              << " dB, H3 " << 20 * std::log10 (h3 / h1 + 1e-12) << " dB, H5 " << 20 * std::log10 (h5 / h1 + 1e-12) << " dB\n";
                }
        p.analogFlavour = 0;
        if (auto* cp = p.apvts.getParameter ("color")) cp->setValueNotifyingHost (0.5f);
    }

    // 4 quater. menu des formes (icônes de courbe), dessiné hors fenêtre pour contrôle
    {
        std::unique_ptr<QmixEditor> ed (dynamic_cast<QmixEditor*> (p.createEditor()));
        const int W = 240, H = 8 * 32 + 14;
        Image im (Image::ARGB, W * 2, H * 2, true);
        Graphics g (im); g.addTransform (AffineTransform::scale (2.0f));
        ed->menuLnf.drawPopupMenuBackground (g, W, H);
        for (int t = 0; t < qmix::numTypes; ++t)
            ed->menuLnf.drawPopupMenuItem (g, Rectangle<int> (0, 7 + t * 32, W, 32), false, true, t == qmix::lowCut, t == qmix::lowShelf, false,
                                           qmix::typeName (t), {}, nullptr, nullptr);
        File out = File::getCurrentWorkingDirectory().getChildFile ("qmix_shape_menu.png"); out.deleteFile();
        FileOutputStream os (out); PNGImageFormat png; png.writeImageToStream (im, os);
    }

    // 5. captures (notice, README de contrôle)
    p.phaseMode = qmix::analog; p.prepareToPlay (48000.0, 512);
    p.loadFactoryPreset (1);
    p.createBand (6200.0f, 0.0f, qmix::bell);
    p.setBandParam (4, "q", 3.0f); p.setBandParam (4, "dyn", 1.0f); p.setBandParam (4, "thresh", -42.0f); p.setBandParam (4, "range", -7.0f);
    p.analyzerMode = 2;
    {
        std::unique_ptr<QmixEditor> ed (dynamic_cast<QmixEditor*> (p.createEditor()));
        ed->setSize (960, 540);
        ed->selectBand (2);
        runAudio (p, ed.get(), 90);
        auto quiet = [&] { ed->statusTime = -10; ed->statusAlpha = 0; ed->display.hover = -1; };
        quiet(); snap (*ed, "qmix_overview.png", 2.0f);
        ed->showStatus (ed->bandSummary (2)); ed->statusAlpha = 1.0f; snap (*ed, "qmix_status.png", 2.0f);
        ed->selectBand (4); runAudio (p, ed.get(), 30); quiet(); ed->display.hover = 4; snap (*ed, "qmix_dyn.png", 2.0f);
        p.phaseMode = qmix::linearPhase; ed->selectBand (-1); runAudio (p, ed.get(), 30); quiet(); snap (*ed, "qmix_linear.png", 2.0f);
        // même réglage, trois caractères (cloches à +4 / −4 dB et plateau bas +8 dB)
        p.phaseMode = qmix::analog; p.loadFactoryPreset (0); ed->selectBand (-1);
        p.createBand (90.0f, 8.0f, qmix::lowShelf); p.createBand (400.0f, -4.0f, qmix::bell); p.createBand (3000.0f, 4.0f, qmix::bell);
        for (int fl = 0; fl < qmix::numFlavours; ++fl)
        {
            p.analogFlavour = fl; runAudio (p, ed.get(), 12); quiet();
            snap (*ed, "qmix_flavour" + String (fl) + ".png", 2.0f);
        }
        p.analogFlavour = 0;
        p.phaseMode = qmix::zeroLatency; p.loadFactoryPreset (0); runAudio (p, ed.get(), 10); quiet(); snap (*ed, "qmix_empty.png", 2.0f);
    }
    return 0;
}
