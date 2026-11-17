# QMIX (NINE HANDS | DSP) - notice PDF en français, illustrée avec le rendu réel du plugin (tools/render).
# Usage : python3 tools/manual.py <dossier des captures (sortie de tools/render)> <fichier PDF de sortie>
import os, sys, re, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether, CondPageBreak)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1]
OUT = sys.argv[2]
VERSION = re.search(r"project\(QMix VERSION ([0-9.]+)\)", open(os.path.join(ROOT, "CMakeLists.txt")).read()).group(1)
TMP = os.path.join(os.path.dirname(os.path.abspath(OUT)), "notice_tmp")
os.makedirs(TMP, exist_ok=True)

FONTS = os.path.join(ROOT, "fonts"); FONTS2 = os.path.join(ROOT, "tools", "fonts_notice")
for n, f, d in (("Pop", "Poppins-Regular", FONTS), ("PopL", "Poppins-Light", FONTS2), ("PopM", "Poppins-Medium", FONTS), ("PopB", "Poppins-Bold", FONTS),
                ("Title", "BarlowCondensed-ExtraBold", FONTS2)):
    pdfmetrics.registerFont(TTFont(n, os.path.join(d, f + ".ttf")))

ACC = colors.HexColor("#1E9BD0"); ACC_L = colors.HexColor("#4FC3F7")        # bleu d'accent de QMix (thème Graphite)
DARK = colors.HexColor("#1A1C22"); GREY = colors.HexColor("#646B76"); LIGHT = colors.HexColor("#EAF4FA")
LOGO_PNG = os.path.join(TMP, "nine_hands_logo_blanc.png")
subprocess.run(["rsvg-convert", "-w", "1200", os.path.join(ROOT, "logo", "NineHandsLogoWhite.svg"), "-o", LOGO_PNG], check=True)

def shot(name): return Image.open(os.path.join(SHOTS, name)).convert("RGB")
src = shot("qmix_overview.png")
k = src.width / 960.0                                  # repère de l'interface : 960 x 540 unités (taille 60 %)

# ---------- capture annotée ----------
marks = [(1, 352, 23), (2, 663, 23), (3, 906, 23), (4, 70, 80), (5, 690, 196), (6, 248, 425), (7, 258, 368), (8, 556, 368), (9, 703, 372),
         (10, 14, 509), (11, 214, 509), (12, 410, 509), (13, 790, 472), (14, 878, 472), (15, 708, 472)]
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(FONTS, "Poppins-Bold.ttf"), 22)
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 17
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(30, 155, 208), width=4)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(10, 80, 120), anchor="mm")
annotated = os.path.join(TMP, "annotated.png"); ann.save(annotated)

def crop_u(im, box): return im.crop(tuple(int(v * k) for v in box))
panel_dyn = os.path.join(TMP, "panel_dyn.png"); crop_u(shot("qmix_dyn.png"), (160, 362, 800, 460)).save(panel_dyn)
curve_dyn = os.path.join(TMP, "curve_dyn.png"); crop_u(shot("qmix_dyn.png"), (560, 150, 940, 380)).save(curve_dyn)
modes = os.path.join(TMP, "modes.png"); crop_u(shot("qmix_linear.png"), (660, 6, 950, 40)).save(modes)
bars = os.path.join(TMP, "bars.png")
a = crop_u(src, (350, 6, 610, 40)); b = crop_u(shot("qmix_status.png"), (350, 6, 610, 40))
st_img = Image.new("RGB", (a.width, a.height * 2 + 12), (255, 255, 255)); st_img.paste(a, (0, 0)); st_img.paste(b, (0, a.height + 12)); st_img.save(bars)
linear = os.path.join(TMP, "linear.png"); shot("qmix_linear.png").save(linear)
flav = os.path.join(TMP, "flavours.png")
_crops = [crop_u(shot("qmix_flavour%d.png" % i), (10, 120, 950, 400)) for i in range(3)]
_lbl = ImageFont.truetype(os.path.join(FONTS, "Poppins-Bold.ttf"), 30)
_fs = Image.new("RGB", (_crops[0].width, (_crops[0].height + 8) * 3), (255, 255, 255))
for _i, (_c, _t) in enumerate(zip(_crops, ["ANALOG (Clean)", "SSL G", "NEVE VR"])):
    _fs.paste(_c, (0, _i * (_c.height + 8)))
    ImageDraw.Draw(_fs).text((24, _i * (_c.height + 8) + 18), _t, font=_lbl, fill=(79, 195, 247))
_fs.save(flav)

