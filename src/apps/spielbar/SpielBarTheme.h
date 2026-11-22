/*
 * SpielBar v3 - colori derivati, uguali su tema chiaro e scuro
 *
 * ContrastShade() e' la scorciatoia globale usata dai widget; il
 * lavoro vero lo fanno le funzioni di themecolors (ThemeColors.h).
 */

#ifndef SPIELBAR_THEME_H
#define SPIELBAR_THEME_H

#include "ThemeColors.h"

using themecolors::Shade;
using themecolors::MutedText;
using themecolors::DisabledText;

// Banda/riga derivata dallo sfondo: +7%/+14% verso il bianco sul tema
// scuro, tint_color() sul tema chiaro. Vedi ThemeColors.cpp.
static inline rgb_color
ContrastShade(rgb_color base, int level)
{
	return themecolors::ContrastShade(base, level);
}

#endif	// SPIELBAR_THEME_H
