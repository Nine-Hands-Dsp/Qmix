#pragma once
#include <cmath>
#include <algorithm>

//==============================================================================
//  QMix : couleur de console (mode Analog, caractères SSL 4000 G et Neve VR)
//
//  Interprétation de QMix, sans mesure de console réelle :
//   - SSL 4000 G : chaîne d'amplis opérationnels NE5532 / 5534 et VCA de sortie ; la distorsion d'un VCA
//     se limite surtout aux harmoniques 2 et 3, presque uniquement la 2 d'après les mesures publiées.
//     -> saturation douce asymétrique (H2 dominante, H3 en plus quand on pousse), identique à toutes les fréquences.
//   - Neve VR : entrée et sorties à transformateur, nombreux condensateurs de liaison ; la couleur
//     Neve est attribuée aux transformateurs, qui saturent davantage dans le grave.
//     -> saturation plus douce et plus symétrique, renforcée sous ~150 Hz (comme un noyau de transformateur).
//  Les deux étages sont sans latence : anti-repliement par « ADAA » (moyenne de la primitive entre deux
//  échantillons) au lieu d'un suréchantillonnage, pour garder le mode Analog à latence nulle.
//==============================================================================
namespace qmix
{
    inline double logCosh (double z)
    {
        const double a = std::abs (z);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }

    // tanh décalé : F(z) = tanh(z + b) - tanh(b) ; primitive AF(z) = logcosh(z + b) - z tanh(b)
    struct BiasedTanh
    {
        double b = 0.0, tb = 0.0, norm = 1.0;
        void set (double bias) { b = bias; tb = std::tanh (b); norm = 1.0 / (1.0 - tb * tb); }
        double f  (double z) const { return std::tanh (z + b) - tb; }
        double af (double z) const { return logCosh (z + b) - z * tb; }
    };

    // anti-repliement du 1er ordre (ADAA), appliqué seulement à la partie non linéaire :
    // F(z) = s z + r(z), s = F'(0). La partie linéaire passe telle quelle (pas de moyenne entre deux
    // échantillons, donc ni perte d'aigu ni retard d'un demi-échantillon) ; seul le résidu r(z), qui
    // porte les harmoniques, est moyenné par sa primitive.
    struct Adaa
    {
        double x1 = 0.0;
        void reset() { x1 = 0.0; }
        double process (const BiasedTanh& s, double x)
        {
            const double sl = 1.0 / s.norm;
            auto r  = [&] (double z) { return s.f (z) - sl * z; };
            auto ar = [&] (double z) { return s.af (z) - 0.5 * sl * z * z; };
            const double d = x - x1;
            const double res = std::abs (d) > 1.0e-5 ? (ar (x) - ar (x1)) / d : r (0.5 * (x + x1));
            x1 = x;
            return sl * x + res;
        }
    };

    struct ConsoleColour
    {
        enum Kind { none = 0, ssl, neve };

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            lpA = 1.0 - std::exp (-2.0 * 3.14159265358979 * 150.0 / fs);     // grave du transformateur (Neve)
            dcA = 1.0 - std::exp (-2.0 * 3.14159265358979 * 4.0 / fs);       // blocage du continu (asymétrie)
            reset();
        }
        void reset()
        {
            for (int c = 0; c < 2; ++c) { main[c].reset(); low[c].reset(); lp[c] = 0.0; dc[c] = 0.0; }
            mixC[0] = mixC[1] = 0.0;
        }

        // kind : caractère ; amount : 0..1 (COLOR) ; ch : 0 / 1
        void setup (int kindIn, double amount)
        {
            kind = kindIn;
            amt = std::clamp (amount, 0.0, 1.0);
            if (kind == ssl)
            {
                shaper.set (0.22);                       // asymétrie : harmonique 2 dominante
                k = 0.075 * std::pow (10.0, (amt - 0.5) * 2.0);   // 50 % : H2 ≈ −60 dB à −18 dBFS (niveau nominal)
                kLow = 0.0;
            }
            else if (kind == neve)
            {
                shaper.set (0.07);                       // presque symétrique : harmoniques impaires douces
                k = 0.11 * std::pow (10.0, (amt - 0.5) * 2.0);    // 50 % : H2 / H3 ≈ −65 dB à 1 kHz, −18 dBFS
                kLow = 0.6 * std::pow (10.0, (amt - 0.5) * 1.0);  // saturation supplémentaire dans le grave (transformateur)
                lowShaper.set (0.0);
            }
        }

        // un échantillon ; target = 1 si la couleur est active (fondu de 20 ms sinon)
        inline double process (double x, int ch, double target)
        {
            double& mix = mixC[ch];
            mix += (target - mix) * mixA;
            if (mix < 1.0e-6 && target == 0.0) { main[ch].x1 = x * k; low[ch].x1 = lp[ch] * kLow; return x; }
            double in = x;
            if (kind == neve && kLow > 0.0)
            {
                lp[ch] += (in - lp[ch]) * lpA;
                const double l = lp[ch];
                const double ls = low[ch].process (lowShaper, kLow * l) / kLow;
                in = in - l + ls;
            }
            double y = main[ch].process (shaper, k * in) * shaper.norm / k;
            dc[ch] += (y - dc[ch]) * dcA;               // l'asymétrie crée un léger continu : retiré
            y -= dc[ch];
            return x + mix * (y - x);
        }

        double fs = 48000.0, lpA = 0.02, dcA = 0.0005, mixA = 0.001, mixC[2] { 0.0, 0.0 };
        double mixLevel() const { return std::max (mixC[0], mixC[1]); }
        int kind = none;
        double amt = 0.5, k = 1.0, kLow = 0.0;
        BiasedTanh shaper, lowShaper;
        Adaa main[2], low[2];
        double lp[2] {}, dc[2] {};
        void setMixTime (double seconds) { mixA = 1.0 - std::exp (-1.0 / (seconds * fs)); }
    };
}
