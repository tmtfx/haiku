/* Read initialisation information from card */
/* some bits are hacks, where PINS is not known */
/* Author:
   Rudolf Cornelissen 7/2003-5/2006

   Portions based on the OpenChrome Project (https://www.freedesktop.org/wiki/Openchrome):
   Copyright 2005-2016 The OpenChrome Project
   Copyright 2004-2005 The Unichrome Project
   Copyright 1998-2003 VIA Technologies, Inc.
   Copyright 2001-2003 S3 Graphics, Inc.
   Authors: Kevin Brace, James Simmons, Xavier Bachelot, Thomas Hellström, Luc Verhaegen
*/

#define MODULE_BIT 0x00002000

#include "std.h"

static void detect_panels(void);
static void setup_output_matrix(void);
static void pins_cle266_fake(void);
static void pins_unichrome_pro_fake(void);
static void pins_chrome9_fake(void);
static void getRAMsize(void);
static status_t eng_crtc_setup_fifo(void);


/* (pre)set 'fixed' card specifications */
void set_specs(void)
{
	LOG(8,("INFO: setting up card specifications\n"));

	/* set failsave speeds */
	switch (si->ps.card_arch)
	{
	case K8M890:
	case P4M900:
	case VX800:
	case VX855:
	case VX900:
		pins_chrome9_fake();
		break;
	case K8M800:
	case PM800:
	case P4M800PRO:
	case CX700:
	case CN750:
	case P4M890:
		pins_unichrome_pro_fake();
		break;
	case CLE266:
	case KM400:
	default:
		pins_cle266_fake();
		break;
	}


}

/* this routine presumes the card was coldstarted by the card's BIOS for panel stuff */
void fake_panel_start(void)
{
	LOG(8,("INFO: detecting RAM size\n"));

	/* detect RAM amount */
	getRAMsize();

	/* override memory detection if requested by user */
	if (si->settings.memory != 0)
	{
		LOG(2,("INFO: forcing memory size (specified in settings file)\n"));
		si->ps.memory_size = si->settings.memory * 1024 * 1024;
	}

	/* find out if the card has a tvout chip */
	si->ps.tvout = false;
	si->ps.tvout_chip_type = NONE;
//fixme ;-)
/*	if (i2c_maven_probe() == B_OK)
	{
		si->ps.tvout = true;
		si->ps.tvout_chip_bus = ???;
		si->ps.tvout_chip_type = ???;
	}
*/

	LOG(8,("INFO: faking panel startup\n"));

	/* find out the BIOS preprogrammed panel use status... */
//	detect_panels();

	/* determine and setup output devices and heads */
	setup_output_matrix();

	/* select other CRTC for primary head use if specified by user in settings file */
	if (si->ps.secondary_head && si->settings.switchhead)
	{
		LOG(2,("INFO: inverting head use (specified in settings file)\n"));
		si->ps.crtc2_prim = !si->ps.crtc2_prim;
	}
}

