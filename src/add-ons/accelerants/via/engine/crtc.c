/* CTRC functionality */
/* Author:
    Rudolf Cornelissen 11/2002-9/2005

    Portions based on the OpenChrome Project (https://www.freedesktop.org/wiki/Openchrome):
    Copyright 2005-2016 The OpenChrome Project
    Copyright 2004-2005 The Unichrome Project
    Copyright 1998-2003 VIA Technologies, Inc.
    Copyright 2001-2003 S3 Graphics, Inc.
    Authors: Kevin Brace, James Simmons, Xavier Bachelot, Thomas Hellström, Luc Verhaegen
*/

#define MODULE_BIT 0x00040000
#include <stdbool.h>
#define LOG_PREFIX "[VIA Graphics ACC] "
bool enableCRTCDebug = false;

#include "std.h"

/* Helper macro per eseguire e loggare la modifica di un registro a 8-bit (CRTC/SEQ) */
#define LOG_CRTC_WRITE8(name, read_expr, write_stmt) do { \
    if (enableCRTCDebug) { \
        uint8 __old_val = (read_expr); \
        write_stmt; \
        uint8 __new_val = (read_expr); \
        debug_printf(LOG_PREFIX "  [REG WRITE] %s: PRIMA=0x%02x -> DOPO=0x%02x\n", (name), __old_val, __new_val); \
    } else { \
        write_stmt; \
    } \
} while (0)

/* Helper macro per registri a 32-bit MMIO (DACR / CRTCDR) */
#define LOG_REG32_WRITE(name, reg_id, value) do { \
    if (enableCRTCDebug) { \
        uint32 __old_val = CRTCDR(reg_id); \
        CRTCDW(reg_id, (value)); \
        uint32 __new_val = CRTCDR(reg_id); \
        debug_printf(LOG_PREFIX "  [REG32 WRITE] %s: PRIMA=0x%08" B_PRIx32 " -> DOPO=0x%08" B_PRIx32 "\n", (name), __old_val, __new_val); \
    } else { \
        CRTCDW(reg_id, (value)); \
    } \
} while (0)

/* Adjust passed parameters to a valid mode line */
status_t eng_crtc_validate_timing(
    uint16 *hd_e, uint16 *hs_s, uint16 *hs_e, uint16 *ht,
    uint16 *vd_e, uint16 *vs_s, uint16 *vs_e, uint16 *vt
)
{
/* horizontal */
    /* make all parameters multiples of 8 */
    *hd_e &= 0xfff8;
    *hs_s &= 0xfff8;
    *hs_e &= 0xfff8;
    *ht   &= 0xfff8;

    /* confine to required number of bits, taking logic into account */
    if (*hd_e > ((0x00ff - 2) << 3)) *hd_e = ((0x00ff - 2) << 3);
    if (*hs_s > ((0x01ff - 1) << 3)) *hs_s = ((0x01ff - 1) << 3);
    if (*hs_e > ( 0x01ff      << 3)) *hs_e = ( 0x01ff      << 3);
    if (*ht   > ((0x01ff + 5) << 3)) *ht   = ((0x01ff + 5) << 3);

    /* NOTE: keep horizontal timing at multiples of 8! */
    /* confine to a reasonable width (VX900 supports up to 4096, others 2048) */
    if (*hd_e < 640) *hd_e = 640;
    if (si->ps.card_arch == VX900) {
        if (*hd_e > 4096) *hd_e = 4096;
    } else {
        if (*hd_e > 2048) *hd_e = 2048;
    }

    /* if hor. total does not leave room for a sensible sync pulse, increase it! */
    if (*ht < (*hd_e + 80)) *ht = (*hd_e + 80);

    /* if hor. total does not adhere to max. blanking pulse width, decrease it! */
    if (*ht > (*hd_e + 0x3f8)) *ht = (*hd_e + 0x3f8);

    /* make sure sync pulse is not during display */
    if (*hs_e > (*ht - 8)) *hs_e = (*ht - 8);
    if (*hs_s < (*hd_e + 8)) *hs_s = (*hd_e + 8);

    /* correct sync pulse if it is too long:
     * there are only 5 bits available to save this in the card registers! */
    if (*hs_e > (*hs_s + 0xf8)) *hs_e = (*hs_s + 0xf8);

/*vertical*/
    /* confine to required number of bits, taking logic into account */
    if (*vd_e > (0x7ff - 2)) *vd_e = (0x7ff - 2);
    if (*vs_s > (0x7ff - 1)) *vs_s = (0x7ff - 1);
    if (*vs_e >  0x7ff     ) *vs_e =  0x7ff     ;
    if (*vt   > (0x7ff + 2)) *vt   = (0x7ff + 2);

    /* confine to a reasonable height (OpenChrome supports up to 2048) */
    if (*vd_e < 480) *vd_e = 480;
    if (*vd_e > 2048) *vd_e = 2048;

    /* if vertical total does not leave room for a sync pulse, increase it! */
    if (*vt < (*vd_e + 3)) *vt = (*vd_e + 3);

    /* if vert. total does not adhere to max. blanking pulse width, decrease it! */
    if (*vt > (*vd_e + 0xff)) *vt = (*vd_e + 0xff);

    /* make sure sync pulse is not during display */
    if (*vs_e > (*vt - 1)) *vs_e = (*vt - 1);
    if (*vs_s < (*vd_e + 1)) *vs_s = (*vd_e + 1);

    /* correct sync pulse if it is too long:
     * there are only 4 bits available to save this in the card registers! */
    if (*vs_e > (*vs_s + 0x0f)) *vs_e = (*vs_s + 0x0f);

    return B_OK;
}

