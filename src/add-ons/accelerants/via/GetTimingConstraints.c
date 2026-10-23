/*
	Author:
	Rudolf Cornelissen 7/2005
*/

#define MODULE_BIT 0x01000000

#include "acc_std.h"
#define LOG_PREFIX "[VIA Graphics ACC] "
bool enableGTCDebug = false;

/* Used to help generate mode lines */
status_t GET_TIMING_CONSTRAINTS(display_timing_constraints *dtc) 
{
    if (enableGTCDebug) debug_printf(LOG_PREFIX "GET_TIMING_CONSTRAINTS: setting constraints by card generation\n");

    /* Risoluzione base del carattere (8 pixel per carattere sui registri VGA/VIA) */
    dtc->h_res = 8;
    dtc->h_sync_min = 8;
    dtc->h_sync_max = 248;
    dtc->h_blank_min = 8;

    dtc->v_res = 1;
    dtc->v_sync_min = 1;
    dtc->v_sync_max = 15;
    dtc->v_blank_min = 1;

    /* Impostazione dinamica dei limiti di Blanking in base alla generazione del chip */
    if (si->ps.card_type >= VT3410) {
        /* Chrome9 HD (VX900)
         * Registri CRTC estesi a 12-bit (CR36, CR33, SR1C/SR1D):
         * Permette Blanking orizzontale fino a 4088 px e verticale fino a 2047 righe
         * per supportare risoluzioni fino a 2048x1536 / Full HD. */
        dtc->h_blank_max = 4088;
        dtc->v_blank_max = 2047;
    } 
    else if (si->ps.card_type >= VT3327) {
        /* Chrome9 / Chrome9 HC / HCM (da P4M890 a VX855)
         * Registri CRTC estesi a 11-bit:
         * Permette Blanking orizzontale fino a 2040 px e verticale fino a 1023 righe. */
        dtc->h_blank_max = 2040;
        dtc->v_blank_max = 1023;
    } 
    else if (si->ps.card_type >= VT3108) {
        /* UniChrome Pro (K8M800, CN400, CX700, ecc.)
         * Supporto intermedio per widescreen/HD a 720p. */
        dtc->h_blank_max = 2040;
        dtc->v_blank_max = 511;
    } 
    else {
        /* UniChrome legacy (CLE266, KM400)
         * Limiti standard dei registri VGA estesi a 8-bit / 9-bit. */
        dtc->h_blank_max = 1016;
        dtc->v_blank_max = 255;
    }

    if (enableGTCDebug) {
        debug_printf(LOG_PREFIX "GET_TIMING_CONSTRAINTS: card_type=%d, h_blank_max=%" B_PRIu16 ", v_blank_max=%" B_PRIu16 "\n",
            si->ps.card_type, dtc->h_blank_max, dtc->v_blank_max);
    }

    return B_OK;
}