static void detect_panels()
{
	/* detect if the BIOS enabled LCD's (internal panels or DVI) or TVout */

	/* both external TMDS transmitters (used for LCD/DVI) and external TVencoders
	 * (can) use the CRTC's in slaved mode. */
	/* Note:
	 * DFP's are programmed with standard VESA modelines by the card's BIOS! */
	bool slaved_for_dev1 = false, slaved_for_dev2 = false;
	bool tvout1 = false, tvout2 = false;

	/* check primary head: */
	/* enable access to primary head */
	set_crtc_owner(0);

	/* unlock head's registers for R/W access */
	CRTCW(LOCK, 0x57);
	CRTCW(VSYNCE ,(CRTCR(VSYNCE) & 0x7f));

	LOG(2,("INFO: Dumping flatpanel related CRTC registers:\n"));
	/* related info PIXEL register:
	 * b7: 1 = slaved mode										(all cards). */
	LOG(2,("CRTC1: PIXEL register: $%02x\n", CRTCR(PIXEL)));
	/* info LCD register:
	 * b7: 1 = stereo view (shutter glasses use)				(all cards),
	 * b5: 1 = power ext. TMDS (or something)/0 = TVout	use	(?)	
	 * b4: 1 = power ext. TMDS (or something)/0 = TVout use	(?)	
	 * b3: 1 = ??? (not panel related probably!)				
	 * b1: 1 = power ext. TMDS (or something) (?)				
	 * b0: 1 = select panel encoder / 0 = select TVout encoder	(all cards). */
	LOG(2,("CRTC1: LCD register: $%02x\n", CRTCR(LCD)));
	/* info 0x59 register:
	 * b0: 1 = enable ext. TMDS clock (DPMS)					 */
	LOG(2,("CRTC1: register $59: $%02x\n", CRTCR(0x59)));
	/* info 0x9f register:
	 * b4: 0 = TVout use (?). */
	LOG(2,("CRTC1: register $9f: $%02x\n", CRTCR(0x9f)));

	/* detect active slave device (if any) */
	slaved_for_dev1 = (CRTCR(PIXEL) & 0x80);
	if (slaved_for_dev1)
	{
		/* if the panel isn't selected, tvout is.. */
		tvout1 = !(CRTCR(LCD) & 0x01);
	}

	if (si->ps.secondary_head)
	{
		/* check secondary head: */
		/* enable access to secondary head */
		set_crtc_owner(1);
		/* unlock head's registers for R/W access */
		CRTC2W(LOCK, 0x57);
		CRTC2W(VSYNCE ,(CRTC2R(VSYNCE) & 0x7f));

		LOG(2,("CRTC2: PIXEL register: $%02x\n", CRTC2R(PIXEL)));
		LOG(2,("CRTC2: LCD register: $%02x\n", CRTC2R(LCD)));
		LOG(2,("CRTC2: register $59: $%02x\n", CRTC2R(0x59)));
		LOG(2,("CRTC2: register $9f: $%02x\n", CRTC2R(0x9f)));

		/* detect active slave device (if any) */
		slaved_for_dev2 = (CRTC2R(PIXEL) & 0x80);
		if (slaved_for_dev2)
		{
			/* if the panel isn't selected, tvout is.. */
			tvout2 = !(CRTC2R(LCD) & 0x01);
		}
	}

	LOG(2,("INFO: End flatpanel related CRTC registers dump.\n"));

	/* do some presets */
	si->ps.p1_timing.h_display = 0;
	si->ps.p1_timing.v_display = 0;
	si->ps.panel1_aspect = 0;
	si->ps.p2_timing.h_display = 0;
	si->ps.p2_timing.v_display = 0;
	si->ps.panel2_aspect = 0;
	si->ps.slaved_tmds1 = false;
	si->ps.slaved_tmds2 = false;
	si->ps.master_tmds1 = false;
	si->ps.master_tmds2 = false;
	si->ps.tmds1_active = false;
	si->ps.tmds2_active = false;
	/* determine the situation we are in... (regarding flatpanels) */
	/* fixme: add VESA DDC EDID stuff one day... */
	/* fixme: find out how to program those transmitters one day instead of
	 * relying on the cards BIOS to do it. This adds TVout options where panels
	 * are used!
	 * Currently we'd loose the panel setup while not being able to restore it. */

	/* note also:
	 * external TMDS encoders are only used for logic-level translation: it's
	 * modeline registers are not used. Instead the GPU's internal modeline registers
	 * are used. The external encoder is not connected to a I2C bus . */
	if (slaved_for_dev1 && !tvout1)
	{
		uint16 width = ((DACR(FP_HDISPEND) & 0x0000ffff) + 1);
		uint16 height = ((DACR(FP_VDISPEND) & 0x0000ffff) + 1);
		if ((width >= 640) && (height >= 480))
		{
			si->ps.slaved_tmds1 = true;
			si->ps.tmds1_active = true;
			si->ps.p1_timing.h_display = width;
			si->ps.p1_timing.v_display = height;
		}
	}

	if (si->ps.secondary_head && slaved_for_dev2 && !tvout2)
	{
		uint16 width = ((DAC2R(FP_HDISPEND) & 0x0000ffff) + 1);
		uint16 height = ((DAC2R(FP_VDISPEND) & 0x0000ffff) + 1);
		if ((width >= 640) && (height >= 480))
		{
			si->ps.slaved_tmds2 = true;
			si->ps.tmds2_active = true;
			si->ps.p2_timing.h_display = width;
			si->ps.p2_timing.v_display = height;
		}
	}



	//fixme...:
	//we are assuming that no DVI is used as external monitor on laptops;
	//otherwise we probably get into trouble here if the checked specs match.
	if (si->ps.laptop && si->ps.tmds1_active && si->ps.tmds2_active &&
		((DACR(FP_TG_CTRL) & 0x80000000) == (DAC2R(FP_TG_CTRL) & 0x80000000)) &&
		(si->ps.p1_timing.h_display == si->ps.p2_timing.h_display) &&
		(si->ps.p1_timing.v_display == si->ps.p2_timing.v_display))
	{
		LOG(2,("INFO: correcting double detection of single panel!\n"));

		if (DACR(FP_TG_CTRL) & 0x80000000)
		{
			/* LVDS panel is on CRTC2, so clear false primary detection */
			si->ps.slaved_tmds1 = false;
			si->ps.master_tmds1 = false;
			si->ps.tmds1_active = false;
			si->ps.p1_timing.h_display = 0;
			si->ps.p1_timing.v_display = 0;
		}
		else
		{
			/* LVDS panel is on CRTC1, so clear false secondary detection */
			si->ps.slaved_tmds2 = false;
			si->ps.master_tmds2 = false;
			si->ps.tmds2_active = false;
			si->ps.p2_timing.h_display = 0;
			si->ps.p2_timing.v_display = 0;
		}
	}

	/* fetch panel(s) modeline(s) */
	if (si->ps.tmds1_active)
	{
		/* determine panel aspect ratio */
		si->ps.panel1_aspect =
			(si->ps.p1_timing.h_display / ((float)si->ps.p1_timing.v_display));
		/* horizontal timing */
		si->ps.p1_timing.h_sync_start = (DACR(FP_HSYNC_S) & 0x0000ffff) + 1;
		si->ps.p1_timing.h_sync_end = (DACR(FP_HSYNC_E) & 0x0000ffff) + 1;
		si->ps.p1_timing.h_total = (DACR(FP_HTOTAL) & 0x0000ffff) + 1;
		/* vertical timing */
		si->ps.p1_timing.v_sync_start = (DACR(FP_VSYNC_S) & 0x0000ffff) + 1;
		si->ps.p1_timing.v_sync_end = (DACR(FP_VSYNC_E) & 0x0000ffff) + 1;
		si->ps.p1_timing.v_total = (DACR(FP_VTOTAL) & 0x0000ffff) + 1;
		/* sync polarity */
		si->ps.p1_timing.flags = 0;
		if (DACR(FP_TG_CTRL) & 0x00000001) si->ps.p1_timing.flags |= B_POSITIVE_VSYNC;
		if (DACR(FP_TG_CTRL) & 0x00000010) si->ps.p1_timing.flags |= B_POSITIVE_HSYNC;
		/* refreshrate:
		 * fix a DVI or laptop flatpanel to 60Hz refresh! */
		si->ps.p1_timing.pixel_clock =
			(si->ps.p1_timing.h_total * si->ps.p1_timing.v_total * 60) / 1000;
	}
	if (si->ps.tmds2_active)
	{
		/* determine panel aspect ratio */
		si->ps.panel2_aspect =
			(si->ps.p2_timing.h_display / ((float)si->ps.p2_timing.v_display));
		/* horizontal timing */
		si->ps.p2_timing.h_sync_start = (DAC2R(FP_HSYNC_S) & 0x0000ffff) + 1;
		si->ps.p2_timing.h_sync_end = (DAC2R(FP_HSYNC_E) & 0x0000ffff) + 1;
		si->ps.p2_timing.h_total = (DAC2R(FP_HTOTAL) & 0x0000ffff) + 1;
		/* vertical timing */
		si->ps.p2_timing.v_sync_start = (DAC2R(FP_VSYNC_S) & 0x0000ffff) + 1;
		si->ps.p2_timing.v_sync_end = (DAC2R(FP_VSYNC_E) & 0x0000ffff) + 1;
		si->ps.p2_timing.v_total = (DAC2R(FP_VTOTAL) & 0x0000ffff) + 1;
		/* sync polarity */
		si->ps.p2_timing.flags = 0;
		if (DAC2R(FP_TG_CTRL) & 0x00000001) si->ps.p2_timing.flags |= B_POSITIVE_VSYNC;
		if (DAC2R(FP_TG_CTRL) & 0x00000010) si->ps.p2_timing.flags |= B_POSITIVE_HSYNC;
		/* refreshrate:
		 * fix a DVI or laptop flatpanel to 60Hz refresh! */
		si->ps.p2_timing.pixel_clock =
			(si->ps.p2_timing.h_total * si->ps.p2_timing.v_total * 60) / 1000;
	}

	/* dump some panel configuration registers... */
	LOG(2,("INFO: Dumping flatpanel registers:\n"));
	LOG(2,("DUALHEAD_CTRL: $%08x\n", ENG_REG32(RG32_DUALHEAD_CTRL)));
	LOG(2,("DAC1: FP_HDISPEND: %d\n", DACR(FP_HDISPEND)));
	LOG(2,("DAC1: FP_HTOTAL: %d\n", DACR(FP_HTOTAL)));
	LOG(2,("DAC1: FP_HCRTC: %d\n", DACR(FP_HCRTC)));
	LOG(2,("DAC1: FP_HSYNC_S: %d\n", DACR(FP_HSYNC_S)));
	LOG(2,("DAC1: FP_HSYNC_E: %d\n", DACR(FP_HSYNC_E)));
	LOG(2,("DAC1: FP_HVALID_S: %d\n", DACR(FP_HVALID_S)));
	LOG(2,("DAC1: FP_HVALID_E: %d\n", DACR(FP_HVALID_E)));

	LOG(2,("DAC1: FP_VDISPEND: %d\n", DACR(FP_VDISPEND)));
	LOG(2,("DAC1: FP_VTOTAL: %d\n", DACR(FP_VTOTAL)));
	LOG(2,("DAC1: FP_VCRTC: %d\n", DACR(FP_VCRTC)));
	LOG(2,("DAC1: FP_VSYNC_S: %d\n", DACR(FP_VSYNC_S)));
	LOG(2,("DAC1: FP_VSYNC_E: %d\n", DACR(FP_VSYNC_E)));
	LOG(2,("DAC1: FP_VVALID_S: %d\n", DACR(FP_VVALID_S)));
	LOG(2,("DAC1: FP_VVALID_E: %d\n", DACR(FP_VVALID_E)));

	LOG(2,("DAC1: FP_CHKSUM: $%08x = (dec) %d\n", DACR(FP_CHKSUM),DACR(FP_CHKSUM)));
	LOG(2,("DAC1: FP_TST_CTRL: $%08x\n", DACR(FP_TST_CTRL)));
	LOG(2,("DAC1: FP_TG_CTRL: $%08x\n", DACR(FP_TG_CTRL)));
	LOG(2,("DAC1: FP_DEBUG0: $%08x\n", DACR(FP_DEBUG0)));
	LOG(2,("DAC1: FP_DEBUG1: $%08x\n", DACR(FP_DEBUG1)));
	LOG(2,("DAC1: FP_DEBUG2: $%08x\n", DACR(FP_DEBUG2)));
	LOG(2,("DAC1: FP_DEBUG3: $%08x\n", DACR(FP_DEBUG3)));

	LOG(2,("DAC1: FUNCSEL: $%08x\n", ENG_REG32(RG32_FUNCSEL)));
	LOG(2,("DAC1: PANEL_PWR: $%08x\n", ENG_REG32(RG32_PANEL_PWR)));

	if(si->ps.secondary_head)
	{
		LOG(2,("DAC2: FP_HDISPEND: %d\n", DAC2R(FP_HDISPEND)));
		LOG(2,("DAC2: FP_HTOTAL: %d\n", DAC2R(FP_HTOTAL)));
		LOG(2,("DAC2: FP_HCRTC: %d\n", DAC2R(FP_HCRTC)));
		LOG(2,("DAC2: FP_HSYNC_S: %d\n", DAC2R(FP_HSYNC_S)));
		LOG(2,("DAC2: FP_HSYNC_E: %d\n", DAC2R(FP_HSYNC_E)));
		LOG(2,("DAC2: FP_HVALID_S:%d\n", DAC2R(FP_HVALID_S)));
		LOG(2,("DAC2: FP_HVALID_E: %d\n", DAC2R(FP_HVALID_E)));

		LOG(2,("DAC2: FP_VDISPEND: %d\n", DAC2R(FP_VDISPEND)));
		LOG(2,("DAC2: FP_VTOTAL: %d\n", DAC2R(FP_VTOTAL)));
		LOG(2,("DAC2: FP_VCRTC: %d\n", DAC2R(FP_VCRTC)));
		LOG(2,("DAC2: FP_VSYNC_S: %d\n", DAC2R(FP_VSYNC_S)));
		LOG(2,("DAC2: FP_VSYNC_E: %d\n", DAC2R(FP_VSYNC_E)));
		LOG(2,("DAC2: FP_VVALID_S: %d\n", DAC2R(FP_VVALID_S)));
		LOG(2,("DAC2: FP_VVALID_E: %d\n", DAC2R(FP_VVALID_E)));

		LOG(2,("DAC2: FP_CHKSUM: $%08x = (dec) %d\n", DAC2R(FP_CHKSUM),DAC2R(FP_CHKSUM)));
		LOG(2,("DAC2: FP_TST_CTRL: $%08x\n", DAC2R(FP_TST_CTRL)));
		LOG(2,("DAC2: FP_TG_CTRL: $%08x\n", DAC2R(FP_TG_CTRL)));
		LOG(2,("DAC2: FP_DEBUG0: $%08x\n", DAC2R(FP_DEBUG0)));
		LOG(2,("DAC2: FP_DEBUG1: $%08x\n", DAC2R(FP_DEBUG1)));
		LOG(2,("DAC2: FP_DEBUG2: $%08x\n", DAC2R(FP_DEBUG2)));
		LOG(2,("DAC2: FP_DEBUG3: $%08x\n", DAC2R(FP_DEBUG3)));

		LOG(2,("DAC2: FUNCSEL: $%08x\n", ENG_REG32(RG32_2FUNCSEL)));
		LOG(2,("DAC2: PANEL_PWR: $%08x\n", ENG_REG32(RG32_2PANEL_PWR)));
	}
	LOG(2,("INFO: End flatpanel registers dump.\n"));
}

