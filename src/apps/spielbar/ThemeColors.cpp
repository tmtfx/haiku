/*
 * ThemeColors.cpp - vedi ThemeColors.h. Licenza: MIT.
 */

#include "ThemeColors.h"

#include <math.h>

#ifndef THEMECOLORS_HOST_TEST
#	include <InterfaceDefs.h>
#endif


namespace themecolors {


rgb_color
Blend(rgb_color from, rgb_color to, uint8 amount)
{
	// Identico a mix_color() di Haiku (troncamento verso lo zero compreso).
	rgb_color result;
	result.red = (uint8)(((int16(to.red) - int16(from.red)) * amount)
		/ 255 + from.red);
	result.green = (uint8)(((int16(to.green) - int16(from.green)) * amount)
		/ 255 + from.green);
	result.blue = (uint8)(((int16(to.blue) - int16(from.blue)) * amount)
		/ 255 + from.blue);
	result.alpha = from.alpha;
	return result;
}


rgb_color
Shade(rgb_color base, int delta)
{
	rgb_color result = base;
	int32 channels[3] = { base.red, base.green, base.blue };

	for (int i = 0; i < 3; i++) {
		int32 value = channels[i] + delta;
		if (value < 0)
			value = 0;
		else if (value > 255)
			value = 255;
		channels[i] = value;
	}

	result.red = (uint8)channels[0];
	result.green = (uint8)channels[1];
	result.blue = (uint8)channels[2];
	return result;
}


static float
_Linearize(uint8 channel)
{
	float value = channel / 255.0f;
	if (value <= 0.03928f)
		return value / 12.92f;
	return powf((value + 0.055f) / 1.055f, 2.4f);
}


float
RelativeLuminance(rgb_color color)
{
	return 0.2126f * _Linearize(color.red)
		+ 0.7152f * _Linearize(color.green)
		+ 0.0722f * _Linearize(color.blue);
}


float
ContrastRatio(rgb_color a, rgb_color b)
{
	float aLuminance = RelativeLuminance(a);
	float bLuminance = RelativeLuminance(b);
	if (aLuminance < bLuminance) {
		float swap = aLuminance;
		aLuminance = bLuminance;
		bLuminance = swap;
	}
	return (aLuminance + 0.05f) / (bLuminance + 0.05f);
}


rgb_color
MutedText(rgb_color text, rgb_color bg, float targetContrast)
{
	if (ContrastRatio(text, bg) <= targetContrast)
		return text;

	// Mescolare verso lo sfondo riduce il contrasto in modo monotono:
	// cerchiamo il valore piu' alto (testo piu' tenue) che rispetta la
	// soglia. Il risultato e' lo stesso su tema chiaro e scuro.
	int32 low = 0;
	int32 high = 255;
	while (low < high) {
		int32 mid = (low + high + 1) / 2;
		if (ContrastRatio(Blend(text, bg, (uint8)mid), bg) >= targetContrast)
			low = mid;
		else
			high = mid - 1;
	}

	return Blend(text, bg, (uint8)low);
}


rgb_color
DisabledText(rgb_color text, rgb_color bg)
{
	return Blend(text, bg, 115);	// 115/255 ~ 45%
}


rgb_color
ContrastShade(rgb_color base, int level)
{
	if (base.IsLight()) {
		// Tema chiaro: invariato, cosi' l'aspetto non cambia di un pixel.
		return tint_color(base,
			level >= 2 ? B_DARKEN_2_TINT : B_DARKEN_1_TINT);
	}

	// Tema scuro: B_LIGHTEN_1_TINT mescola il 41% della distanza dal bianco,
	// B_LIGHTEN_2_TINT il 62%. Su uno sfondo nero sono +104 e +156 livelli.
	// Qui: +7% e +14%, cioe' +17 e +35 su nero, +15 e +30 su un grigio 39.
	// (Le due percentuali sono le stesse che Haiku usa in Appearance per i
	// colori derivati dei temi scuri.)
	float toward = (level >= 2) ? 0.14f : 0.07f;
	uint8 amount = (uint8)(toward * 255.0f + 0.5f);
	return Blend(base, (rgb_color){ 255, 255, 255, 255 }, amount);
}


}	// namespace themecolors
