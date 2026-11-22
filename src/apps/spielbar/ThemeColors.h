/*
 * ThemeColors.h - colori derivati che si comportano uguale su tema chiaro e
 * scuro. Licenza: MIT.
 *
 * Perche' serve: tint_color()/B_LIGHTEN_* dosano la distanza dal BIANCO in
 * proporzione, non in valore assoluto. Vanno benissimo quando lo sfondo e'
 * quasi bianco (255 -> 217 con B_DARKEN_1_TINT), ma su uno sfondo quasi nero
 * B_LIGHTEN_1_TINT porta 0 -> 104: un salto di 104 livelli per una banda che
 * dovrebbe essere appena percettibile.
 *
 * Qui invece:
 *   Shade()      - passo piccolo in valore assoluto, uguale in entrambi i temi
 *   MutedText()  - mescola testo e sfondo fino a un rapporto di contrasto
 *                  PRECISO (WCAG), cosi' l'etichetta ha lo stesso "peso"
 *                  visivo sia su bianco sia su nero
 */

#ifndef THEME_COLORS_H
#define THEME_COLORS_H

#ifndef THEMECOLORS_HOST_TEST
#	include <GraphicsDefs.h>
#endif

namespace themecolors {


// Mescola due colori come mix_color(): amount 0 -> from, 255 -> to.
// (Stessa aritmetica intera di Haiku, cosi' i valori coincidono al bit.)
rgb_color Blend(rgb_color from, rgb_color to, uint8 amount);

// Scurisce (delta < 0) o schiarisce (delta > 0) di delta livelli esatti.
rgb_color Shade(rgb_color base, int delta);

// Luminanza relativa e rapporto di contrasto secondo WCAG 2.x.
float RelativeLuminance(rgb_color color);
float ContrastRatio(rgb_color a, rgb_color b);

// Testo secondario: parte da text e lo avvicina a bg quanto basta per
// arrivare a targetContrast (4.5 e' la soglia WCAG per il testo normale).
// Con target 4.6 il tema chiaro resta identico a mix_color(..., 110).
rgb_color MutedText(rgb_color text, rgb_color bg, float targetContrast = 4.6f);

// Testo disabilitato: mescola al 45% verso lo sfondo. Simmetrico, cioe'
// funziona sia con testo scuro su chiaro sia con testo chiaro su scuro
// (mentre B_DISABLED_LABEL_TINT scurisce soltanto, e il nero resta nero).
rgb_color DisabledText(rgb_color text, rgb_color bg);

// Sostituto drop-in di ContrastShade(): stessa segnatura, stessi call site.
// Tema chiaro: identico a prima (tint_color + B_DARKEN_*).
// Tema scuro:  +7% / +14% verso il bianco invece del 41% / 62% di
//              B_LIGHTEN_1_TINT / B_LIGHTEN_2_TINT.
rgb_color ContrastShade(rgb_color base, int level);

}	// namespace themecolors

#endif	// THEME_COLORS_H