# ---------- couverture : fenêtre détourée (coins arrondis) sur fond noir, reflet au sol, halo bleu très doux ----------
cutw, cuth = src.size
rmask = Image.new("L", src.size, 0); ImageDraw.Draw(rmask).rounded_rectangle((0, 0, cutw - 1, cuth - 1), radius=int(16 * k), fill=255)
cut = Image.new("RGBA", src.size, (0, 0, 0, 0)); cut.paste(src, (0, 0), rmask)
cw, ch = cut.size
pad = int(cw * 0.10)
canvas = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.30) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.45
glow = np.exp(-(((xx - gcx) / (cw * 0.52)) ** 2 + ((yy - gcy) / (ch * 0.48)) ** 2)) * 34
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
halo = np.dstack([glow * 0.30, glow * 0.76, glow * 0.97, np.full_like(glow, 255)]).astype(np.uint8)
canvas = Image.alpha_composite(canvas, Image.fromarray(halo, "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM)
ra = np.array(refl).astype(np.float64)
fade = np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22
ra[..., 3] *= fade[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
canvas = canvas.crop((0, 0, canvas.width, pad + int(ch * 1.20)))
cover = os.path.join(TMP, "cover_cutout.png"); canvas.convert("RGB").save(cover)
COVER_PAD, COVER_CW, COVER_CH = pad, cw, ch

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

# ---------- styles ----------
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY, spaceAfter=6),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items): return [P("&bull;&nbsp;&nbsp;" + i) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#D5DDE5")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

W = A4[0] - 36 * mm
f = [NextPageTemplate("p"), Spacer(1, 1), PageBreak()]

# ---------- 1. Présentation ----------
f.append(P("1. Présentation", "h1"))
f += [P("<b>QMix</b> est un égaliseur paramétrique de mixage : jusqu'à <b>24 bandes</b> que l'on règle directement sur la courbe, un analyseur de spectre en temps réel, des <b>bandes dynamiques</b> et <b>trois modes de traitement</b> au choix selon ce que l'on attend de la phase et de la latence."),
      P("Le moteur a été écrit entièrement pour QMix (il ne reprend pas le code de ReaEQ). Chaque bande est d'abord décrite comme un vrai filtre analogique, puis transposée en numérique selon le mode choisi. La courbe affichée n'est pas une approximation : elle est calculée avec exactement les mêmes filtres que le son, et l'écart mesuré entre les deux est de l'ordre du millième de dB.")]
f.append(P("Ce que QMix sait faire", "h2"))
f += bullets(["24 bandes : cloche, plateau bas, plateau haut, coupe-bas, coupe-haut (6 à 96 dB/oct, résonance réglable), notch, passe-bande, tilt.",
              "Chaque bande agit en stéréo, sur la gauche, la droite, le Mid ou le Side.",
              "Modes <b>Zero Latency</b>, <b>Analog</b> (avec le caractère des consoles <b>SSL 4000 G</b> ou <b>Neve VR</b> au choix) et <b>Linear Phase</b> (section 6).",
              "Bandes dynamiques : la bande s'enfonce ou se relève toute seule quand le signal dépasse un seuil (section 7).",
              "Analyseur avant et après l'égaliseur, échelle d'affichage réglable, Gain Scale, sortie, presets, MIDI Learn sur tous les réglages."])

# ---------- 2. Installation ----------
f.append(P("2. Installation", "h1"))
f.append(P("Windows", "h2"))
f.append(P("Téléchargez <b>QMix-VST3-Windows.zip</b>, décompressez-le et copiez le dossier <b>QMix.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3</b>."))
f.append(P("macOS", "h2"))
f.append(P("Téléchargez <b>QMix-VST3-macOS.zip</b>, décompressez-le et copiez <b>QMix.vst3</b> dans <b>~/Library/Audio/Plug-Ins/VST3</b> (binaire universel Apple Silicon et Intel). Le plugin est signé ad hoc : au premier lancement, autorisez-le si besoin dans Réglages Système &gt; Confidentialité et sécurité."))
f.append(P("Dans Reaper", "h2"))
f.append(P("Options &gt; Preferences &gt; Plug-ins &gt; VST : <b>Re-scan</b>. QMix apparaît sous le nom <b>QMix (NINE HANDS | DSP)</b>. Il accepte une piste mono ou stéréo."))
f.append(PageBreak())

# ---------- 3. Vue d'ensemble ----------
f.append(P("3. Vue d'ensemble", "h1"))
f.append(img(annotated, W)); f.append(Spacer(1, 3 * mm))
ov = [["N°", "Élément", "Rôle"],
      ["1", "Barre de presets", "Flèches : preset précédent / suivant ; clic sur le nom : liste des presets. Devient la ligne d'état pendant un réglage."],
      ["2", "Mode de traitement", "ZERO LAT, ANALOG (ou SSL G / NEVE VR), LINEAR (section 6). Nouveau clic ou clic droit sur ANALOG : caractère de console ; sur LINEAR : qualité de la phase linéaire."],
      ["3", "Power", "Allume ou contourne tout l'égaliseur, en fondu, sans clic."],
      ["4", "Écran", "Grille fréquences / gains, analyseur, courbe de chaque bande et courbe totale (en blanc)."],
      ["5", "Point de bande", "Numéro et couleur de la bande ; se déplace à la souris (section 4)."],
      ["6", "Forme, pente, placement, DYN", "Puces du panneau de la bande sélectionnée (section 5)."],
      ["7", "Panneau de bande", "Apparaît quand une bande est sélectionnée."],
      ["8", "FREQ, GAIN, Q", "Potards de la bande ; THRESHOLD et RANGE en plus quand la bande est dynamique."],
      ["9", "Marche / suppression", "Coupe la bande (elle garde ses réglages) ou la supprime."],
      ["10", "ANALYZER", "OFF, POST (après l'égaliseur) ou PRE+POST (avant et après)."],
      ["11", "RANGE", "Échelle de l'écran : ±6, ±12, ±24 ou ±30 dB. Ne change pas le son."],
      ["12", "Bandes et latence", "Nombre de bandes utilisées sur 24, latence du mode en cours."],
      ["13", "GAIN SCALE", "Multiplie le gain (et la plage dynamique) de toutes les bandes : 0 à 200 %."],
      ["14", "OUTPUT", "Volume de sortie, −36 à +36 dB."],
      ["15", "COLOR", "Couleur de console SSL G ou Neve VR, 0 à 100 % (actif seulement en Analog SSL G / NEVE VR)."]]
f.append(table(ov, [9 * mm, 40 * mm, W - 49 * mm]))
f.append(PageBreak())

# ---------- 4. Bandes ----------
f.append(P("4. Créer et régler les bandes", "h1"))
f += bullets(["<b>Double-clic</b> dans une zone vide de l'écran : crée une bande à cet endroit (fréquence et gain sous la souris). C'est une cloche ; tout à gauche (sous 30 Hz) un coupe-bas, tout à droite (au-dessus de 16 kHz) un coupe-haut.",
              "<b>Glisser un point</b> : fréquence (horizontal) et gain (vertical). Avec <b>Maj</b> : réglage fin. Pour les coupe-bas, coupe-haut, notch et passe-bande, seul le déplacement horizontal compte.",
              "<b>Molette</b> sur un point (ou n'importe où quand une bande est sélectionnée) : Q. Avec <b>Maj</b> : réglage fin.",
              "<b>Double-clic sur un point</b> : coupe ou rallume la bande. Une bande coupée apparaît en cercle creux.",
              "<b>Clic droit sur un point</b> : menu de la bande (forme, pente, placement stéréo, Enabled, Dynamic, MIDI, Delete band).",
              "Au survol d'un point, une bulle donne fréquence, gain, Q (et le gain dynamique en cours). Un clic dans le vide désélectionne la bande."])
f.append(P("Formes", "h2"))
f.append(table([["Forme", "Réglages", "Remarques"],
                ["Bell (cloche)", "FREQ, GAIN, Q", "Accentue ou creuse autour de la fréquence."],
                ["Low Shelf / High Shelf", "FREQ, GAIN, Q", "Plateau sous ou au-dessus de la fréquence ; Q règle la pente du passage (un Q élevé crée un léger rebond)."],
                ["Low Cut / High Cut", "FREQ, Q, pente", "6, 12, 18, 24, 30, 36, 48, 72 ou 96 dB/oct. Q = 0,71 : Butterworth (sans bosse) ; au-dessus, résonance à la coupure. À 6 dB/oct, Q est sans effet."],
                ["Notch", "FREQ, Q", "Supprime une fréquence précise (ronflement, sifflement). Q élevé = encoche étroite."],
                ["Band Pass", "FREQ, Q", "Ne garde que la bande autour de la fréquence (effet téléphone, écoute d'une zone)."],
                ["Tilt Shelf", "FREQ, GAIN, Q", "Bascule tout le spectre autour de la fréquence : la moitié du gain en plus dans l'aigu, la moitié en moins dans le grave."]],
               [36 * mm, 28 * mm, W - 64 * mm]))
f.append(P("Placement stéréo", "h2"))
f.append(P("<b>Stereo</b> : les deux canaux. <b>Left</b> / <b>Right</b> : un seul canal. <b>Mid</b> : ce qui est commun aux deux canaux (le centre). <b>Side</b> : la différence (les côtés). Par exemple, un coupe-bas sur le Side à 120 Hz resserre le grave au centre sans toucher au reste. Sur une piste mono, toutes les bandes sont en Stereo."))

# ---------- 5. Panneau ----------
f.append(P("5. Panneau de la bande", "h1"))
f.append(P("Le panneau apparaît en bas de l'écran pour la bande sélectionnée. À gauche : numéro et couleur de la bande, puis les puces <b>forme</b> (clic : liste des formes), <b>pente</b> (coupe-bas et coupe-haut seulement), <b>placement stéréo</b> et <b>DYN</b> (rend la bande dynamique ; cloche, plateaux et tilt seulement). À droite : marche / arrêt de la bande et la croix pour la supprimer."))
f += bullets(["<b>FREQ</b> : 10 Hz à 30 kHz. <b>GAIN</b> : −30 à +30 dB. <b>Q</b> : 0,025 à 40.",
              "Potards : glisser verticalement (Maj : réglage fin), molette, <b>double-clic</b> : valeur par défaut, <b>clic droit</b> : MIDI Learn (section 9). L'anneau coloré montre la valeur ; celui du gain part du centre."])
f.append(KeepTogether([Spacer(1, 2 * mm), img(panel_dyn, W * 0.82), P("Panneau d'une bande dynamique : FREQ, GAIN, Q, puis THRESHOLD et RANGE.", "small")]))

# ---------- 6. Modes ----------
f.append(CondPageBreak(70 * mm))
f.append(P("6. Modes de traitement", "h1"))
f.append(img(modes, 70 * mm)); f.append(Spacer(1, 2 * mm))
f.append(P("Zero Latency", "h2"))
f.append(P("Filtres numériques classiques (transformée bilinéaire, comme ReaEQ ou la plupart des égaliseurs « zéro latence »). Aucune latence, très léger pour le processeur. Contrepartie : près de la limite de l'audio numérique (la moitié de la fréquence d'échantillonnage), les cloches et plateaux aigus se resserrent. À 44,1 kHz, une cloche de +12 dB réglée à 15 kHz ne monte plus qu'à +3,8 dB à 17 kHz, au lieu des +9,2 dB du filtre analogique."))
f.append(P("Analog (mode par défaut)", "h2"))
f.append(P("Aucune latence, mais chaque section de filtre est <b>appariée au filtre analogique d'origine</b> : ses pôles sont transposés exactement, et ses zéros sont calculés pour retrouver la réponse analogique jusqu'à 20 kHz. Pour chaque section, QMix compare plusieurs façons de faire et garde celle qui colle le mieux. Résultat : la même cloche à 15 kHz monte à +8,8 dB à 17 kHz (+9,2 dB en analogique). Sur les réglages courants, l'écart avec l'analogique est inférieur à 0,1 dB ; il reste sous 2 dB dans les cas extrêmes au-dessus de 15 kHz (pente de 96 dB/oct, cloche très étroite). La phase est celle d'un égaliseur analogique."))
f.append(P("Caractère du mode Analog : Clean, SSL G, Neve VR", "h2"))
f.append(P("En mode Analog, un nouveau clic sur le bouton (ou un clic droit, ou clic droit &gt; <b>Analog character</b>) choisit la façon dont les bandes réagissent au gain. Le nom du bouton suit le choix : <b>ANALOG</b>, <b>SSL G</b> ou <b>NEVE VR</b>. Le choix est enregistré avec le projet et avec les presets."))
f.append(table([["Caractère", "Cloches", "Plateaux"],
                ["Clean", "Q constant : la largeur ne change pas avec le gain.", "Plateau du 2e ordre tel que réglé."],
                ["SSL 4000 G", "Égaliseur « 292 Pink Knob » de la SSL 4000 G : Q proportionnel. Les petites corrections sont larges, la courbe se resserre en poussant ; le Q affiché est atteint vers ±18 dB (où, d'après les utilisateurs, les courbes E et G se rejoignent). Avec Q = 1 : 2,4 octaves à 3 dB, 1,8 à 9 dB, 1,4 à 18 dB.", "Plus raides, avec le léger creux (undershoot) avant une accentuation et la bosse (overshoot) avant une coupe, typiques de la G : environ −1,3 dB pour un plateau bas de +12 dB."],
                ["Neve VR", "Console Neve VR Legend : le manuel indique que « le Q varie automatiquement avec le gain sur toutes les bandes, en cloche comme en plateau » (plus de gain, plus de Q). Loi plus douce que la SSL : 2,1 octaves à 3 dB, 1,7 à 9 dB, 1,4 à 18 dB (Q = 1).", "S'élargissent aussi aux petits gains : plateaux très progressifs et doux, qui retrouvent le Q réglé vers ±18 dB. Q = 2 donne le plateau résonant du bouton HI-Q de la console."]],
               [24 * mm, (W - 24 * mm) * 0.58, (W - 24 * mm) * 0.42]))
f.append(P("Ces caractères reproduisent la façon dont les deux consoles réagissent (largeur selon le gain, forme des plateaux), d'après les documentations des constructeurs et des émulations reconnues ; ce ne sont pas des mesures de consoles réelles. Ils changent les courbes ; la saturation est réglée à part avec COLOR (ci-dessous). Coupe-bas, coupe-haut, notch, passe-bande et tilt ne changent pas. Les plages de fréquences et de gain restent celles de QMix (les consoles vont de ±18 à ±20 dB environ, de 30 Hz à 17 kHz).", "small"))
f.append(KeepTogether([img(flav, W * 0.92), P("Même réglage (plateau bas +8 dB à 90 Hz, −4 dB à 400 Hz, +4 dB à 3 kHz) en Clean, SSL G et Neve VR.", "small")]))
f.append(P("Couleur de console (COLOR)", "h2"))
f.append(P("En <b>SSL G</b> et <b>NEVE VR</b>, le potard <b>COLOR</b> (en bas de la fenêtre) ajoute la saturation légère du trajet audio de la console, après l'égalisation. 0 % : aucune couleur. 50 % (par défaut) : couleur discrète, à peu près celle d'une console à son niveau nominal. 100 % : console poussée. La couleur dépend du niveau : plus le signal est fort, plus elle s'entend. Le potard est grisé en Clean, Zero Latency et Linear Phase, où il n'agit pas."))
f.append(table([["Console", "Ce qui donne la couleur", "Ce que fait QMix"],
                ["SSL 4000 G", "Une chaîne d'amplificateurs opérationnels NE5532 / 5534 et le VCA de sortie ; la distorsion d'un VCA se limite surtout aux harmoniques 2 et 3, presque uniquement la 2.", "Saturation douce asymétrique : harmonique 2 dominante, harmonique 3 quand on pousse ; identique sur tout le spectre. À 50 % et −18 dBFS : H2 ≈ −60 dB ; à 100 % et −6 dBFS : H2 ≈ −28 dB, H3 ≈ −40 dB."],
                ["Neve VR", "Entrée et sorties à transformateur, nombreux condensateurs de liaison ; la couleur Neve est attribuée aux transformateurs, qui saturent davantage dans le grave.", "Saturation plus ronde et presque symétrique, renforcée sous 150 Hz : dans le grave, l'harmonique 3 domine. À 50 % et −18 dBFS : H2 ≈ −66 dB à 1 kHz ; à 60 Hz et −12 dBFS : H3 ≈ −56 dB."]],
               [24 * mm, (W - 24 * mm) * 0.48, (W - 24 * mm) * 0.52]))
f.append(P("Interprétation de QMix à partir des descriptions des circuits, pas une mesure de console réelle. Aucune latence ajoutée : l'anti-repliement est fait sans suréchantillonnage (méthode ADAA). Pas de souffle ajouté. Comme toute saturation, la couleur rend la réponse dépendante du niveau : pour une correction strictement linéaire, mettez COLOR à 0 % ou choisissez Clean.", "small"))
f.append(P("Linear Phase", "h2"))
f.append(P("L'égaliseur devient un filtre à réponse impulsionnelle finie symétrique, calculé à partir de la réponse analogique exacte de toutes les bandes : <b>aucune rotation de phase</b>, quel que soit le réglage. Utile sur un bus, un mix ou des pistes enregistrées à plusieurs micros, quand on veut corriger sans déplacer les transitoires. Les bandes Mid / Side sont prises en compte exactement (matrice de quatre filtres)."))
f += bullets(["Il y a une <b>latence</b>, annoncée au DAW qui la compense : <b>Low</b> 2 048 échantillons (42,7 ms à 48 kHz), <b>Medium</b> 4 096 (85,3 ms, par défaut), <b>High</b> 8 192 (170,7 ms). Plus la qualité est haute, plus les coupe-bas raides et les cloches très étroites dans le grave sont précis.",
              "Choix de la qualité : clic droit sur <b>LINEAR</b>, nouveau clic sur LINEAR quand le mode est actif, ou clic droit &gt; <b>Linear phase quality</b>.",
              "Comme tout égaliseur à phase linéaire, un fort creux ou une forte accentuation dans le grave peut produire un léger pré-écho sur les attaques : en cas de doute, comparez avec le mode Analog.",
              "Les <b>bandes dynamiques</b> restent traitées en mode Analog (phase analogique), juste après le filtre à phase linéaire.",
              "Changer de mode fait un court fondu d'entrée (40 ms)."])

# ---------- 7. (capture) ----------
f.append(KeepTogether([Spacer(1, 2 * mm), img(linear, W * 0.9), P("Mode Linear Phase : la latence s'affiche en bas de la fenêtre.", "small")]))

# ---------- 8. Dynamique ----------
f.append(P("7. Bandes dynamiques", "h1"))
f.append(P("Une cloche, un plateau ou un tilt peut devenir <b>dynamique</b> (puce <b>DYN</b> du panneau, ou clic droit sur le point &gt; Dynamic). La bande écoute alors le signal dans sa propre zone de fréquences et ajoute un gain qui dépend de ce niveau :"))
f += bullets(["<b>THRESHOLD</b> (−60 à 0 dB) : seuil de déclenchement, mesuré dans la bande.",
              "<b>RANGE</b> (−30 à +30 dB) : jusqu'où la bande peut aller. Négatif : elle se creuse quand ça dépasse (dé-essage, résonance qui sort, grave qui gonfle). Positif : elle s'accentue quand ça dépasse.",
              "Au-dessus du seuil, la bande bouge de 1 dB pour 2 dB de dépassement (genou doux de 6 dB), jusqu'à RANGE. Temps d'attaque 4 ms, de retour 120 ms.",
              "Le <b>GAIN</b> statique de la bande s'ajoute : une cloche à 0 dB avec RANGE −6 dB ne fait rien tant que le signal reste sous le seuil.",
              "À l'écran : la limite de la plage en pointillés, un trait vertical sous le point jusqu'à la limite, et un anneau autour du point qui se remplit avec le gain dynamique en cours. La courbe totale suit le gain en direct."])
f.append(KeepTogether([Spacer(1, 2 * mm), img(curve_dyn, W * 0.6), P("Bande 5 dynamique à 6,2 kHz : seuil −42 dB, RANGE −7 dB ; elle se creuse ici de 7 dB.", "small")]))

# ---------- 9. Analyseur / affichage ----------
f.append(P("8. Analyseur, échelle et sortie", "h1"))
f += bullets(["<b>ANALYZER</b> : <b>POST</b> montre le signal après l'égaliseur (surface bleue) ; <b>PRE+POST</b> ajoute le signal d'entrée (trait gris). Analyse sur 8 192 points, pente de 4,5 dB/oct pour qu'un mix équilibré paraisse à peu près plat. Également dans clic droit &gt; Analyzer.",
              "<b>RANGE</b> (bas de la fenêtre) et clic droit &gt; <b>Display range</b> : échelle verticale de la courbe. Ne change pas le son.",
              "<b>GAIN SCALE</b> : 100 % = réglages tels quels ; 50 % divise par deux tous les gains et plages dynamiques, 200 % les double, 0 % met l'égaliseur à plat (les coupe-bas et coupe-haut restent).",
              "<b>OUTPUT</b> : −36 à +36 dB, appliqué après l'égalisation. <b>Power</b> (en haut à droite) : contournement en fondu ; en Linear Phase, le son contourné est retardé de la même latence pour rester aligné."])

# ---------- 10. MIDI ----------
f.append(P("9. MIDI et automation", "h1"))
f += bullets(["Tous les réglages sont automatisables dans le DAW (Band n Frequency, Band n Gain, Band n Q, Band n Dynamic Range…, Output, Gain Scale, Power).",
              "<b>MIDI Learn</b> : clic droit sur un potard &gt; <b>MIDI Learn</b>, puis bougez la pédale d'expression ou le bouton MIDI. On peut aussi passer par le menu d'une bande &gt; MIDI (Frequency, Gain, Q, et Threshold / Dynamic range pour une bande dynamique). La ligne d'état affiche MIDI LEARN puis le CC appris. <b>Clear MIDI</b> retire l'assignation ; clic droit &gt; MIDI &gt; <b>Clear all MIDI</b> les retire toutes.",
              "Aucun CC n'est assigné par défaut. Un CC déjà utilisé passe au nouveau réglage. Les assignations sont sauvegardées avec le projet.",
              "La course réelle de la pédale d'expression est apprise : une pédale qui n'envoie que 2 à 116 atteint quand même 0 et 100 %. Les réglages audio sont lissés (pas de saccades)."])

# ---------- 11. Fenêtre ----------
f.append(P("10. Fenêtre et ligne d'état", "h1"))
wt = Table([[[P("La fenêtre s'ouvre à la taille par défaut (<b>60 %</b>) et ne peut pas être réduite en dessous. Clic droit &gt; <b>Plugin size</b> : 60 %, 75 % ou 100 % (taille enregistrée avec le projet) ; le coin en bas à droite agrandit jusqu'à 150 % en gardant les proportions. Clic droit &gt; <b>Glow intensity</b> : 25, 50, 75 ou 100 % pour l'intensité des halos (courbe, points, potards, Power)."),
              P("La <b>barre de presets</b> et la <b>ligne d'état</b> partagent la même pastille. Dès qu'un réglage bouge (souris, molette, automation, MIDI), la ligne d'état prend sa place avec les valeurs de la bande en direct ; elle s'efface 2,5 s après le dernier mouvement. Un clic sur la pastille fait revenir la barre de presets tout de suite.")],
             [img(bars, 66 * mm), P("Au repos (barre de presets) et pendant un réglage (ligne d'état).", "small")]]], colWidths=[W - 70 * mm, 70 * mm])
wt.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
f.append(wt)

# ---------- 12. Presets ----------
f.append(P("11. Presets", "h1"))
f.append(P("Flèches de la barre de presets, clic sur son nom, clic droit &gt; Presets, ou menu de presets du DAW. Un astérisque après le nom signale un preset modifié. <b>Save preset...</b> et <b>Load preset...</b> enregistrent et rechargent vos réglages (fichiers .qmix, dossier Documents/Nine Hands DSP/QMix par défaut) ; le mode de traitement est enregistré avec le preset."))
f.append(table([["Preset", "Contenu"],
                ["Flat", "Aucune bande."],
                ["Vocal Presence", "Coupe-bas 90 Hz, −2,5 dB à 250 Hz, +2,5 dB à 3,2 kHz, plateau haut +2 dB à 11 kHz."],
                ["Vocal De-Ess", "Coupe-bas 80 Hz, cloche dynamique à 6,5 kHz (Q 3, seuil −30 dB, RANGE −8 dB), plateau haut +1,5 dB."],
                ["Guitar Cab Tamer", "Coupe-bas 80 Hz, −2 dB à 420 Hz, cloche dynamique à 3,8 kHz (seuil −28 dB, RANGE −5 dB), coupe-haut 9,5 kHz à 12 dB/oct."],
                ["Tight Bass", "Coupe-bas 35 Hz, +2 dB à 90 Hz, −3 dB à 280 Hz, +1,5 dB à 1,8 kHz."],
                ["Mud Cut", "−4 dB à 300 Hz, Q 1,6."],
                ["Air", "Plateau haut +3 dB à 12 kHz."],
                ["Telephone", "Coupe-bas 400 Hz, coupe-haut 3,4 kHz, +4 dB à 1,5 kHz."],
                ["Mid / Side Wide", "Côtés : coupe-bas 120 Hz et plateau haut +2,5 dB à 8 kHz ; centre : −1 dB à 3 kHz."],
                ["Warm Tilt", "Tilt −3 dB pivot 900 Hz (grave +1,5 dB, aigu −1,5 dB)."]],
               [32 * mm, W - 32 * mm]))

# ---------- 13. Conseils ----------
f.append(P("12. Conseils d'utilisation", "h1"))
f += bullets(["Pour une guitare, placez QMix <b>après</b> Ampire et Impulse pour sculpter le son d'ampli (preset Guitar Cab Tamer), ou <b>avant</b> l'ampli pour préparer le signal (coupe-bas, bosse dans les médiums).",
              "Avec une 7 cordes en Sol ou plus bas, réglez le coupe-bas autour de 40 à 60 Hz plutôt que 80 Hz, pour garder la fondamentale de la corde grave.",
              "Pour une résonance qui ne sort que sur certaines notes, préférez une cloche dynamique (RANGE négatif) à une cloche fixe : le reste du temps, le son n'est pas touché.",
              "Mode Analog pour le suivi et les prises (aucune latence), Linear Phase pour un bus ou un mix quand la phase compte.",
              "SSL G pour des retouches larges qui se resserrent quand on pousse (voix, bus, caisse claire) ; Neve VR pour des corrections très douces et des plateaux progressifs (grave et air d'un mix) ; Clean pour des corrections précises à Q constant (résonances)."])

# ---------- 14. Caractéristiques ----------
f.append(KeepTogether([P("13. Caractéristiques techniques", "h1"), table([
    ["Format", "VST3 (Windows 64 bits, macOS universel), mono ou stéréo"],
    ["Bandes", "24 ; fréquence 10 Hz à 30 kHz, gain ±30 dB, Q 0,025 à 40, pentes 6 à 96 dB/oct"],
    ["Modes", "Zero Latency (bilinéaire), Analog (sections appariées à l'analogique ; caractère Clean, SSL 4000 G ou Neve VR), Linear Phase (RIF symétrique, fenêtre de Hann)"],
    ["Latence", "0 en Zero Latency et Analog ; 2 048 / 4 096 / 8 192 échantillons en Linear Phase (Low / Medium / High)"],
    ["Couleur", "COLOR 0 à 100 % en Analog SSL G (harmonique 2 dominante) et Neve VR (grave saturé, harmonique 3) ; anti-repliement ADAA, sans latence"],
    ["Dynamique", "détecteur passe-bande sur la bande, attaque 4 ms, retour 120 ms, rapport 2:1, genou 6 dB, seuil −60 à 0 dB, plage ±30 dB"],
    ["Moteur", "filtres à variables d'état, tout le trajet audio en double précision ; coefficients interpolés échantillon par échantillon quand un réglage bouge (pas de bruit de zipper, même en automation rapide) ; contournement par le DAW compensé en latence"],
    ["Analyseur", "FFT 8 192 points, fenêtre Blackman-Harris, pente 4,5 dB/oct, avant / après"],
    ["MIDI", "MIDI Learn sur tous les potards, aucun CC par défaut"],
    ["Fenêtre", "60 / 75 / 100 %, jusqu'à 150 %, jamais sous 60 % ; anti-gel v2 sous Windows"]],
    [26 * mm, W - 26 * mm], header=False)]))

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "QMix (NINE HANDS | DSP) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

# ---- Couverture « minimale » (modèle 6) : gabarit fixe commun à toutes les notices ----
COVER_NAME = "QMIX"; COVER_SUB = "Égaliseur paramétrique"
PEDAL_H, PEDAL_MAXW, PEDAL_TOP = 82 * mm, 115 * mm, 84 * mm
LANG_TOP = 18 * mm
NAME_TOP, NAME_PT = 194 * mm, 30
SUB_TOP, SUB_PT = 208 * mm, 10
LOGO_W, LOGO_BOTTOM = 38 * mm, 22 * mm
BAND_H = 3 * mm
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, 0, A4[0], BAND_H, stroke=0, fill=1)
    def line(text, top, font, size, cs, col):
        w = pdfmetrics.stringWidth(text, font, size) + cs * (len(text) - 1)
        t = c.beginText((A4[0] - w) / 2, A4[1] - top - size * 0.8); t.setFont(font, size); t.setCharSpace(cs)
        t.setFillColor(col); t.textOut(text); c.drawText(t)
    line("FRANÇAIS", LANG_TOP, "PopM", 8.5, 0, ACC_L)
    ci = Image.open(cover); iw, ih = ci.size
    kk = min(PEDAL_H / COVER_CH, PEDAL_MAXW / COVER_CW)
    dw, dh = iw * kk, ih * kk
    y0 = A4[1] - (PEDAL_TOP + (PEDAL_H - COVER_CH * kk) / 2) + COVER_PAD * kk
    c.drawImage(cover, (A4[0] - dw) / 2, y0 - dh, dw, dh)
    line(COVER_NAME, NAME_TOP - NAME_PT * 0.02, "Title", NAME_PT, NAME_PT * 0.5, colors.white)
    line(COVER_SUB, SUB_TOP, "Pop", SUB_PT, 1.5, colors.HexColor("#BDC4B8"))
    lw, lh = Image.open(LOGO_PNG).size
    c.drawImage(LOGO_PNG, (A4[0] - LOGO_W) / 2, LOGO_BOTTOM, LOGO_W, LOGO_W * lh / lw, mask="auto")
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="QMIX - Notice v" + VERSION, author="NINE HANDS | DSP")
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
doc.build(f)
print(OUT)