/* set a mode line - inputs are in pixels */
status_t eng_crtc_set_timing(display_mode target)
{
    uint16 fifolimit = 0;
    uint8 temp;

    uint32 htotal;        /* total horizontal total VCLKs */
    uint32 hdisp_e;       /* end of horizontal display (begins at 0) */
    uint32 hsync_s;       /* begin of horizontal sync pulse */
    uint32 hsync_e;       /* end of horizontal sync pulse */
    uint32 hblnk_s;       /* begin horizontal blanking */
    uint32 hblnk_e;       /* end horizontal blanking */

    uint32 vtotal;        /* total vertical total scanlines */
    uint32 vdisp_e;       /* end of vertical display */
    uint32 vsync_s;       /* begin of vertical sync pulse */
    uint32 vsync_e;       /* end of vertical sync pulse */
    uint32 vblnk_s;       /* begin vertical blanking */
    uint32 vblnk_e;       /* end vertical blanking */

    uint32 linecomp;      /* split screen and vdisp_e interrupt */

    if (enableCRTCDebug) debug_printf(LOG_PREFIX "CRTC: eng_crtc_set_timing START\n");

    /* Modify parameters as required by standard VGA */
    htotal = ((target.timing.h_total >> 3) - 5);
    hdisp_e = ((target.timing.h_display >> 3) - 1);
    hblnk_s = hdisp_e;
    hblnk_e = (htotal + 4);
    hsync_s = (target.timing.h_sync_start >> 3);
    hsync_e = (target.timing.h_sync_end >> 3);

    vtotal = target.timing.v_total - 2;
    vdisp_e = target.timing.v_display - 1;
    vblnk_s = vdisp_e;
    vblnk_e = (vtotal + 1);
    vsync_s = target.timing.v_sync_start;
    vsync_e = target.timing.v_sync_end;

    linecomp = 0xffff;

    if (enableCRTCDebug) {
        debug_printf(LOG_PREFIX "CRTC Timings Calc:\n\tHTOT:%x HDISPEND:%x HBLNKS:%x HBLNKE:%x HSYNCS:%x HSYNCE:%x\n",
            htotal, hdisp_e, hblnk_s, hblnk_e, hsync_s, hsync_e);
        debug_printf(LOG_PREFIX "\tVTOT:%x VDISPEND:%x VBLNKS:%x VBLNKE:%x VSYNCS:%x VSYNCE:%x\n",
            vtotal, vdisp_e, vblnk_s, vblnk_e, vsync_s, vsync_e);
    }

    /* Unlock CRTC registers at index 0-7 */
    LOG_CRTC_WRITE8("CRTC VSYNCE (CR11 unlock)", CRTCR(VSYNCE), CRTCW(VSYNCE, (CRTCR(VSYNCE) & 0x7f)));

    /* Horizontal standard VGA regs */
    LOG_CRTC_WRITE8("CRTC HTOTAL (CR00)", CRTCR(HTOTAL), CRTCW(HTOTAL, (htotal & 0xff)));
    LOG_CRTC_WRITE8("CRTC HDISPE (CR01)", CRTCR(HDISPE), CRTCW(HDISPE, (hdisp_e & 0xff)));
    LOG_CRTC_WRITE8("CRTC HBLANKS (CR02)", CRTCR(HBLANKS), CRTCW(HBLANKS, (hblnk_s & 0xff)));
    LOG_CRTC_WRITE8("CRTC HBLANKE (CR03)", CRTCR(HBLANKE), CRTCW(HBLANKE, ((hblnk_e & 0x1f) | 0x80)));
    LOG_CRTC_WRITE8("CRTC HSYNCS (CR04)", CRTCR(HSYNCS), CRTCW(HSYNCS, (hsync_s & 0xff)));
    LOG_CRTC_WRITE8("CRTC HSYNCE (CR05)", CRTCR(HSYNCE), CRTCW(HSYNCE, ((hsync_e & 0x1f) | ((hblnk_e & 0x20) << 2))));

    /* Vertical standard VGA regs */
    LOG_CRTC_WRITE8("CRTC VTOTAL (CR06)", CRTCR(VTOTAL), CRTCW(VTOTAL, (vtotal & 0xff)));
    LOG_CRTC_WRITE8("CRTC OVERFLOW (CR07)", CRTCR(OVERFLOW), CRTCW(OVERFLOW, (
        ((vtotal & 0x100) >> (8 - 0)) | ((vtotal & 0x200) >> (9 - 5)) |
        ((vdisp_e & 0x100) >> (8 - 1)) | ((vdisp_e & 0x200) >> (9 - 6)) |
        ((vsync_s & 0x100) >> (8 - 2)) | ((vsync_s & 0x200) >> (9 - 7)) |
        ((vblnk_s & 0x100) >> (8 - 3)) | ((linecomp & 0x0100) >> (8 - 4))
    )));
    LOG_CRTC_WRITE8("CRTC PRROWSCN (CR08)", CRTCR(PRROWSCN), CRTCW(PRROWSCN, 0x00));
    LOG_CRTC_WRITE8("CRTC MAXSCLIN (CR09)", CRTCR(MAXSCLIN), CRTCW(MAXSCLIN, (((vblnk_s & 0x200) >> (9 - 5)) | ((linecomp & 0x0200) >> (9 - 6)))));
    LOG_CRTC_WRITE8("CRTC VSYNCS (CR10)", CRTCR(VSYNCS), CRTCW(VSYNCS, (vsync_s & 0xff)));
    LOG_CRTC_WRITE8("CRTC VSYNCE (CR11)", CRTCR(VSYNCE), CRTCW(VSYNCE, ((CRTCR(VSYNCE) & 0xf0) | (vsync_e & 0x0f))));
    LOG_CRTC_WRITE8("CRTC VDISPE (CR12)", CRTCR(VDISPE), CRTCW(VDISPE, (vdisp_e & 0xff)));
    LOG_CRTC_WRITE8("CRTC VBLANKS (CR15)", CRTCR(VBLANKS), CRTCW(VBLANKS, (vblnk_s & 0xff)));
    LOG_CRTC_WRITE8("CRTC VBLANKE (CR16)", CRTCR(VBLANKE), CRTCW(VBLANKE, (vblnk_e & 0xff)));
    LOG_CRTC_WRITE8("CRTC LINECOMP (CR18)", CRTCR(LINECOMP), CRTCW(LINECOMP, (linecomp & 0xff)));

    /* Horizontal extended regs */
    LOG_CRTC_WRITE8("CRTC HTIMEXT1 (CR33)", CRTCR(HTIMEXT1), CRTCW(HTIMEXT1, (CRTCR(HTIMEXT1) & 0xc8) | (
        ((linecomp & 0x1c00) >> (10 - 0)) |
        ((hblnk_e & 0x040) >> (6 - 5)) |
        ((hsync_s & 0x100) >> (8 - 4))
    )));
    LOG_CRTC_WRITE8("CRTC HTIMEXT2 (CR36)", CRTCR(HTIMEXT2), CRTCW(HTIMEXT2, (CRTCR(HTIMEXT2) & 0xf7) | ((htotal & 0x100) >> (8 - 3))));

    /* Vertical extended regs */
    LOG_CRTC_WRITE8("CRTC VTIMEXT_PIT (CR35)", CRTCR(VTIMEXT_PIT), CRTCW(VTIMEXT_PIT, (CRTCR(VTIMEXT_PIT) & 0xe0) | (
        ((vtotal & 0x400) >> (10 - 0)) |
        ((vsync_s & 0x400) >> (10 - 1)) |
        ((vdisp_e & 0x400) >> (10 - 2)) |
        ((vblnk_s & 0x400) >> (10 - 3)) |
        ((linecomp & 0x2000) >> (13 - 4))
    )));

    /* VX900 CR45 (Bits estesi H-Display End e H-Blank Start) */
    if (si->ps.card_arch == VX900) {
        LOG_CRTC_WRITE8("CRTC CR45 (VX900 H-Ext)", CRTCR(CR45), CRTCW(CR45, (CRTCR(CR45) & ~0x06) |
            (((hdisp_e >> 8) & 0x01) << 1) |
            (((hblnk_s >> 8) & 0x01) << 2)));
    }

    /* Unprotect CRT timing registers (CR47) */
    if (si->ps.card_arch >= VX855) {
        LOG_CRTC_WRITE8("CRTC CR47 (Unprotect)", CRTCR(CR47), CRTCW(CR47, (CRTCR(CR47) & ~0x10)));
    } else {
        LOG_CRTC_WRITE8("CRTC CR47 (Unprotect)", CRTCR(CR47), CRTCW(CR47, (CRTCR(CR47) & ~0x01)));
    }

    /* Set horizontal display fetch count (SR1C & SR1D) */
    {
        uint8 bpp = 1;
        switch (target.space) {
            case B_RGB15_LITTLE:
            case B_RGB16_LITTLE: bpp = 2; break;
            case B_RGB24_LITTLE: bpp = 3; break;
            case B_RGB32_LITTLE: bpp = 4; break;
            default: bpp = 1; break;
        }
        uint32 fetch = (target.timing.h_display * bpp) >> 4;
        LOG_CRTC_WRITE8("SEQ FETCHCNTLO (SR1C)", SEQR(FETCHCNTLO), SEQW(FETCHCNTLO, fetch & 0xff));
        LOG_CRTC_WRITE8("SEQ FETCHCNTHI (SR1D)", SEQR(FETCHCNTHI), SEQW(FETCHCNTHI, (SEQR(FETCHCNTHI) & ~0x03) | ((fetch >> 8) & 0x03)));
    }

    /* Set FIFO depth/threshold/queue-expire (SR16, SR17, SR18, SR22).
     * These were never programmed by this driver (left at whatever the
     * card's BIOS/POST set them to). Values taken verbatim from the
     * OpenChrome project's ViaSetPrimaryFIFO() (via_display.c), which sets
     * them explicitly per chip generation. Only architectures with a plain
     * fixed-value case there (no lookup table) are covered here. */
    switch (si->ps.card_arch) {
        case K8M800:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0xbf));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x92));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x8a));
            break;
        case PM800:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0x5f));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x20));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x10));
            break;
        case P4M800PRO:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0x2f));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x14));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x08));
            break;
        case K8M890:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0xb3));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x92));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x8a));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x1f));
            break;
        case P4M900:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0x2f));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x13));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x13));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x08));
            break;
        case P4M890:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0x2f));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x13));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x10));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x08));
            break;
        case CX700:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0x5f));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x20));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x20));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x1f));
            break;
        case VX800:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0x5f));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x26));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x26));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x10));
            break;
        case VX855:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0xc7));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x90));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x90));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x28));
            break;
        case VX900:
            LOG_CRTC_WRITE8("SEQ FIFODEPTH (SR17)", SEQR(FIFODEPTH), SEQW(FIFODEPTH, 0xc7));
            LOG_CRTC_WRITE8("SEQ FIFOWM (SR16)", SEQR(FIFOWM), SEQW(FIFOWM, 0x90));
            LOG_CRTC_WRITE8("SEQ SR18", SEQR(0x18), SEQW(0x18, 0x90));
            LOG_CRTC_WRITE8("SEQ SR22", SEQR(0x22), SEQW(0x22, 0x28));
            break;
        default:
            /* CLE266/KM400/CN750: OpenChrome uses per-mode lookup tables here
             * that we don't have ported; leave at BIOS/POST default as before. */
            break;
    }

    /* Setup HSYNC & VSYNC polarity */
    temp = ENG_REG8(RG8_MISCR);

    /* 1. Gestione HSYNC (bit 6) */
    if (target.timing.flags & B_POSITIVE_HSYNC)
        temp &= ~0x40;
    else
        temp |= 0x40;

    /* 2. Gestione VSYNC (bit 7) */
    if (target.timing.flags & B_POSITIVE_VSYNC)
        temp &= ~0x80;
    else
        temp |= 0x80;

    if (enableCRTCDebug) {
        uint8 old_misc = ENG_REG8(RG8_MISCR);
        ENG_REG8(RG8_MISCW) = temp;
        uint8 new_misc = ENG_REG8(RG8_MISCR);
        debug_printf(LOG_PREFIX "  [REG WRITE] MISC Register: PRIMA=0x%02x -> DOPO=0x%02x\n", old_misc, new_misc);
    } else {
        ENG_REG8(RG8_MISCW) = temp;
    }

    /* Setup CRTC FIFO depth */
    switch (target.space) {
        case B_CMAP8: fifolimit = 0x0001; break;
        case B_RGB15_LITTLE:
        case B_RGB16_LITTLE: fifolimit = 0x0002; break;
        case B_RGB24_LITTLE: fifolimit = 0x0003; break;
        case B_RGB32_LITTLE: fifolimit = 0x0004; break;
    }
    fifolimit *= target.timing.h_display;
    fifolimit >>= 4;
    fifolimit += 4;

    LOG_CRTC_WRITE8("SEQ FETCHCNTLO (FIFO Calc)", SEQR(FETCHCNTLO), SEQW(FETCHCNTLO, (fifolimit & 0x00fe)));
    LOG_CRTC_WRITE8("SEQ FETCHCNTHI (FIFO Calc)", SEQR(FETCHCNTHI), SEQW(FETCHCNTHI, (((SEQR(FETCHCNTHI)) & 0xfc) | ((fifolimit & 0x0300) >> 8))));

    return B_OK;
}