static void setup_output_matrix()
{
	/* setup defaults: */
	/* no monitors (output devices) detected */
	si->ps.monitors = 0x00;
	/* head 1 will be the primary head */
	si->ps.crtc2_prim = false;

	/* setup output devices and heads */
	if (0)//si->ps.secondary_head)
	{
			/* setup defaults: */
			/* connect analog outputs straight through */
			eng_general_output_select(false);

			/* presetup by the card's BIOS, we can't change this (lack of info) */
			if (si->ps.tmds1_active) si->ps.monitors |= 0x01;
			if (si->ps.tmds2_active) si->ps.monitors |= 0x10;
			/* detect analog monitors: */
			/* sense analog monitor on primary connector */
			if (eng_dac_crt_connected()) si->ps.monitors |= 0x02;
			/* sense analog monitor on secondary connector */
			if (eng_dac2_crt_connected()) si->ps.monitors |= 0x20;

			/* setup correct output and head use */
			//fixme? add TVout (only, so no CRT(s) connected) support...
			switch (si->ps.monitors)
			{
			case 0x00: /* no monitor found at all */
				LOG(2,("INFO: head 1 has nothing connected;\n"));
				LOG(2,("INFO: head 2 has nothing connected:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			case 0x01: /* digital panel on head 1, nothing on head 2 */
				LOG(2,("INFO: head 1 has a digital panel;\n"));
				LOG(2,("INFO: head 2 has nothing connected:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			case 0x02: /* analog panel or CRT on head 1, nothing on head 2 */
				LOG(2,("INFO: head 1 has an analog panel or CRT;\n"));
				LOG(2,("INFO: head 2 has nothing connected:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			case 0x03: /* both types on head 1, nothing on head 2 */
				LOG(2,("INFO: head 1 has a digital panel AND an analog panel or CRT;\n"));
				LOG(2,("INFO: head 2 has nothing connected:\n"));
				LOG(2,("INFO: correcting...\n"));
				/* cross connect analog outputs so analog panel or CRT gets head 2 */
				eng_general_output_select(true);
				LOG(2,("INFO: head 1 has a digital panel;\n"));
				LOG(2,("INFO: head 2 has an analog panel or CRT:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			case 0x10: /* nothing on head 1, digital panel on head 2 */
				LOG(2,("INFO: head 1 has nothing connected;\n"));
				LOG(2,("INFO: head 2 has a digital panel:\n"));
				LOG(2,("INFO: defaulting to head 2 for primary use.\n"));
				si->ps.crtc2_prim = true;
				break;
			case 0x20: /* nothing on head 1, analog panel or CRT on head 2 */
				LOG(2,("INFO: head 1 has nothing connected;\n"));
				LOG(2,("INFO: head 2 has an analog panel or CRT:\n"));
				LOG(2,("INFO: defaulting to head 2 for primary use.\n"));
				si->ps.crtc2_prim = true;
				break;
			case 0x30: /* nothing on head 1, both types on head 2 */
				LOG(2,("INFO: head 1 has nothing connected;\n"));
				LOG(2,("INFO: head 2 has a digital panel AND an analog panel or CRT:\n"));
				LOG(2,("INFO: correcting...\n"));
				/* cross connect analog outputs so analog panel or CRT gets head 1 */
				eng_general_output_select(true);
				LOG(2,("INFO: head 1 has an analog panel or CRT;\n"));
				LOG(2,("INFO: head 2 has a digital panel:\n"));
				LOG(2,("INFO: defaulting to head 2 for primary use.\n"));
				si->ps.crtc2_prim = true;
				break;
			case 0x11: /* digital panels on both heads */
				LOG(2,("INFO: head 1 has a digital panel;\n"));
				LOG(2,("INFO: head 2 has a digital panel:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			case 0x12: /* analog panel or CRT on head 1, digital panel on head 2 */
				LOG(2,("INFO: head 1 has an analog panel or CRT;\n"));
				LOG(2,("INFO: head 2 has a digital panel:\n"));
				LOG(2,("INFO: defaulting to head 2 for primary use.\n"));
				si->ps.crtc2_prim = true;
				break;
			case 0x21: /* digital panel on head 1, analog panel or CRT on head 2 */
				LOG(2,("INFO: head 1 has a digital panel;\n"));
				LOG(2,("INFO: head 2 has an analog panel or CRT:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			case 0x22: /* analog panel(s) or CRT(s) on both heads */
				LOG(2,("INFO: head 1 has an analog panel or CRT;\n"));
				LOG(2,("INFO: head 2 has an analog panel or CRT:\n"));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			default: /* more than two monitors connected to just two outputs: illegal! */
				LOG(2,("INFO: illegal monitor setup ($%02x):\n", si->ps.monitors));
				LOG(2,("INFO: defaulting to head 1 for primary use.\n"));
				break;
			}
	}
	else /* singlehead cards */
	{
//temporary (VIA)
	si->ps.tmds1_active = false;
	si->ps.tmds2_active = false;


		/* presetup by the card's BIOS, we can't change this (lack of info) */
		if (si->ps.tmds1_active) si->ps.monitors |= 0x01;
		/* detect analog monitor (confirmed working OK on all cards): */
		/* sense analog monitor on primary connector */
//temporary (VIA)
		if (1/*eng_dac_crt_connected()*/) si->ps.monitors |= 0x02;

		//fixme? add TVout (only, so no CRT connected) support...
	}
}

void get_panel_modes(display_mode *p1, display_mode *p2, bool *pan1, bool *pan2)
{
	if (si->ps.tmds1_active)
	{
		/* timing ('modeline') */
		p1->timing = si->ps.p1_timing;
		/* setup the rest */
		p1->space = B_CMAP8;
		p1->virtual_width = p1->timing.h_display;
		p1->virtual_height = p1->timing.v_display;
		p1->h_display_start = 0;
		p1->v_display_start = 0;
		p1->flags = 0;
		*pan1 = true;
	}
	else
		*pan1 = false;

	if (si->ps.tmds2_active)
	{
		/* timing ('modeline') */
		p2->timing = si->ps.p2_timing;
		/* setup the rest */
		p2->space = B_CMAP8;
		p2->virtual_width = p2->timing.h_display;
		p2->virtual_height = p2->timing.v_display;
		p2->h_display_start = 0;
		p2->v_display_start = 0;
		p2->flags = 0;
		*pan2 = true;
	}
	else
		*pan2 = false;
}

static void pins_cle266_fake(void)
{
	si->ps.f_ref = 14.31818;
	/* we have a standard PLL */
	si->ps.ext_pll = false;
	/* carefull not to take to high limits, and high should be >= 2x low. */
	si->ps.max_system_vco = 230;
	si->ps.min_system_vco = 20;
	si->ps.max_pixel_vco = 400; /* VESA BIOS uses upto 433Mhz */
	si->ps.min_pixel_vco = 50; /* VESA BIOS uses downto 53.2Mhz */
	si->ps.max_video_vco = 0;
	si->ps.min_video_vco = 0;
	si->ps.max_dac1_clock = 230;
	si->ps.max_dac1_clock_8 = 230;
	si->ps.max_dac1_clock_16 = 230;
	/* 'failsave' values */
	si->ps.max_dac1_clock_24 = 200;
	si->ps.max_dac1_clock_32 = 180;
	si->ps.max_dac1_clock_32dh = 180;
	/* secondary head */
	si->ps.max_dac2_clock = 0;
	si->ps.max_dac2_clock_8 = 0;
	si->ps.max_dac2_clock_16 = 0;
	si->ps.max_dac2_clock_24 = 0;
	si->ps.max_dac2_clock_32 = 0;
	/* 'failsave' values */
	si->ps.max_dac2_clock_32dh = 0;
	//fixme: primary & secondary_dvi should be overrule-able via skel.settings
	si->ps.primary_dvi = false;
	si->ps.secondary_dvi = false;
	/* not used (yet) because no coldstart will be attempted (yet) */
	si->ps.std_engine_clock = 90;
	si->ps.std_memory_clock = 110;
}

static void pins_unichrome_pro_fake(void)
{
	si->ps.f_ref = 14.31818;
	si->ps.ext_pll = true;
	si->ps.max_system_vco = 400;
	si->ps.min_system_vco = 50;
	si->ps.max_pixel_vco = 800;
	si->ps.min_pixel_vco = 200;
	si->ps.max_video_vco = 0;
	si->ps.min_video_vco = 0;
	si->ps.max_dac1_clock = 300;
	si->ps.max_dac1_clock_8 = 300;
	si->ps.max_dac1_clock_16 = 300;
	si->ps.max_dac1_clock_24 = 250;
	si->ps.max_dac1_clock_32 = 250;
	si->ps.max_dac1_clock_32dh = 250;
	si->ps.max_dac2_clock = 0;
	si->ps.max_dac2_clock_8 = 0;
	si->ps.max_dac2_clock_16 = 0;
	si->ps.max_dac2_clock_24 = 0;
	si->ps.max_dac2_clock_32 = 0;
	si->ps.max_dac2_clock_32dh = 0;
	si->ps.primary_dvi = false;
	si->ps.secondary_dvi = false;
	si->ps.std_engine_clock = 150;
	si->ps.std_memory_clock = 150;
}

static void pins_chrome9_fake(void)
{
	si->ps.f_ref = 14.31818;
	si->ps.ext_pll = true;
	si->ps.max_system_vco = 600;
	si->ps.min_system_vco = 100;
	si->ps.max_pixel_vco = 1000;
	si->ps.min_pixel_vco = 300;
	si->ps.max_video_vco = 0;
	si->ps.min_video_vco = 0;
	si->ps.max_dac1_clock = 350;
	si->ps.max_dac1_clock_8 = 350;
	si->ps.max_dac1_clock_16 = 350;
	si->ps.max_dac1_clock_24 = 350;
	si->ps.max_dac1_clock_32 = 350;
	si->ps.max_dac1_clock_32dh = 350;
	si->ps.max_dac2_clock = 0;
	si->ps.max_dac2_clock_8 = 0;
	si->ps.max_dac2_clock_16 = 0;
	si->ps.max_dac2_clock_24 = 0;
	si->ps.max_dac2_clock_32 = 0;
	si->ps.max_dac2_clock_32dh = 0;
	si->ps.primary_dvi = false;
	si->ps.secondary_dvi = false;
	si->ps.std_engine_clock = 200;
	si->ps.std_memory_clock = 200;
}


/* Parse the BIOS PINS structure if there */
status_t parse_pins ()
{
	uint8 *rom;
	status_t result = B_ERROR;

	/* preset PINS read status to failed */
	si->ps.pins_status = B_ERROR;

	/* check the validity of PINS */
	LOG(2,("INFO: Reading PINS info\n"));
	rom = (uint8 *) si->rom_mirror;
	/* check BIOS signature - this is defined in the PCI standard */
	if (rom[0]!=0x55 || rom[1]!=0xaa)
	{
		LOG(8,("INFO: BIOS signature not found\n"));
		return B_ERROR;
	}
	LOG(2,("INFO: BIOS signature $AA55 found OK\n"));

	/* find the PINS struct adress */
	//fixme...

	/* check PINS read result */
	if (result == B_ERROR)
	{
		LOG(8,("INFO: PINS read/decode/execute error\n"));
		return B_ERROR;
	}
	/* PINS scan succeeded */
	si->ps.pins_status = B_OK;
	LOG(2,("INFO: PINS scan completed succesfully\n"));
	return B_OK;
}

//fixme: move to crtc sourcefile, also setup for crtc2(?)
static status_t	eng_crtc_setup_fifo()
{
	/* enable access to primary head */
	set_crtc_owner(0);

	//fixme: setup according to colordepth and RAM bus width...
	/* set CRTC FIFO burst size to 256 */
	CRTCW(FIFO, 0x03);

	/* set CRTC FIFO low watermark to 32 */
	CRTCW(FIFO_LWM, 0x20);

	return B_OK;
}


static void getRAMsize(void)
{
	uint8 ram_size = 0;

	/* If memory size was already probed by kernel driver, keep it */
	if (si->ps.memory_size > 0)
	{
		LOG(2,("INFO: using kernel driver probed RAM size (%d MB)\n", (int)(si->ps.memory_size / (1024 * 1024))));
		return;
	}

	if (si->ps.card_arch == CLE266)
	{
		ram_size = SEQR(MSIZE_CLE266);
	}
	else
	{
		ram_size = SEQR(MSIZE_OTHER);
	}

	if ((ram_size > 16) && (ram_size <= 128))
	{
		/* 9.0 - 64.5Mb in 0.5Mb steps */
		si->ps.memory_size = (ram_size + 1) * 512 * 1024;
	}
	else
	{
		if ((ram_size > 0) && (ram_size <= 16))
		{
			/* 4 - 64Mb in 4Mb steps */
			si->ps.memory_size = ram_size * 4 * 1024 * 1024;
		}
		else
		{
			if (si->ps.card_arch >= K8M890)
				si->ps.memory_size = 128 * 1024 * 1024;
			else if (si->ps.card_arch >= K8M800)
				si->ps.memory_size = 64 * 1024 * 1024;
			else
				si->ps.memory_size = 16 * 1024 * 1024;
			LOG(8,("INFO: unable to detect RAMsize (read $%02x), assuming %dMb\n", ram_size, (int)(si->ps.memory_size / (1024 * 1024))));
		}
	}
}

void dump_pins(void)
{
	char *msg = "";

	LOG(2,("INFO: pinsdump follows:\n"));
	LOG(2,("PLL type: "));
	if (si->ps.ext_pll) LOG(2,("extended\n")); else LOG(2,("standard\n"));
	LOG(2,("f_ref: %fMhz\n", si->ps.f_ref));
	LOG(2,("max_system_vco: %dMhz\n", si->ps.max_system_vco));
	LOG(2,("min_system_vco: %dMhz\n", si->ps.min_system_vco));
	LOG(2,("max_pixel_vco: %dMhz\n", si->ps.max_pixel_vco));
	LOG(2,("min_pixel_vco: %dMhz\n", si->ps.min_pixel_vco));
	LOG(2,("max_video_vco: %dMhz\n", si->ps.max_video_vco));
	LOG(2,("min_video_vco: %dMhz\n", si->ps.min_video_vco));
	LOG(2,("std_engine_clock: %dMhz\n", si->ps.std_engine_clock));
	LOG(2,("std_memory_clock: %dMhz\n", si->ps.std_memory_clock));
	LOG(2,("max_dac1_clock: %dMhz\n", si->ps.max_dac1_clock));
	LOG(2,("max_dac1_clock_8: %dMhz\n", si->ps.max_dac1_clock_8));
	LOG(2,("max_dac1_clock_16: %dMhz\n", si->ps.max_dac1_clock_16));
	LOG(2,("max_dac1_clock_24: %dMhz\n", si->ps.max_dac1_clock_24));
	LOG(2,("max_dac1_clock_32: %dMhz\n", si->ps.max_dac1_clock_32));
	LOG(2,("max_dac1_clock_32dh: %dMhz\n", si->ps.max_dac1_clock_32dh));
	LOG(2,("max_dac2_clock: %dMhz\n", si->ps.max_dac2_clock));
	LOG(2,("max_dac2_clock_8: %dMhz\n", si->ps.max_dac2_clock_8));
	LOG(2,("max_dac2_clock_16: %dMhz\n", si->ps.max_dac2_clock_16));
	LOG(2,("max_dac2_clock_24: %dMhz\n", si->ps.max_dac2_clock_24));
	LOG(2,("max_dac2_clock_32: %dMhz\n", si->ps.max_dac2_clock_32));
	LOG(2,("max_dac2_clock_32dh: %dMhz\n", si->ps.max_dac2_clock_32dh));
	LOG(2,("secondary_head: "));
	if (si->ps.secondary_head) LOG(2,("present\n")); else LOG(2,("absent\n"));
	LOG(2,("tvout: "));
	if (si->ps.tvout) LOG(2,("present\n")); else LOG(2,("absent\n"));
	/* setup TVout logmessage text */
	switch (si->ps.tvout_chip_type)
	{
	case NONE:
		msg = "No";
		break;
	case CH7003:
		msg = "Chrontel CH7003";
		break;
	case CH7004:
		msg = "Chrontel CH7004";
		break;
	case CH7005:
		msg = "Chrontel CH7005";
		break;
	case CH7006:
		msg = "Chrontel CH7006";
		break;
	case CH7007:
		msg = "Chrontel CH7007";
		break;
	case CH7008:
		msg = "Chrontel CH7008";
		break;
	case SAA7102:
		msg = "Philips SAA7102";
		break;
	case SAA7103:
		msg = "Philips SAA7103";
		break;
	case SAA7104:
		msg = "Philips SAA7104";
		break;
	case SAA7105:
		msg = "Philips SAA7105";
		break;
	case BT868:
		msg = "Brooktree/Conexant BT868";
		break;
	case BT869:
		msg = "Brooktree/Conexant BT869";
		break;
	case CX25870:
		msg = "Conexant CX25870";
		break;
	case CX25871:
		msg = "Conexant CX25871";
		break;
	default:
		msg = "Unknown";
		break;
	}
	LOG(2, ("%s TVout chip detected\n", msg));
//	LOG(2,("primary_dvi: "));
//	if (si->ps.primary_dvi) LOG(2,("present\n")); else LOG(2,("absent\n"));
//	LOG(2,("secondary_dvi: "));
//	if (si->ps.secondary_dvi) LOG(2,("present\n")); else LOG(2,("absent\n"));
	LOG(2,("card memory_size: %3.3fMb\n", (si->ps.memory_size / (1024.0 * 1024.0))));
	LOG(2,("laptop: "));
	if (si->ps.laptop) LOG(2,("yes\n")); else LOG(2,("no\n"));
	if (si->ps.tmds1_active)
	{
		LOG(2,("found DFP (digital flatpanel) on CRTC1; CRTC1 is "));
		if (si->ps.slaved_tmds1) LOG(2,("slaved\n")); else LOG(2,("master\n"));
		LOG(2,("panel width: %d, height: %d, aspect ratio: %1.2f\n",
			si->ps.p1_timing.h_display, si->ps.p1_timing.v_display, si->ps.panel1_aspect));
	}
	if (si->ps.tmds2_active)
	{
		LOG(2,("found DFP (digital flatpanel) on CRTC2; CRTC2 is "));
		if (si->ps.slaved_tmds2) LOG(2,("slaved\n")); else LOG(2,("master\n"));
		LOG(2,("panel width: %d, height: %d, aspect ratio: %1.2f\n",
			si->ps.p2_timing.h_display, si->ps.p2_timing.v_display, si->ps.panel2_aspect));
	}
	LOG(2,("monitor (output devices) setup matrix: $%02x\n", si->ps.monitors));
	LOG(2,("INFO: end pinsdump.\n"));
}
