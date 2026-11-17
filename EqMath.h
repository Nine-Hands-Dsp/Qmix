#pragma once
#include <cmath>
#include <complex>
#include <algorithm>

//==============================================================================
//  QMix : calcul des filtres (partagé par le moteur audio et l'affichage)
//
//  Chaque bande est une cascade de sections décrites d'abord en analogique
//  (fréquence propre fn en Hz, fonction de transfert en s = j f / fn).
//  Trois façons de les passer en numérique :
//   - Zero Latency : transformée bilinéaire (comme ReaEQ) ; les cloches très
//     aiguës se resserrent près de Nyquist ;
//   - Analog : sections « appariées » (pôles analogiques transposés exactement,
//     zéros calculés pour retrouver le module analogique à 0 Hz, à fn et à
//     Nyquist) : plus de resserrement dans l'aigu, phase analogique, latence nulle ;
//   - Linear Phase : le module analogique exact sert de cible à un filtre RIF
//     symétrique (moteur dans PluginProcessor).
//  Toutes les sections numériques sont exécutées sous forme de filtres à
//  variables d'état (SVF « TPT » de Cytomic / A. Simper), très stables quand on
//  tourne les réglages en temps réel. La courbe affichée utilise exactement la
//  même fonction de transfert que le moteur audio.
//==============================================================================
namespace qmix
{
    constexpr int kNumBands    = 24;
    constexpr int kMaxSections = 9;          // 96 dB/oct = 16 pôles = 8 sections du 2e ordre (+1 de marge)
    constexpr double kPi = 3.14159265358979323846;

    enum BandType { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf, numTypes };
    inline const char* typeName (int t)
    {
        static const char* n[numTypes] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
        return n[std::clamp (t, 0, numTypes - 1)];
    }
    inline const char* typeShort (int t)
    {
        static const char* n[numTypes] = { "BELL", "LO SHELF", "HI SHELF", "LO CUT", "HI CUT", "NOTCH", "BAND PASS", "TILT" };
        return n[std::clamp (t, 0, numTypes - 1)];
    }
    inline bool hasGain (int t)  { return t == bell || t == lowShelf || t == highShelf || t == tiltShelf; }
    inline bool hasSlope (int t) { return t == lowCut || t == highCut; }
    inline bool canBeDynamic (int t) { return hasGain (t); }