status_t eng_crtc_depth(int mode)
{
    uint8 genctrl = 0;

    switch(mode) {
        case BPP8:  genctrl = 0xa2; break;
        case BPP15: genctrl = 0xa6; break;
        case BPP16: genctrl = 0xb6; break;
        case BPP24: genctrl = 0xaa; break;
        case BPP32: genctrl = 0xae; break;
    }

    LOG_CRTC_WRITE8("SEQ COLDEPTH (SR15)", SEQR(COLDEPTH), SEQW(COLDEPTH, genctrl));
    return B_OK;
}

status_t eng_crtc_dpms(bool display, bool h, bool v)
{
    uint8 temp;

    if (enableCRTCDebug) debug_printf(LOG_PREFIX "CRTC: eng_crtc_dpms (display=%d, h=%d, v=%d)\n", display, h, v);

    /* Start synchronous reset */
    LOG_CRTC_WRITE8("SEQ RESET (SR00 Start)", SEQR(RESET), SEQW(RESET, 0x01));

    /* Turn screen on/off */
    temp = SEQR(CLKMODE);
    if (display) {
        LOG_CRTC_WRITE8("SEQ CLKMODE (Display ON)", SEQR(CLKMODE), SEQW(CLKMODE, (temp & ~0x20)));
        LOG_CRTC_WRITE8("SEQ RESET (SR00 End)", SEQR(RESET), SEQW(RESET, 0x03));
    } else {
        LOG_CRTC_WRITE8("SEQ CLKMODE (Display OFF)", SEQR(CLKMODE), SEQW(CLKMODE, (temp | 0x20)));
    }

    /* HSYNC Control */
    if (h) {
        LOG_CRTC_WRITE8("CRTC HTIMEXT2 (HSYNC ON)", CRTCR(HTIMEXT2), CRTCW(HTIMEXT2, (CRTCR(HTIMEXT2) & 0xef)));
    } else {
        LOG_CRTC_WRITE8("CRTC HTIMEXT2 (HSYNC OFF)", CRTCR(HTIMEXT2), CRTCW(HTIMEXT2, (CRTCR(HTIMEXT2) | 0x10)));
    }

    /* VSYNC Control */
    if (v) {
        LOG_CRTC_WRITE8("CRTC HTIMEXT2 (VSYNC ON)", CRTCR(HTIMEXT2), CRTCW(HTIMEXT2, (CRTCR(HTIMEXT2) & 0xdf)));
    } else {
        LOG_CRTC_WRITE8("CRTC HTIMEXT2 (VSYNC OFF)", CRTCR(HTIMEXT2), CRTCW(HTIMEXT2, (CRTCR(HTIMEXT2) | 0x20)));
    }

    return B_OK;
}

