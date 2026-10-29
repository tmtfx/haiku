/*
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 * Copyright (C) 1998, 1999 by Alan Hourihane, Wigan, England
 * Copyright (C) 2004-2009 eXtreme Graphics Innovation Corp, Inc.
 * Copyright (C) 1999-2004 The XFree86 Project, Inc.
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * VCLK (pixel clock PLL) divider computation for XGI Volari.
 *
 * Porting of compute_vclk() and XGICalcClock() from
 * refcode/xgi_dac.c:66-293 (XFree86/X.Org xgi driver, MIT license). Both
 * are pure arithmetic searches for the M/N/divider values that produce the
 * requested pixel clock from the 14.31818 MHz reference - no BIOS or table
 * dependency, which is why they can be ported directly instead of the
 * VBIOS-table based path used for standard VESA modes.
 */

#include "accelerant.h"

/* VCLK = 14.318 * (Divider/PostScaler) * (Numerator/DeNumerator).
 * Range: 18.75-250 MHz. Returns false if out of range (caller falls back
 * to xgi_calc_clock_fallback()). Porting of compute_vclk(), xgi_dac.c:66-167.
 */
static bool
xgi_compute_vclk_simple(uint32 clockKHz, int* outN, int* outDn, int* outDiv,
	int* outSbit, int* outScale)
{
	double f = clockKHz / 1000.0;
	if (f > 250.0 || f < 18.75)
		return false;

	double minError = f;
	double y = 1.0;
	double x = f;
	while (x > 31.25) {
		y *= 2.0;
		x /= 2.0;
	}
	if (x >= 18.25) {
		x *= 8.0;
		y = 8.0 / y;
	} else if (x >= 15.625) {
		x *= 12.0;
		y = 12.0 / y;
	}

	double t = y;
	if (t == 1.5) {
		*outDiv = 2;
		t *= 2.0;
	} else
		*outDiv = 1;

	if (t > 4.0) {
		*outSbit = 1;
		t /= 2.0;
	} else
		*outSbit = 0;

	*outScale = (int)t;

	int bestN = 0, bestDn = 0;
	for (int dn = 2; dn <= 32; dn++) {
		for (int n = 1; n <= 128; n++) {
			double error = x - (14.318 * n / dn);
			if (error < 0)
				error = -error;
			if (error < minError) {
				minError = error;
				bestN = n;
				bestDn = dn;
			}
		}
	}
	*outN = bestN;
	*outDn = bestDn;
	return true;
}

/* M/N/VLD/P exhaustive search, fd = Fref*(M/N)*(VLD/P). Covers a wider
 * range than compute_vclk() (used as fallback). Porting of XGICalcClock(),
 * xgi_dac.c:169-293 (maxVld hardcoded to 2, as called from
 * XGI_CheckBuildCustomMode(), refcode/init.c:2070).
 */
static void
xgi_calc_clock_fallback(uint32 clockKHz, int maxVld, unsigned int vclk[5])
{
	const double kFref = 14318180.0;
	const double kMinVco = kFref;
	const double kMaxVco = 135000000.0;
	const int kMMin = 2, kMMax = 128;

	double target = (double)clockKHz * 1000.0;
	double aBest = 42.0;
	int bestM = 0, bestN = 0, bestP = 0, bestPsn = 0, bestVld = 0;

	/* MAX_PSN is 0 for this chip - no pre-scaler - so psnx only ever 0. */
	int psn = 1;

	for (int vld = 1; vld <= maxVld; vld++) {
		double frefVldPsn = kFref * vld / psn;

		for (int n = 2; n <= 32; n++) {
			double tmp = frefVldPsn / n;

			for (int p = 1; p <= 4; p++) {
				double fvcoDesired = target * p;
				double mDesired = fvcoDesired / tmp;

				int mLow = (int)(mDesired - 1);
				int mHi = (int)(mDesired + 1);
				if (mHi < kMMin || mLow > kMMax)
					continue;
				if (mLow < kMMin)
					mLow = kMMin;
				if (mHi > kMMax)
					mHi = kMMax;

				for (int m = mLow; m <= mHi; m++) {
					double fvco = tmp * m;
					if (fvco <= kMinVco)
						continue;
					if (fvco > kMaxVco)
						break;

					double fout = fvco / p;
					double error = (target - fout) / target;
					double aerror = (error < 0) ? -error : error;
					if (aerror < aBest) {
						aBest = aerror;
						bestM = m;
						bestN = n;
						bestP = p;
						bestPsn = psn;
						bestVld = vld;
					}
				}
			}
		}
	}

	vclk[0] = bestM;
	vclk[1] = bestN;
	vclk[2] = bestVld;
	vclk[3] = bestP;
	vclk[4] = bestPsn;
}

bool
xgi_calc_pll(uint32 pixelClockKHz, XgiPllParams* out)
{
	if (out == NULL)
		return false;

	int n, dn, div, sbit, scale;
	if (xgi_compute_vclk_simple(pixelClockKHz, &n, &dn, &div, &sbit,
			&scale)) {
		/* refcode/init.c:2059-2064 */
		out->sr2b = (div == 2 ? 0x80 : 0x00) | ((n - 1) & 0x7F);
		out->sr2c = ((dn - 1) & 0x1F) | (((scale - 1) & 3) << 5)
			| ((sbit & 0x01) << 7);
		return true;
	}

	/* refcode/init.c:2069-2081 */
	unsigned int vclk[5];
	xgi_calc_clock_fallback(pixelClockKHz, 2, vclk);

	out->sr2b = (vclk[2] == 2 ? 0x80 : 0x00) | ((vclk[0] - 1) & 0x7F);
	if (vclk[3] <= 4) {
		/* postscale 1,2,3,4 */
		out->sr2c = ((vclk[1] - 1) & 0x1F) | (((vclk[3] - 1) & 3) << 5);
	} else {
		/* postscale 6,8 */
		out->sr2c = ((vclk[1] - 1) & 0x1F)
			| ((((vclk[3] / 2) - 1) & 3) << 5) | 0x80;
	}
	return true;
}

void
xgi_program_vclk(const XgiPllParams& params)
{
	/* refcode/init.c:1068-1100. All Volari chips are jChipType >=
	 * XGI_315H in the refcode's chip enum, so the "new chip" branch
	 * (SR2D=0x01) always applies here. */
	out_seq(0x31, in_seq(0x31) & 0xCF);
	out_seq(0x2B, params.sr2b);
	out_seq(0x2C, params.sr2c);
	out_seq(0x2D, 0x01);
}