    constexpr int kNumSlopes = 9;
    inline int slopeDb (int i) { static const int s[kNumSlopes] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 }; return s[std::clamp (i, 0, kNumSlopes - 1)]; }

    enum Placement { stereo = 0, left, right, mid, side, numPlacements };
    inline const char* placementName (int p) { static const char* n[numPlacements] = { "Stereo", "Left", "Right", "Mid", "Side" }; return n[std::clamp (p, 0, numPlacements - 1)]; }
    inline const char* placementShort (int p) { static const char* n[numPlacements] = { "ST", "L", "R", "M", "S" }; return n[std::clamp (p, 0, numPlacements - 1)]; }

    enum PhaseMode { zeroLatency = 0, analog, linearPhase, numModes };
    inline const char* modeName (int m) { static const char* n[numModes] = { "Zero Latency", "Analog", "Linear Phase" }; return n[std::clamp (m, 0, numModes - 1)]; }

    //------------------------------------------------------------------ caractère du mode Analog (comportement de console)
    //  Clean      : les réglages tels quels (Q constant).
    //  SSL 4000 G : égaliseur « 292 Pink Knob » : Q proportionnel au gain sur les cloches (courbes larges
    //               aux petits gains, Q affiché atteint vers ±18 dB, comme l'égaliseur E au gain maximal) ;
    //               plateaux plus raides avec léger creux / bosse (undershoot / overshoot) au point de bascule.
    //  Neve VR    : « Q automatically varies with gain on all bands in peaking and shelving modes » (manuel VR
    //               Legend) : cloches ET plateaux s'élargissent aux petits gains, de façon plus douce que la SSL.
    //  Interprétation de QMix à partir des documentations (aucune mesure de console fournie).
    enum AnalogFlavour { flavourClean = 0, flavourSsl, flavourNeve, numFlavours };
    inline const char* flavourName (int f) { static const char* n[numFlavours] = { "Clean", "SSL 4000 G", "Neve VR" }; return n[std::clamp (f, 0, numFlavours - 1)]; }
    inline const char* flavourShort (int f) { static const char* n[numFlavours] = { "ANALOG", "SSL G", "NEVE VR" }; return n[std::clamp (f, 0, numFlavours - 1)]; }

    inline double flavourQ (int flavour, int type, double q, double db)
    {
        const double x = std::min (1.0, std::abs (db) / 18.0);           // part du gain maximal des deux consoles (±18 dB)
        switch (flavour)
        {
            case flavourSsl:
                if (type == 0 /*bell*/)                 return q * (0.4 + 0.6 * std::pow (x, 0.8));
                if (type == 1 || type == 2 /*shelves*/) return q * 1.6;
                return q;
            case flavourNeve:
                if (type == 0 || type == 1 || type == 2) return q * (0.55 + 0.45 * x);
                return q;
            default: return q;
        }
    }

    //------------------------------------------------------------------ une section
    //  2e ordre : H(s) = m0 + (m1 s + m2) / (s² + k s + 1)
    //  1er ordre : H(s) = m0 + m2 / (1 + s)
    //  s = j f / fn en analogique ; en numérique, s = j tan(pi f / fs) / g
    struct Section
    {
        bool firstOrder = false;
        double fn = 1000.0;                               // fréquence propre analogique (Hz)
        double g = 0.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };

    struct Design
    {
        int n = 0;
        Section s[kMaxSections];
    };

    inline double prewarp (double hz, double fs) { return std::tan (kPi * std::min (hz, fs * 0.4999) / fs); }

    // Conception analogique d'une bande : fréquence (Hz), gain (dB), Q, type, pente (index)
    inline Design designAnalog (int type, double hz, double db, double q, int slopeIdx)
    {
        Design d;
        q = std::clamp (q, 0.025, 40.0);
        const double A = std::pow (10.0, db / 40.0);
        auto add2 = [&] (double fn, double k, double m0, double m1, double m2)
        {
            auto& s = d.s[d.n++]; s.firstOrder = false; s.fn = fn; s.k = k; s.m0 = m0; s.m1 = m1; s.m2 = m2;
        };
        switch (type)
        {
            case bell:      add2 (hz, 1.0 / (q * A), 1.0, (1.0 / (q * A)) * (A * A - 1.0), 0.0); break;
            case lowShelf:  add2 (hz / std::sqrt (A), 1.0 / q, 1.0, (1.0 / q) * (A - 1.0), A * A - 1.0); break;
            case highShelf: add2 (hz * std::sqrt (A), 1.0 / q, A * A, (1.0 / q) * (1.0 - A) * A, 1.0 - A * A); break;
            case notch:     add2 (hz, 1.0 / q, 1.0, -1.0 / q, 0.0); break;
            case bandPass:  add2 (hz, 1.0 / q, 0.0, 1.0 / q, 0.0); break;
            case tiltShelf:                                        // -g/2 en bas, +g/2 en haut, pivot sur la fréquence
            {
                const double a = std::pow (10.0, (db * 0.5) / 40.0);
                const double kq = 1.0 / std::clamp (q * 0.7071, 0.1, 2.0);
                const double la = 1.0 / a;
                add2 (hz / std::sqrt (la), kq, 1.0, kq * (la - 1.0), la * la - 1.0);
                add2 (hz * std::sqrt (a),  kq, a * a, kq * (1.0 - a) * a, 1.0 - a * a);
                break;
            }
            case lowCut:
            case highCut:
            {
                const bool hp = type == lowCut;
                const int order = slopeDb (slopeIdx) / 6;
                const double res = q / 0.70710678118654752;         // Q = 0,707 : Butterworth pur
                if (order & 1)
                {
                    auto& s = d.s[d.n++]; s.firstOrder = true; s.fn = hz;
                    s.m0 = hp ? 1.0 : 0.0; s.m2 = hp ? -1.0 : 1.0;
                }
                const int pairs = order / 2;
                for (int i = 0; i < pairs; ++i)
                {
                    const double theta = (order & 1) ? kPi * (i + 1) / order : kPi * (2 * i + 1) / (2.0 * order);
                    double qs = 1.0 / (2.0 * std::cos (theta));
                    if (i == pairs - 1) qs *= res;                     // la résonance sur la section la plus aiguë
                    qs = std::clamp (qs, 0.1, 60.0);
                    const double k = 1.0 / qs;
                    if (hp) add2 (hz, k, 1.0, -k, -1.0);
                    else    add2 (hz, k, 0.0, 0.0, 1.0);
                }
                break;
            }
            default: break;
        }
        return d;
    }

    // réponse analogique exacte d'une section / d'une conception (f en Hz)
    inline std::complex<double> sectionAnalog (const Section& s, double f)
    {
        const std::complex<double> sj (0.0, f / s.fn);
        if (s.firstOrder) return s.m0 + s.m2 / (1.0 + sj);
        return s.m0 + (s.m1 * sj + s.m2) / (sj * sj + s.k * sj + 1.0);
    }
    inline double analogMag2 (const Design& d, double f)
    {
        double m = 1.0;
        for (int i = 0; i < d.n; ++i) m *= std::norm (sectionAnalog (d.s[i], f));
        return m;
    }

    //------------------------------------------------------------------ passage en numérique
    // bilinéaire (Zero Latency)
    inline void toBilinear (Design& d, double fs)
    {
        for (int i = 0; i < d.n; ++i) d.s[i].g = prewarp (d.s[i].fn, fs);
    }

    // biquad numérique (a0 = 1) -> SVF équivalent exact
    inline void biquadToSvf (Section& s, double b0, double b1, double b2, double a1, double a2)
    {
        const double g = std::sqrt (std::max (1.0e-18, (1.0 + a1 + a2) / std::max (1.0e-18, 1.0 - a1 + a2)));
        const double D = 4.0 / std::max (1.0e-18, 1.0 - a1 + a2);
        const double k = (1.0 - a2) * D / (2.0 * g);
        const double X = D * (b0 + b2) * 0.5, Y = D * b1 * 0.5;
        const double N0 = (X + Y) * 0.5 / (g * g), N2 = (X - Y) * 0.5, N1 = D * (b0 - b2) * 0.5 / g;
        s.g = g; s.k = k; s.m0 = N2; s.m1 = N1 - N2 * k; s.m2 = N0 - N2;
    }

    //------------------------------------------------------------------ mode « Analog » : sections appariées au module analogique
    //  Plusieurs façons d'apparier une section ; pour chaque section on garde celle qui colle le mieux
    //  au prototype analogique (écart mesuré de 20 Hz à Nyquist). Écart typique < 0,1 dB, et < 0,8 dB pour
    //  une cloche ou un plateau extrême à 18 kHz (contre plusieurs dizaines de dB en bilinéaire).
    struct BQ { double b0, b1, b2, a1, a2; };

    inline void polesOf (double k, double w0, double& a1, double& a2)     // pôles analogiques transposés exactement
    {
        const double z = k * 0.5;
        a1 = z <= 1.0 ? -2.0 * std::exp (-z * w0) * std::cos (std::sqrt (1.0 - z * z) * w0)
                      : -2.0 * std::exp (-z * w0) * std::cosh (std::sqrt (z * z - 1.0) * w0);
        a2 = std::exp (-2.0 * z * w0);
    }
    inline double phi1 (double w) { const double v = std::sin (w * 0.5); return v * v; }

    // zéros : module apparié à 0 Hz, à fn et à Nyquist
    inline BQ match3 (const Section& s, double fs, double a1, double a2, double w0)
    {
        const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
        const double p1 = phi1 (w0), p0 = 1 - p1, p2 = 4 * p0 * p1;
        const double H0 = std::norm (s.m0 + s.m2), HN = std::norm (sectionAnalog (s, fs * 0.5)), HW = std::norm (sectionAnalog (s, w0 * fs / (2 * kPi)));
        const double B0 = H0 * A0, B1 = HN * A1, B2 = (HW * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / std::max (1e-30, p2);
        const double sB0 = std::sqrt (B0), sB1 = std::sqrt (B1), W = 0.5 * (sB0 + sB1), b1 = 0.5 * (sB0 - sB1);
        const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2))), b2 = W - b0;
        return { b0, b1, b2, a1, a2 };
    }
    // zéros : moindres carrés sur l'écart relatif (0 Hz exact)
    inline bool fitNum (const Section& s, double fs, double a1, double a2, double w0, BQ& out)
    {
        const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
        const double B0 = std::norm (s.m0 + s.m2) * A0;
        double S11 = 0, S12 = 0, S22 = 0, R1 = 0, R2 = 0;
        constexpr int M = 48;
        const double wlo = std::max (1e-4, w0 * 0.1);
        const double step = std::pow (kPi / wlo, 1.0 / (M - 1));
        // |H(jΩ)|² en réel (sans nombres complexes) : N = m0 s² + (m0 k + m1) s + m0 + m2
        const double n2 = s.m0, n1 = s.m0 * s.k + s.m1, n0 = s.m0 + s.m2, toOmega = fs / (2 * kPi * s.fn);
        double w = wlo;
        for (int i = 0; i < M; ++i, w *= step)
        {
            const double p1 = phi1 (w), p0 = 1 - p1, p2 = 4 * p0 * p1;
            const double om = w * toOmega, om2 = om * om;
            const double nr = n0 - n2 * om2, ni = n1 * om, dr = 1.0 - om2, di = s.k * om;
            const double T = (nr * nr + ni * ni) / (dr * dr + di * di) * (A0 * p0 + A1 * p1 + A2 * p2);
            if (T < 1e-20) continue;
            const double wt = 1.0 / (T * T), y = T - B0 * p0;
            S11 += p1 * p1 * wt; S12 += p1 * p2 * wt; S22 += p2 * p2 * wt; R1 += p1 * y * wt; R2 += p2 * y * wt;
        }
        const double det = S11 * S22 - S12 * S12;
        if (std::abs (det) < 1e-300) return false;
        const double B1 = (R1 * S22 - R2 * S12) / det, B2 = (S11 * R2 - S12 * R1) / det;
        if (B1 < 0) return false;
        const double sB0 = std::sqrt (B0), sB1 = std::sqrt (B1), W = 0.5 * (sB0 + sB1), b1 = 0.5 * (sB0 - sB1);
        if (W * W + B2 < 0) return false;
        const double b0 = 0.5 * (W + std::sqrt (W * W + B2));
        out = { b0, b1, W - b0, a1, a2 };
        return true;
    }
    // section inverse (pour une section qui creuse : on apparie l'inverse, dont les pôles sont bien placés)
    inline Section inverseAnalog (const Section& s)
    {
        const double n2 = s.m0, n1 = s.m0 * s.k + s.m1, n0 = s.m0 + s.m2;
        const double r = std::sqrt (n0 / n2);
        Section o; o.firstOrder = false; o.fn = s.fn * r; o.k = n1 / std::sqrt (n0 * n2);
        o.m0 = r * r / n0; o.m1 = s.k * r / n0 - o.m0 * o.k; o.m2 = 1.0 / n0 - o.m0;
        return o;
    }
    inline bool isCutSection (const Section& s)
    {
        const double n2 = s.m0, n0 = s.m0 + s.m2, n1 = s.m0 * s.k + s.m1;
        return ! s.firstOrder && n2 > 1e-9 && n0 > 1e-9 && n1 > 1e-12 && std::abs (sectionAnalog (s, s.fn)) < 0.999;
    }
    inline bool isHpSection (const Section& s)    { return ! s.firstOrder && std::abs (s.m0 - 1) < 1e-9 && std::abs (s.m2 + 1) < 1e-9 && std::abs (s.m1 + s.k) < 1e-9; }
    inline bool isLpSection (const Section& s)    { return ! s.firstOrder && s.m0 == 0.0 && s.m1 == 0.0; }
    inline bool isNotchSection (const Section& s) { return ! s.firstOrder && s.m0 > 0 && s.m0 + s.m2 > 0 && std::abs (s.m0 * s.k + s.m1) < 1e-12; }

    enum MatchKind { mkBilinear = 0, mkMatch3, mkInvMatch3, mkInvFit, mkFit, mkHpNyq, mkHpW0, mkNotch, mkLpV, mkNumKinds };

    inline bool kindApplies (const Section& s, int kind)
    {
        if (s.firstOrder) return kind == mkBilinear || kind == mkMatch3;
        switch (kind)
        {
            case mkInvMatch3: case mkInvFit: return isCutSection (s);
            case mkHpNyq: case mkHpW0:       return isHpSection (s);
            case mkNotch:                    return isNotchSection (s);
            case mkLpV:                      return isLpSection (s);
            default:                         return true;
        }
    }

    // une section numérique pour une façon d'apparier ; false si elle ne convient pas
    inline bool matchAs (Section& s, double fs, int kind)
    {
        if (kind == mkBilinear) { s.g = prewarp (s.fn, fs); return true; }
        if (s.firstOrder)
        {
            const double w0 = std::min (2.0 * kPi * s.fn / fs, 0.97 * kPi);
            const double a1 = -std::exp (-w0);
            const double H0 = std::abs (s.m0 + s.m2), HN = std::abs (sectionAnalog (s, fs * 0.5));
            const double sB0 = H0 * (1.0 + a1), sB1 = HN * (1.0 - a1);
            const double b0 = 0.5 * (sB0 + sB1), b1 = 0.5 * (sB0 - sB1);
            const double g = (1.0 + a1) / (1.0 - a1);
            const double N0 = (b0 + b1) * (1.0 + g) * 0.5 / g, N1 = (b0 - b1) * (1.0 + g) * 0.5;
            s.g = g; s.m0 = N1; s.m2 = N0 - N1;
            return true;
        }
        const bool inv = kind == mkInvMatch3 || kind == mkInvFit;
        const Section t = inv ? inverseAnalog (s) : s;
        const double w0 = std::min (2.0 * kPi * t.fn / fs, 0.97 * kPi);
        double a1, a2; polesOf (t.k, w0, a1, a2);
        BQ q;
        switch (kind)
        {
            case mkFit: case mkInvFit: if (! fitNum (t, fs, a1, a2, w0, q)) return false; break;
            case mkHpNyq: { const double c = std::abs (sectionAnalog (t, fs * 0.5)) * (1 - a1 + a2) / 4.0; q = { c, -2 * c, c, a1, a2 }; break; }
            case mkHpW0:
            {
                const double p1 = phi1 (w0), p0 = 1 - p1, p2 = 4 * p0 * p1;
                const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
                const double c = std::sqrt (std::norm (sectionAnalog (t, w0 * fs / (2 * kPi))) * (A0 * p0 + A1 * p1 + A2 * p2)) / (4 * p1);
                q = { c, -2 * c, c, a1, a2 }; break;
            }
            case mkNotch:
            {
                const double wz = std::min (2.0 * kPi * t.fn * std::sqrt ((t.m0 + t.m2) / t.m0) / fs, 0.999 * kPi);
                const double c = std::abs (t.m0 + t.m2) * (1 + a1 + a2) / std::max (1e-12, 2.0 - 2.0 * std::cos (wz));
                q = { c, -2.0 * std::cos (wz) * c, c, a1, a2 }; break;
            }
            case mkLpV:
            {
                const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
                const double p1 = phi1 (w0), p0 = 1 - p1, p2 = 4 * p0 * p1;
                const double B0 = A0 * std::norm (t.m0 + t.m2), R1 = (A0 * p0 + A1 * p1 + A2 * p2) * std::norm (sectionAnalog (t, w0 * fs / (2 * kPi)));
                const double B1 = std::max (0.0, (R1 - B0 * p0) / p1);
                const double b0 = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
                q = { b0, std::sqrt (B0) - b0, 0.0, a1, a2 }; break;
            }
            default: q = match3 (t, fs, a1, a2, w0); break;
        }
        if (inv)
        {
            if (std::abs (q.b0) < 1e-15) return false;
            q = { 1.0 / q.b0, q.a1 / q.b0, q.a2 / q.b0, q.b1 / q.b0, q.b2 / q.b0 };
            if (std::abs (q.a2) >= 0.9999 || std::abs (q.a1) >= 1.0 + q.a2) return false;   // zéros hors du cercle : inverse instable
        }
        biquadToSvf (s, q.b0, q.b1, q.b2, q.a1, q.a2);
        return std::isfinite (s.g) && std::isfinite (s.k) && std::isfinite (s.m0) && std::isfinite (s.m1) && std::isfinite (s.m2) && s.k > 0;
    }

    inline double sectionError (const Section& an, const Section& dg, double fs)
    {
        double e = 0.0;
        // 32 points répartis de 20 Hz à 20 kHz, plus 7 points serrés autour de la fréquence propre
        // (une résonance étroite, Q élevé, tomberait sinon entre deux points)
        static const double around[7] = { 0.85, 0.92, 0.97, 1.0, 1.03, 1.08, 1.17 };
        const double qn = an.firstOrder ? 1.0 : std::max (1.0, 1.0 / std::max (1e-3, an.k));
        for (int i = 0; i < 39; ++i)
        {
            const double f = i < 32 ? 20.0 * std::pow (1000.0, (double) i / 31.0)
                                    : an.fn * std::pow (around[i - 32], 1.0 / std::sqrt (qn));
            if (f >= fs * 0.5) { if (i < 32) i = 31; continue; }
            const double r = std::abs (sectionAnalog (an, f));
            if (r < 0.03) continue;
            const double w = prewarp (f, fs);
            const std::complex<double> sj (0.0, w / dg.g);
            const auto h = dg.firstOrder ? dg.m0 + dg.m2 / (1.0 + sj) : dg.m0 + (dg.m1 * sj + dg.m2) / (sj * sj + dg.k * sj + 1.0);
            e = std::max (e, std::abs (20.0 * std::log10 (std::abs (h) / r)));
        }
        return std::isfinite (e) ? e : 1.0e9;
    }

    // choisit la meilleure façon d'apparier chaque section ; kinds (facultatif) : façon imposée (>= 0) ou retour du choix
    inline void toMatched (Design& d, double fs, int* kinds = nullptr, bool reuse = false)
    {
        for (int i = 0; i < d.n; ++i)
        {
            const Section an = d.s[i];
            if (reuse && kinds != nullptr && kinds[i] >= 0)
            {
                Section t = an;
                if (matchAs (t, fs, kinds[i])) { d.s[i] = t; continue; }
            }
            Section best = an; best.g = prewarp (an.fn, fs);
            double be = sectionError (an, best, fs); int bk = mkBilinear;
            for (int k = 1; k < mkNumKinds; ++k)
            {
                if (! kindApplies (an, k)) continue;
                Section t = an;
                if (! matchAs (t, fs, k)) continue;
                const double e = sectionError (an, t, fs);
                if (e < be) { be = e; best = t; bk = k; }
            }
            d.s[i] = best;
            if (kinds != nullptr) kinds[i] = bk;
        }
    }

    // conception numérique complète selon le mode (en phase linéaire : sections appariées, pour l'affichage d'appoint)
    inline Design design (int type, double hz, double db, double q, int slopeIdx, double fs, int mode = zeroLatency, int* kinds = nullptr, bool reuse = false, int flavour = flavourClean)
    {
        if (mode == analog) q = flavourQ (flavour, type, q, db);
        auto d = designAnalog (type, hz, db, q, slopeIdx);
        if (mode == zeroLatency) toBilinear (d, fs); else toMatched (d, fs, kinds, reuse);
        return d;
    }

    // Réponse complexe d'une conception numérique à la fréquence hz (identique au moteur audio)
    inline std::complex<double> response (const Design& d, double hz, double fs)
    {
        const double w = prewarp (hz, fs);
        std::complex<double> h (1.0, 0.0);
        for (int i = 0; i < d.n; ++i)
        {
            const auto& s = d.s[i];
            const std::complex<double> sj (0.0, w / s.g);
            if (s.firstOrder) h *= s.m0 + s.m2 / (1.0 + sj);
            else              h *= s.m0 + (s.m1 * sj + s.m2) / (sj * sj + s.k * sj + 1.0);
        }
        return h;
    }

    inline double responseDb (const Design& d, double hz, double fs)
    {
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (response (d, hz, fs))));
    }
    inline double analogDb (const Design& d, double hz) { return 10.0 * std::log10 (std::max (1.0e-24, analogMag2 (d, hz))); }

    //------------------------------------------------------------------ état audio d'une section (par canal)
    struct SvfState
    {
        double ic1 = 0.0, ic2 = 0.0;
        void reset() { ic1 = ic2 = 0.0; }
    };

    struct SectionCoeffs
    {
        bool firstOrder = false;
        double a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0, G = 0, g = 0, k = 1;
        void from (const Section& s)
        {
            firstOrder = s.firstOrder; m0 = s.m0; m1 = s.m1; m2 = s.m2; g = s.g; k = s.k;
            if (firstOrder) { G = s.g / (1.0 + s.g); return; }
            a1 = 1.0 / (1.0 + s.g * (s.g + s.k)); a2 = s.g * a1; a3 = s.g * a2;
        }
        inline double tick (double x, SvfState& st) const noexcept
        {
            if (firstOrder)
            {
                const double v = (x - st.ic1) * G;
                const double lp = v + st.ic1;
                st.ic1 = lp + v;
                return m0 * x + m2 * lp;
            }
            const double v3 = x - st.ic2;
            const double v1 = a1 * st.ic1 + a2 * v3;
            const double v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
            st.ic1 = 2.0 * v1 - st.ic1;
            st.ic2 = 2.0 * v2 - st.ic2;
            return m0 * x + m1 * v1 + m2 * v2;
        }
    };
}