status_t eng_crtc_dpms_fetch(bool *display, bool *h, bool *v)
{
    *display = !(SEQR(CLKMODE) & 0x20);
    *h = !(CRTCR(HTIMEXT2) & 0x10);
    *v = !(CRTCR(HTIMEXT2) & 0x20);

    if (enableCRTCDebug) {
        debug_printf(LOG_PREFIX "CRTC: eng_crtc_dpms_fetch -> display=%s, hsync=%s, vsync=%s\n",
            *display ? "ON" : "OFF", *h ? "ON" : "OFF", *v ? "ON" : "OFF");
    }

    return B_OK;
}

status_t eng_crtc_set_display_pitch() 
{
    uint16 offset;

    offset = si->fbc.bytes_per_row >> 3;

    if (enableCRTCDebug) debug_printf(LOG_PREFIX "CRTC: eng_crtc_set_display_pitch (bytes_per_row=%" B_PRIu32 " -> offset=0x%04x)\n",
        si->fbc.bytes_per_row, offset);

    LOG_CRTC_WRITE8("CRTC PITCHL (CR13)", CRTCR(PITCHL), CRTCW(PITCHL, (offset & 0x00ff)));
    LOG_CRTC_WRITE8("CRTC VTIMEXT_PIT (CR35)", CRTCR(VTIMEXT_PIT), CRTCW(VTIMEXT_PIT, (((CRTCR(VTIMEXT_PIT)) & 0x1f) | ((offset & 0x0700) >> 3))));

    return B_OK;
}

