# QMix — NINE HANDS | DSP

Égaliseur paramétrique VST3 (Windows et macOS) : 24 bandes, courbe interactive et analyseur de spectre en temps réel, trois modes de traitement (Zero Latency, Analog sans resserrement dans l'aigu avec le caractère des consoles SSL 4000 G ou Neve VR au choix, Linear Phase) et bandes dynamiques.

<p align="center">
  <a href="https://github.com/Nine-Hands-Dsp/Qmix/releases/latest/download/QMix-VST3-Windows.zip"><img alt="Télécharger pour Windows" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-Windows-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/Nine-Hands-Dsp/Qmix/releases/latest/download/QMix-VST3-macOS.zip"><img alt="Télécharger pour macOS" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-macOS-111111?style=for-the-badge&logo=apple&logoColor=white"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/Qmix/releases/latest/download/QMix-Notice.pdf"><img alt="Télécharger la notice (PDF)" src="https://img.shields.io/badge/%F0%9F%93%96%20Notice-PDF-919496?style=for-the-badge"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/Qmix/releases/latest"><img alt="Dernière version" src="https://img.shields.io/github/v/release/Nine-Hands-Dsp/Qmix?label=derni%C3%A8re%20version&color=4fc3f7"></a>
</p>

## Fonctions

- **24 bandes** : cloche, plateau bas, plateau haut, coupe-bas et coupe-haut de 6 à 96 dB/oct (résonance réglable), notch, passe-bande, tilt.
- **Placement stéréo par bande** : stéréo, gauche, droite, Mid ou Side.
- **Trois modes de traitement** :
  - *Zero Latency* : filtres classiques (transformée bilinéaire, comme ReaEQ), aucune latence ;
  - *Analog* : chaque section est appariée au filtre analogique d'origine ; les cloches et plateaux aigus gardent leur forme jusqu'à 20 kHz, aucune latence. Trois caractères : *Clean* (Q constant), *SSL 4000 G* (Q proportionnel de l'égaliseur « 292 Pink Knob », plateaux plus raides avec creux / bosse au point de bascule) et *Neve VR* (Q qui varie avec le gain sur toutes les bandes, plateaux compris, d'après le manuel de la VR Legend). En SSL G et Neve VR, le potard **COLOR** ajoute la couleur de la console (saturation légère, harmonique 2 côté SSL, grave saturé côté Neve), sans latence ;
  - *Linear Phase* : filtres RIF symétriques calculés sur la réponse analogique exacte, aucune rotation de phase ; latence de 42,7 / 85,3 / 170,7 ms à 48 kHz (qualité Low / Medium / High).
- **Bandes dynamiques** (cloche, plateaux, tilt) : seuil et plage réglables ; la bande bouge de 2 dB par dB au-dessus du seuil (genou doux), jusqu'à la plage choisie, en atténuation ou en accentuation.
- **Courbe interactive** : double-clic pour créer une bande, glisser pour la fréquence et le gain (Maj : réglage fin), molette pour le Q, double-clic sur un point pour couper la bande, clic droit pour son menu.
- **Analyseur** avant et après l'EQ (pente 4,5 dB/oct), échelle d'affichage ±6 / 12 / 24 / 30 dB, Gain Scale de 0 à 200 %.
- Presets d'usine, sauvegarde et chargement de vos presets (`Documents/Nine Hands DSP/QMix`).
- MIDI Learn sur tous les réglages (aucun CC par défaut), fenêtre redimensionnable de 60 à 150 %, réglage Glow.

## Installation

Les boutons ci-dessus téléchargent toujours la dernière version.

- **Windows** : décompresser et copier `QMix.vst3` dans `C:\Program Files\Common Files\VST3`.
- **macOS** : décompresser et copier `QMix.vst3` dans `~/Library/Audio/Plug-Ins/VST3` (le plugin est signé ad hoc ; au premier lancement, autoriser si besoin dans Réglages Système > Confidentialité et sécurité).

Dans Reaper, le plugin apparaît sous le nom **QMix (NINE HANDS | DSP)**.

## Compilation

JUCE 8.0.15 (récupéré par CMake) :

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`tools/render` compile l'interface sous Linux et exporte des captures, et vérifie au passage que la réponse mesurée du moteur audio est identique à la courbe affichée (mode Analog, phase linéaire, bandes dynamiques).

## Historique

| Version | Changements |
|---|---|
| 1.2.2 | Moteur revu : trajet en double précision, coefficients interpolés à chaque échantillon (aucun zipper), section par section ; jusqu'à 4 fois moins de CPU en Analog (bandes dynamiques, coupes raides) ; couleur COLOR sans perte d'aigu (l'anti-repliement retirait jusqu'à 11 dB à 20 kHz) ; contournement par le DAW compensé en latence ; phase linéaire : plus de passage non retardé au changement de mode, rendu hors temps réel sans blanc. |
| 1.2.1 | Nouveau nom : QMix (fichiers QMix.vst3 et QMix-VST3-…zip ; les projets existants retrouvent le plugin). |
| 1.2.0 | Couleur de console (potard COLOR) en Analog SSL G et Neve VR, sans latence (anti-repliement ADAA). |
| 1.1.0 | Mode Analog : caractères SSL 4000 G et Neve VR (Clean reste disponible), choix enregistré avec le projet et les presets. |
| 1.0.0 | Première version : 24 bandes, modes Zero Latency / Analog / Linear Phase, bandes dynamiques, analyseur, presets, MIDI Learn, anti-gel v2. |