status_t eng_crtc_set_display_start(uint32 startadd, uint8 bpp) 
{
    if (enableCRTCDebug) {
        debug_printf(LOG_PREFIX "CRTC: eng_crtc_set_display_start (startadd=0x%08" B_PRIx32 ", bpp=%d)\n", startadd, bpp);
    }

    LOG_CRTC_WRITE8("CRTC FBSTADDL (CR0D)", CRTCR(FBSTADDL), CRTCW(FBSTADDL, ((startadd & 0x000001f8) >> 1)));
    LOG_CRTC_WRITE8("CRTC FBSTADDH (CR0C)", CRTCR(FBSTADDH), CRTCW(FBSTADDH, ((startadd & 0x0001fe00) >> 9)));

    if (si->ps.card_arch >= K8M890) {
        LOG_CRTC_WRITE8("CRTC CR48 (Extended FB Addr)", CRTCR(CR48), CRTCW(CR48, (CRTCR(CR48) & ~0x1f) | ((startadd >> 25) & 0x1f)));
    }

    /* Fire register (FBSTADDE / CR34) */
    LOG_CRTC_WRITE8("CRTC FBSTADDE (CR34 - Fire)", CRTCR(FBSTADDE), CRTCW(FBSTADDE, ((startadd & 0x01fe0000) >> 17)));

    return B_OK;
}

status_t eng_crtc_cursor_init()
{
    int i;
    uint32 *fb;
    const uint32 curadd = 0;

    LOG_REG32_WRITE("CURSOR_BG", CURSOR_BG, 0xffffffff);
    LOG_REG32_WRITE("CURSOR_FG", CURSOR_FG, 0x00000000);
    LOG_REG32_WRITE("CURSOR_MODE", CURSOR_MODE, (curadd & 0xfffffffc));

    fb = (uint32 *)si->framebuffer + curadd;
    for (i = 0; i < (4096 / 4); i += 2) {
        fb[i + 0] = 0x00000000;
        fb[i + 1] = 0xffffffff;
    }

    eng_crtc_cursor_show();
    return B_OK;
}

status_t eng_crtc_cursor_show()
{
    if (enableCRTCDebug) debug_printf(LOG_PREFIX "CRTC: eng_crtc_cursor_show\n");
    LOG_REG32_WRITE("CURSOR_MODE (Show)", CURSOR_MODE, (CRTCDR(CURSOR_MODE) | 0x00000003));
    return B_OK;
}

status_t eng_crtc_cursor_hide()
{
    if (enableCRTCDebug) debug_printf(LOG_PREFIX "CRTC: eng_crtc_cursor_hide\n");
    LOG_REG32_WRITE("CURSOR_MODE (Hide)", CURSOR_MODE, (CRTCDR(CURSOR_MODE) & 0xfffffffc));
    return B_OK;
}

status_t eng_crtc_cursor_define(uint8* andMask, uint8* xorMask)
{
    int y;
    uint8 *cursor;

    cursor = (uint8*)si->framebuffer;

    for (y = 0; y < 16; y++) {
        cursor[0 + (y * 8)] = *xorMask++;
        cursor[1 + (y * 8)] = *xorMask++;
        cursor[4 + (y * 8)] = *andMask++;
        cursor[5 + (y * 8)] = *andMask++;
    }

    return B_OK;
}

status_t eng_crtc_cursor_position(uint16 x, uint16 y)
{
    LOG_REG32_WRITE("CURSOR_ORG", CURSOR_ORG, 0x00000000);
    LOG_REG32_WRITE("CURSOR_POS", CURSOR_POS, (((x & 0x07ff) << 16) | (y & 0x07ff)));
    return B_OK;
}
