/* registers definitions and macros for access to them */

/* documentation can be found at http://unichrome.sourceforge.net/
   and OpenChrome project: https://www.freedesktop.org/wiki/Openchrome/
   Credits and Copyright:
   Copyright 1998-2003 VIA Technologies, Inc.
   Copyright 2001-2003 S3 Graphics, Inc.
   Copyright 2004-2005 The Unichrome Project
   Copyright 2005-2016 The OpenChrome Project
   Authors: Kevin Brace, James Simmons, Xavier Bachelot, Thomas Hellström, Luc Verhaegen
*/

/* PCI_config_space */
#define ENCFG_DEVID		0x00
#define ENCFG_DEVCTRL	0x04
#define ENCFG_CLASS		0x08
#define ENCFG_HEADER	0x0c
#define ENCFG_BASE1REGS	0x10
#define ENCFG_BASE2FB	0x14
#define ENCFG_BASE3		0x18
#define ENCFG_BASE4		0x1c //unknown if used
#define ENCFG_BASE5		0x20 //unknown if used
#define ENCFG_BASE6		0x24 //unknown if used
#define ENCFG_BASE7		0x28 //unknown if used
#define ENCFG_SUBSYSID1	0x2c
#define ENCFG_ROMBASE	0x30
#define ENCFG_CAPPTR	0x34
#define ENCFG_CFG_1		0x38 //unknown if used
#define ENCFG_INTERRUPT	0x3c
#define ENCFG_SUBSYSID2	0x40
#define ENCFG_AGPREF	0x44
#define ENCFG_AGPSTAT	0x48
#define ENCFG_AGPCMD	0x4c
#define ENCFG_ROMSHADOW	0x50
#define ENCFG_VGA		0x54
#define ENCFG_SCHRATCH	0x58
#define ENCFG_CFG_10	0x5c
#define ENCFG_CFG_11	0x60
#define ENCFG_CFG_12	0x64
#define ENCFG_CFG_13	0x68 //unknown if used
#define ENCFG_CFG_14	0x6c //unknown if used
#define ENCFG_CFG_15	0x70 //unknown if used
#define ENCFG_CFG_16	0x74 //unknown if used
#define ENCFG_CFG_17	0x78 //unknown if used
#define ENCFG_CFG_18	0x7c //unknown if used
#define ENCFG_CFG_19	0x80 //unknown if used
#define ENCFG_CFG_20	0x84 //unknown if used
#define ENCFG_CFG_21	0x88 //unknown if used
#define ENCFG_CFG_22	0x8c //unknown if used
#define ENCFG_CFG_23	0x90 //unknown if used
#define ENCFG_CFG_24	0x94 //unknown if used
#define ENCFG_CFG_25	0x98 //unknown if used
#define ENCFG_CFG_26	0x9c //unknown if used
#define ENCFG_CFG_27	0xa0 //unknown if used
#define ENCFG_CFG_28	0xa4 //unknown if used
#define ENCFG_CFG_29	0xa8 //unknown if used
#define ENCFG_CFG_30	0xac //unknown if used
#define ENCFG_CFG_31	0xb0 //unknown if used
#define ENCFG_CFG_32	0xb4 //unknown if used
#define ENCFG_CFG_33	0xb8 //unknown if used
#define ENCFG_CFG_34	0xbc //unknown if used
#define ENCFG_CFG_35	0xc0 //unknown if used
#define ENCFG_CFG_36	0xc4 //unknown if used
#define ENCFG_CFG_37	0xc8 //unknown if used
#define ENCFG_CFG_38	0xcc //unknown if used
#define ENCFG_CFG_39	0xd0 //unknown if used
#define ENCFG_CFG_40	0xd4 //unknown if used
#define ENCFG_CFG_41	0xd8 //unknown if used
#define ENCFG_CFG_42	0xdc //unknown if used
#define ENCFG_CFG_43	0xe0 //unknown if used
#define ENCFG_CFG_44	0xe4 //unknown if used
#define ENCFG_CFG_45	0xe8 //unknown if used
#define ENCFG_CFG_46	0xec //unknown if used
#define ENCFG_CFG_47	0xf0 //unknown if used
#define ENCFG_CFG_48	0xf4 //unknown if used
#define ENCFG_CFG_49	0xf8 //unknown if used
#define ENCFG_CFG_50	0xfc //unknown if used

/* used INT registers for vblank */
#define RG32_MAIN_INTE		0x00000140
#define RG32_CRTC_INTS		0x00600100
#define RG32_CRTC_INTE		0x00600140

/* ACCeleration registers */
/* engine initialisation registers */
#define ENACC_FORMATS		0x00400618
#define ENACC_OFFSET0		0x00400640
#define ENACC_OFFSET1		0x00400644
#define ENACC_OFFSET2		0x00400648
#define ENACC_OFFSET3		0x0040064c
#define ENACC_OFFSET4		0x00400650
#define ENACC_OFFSET5		0x00400654
#define ENACC_BBASE0		0x00400658
#define ENACC_BBASE1		0x0040065c
#define ENACC_BBASE2		0x00400660
#define ENACC_BBASE3		0x00400664
#define ENACC_PITCH0		0x00400670
#define ENACC_PITCH1		0x00400674
#define ENACC_PITCH2		0x00400678
#define ENACC_PITCH3		0x0040067c
#define ENACC_PITCH4		0x00400680
#define ENACC_BLIMIT0		0x00400684
#define ENACC_BLIMIT1		0x00400688
#define ENACC_BLIMIT2		0x0040068c
#define ENACC_BLIMIT3		0x00400690
#define ENACC_BPIXEL		0x00400724
#define ENACC_STRD_FMT		0x00400830

/* specials */
#define	ENACC_DEBUG0 		0x00400080
#define	ENACC_DEBUG1 		0x00400084
#define	ENACC_DEBUG2		0x00400088
#define	ENACC_DEBUG3		0x0040008c
#define ENACC_ACC_INTS		0x00400100
#define ENACC_ACC_INTE		0x00400140
#define ENACC_STATUS		0x00400700
#define ENACC_FIFO_EN		0x00400720
#define ENACC_PAT_SHP		0x00400810
/* PGRAPH cache registers */
#define	ENACC_CACHE1_1		0x00400160
#define	ENACC_CACHE1_2		0x00400180
#define	ENACC_CACHE1_3		0x004001a0
#define	ENACC_CACHE1_4		0x004001c0
#define	ENACC_CACHE1_5		0x004001e0
#define	ENACC_CACHE2_1		0x00400164
#define	ENACC_CACHE2_2		0x00400184
#define	ENACC_CACHE2_3		0x004001a4
#define	ENACC_CACHE2_4		0x004001c4
#define	ENACC_CACHE2_5		0x004001e4
#define	ENACC_CACHE3_1		0x00400168
#define	ENACC_CACHE3_2		0x00400188
#define	ENACC_CACHE3_3		0x004001a8
#define	ENACC_CACHE3_4		0x004001c8
#define	ENACC_CACHE3_5		0x004001e8
#define	ENACC_CACHE4_1		0x0040016c
#define	ENACC_CACHE4_2		0x0040018c
#define	ENACC_CACHE4_3		0x004001ac
#define	ENACC_CACHE4_4		0x004001cc
#define	ENACC_CACHE4_5		0x004001ec
#define	ENACC_CACHE5_2		0x00400190
#define	ENACC_CACHE5_3		0x004001b0
#define	ENACC_CACHE5_4		0x004001d0
#define	ENACC_CACHE5_5		0x004001f0
#define	ENACC_CACHE6_2		0x00400194
#define	ENACC_CACHE6_3		0x004001b4
#define	ENACC_CACHE6_4		0x004001d4
#define	ENACC_CACHE6_5		0x004001f4
#define	ENACC_CACHE7_2		0x00400198
#define	ENACC_CACHE7_3		0x004001b8
#define	ENACC_CACHE7_4		0x004001d8
#define	ENACC_CACHE7_5		0x004001f8
#define	ENACC_CACHE8_2		0x0040019c
#define	ENACC_CACHE8_3		0x004001bc
#define	ENACC_CACHE8_4		0x004001dc
#define	ENACC_CACHE8_5		0x004001fc
/* engine tile registers src */
/* engine tile registers dst */
/* cache setup registers */
#define ENACC_PF_INTSTAT	0x00002100
#define ENACC_PF_INTEN		0x00002140
#define ENACC_PF_RAMHT		0x00002210
#define ENACC_PF_RAMFC		0x00002214
#define ENACC_PF_RAMRO		0x00002218
#define ENACC_PF_CACHES		0x00002500
#define ENACC_PF_SIZE		0x0000250c
#define ENACC_PF_CACH0_PSH0	0x00003000
#define ENACC_PF_CACH0_PUL0	0x00003050
#define ENACC_PF_CACH0_PUL1	0x00003054
#define ENACC_PF_CACH1_PSH0	0x00003200
#define ENACC_PF_CACH1_PSH1	0x00003204
#define ENACC_PF_CACH1_DMAI	0x0000322c
#define ENACC_PF_CACH1_PUL0	0x00003250
#define ENACC_PF_CACH1_PUL1 0x00003254
#define ENACC_PF_CACH1_HASH	0x00003258
/* Ptimer registers */
#define ENACC_PT_INTSTAT	0x00009100
#define ENACC_PT_INTEN		0x00009140
#define ENACC_PT_NUMERATOR	0x00009200
#define ENACC_PT_DENOMINATR	0x00009210
/* used PRAMIN registers */
#define ENACC_PR_CTX0_R		0x00711400
#define ENACC_PR_CTX1_R		0x00711404
#define ENACC_PR_CTX2_R		0x00711408
#define ENACC_PR_CTX3_R		0x0071140c
#define ENACC_PR_CTX0_0		0x00711420
#define ENACC_PR_CTX1_0		0x00711424
#define ENACC_PR_CTX2_0		0x00711428
#define ENACC_PR_CTX3_0		0x0071142c
#define ENACC_PR_CTX0_1		0x00711430
#define ENACC_PR_CTX1_1		0x00711434
#define ENACC_PR_CTX2_1		0x00711438
#define ENACC_PR_CTX3_1		0x0071143c
#define ENACC_PR_CTX0_2		0x00711440
#define ENACC_PR_CTX1_2		0x00711444
#define ENACC_PR_CTX2_2		0x00711448
#define ENACC_PR_CTX3_2		0x0071144c
#define ENACC_PR_CTX0_3		0x00711450
#define ENACC_PR_CTX1_3		0x00711454
#define ENACC_PR_CTX2_3		0x00711458
#define ENACC_PR_CTX3_3		0x0071145c
#define ENACC_PR_CTX0_4		0x00711460
#define ENACC_PR_CTX1_4		0x00711464
#define ENACC_PR_CTX2_4		0x00711468
#define ENACC_PR_CTX3_4		0x0071146c
#define ENACC_PR_CTX0_5		0x00711470
#define ENACC_PR_CTX1_5		0x00711474
#define ENACC_PR_CTX2_5		0x00711478
#define ENACC_PR_CTX3_5		0x0071147c
#define ENACC_PR_CTX0_6		0x00711480
#define ENACC_PR_CTX1_6		0x00711484
#define ENACC_PR_CTX2_6		0x00711488
#define ENACC_PR_CTX3_6		0x0071148c
#define ENACC_PR_CTX0_7		0x00711490
#define ENACC_PR_CTX1_7		0x00711494
#define ENACC_PR_CTX2_7		0x00711498
#define ENACC_PR_CTX3_7		0x0071149c
#define ENACC_PR_CTX0_8		0x007114a0
#define ENACC_PR_CTX1_8		0x007114a4
#define ENACC_PR_CTX2_8		0x007114a8
#define ENACC_PR_CTX3_8		0x007114ac
#define ENACC_PR_CTX0_9		0x007114b0
#define ENACC_PR_CTX1_9		0x007114b4
#define ENACC_PR_CTX2_9		0x007114b8
#define ENACC_PR_CTX3_9		0x007114bc
#define ENACC_PR_CTX0_A		0x007114c0
#define ENACC_PR_CTX1_A		0x007114c4 /* not used */
#define ENACC_PR_CTX2_A		0x007114c8
#define ENACC_PR_CTX3_A		0x007114cc
#define ENACC_PR_CTX0_B		0x007114d0
#define ENACC_PR_CTX1_B		0x007114d4
#define ENACC_PR_CTX2_B		0x007114d8
#define ENACC_PR_CTX3_B		0x007114dc
#define ENACC_PR_CTX0_C		0x007114e0
#define ENACC_PR_CTX1_C		0x007114e4
#define ENACC_PR_CTX2_C		0x007114e8
#define ENACC_PR_CTX3_C		0x007114ec
#define ENACC_PR_CTX0_D		0x007114f0
#define ENACC_PR_CTX1_D		0x007114f4
#define ENACC_PR_CTX2_D		0x007114f8
#define ENACC_PR_CTX3_D		0x007114fc
#define ENACC_PR_CTX0_E		0x00711500
#define ENACC_PR_CTX1_E		0x00711504
#define ENACC_PR_CTX2_E		0x00711508
#define ENACC_PR_CTX3_E		0x0071150c
/* used RAMHT registers (hash-table(?)) */
#define ENACC_HT_HANDL_00	0x00710000
#define ENACC_HT_VALUE_00	0x00710004
#define ENACC_HT_HANDL_01	0x00710008
#define ENACC_HT_VALUE_01	0x0071000c
#define ENACC_HT_HANDL_02	0x00710010
#define ENACC_HT_VALUE_02	0x00710014
#define ENACC_HT_HANDL_03	0x00710018
#define ENACC_HT_VALUE_03	0x0071001c
#define ENACC_HT_HANDL_04	0x00710020
#define ENACC_HT_VALUE_04	0x00710024
#define ENACC_HT_HANDL_05	0x00710028
#define ENACC_HT_VALUE_05	0x0071002c
#define ENACC_HT_HANDL_06	0x00710030
#define ENACC_HT_VALUE_06	0x00710034
#define ENACC_HT_HANDL_10	0x00710080
#define ENACC_HT_VALUE_10	0x00710084
#define ENACC_HT_HANDL_11	0x00710088
#define ENACC_HT_VALUE_11	0x0071008c
#define ENACC_HT_HANDL_12	0x00710090
#define ENACC_HT_VALUE_12	0x00710094
#define ENACC_HT_HANDL_13	0x00710098
#define ENACC_HT_VALUE_13	0x0071009c
#define ENACC_HT_HANDL_14	0x007100a0
#define ENACC_HT_VALUE_14	0x007100a4
#define ENACC_HT_HANDL_15	0x007100a8
#define ENACC_HT_VALUE_15	0x007100ac
#define ENACC_HT_HANDL_16	0x007100b0
#define ENACC_HT_VALUE_16	0x007100b4
#define ENACC_HT_HANDL_17	0x007100b8
#define ENACC_HT_VALUE_17	0x007100bc

/* acc engine fifo setup registers (for function_register 'mappings') */
#define	ENACC_FIFO_00800000	0x00800000
#define	ENACC_FIFO_00802000	0x00802000
#define	ENACC_FIFO_00804000	0x00804000
#define	ENACC_FIFO_00806000	0x00806000
#define	ENACC_FIFO_00808000	0x00808000
#define	ENACC_FIFO_0080a000	0x0080a000
#define	ENACC_FIFO_0080c000	0x0080c000
#define	ENACC_FIFO_0080e000	0x0080e000

/* ROP3 registers (Raster OPeration) */
#define RG16_ROP_FIFOFREE	0x00800010 /* little endian */
#define ENACC_ROP_ROP3		0x00800300 /* 'mapped' from 0x00420300 */

/* clip registers */
#define RG16_CLP_FIFOFREE	0x00802010 /* little endian */
#define ENACC_CLP_TOPLEFT	0x00802300 /* 'mapped' from 0x00450300 */
#define ENACC_CLP_WIDHEIGHT	0x00802304 /* 'mapped' from 0x00450304 */

/* pattern registers */
#define RG16_PAT_FIFOFREE	0x00804010 /* little endian */
#define ENACC_PAT_SHAPE		0x00804308 /* 'mapped' from 0x00460308 */
#define ENACC_PAT_COLOR0	0x00804310 /* 'mapped' from 0x00460310 */
#define ENACC_PAT_COLOR1	0x00804314 /* 'mapped' from 0x00460314 */
#define ENACC_PAT_MONO1		0x00804318 /* 'mapped' from 0x00460318 */
#define ENACC_PAT_MONO2		0x0080431c /* 'mapped' from 0x0046031c */

/* blit registers */
#define RG16_BLT_FIFOFREE	0x00808010 /* little endian */
#define ENACC_BLT_TOPLFTSRC	0x00808300 /* 'mapped' from 0x00500300 */
#define ENACC_BLT_TOPLFTDST	0x00808304 /* 'mapped' from 0x00500304 */
#define ENACC_BLT_SIZE		0x00808308 /* 'mapped' from 0x00500308 */

/* used bitmap registers */
#define RG16_BMP_FIFOFREE	0x0080a010 /* little endian */
#define ENACC_BMP_COLOR1A	0x0080a3fc /* 'mapped' from 0x006b03fc */
#define ENACC_BMP_UCRECTL_0	0x0080a400 /* 'mapped' from 0x006b0400 */
#define ENACC_BMP_UCRECSZ_0	0x0080a404 /* 'mapped' from 0x006b0404 */

/* PCI direct registers */
#define RG32_PWRUPCTRL		0x00000200
#define RG32_DUALHEAD_CTRL	0x000010f0//verify!!!
#define RG8_VSE2			0x000c03c3

/* bootstrap info registers */
#define RG32_PFB_CONFIG_0	0x00100200
#define RG32_PFB_CONFIG_1	0x00100204
#define RG32_FB_MRS1		0x001002c0
#define RG32_FB_MRS2		0x001002c8

/* registers needed for 'coldstart' */
#define RG32_PFB_DEBUG_0	0x00100080
#define RG32_PFB_REFCTRL	0x00100210
#define RG32_COREPLL		0x00680500
#define RG32_MEMPLL			0x00680504
#define RG32_PLL_CTRL		0x00680510
#define RG32_CONFIG         0x00600804

//new via
/* primary head */
#define RG8_ATTRINDW		0x000083c0
#define RG8_ATTRDATW		0x000083c0
#define RG8_ATTRDATR		0x000083c1
#define RG8_MISCW 			0x000083c2
#define RG8_MISCR 			0x000083cc
#define RG8_SEQIND			0x000083c4
#define RG16_SEQIND			0x000083c4
#define RG8_SEQDAT			0x000083c5
#define RG8_GRPHIND			0x000083ce
#define RG16_GRPHIND		0x000083ce
#define RG8_GRPHDAT			0x000083cf
#define RG8_CRTCIND			0x000083d4
#define RG16_CRTCIND		0x000083d4
#define RG8_CRTCDAT			0x000083d5
#define RG8_INSTAT1			0x000083da
//end via.
#define RG32_RASTER			0x00600808
#define RG32_CURCONF		0x00600810
#define RG32_PANEL_PWR		0x0060081c
#define RG32_FUNCSEL		0x00600860

/* secondary head */
#define RG8_ATTR2INDW		0x006033c0
#define RG8_ATTR2DATW		0x006033c0
#define RG8_ATTR2DATR		0x006033c1
#define RG8_CRTC2IND		0x006033d4
#define RG16_CRTC2IND		0x006033d4
#define RG8_CRTC2DAT		0x006033d5
#define RG8_2INSTAT1		0x006033da//verify!!!
#define RG32_RASTER2		0x00602808
#define RG32_2CURCONF		0x00602810
#define RG32_2PANEL_PWR		0x0060281c//verify!!!
#define RG32_2FUNCSEL		0x00602860

/* DAC direct registers (standard VGA palette RAM registers) */
/* primary head */
//via new:
#define RG8_PALMASK			0x000083c6
#define RG8_PALINDR			0x000083c7
#define RG8_PALINDW			0x000083c8
#define RG8_PALDATA			0x000083c9
//end via new.
/* secondary head */
#define RG8_PAL2MASK		0x006833c6
#define RG8_PAL2INDR		0x006833c7
#define RG8_PAL2INDW		0x006833c8
#define RG8_PAL2DATA		0x006833c9

/* PCI direct DAC registers (32bit) */
/* primary head */
#define ENDAC_CURPOS		0x00680300
#define ENDAC_PIXPLLC		0x00680508
#define ENDAC_PLLSEL		0x0068050c
#define ENDAC_OUTPUT		0x0068052c
#define ENDAC_PIXPLLC2		0x00680578
#define ENDAC_GENCTRL		0x00680600
#define ENDAC_TSTCTRL		0x00680608
#define ENDAC_TSTDATA		0x00680610
#define ENDAC_TV_SETUP		0x00680700
/* (flatpanel registers: confirmed for TNT2 and up) */
#define ENDAC_FP_VDISPEND	0x00680800
#define ENDAC_FP_VTOTAL		0x00680804
#define ENDAC_FP_VCRTC		0x00680808
#define ENDAC_FP_VSYNC_S	0x0068080c
#define ENDAC_FP_VSYNC_E	0x00680810
#define ENDAC_FP_VVALID_S	0x00680814
#define ENDAC_FP_VVALID_E	0x00680818
#define ENDAC_FP_HDISPEND	0x00680820
#define ENDAC_FP_HTOTAL		0x00680824
#define ENDAC_FP_HCRTC		0x00680828
#define ENDAC_FP_HSYNC_S	0x0068082c
#define ENDAC_FP_HSYNC_E	0x00680830
#define ENDAC_FP_HVALID_S	0x00680834
#define ENDAC_FP_HVALID_E	0x00680838
#define ENDAC_FP_CHKSUM		0x00680840
#define ENDAC_FP_TST_CTRL	0x00680844
#define ENDAC_FP_TG_CTRL	0x00680848
#define ENDAC_FP_DEBUG0		0x00680880
#define ENDAC_FP_DEBUG1		0x00680884
#define ENDAC_FP_DEBUG2		0x00680888
#define ENDAC_FP_DEBUG3		0x0068088c
/* secondary head */
#define ENDAC2_CURPOS		0x00682300
#define ENDAC2_PIXPLLC		0x00680520
#define ENDAC2_OUTPUT		0x0068252c
#define ENDAC2_PIXPLLC2		0x0068057c
#define ENDAC2_GENCTRL		0x00682600
#define ENDAC2_TSTCTRL		0x00682608
#define ENDAC2_TV_SETUP		0x00682700 //verify!!!
/* (flatpanel registers) */
#define ENDAC2_FP_VDISPEND	0x00682800
#define ENDAC2_FP_VTOTAL	0x00682804
#define ENDAC2_FP_VCRTC		0x00682808
#define ENDAC2_FP_VSYNC_S	0x0068280c
#define ENDAC2_FP_VSYNC_E	0x00682810
#define ENDAC2_FP_VVALID_S	0x00682814
#define ENDAC2_FP_VVALID_E	0x00682818
#define ENDAC2_FP_HDISPEND	0x00682820
#define ENDAC2_FP_HTOTAL	0x00682824
#define ENDAC2_FP_HCRTC		0x00682828
#define ENDAC2_FP_HSYNC_S	0x0068282c
#define ENDAC2_FP_HSYNC_E	0x00682830
#define ENDAC2_FP_HVALID_S	0x00682834
#define ENDAC2_FP_HVALID_E	0x00682838
#define ENDAC2_FP_CHKSUM	0x00682840
#define ENDAC2_FP_TST_CTRL	0x00682844
#define ENDAC2_FP_TG_CTRL	0x00682848
#define ENDAC2_FP_DEBUG0	0x00682880
#define ENDAC2_FP_DEBUG1	0x00682884
#define ENDAC2_FP_DEBUG2	0x00682888
#define ENDAC2_FP_DEBUG3	0x0068288c

//new via
/* VIA CRTC indexed registers */
/* VGA standard registers: */
#define ENCRTCX_HTOTAL		0x00
#define ENCRTCX_HDISPE		0x01
#define ENCRTCX_HBLANKS		0x02
#define ENCRTCX_HBLANKE		0x03
#define ENCRTCX_HSYNCS		0x04
#define ENCRTCX_HSYNCE		0x05
#define ENCRTCX_VTOTAL		0x06
#define ENCRTCX_OVERFLOW	0x07
#define ENCRTCX_PRROWSCN	0x08
#define ENCRTCX_MAXSCLIN	0x09
#define ENCRTCX_VGACURCTRL	0x0a
#define ENCRTCX_FBSTADDH	0x0c
#define ENCRTCX_FBSTADDL	0x0d
#define ENCRTCX_PITCHL		0x13
#define ENCRTCX_VSYNCS		0x10
#define ENCRTCX_VSYNCE		0x11
#define ENCRTCX_VDISPE		0x12
#define ENCRTCX_VBLANKS		0x15
#define ENCRTCX_VBLANKE		0x16
#define ENCRTCX_MODECTL		0x17
#define ENCRTCX_LINECOMP	0x18
/* VIA specific registers: */
#define ENCRTCX_0x32		0x32
#define ENCRTCX_HTIMEXT1	0x33
#define ENCRTCX_FBSTADDE	0x34
#define ENCRTCX_VTIMEXT_PIT	0x35
#define ENCRTCX_HTIMEXT2	0x36
#define ENCRTCX_TVTYPE		0x3b //TVtype jumper: b1=1 = PAL, else NTSC
#define ENCRTCX_MEMCLK		0x3d
#define ENCRTCX_CR45		0x45
#define ENCRTCX_CR47		0x47
#define ENCRTCX_CR48		0x48
//end via
/* Extended CRTC registers: */
#define ENCRTCX_REPAINT0	0x19
#define ENCRTCX_REPAINT1	0x1a
#define ENCRTCX_FIFO		0x1b
#define ENCRTCX_LOCK		0x1f
#define ENCRTCX_FIFO_LWM	0x20
#define ENCRTCX_BUFFER		0x21
#define ENCRTCX_LSR			0x25
#define ENCRTCX_PIXEL		0x28
#define ENCRTCX_HEB			0x2d
#define ENCRTCX_CURCTL2		0x2f
#define ENCRTCX_CURCTL1		0x30
#define ENCRTCX_CURCTL0		0x31
#define ENCRTCX_LCD			0x33
#define ENCRTCX_RMA			0x38
#define ENCRTCX_INTERLACE	0x39
#define ENCRTCX_TREG		0x3d
#define ENCRTCX_EXTRA		0x41
#define ENCRTCX_OWNER		0x44
#define ENCRTCX_FP_HTIMING	0x53
#define ENCRTCX_FP_VTIMING	0x54
#define ENCRTCX_0x59		0x59
#define ENCRTCX_0x9f		0x9f

//new via:
/* VIA ATTRIBUTE indexed registers */
/* VGA standard registers: */
#define ENATBX_MODECTL		0x10
#define ENATBX_OSCANCOLOR	0x11
#define ENATBX_COLPLANE_EN	0x12
#define ENATBX_HORPIXPAN	0x13
#define ENATBX_COLSEL		0x14
#define ENATBX_0x16			0x16

/* VIA SEQUENCER indexed registers */
/* VGA standard registers: */
#define ENSEQX_RESET		0x00
#define ENSEQX_CLKMODE		0x01
#define ENSEQX_MAPMASK		0x02
#define ENSEQX_MEMMODE		0x04
/* VIA specific registers: */
#define ENSEQX_LOCK			0x10
#define ENSEQX_0x14			0x14
#define ENSEQX_COLDEPTH		0x15
#define ENSEQX_FIFOWM		0x16
#define ENSEQX_FIFODEPTH	0x17
#define ENSEQX_0x18			0x18
#define ENSEQX_0x19			0x19
#define ENSEQX_MMIO_EN		0x1a
#define ENSEQX_0x1b			0x1b
#define ENSEQX_FETCHCNTLO	0x1c
#define ENSEQX_FETCHCNTHI	0x1d
#define ENSEQX_0x1e			0x1e
#define ENSEQX_0x1f			0x1f
#define ENSEQX_0x22			0x22
#define ENSEQX_0x23			0x23
#define ENSEQX_0x24			0x24
#define ENSEQX_0x25			0x25
#define ENSEQX_0x26			0x26 // ddc
#define ENSEQX_0x27			0x27
#define ENSEQX_0x28			0x28
#define ENSEQX_0x29			0x29
#define ENSEQX_0x2a			0x2a
#define ENSEQX_0x2b			0x2b
#define ENSEQX_0x2e			0x2e
#define ENSEQX_MSIZE_CLE266	0x34
#define ENSEQX_MSIZE_OTHER	0x39
#define ENSEQX_PLL_RESET	0x40
#define ENSEQX_CR5E			0x5e
#define ENSEQX_PPLL2_MP_CLE	0x44
#define ENSEQX_PPLL2_N_CLE	0x45
#define ENSEQX_PPLL_N_OTH	0x44
#define ENSEQX_PPLL_P_OTH	0x45
#define ENSEQX_PPLL_M_OTH	0x46
#define ENSEQX_PPLL_MP_CLE	0x46
#define ENSEQX_PPLL_N_CLE	0x47
#define ENSEQX_PPLL2_N_OTH	0x4a
#define ENSEQX_PPLL2_P_OTH	0x4b
#define ENSEQX_PPLL2_M_OTH	0x4c

/* VIA GRAPHICS indexed registers */
/* VGA standard registers: */
#define ENGRPHX_ENSETRESET	0x01
#define ENGRPHX_DATAROTATE	0x03
#define ENGRPHX_READMAPSEL	0x04
#define ENGRPHX_MODE		0x05
#define ENGRPHX_MISC		0x06
#define ENGRPHX_BITMASK		0x08

/* VIA BES (Back End Scaler) registers) */
#define ENBES_INTFLAG		0x00000200
#define ENBES_RAMTAB		0x00000204
#define ENBES_AWIN_HVSTART	0x00000208
#define ENBES_AWIN_SIZE		0x0000020c
#define ENBES_AWIN_CTL		0x00000210
#define ENBES_CRT_STARTADR	0x00000214
#define ENBES_CRT_STARTADR2	0x00000218
#define ENBES_ABUF_STRIDE	0x0000021c
#define ENBES_COLKEY		0x00000220
#define ENBES_ABUF_ADDR		0x00000224
#define ENBES_CHROMAKEY_LOW	0x00000228
#define ENBES_CHROMAKEY_HI	0x0000022c
#define ENBES_VID1_CTL		0x00000230
#define ENBES_VID1_FETCH	0x00000234
#define ENBES_VID1Y_ADDR1	0x00000238
#define ENBES_VID1_STRIDE	0x0000023c
#define ENBES_VID1_HVSTART	0x00000240
#define ENBES_VID1_SIZE		0x00000244
#define ENBES_VID1Y_ADDR2	0x00000248
#define ENBES_VID1_ZOOM		0x0000024c
#define ENBES_VID1_MINI_CTL	0x00000250
#define ENBES_VID1Y_ADDR0	0x00000254
#define ENBES_VID1_FIFO		0x00000258
#define ENBES_VID1Y_ADDR3	0x0000025c
#define ENBES_HI_CTRL		0x00000260
#define ENBES_SND_COLKEY	0x00000264
#define ENBES_V3A_PREFIFO	0x00000268
#define ENBES_V1_SOURCE_WH	0x0000026c
#define ENBES_HI_TRANSP_COL	0x00000270
#define ENBES_V3A_FIFO		0x00000278
#define ENBES_V3_SRC_WIDTH	0x0000027c
#define ENBES_V3_COLKEY		0x00000280
#define ENBES_VID1_COLSPAC1	0x00000284
#define ENBES_VID1_COLSPAC2	0x00000288
#define ENBES_VID1U_ADDR0	0x0000028c
#define ENBES_VID1_OPQ_CTL	0x00000290
#define ENBES_VID3_OPQ_CTL	0x00000294
#define ENBES_COMPOSE		0x00000298
#define ENBES_VID3_CTL		0x000002a0
#define ENBES_VID3_ADDR0	0x000002a4
#define ENBES_VID3_ADDR1	0x000002a8
#define ENBES_VID3_STRIDE	0x000002ac
#define ENBES_VID3_HVSTART	0x000002b0
#define ENBES_VID3_SIZE		0x000002b4
#define ENBES_V3A_FETCH		0x000002b8
#define ENBES_VID3_ZOOM		0x000002bc
#define ENBES_VID3_MINI_CTL	0x000002c0
#define ENBES_VID3_COLSPAC1	0x000002c4
#define ENBES_VID3_COLSPAC2	0x000002c8
#define ENBES_VID1U_ADDR1	0x000002e4
#define ENBES_VID1U_ADDR2	0x000002e8
#define ENBES_VID1U_ADDR3	0x000002ec
#define ENBES_VID1V_ADDR0	0x000002f0
#define ENBES_VID1V_ADDR1	0x000002f4
#define ENBES_VID1V_ADDR2	0x000002f8
#define ENBES_VID1V_ADDR3	0x000002fc
//end via new.

//old:
/*MAVEN registers (<= G400) */
#define ENMAV_PGM            0x3E
#define ENMAV_PIXPLLM        0x80
#define ENMAV_PIXPLLN        0x81
#define ENMAV_PIXPLLP        0x82
#define ENMAV_GAMMA1         0x83
#define ENMAV_GAMMA2         0x84
#define ENMAV_GAMMA3         0x85
#define ENMAV_GAMMA4         0x86
#define ENMAV_GAMMA5         0x87
#define ENMAV_GAMMA6         0x88
#define ENMAV_GAMMA7         0x89
#define ENMAV_GAMMA8         0x8A
#define ENMAV_GAMMA9         0x8B
#define ENMAV_MONSET         0x8C
#define ENMAV_TEST           0x8D
#define ENMAV_WREG_0X8E_L    0x8E
#define ENMAV_WREG_0X8E_H    0x8F
#define ENMAV_HSCALETV       0x90
#define ENMAV_TSCALETVL      0x91
#define ENMAV_TSCALETVH      0x92
#define ENMAV_FFILTER        0x93
#define ENMAV_MONEN          0x94
#define ENMAV_RESYNC         0x95
#define ENMAV_LASTLINEL      0x96
#define ENMAV_LASTLINEH      0x97
#define ENMAV_WREG_0X98_L    0x98
#define ENMAV_WREG_0X98_H    0x99
#define ENMAV_HSYNCLENL      0x9A
#define ENMAV_HSYNCLENH      0x9B
#define ENMAV_HSYNCSTRL      0x9C
#define ENMAV_HSYNCSTRH      0x9D
#define ENMAV_HDISPLAYL      0x9E
#define ENMAV_HDISPLAYH      0x9F
#define ENMAV_HTOTALL        0xA0
#define ENMAV_HTOTALH        0xA1
#define ENMAV_VSYNCLENL      0xA2
#define ENMAV_VSYNCLENH      0xA3
#define ENMAV_VSYNCSTRL      0xA4
#define ENMAV_VSYNCSTRH      0xA5
#define ENMAV_VDISPLAYL      0xA6
#define ENMAV_VDISPLAYH      0xA7
#define ENMAV_VTOTALL        0xA8
#define ENMAV_VTOTALH        0xA9
#define ENMAV_HVIDRSTL       0xAA
#define ENMAV_HVIDRSTH       0xAB
#define ENMAV_VVIDRSTL       0xAC
#define ENMAV_VVIDRSTH       0xAD
#define ENMAV_VSOMETHINGL    0xAE
#define ENMAV_VSOMETHINGH    0xAF
#define ENMAV_OUTMODE        0xB0
#define ENMAV_LOCK           0xB3
#define ENMAV_LUMA           0xB9
#define ENMAV_VDISPLAYTV     0xBE
#define ENMAV_STABLE         0xBF
#define ENMAV_HDISPLAYTV     0xC2
#define ENMAV_BREG_0XC6      0xC6
//end old.

//new via
#define ENCRTC_CURSOR_MODE	0x000002d0
#define ENCRTC_CURSOR_POS	0x000002d4
#define ENCRTC_CURSOR_ORG	0x000002d8
#define ENCRTC_CURSOR_BG	0x000002dc
#define ENCRTC_CURSOR_FG	0x000002e0
//end new.

/* Real "Hardware Icon" (HW cursor) registers for CX700/P4M890/P4M900/
 * VX800/VX855/VX900 ("Chrome9"). The ENCRTC_CURSOR_* block above is a
 * legacy ("Unichrome") register range that OpenChrome's own source never
 * references for any supported chip - it is dead on this generation.
 * Offsets taken from OpenChrome's via_eng_regs.h. */
#define ENCRTC_V327_HI_INVTCOLOR	0x000002e4
#define ENCRTC_PRIM_HI_FIFO		0x000002e8
#define ENCRTC_PRIM_HI_TRANSCOLOR	0x000002ec
#define ENCRTC_PRIM_HI_CTRL		0x000002f0
#define ENCRTC_PRIM_HI_FBOFFSET	0x000002f4
#define ENCRTC_PRIM_HI_POSSTART	0x000002f8
#define ENCRTC_PRIM_HI_CENTEROFFSET	0x000002fc
#define ENCRTC_PRIM_HI_INVTCOLOR	0x0000120c

/* Macros for convenient accesses to VIA hardware */
#define ENG_REG8(r_)  ((vuint8  *)regs)[(r_)]
#define ENG_REG16(r_) ((vuint16 *)regs)[(r_) >> 1]
#define ENG_REG32(r_) ((vuint32 *)regs)[(r_) >> 2]

/* read and write to PCI config space */
#define CFGR(A)   (eng_pci_access.offset=ENCFG_##A, ioctl(fd,ENG_GET_PCI, &eng_pci_access,sizeof(eng_pci_access)), eng_pci_access.value)
#define CFGW(A,B) (eng_pci_access.offset=ENCFG_##A, eng_pci_access.value = B, ioctl(fd,ENG_SET_PCI,&eng_pci_access,sizeof(eng_pci_access)))

/* read and write from ISA I/O space */
#define ISAWB(A,B)(eng_isa_access.adress=A, eng_isa_access.data = (uint8)B, eng_isa_access.size = 1, ioctl(fd,ENG_ISA_OUT, &eng_isa_access,sizeof(eng_isa_access)))
#define ISAWW(A,B)(eng_isa_access.adress=A, eng_isa_access.data = B, eng_isa_access.size = 2, ioctl(fd,ENG_ISA_OUT, &eng_isa_access,sizeof(eng_isa_access)))
#define ISARB(A)  (eng_isa_access.adress=A, ioctl(fd,ENG_ISA_IN, &eng_isa_access,sizeof(eng_isa_access)), (uint8)eng_isa_access.data)
#define ISARW(A)  (eng_isa_access.adress=A, ioctl(fd,ENG_ISA_IN, &eng_isa_access,sizeof(eng_isa_access)), eng_isa_access.data)

//new via
/* read and write from the dac registers */
#define CRTCDR(A)   (ENG_REG32(ENCRTC_##A))
#define CRTCDW(A,B) (ENG_REG32(ENCRTC_##A)=B)
//end new.

/* read and write from the dac registers */
#define DACR(A)   (ENG_REG32(ENDAC_##A))
#define DACW(A,B) (ENG_REG32(ENDAC_##A)=B)

/* read and write from the secondary dac registers */
#define DAC2R(A)   (ENG_REG32(ENDAC2_##A))
#define DAC2W(A,B) (ENG_REG32(ENDAC2_##A)=B)

/* read and write from the backend scaler registers */
#define BESR(A)   (ENG_REG32(ENBES_##A))
#define BESW(A,B) (ENG_REG32(ENBES_##A)=B)

/* read and write from CRTC indexed registers */
#define CRTCW(A,B)(ENG_REG16(RG16_CRTCIND) = ((ENCRTCX_##A) | ((B) << 8)))
#define CRTCR(A)  (ENG_REG8(RG8_CRTCIND) = (ENCRTCX_##A), ENG_REG8(RG8_CRTCDAT))

/* read and write from second CRTC indexed registers */
#define CRTC2W(A,B)(ENG_REG16(RG16_CRTC2IND) = ((ENCRTCX_##A) | ((B) << 8)))
#define CRTC2R(A)  (ENG_REG8(RG8_CRTC2IND) = (ENCRTCX_##A), ENG_REG8(RG8_CRTC2DAT))

/* read and write from ATTRIBUTE indexed registers */
#define ATBW(A,B)(ENG_REG8(RG8_INSTAT1), ENG_REG8(RG8_ATTRINDW) = ((ENATBX_##A) | 0x20), ENG_REG8(RG8_ATTRDATW) = (B))
#define ATBR(A)  (ENG_REG8(RG8_INSTAT1), ENG_REG8(RG8_ATTRINDW) = ((ENATBX_##A) | 0x20), ENG_REG8(RG8_ATTRDATR))

/* read and write from ATTRIBUTE indexed registers */
#define ATB2W(A,B)(ENG_REG8(RG8_INSTAT1), ENG_REG8(RG8_ATTR2INDW) = ((ENATBX_##A) | 0x20), ENG_REG8(RG8_ATTR2DATW) = (B))
#define ATB2R(A)  (ENG_REG8(RG8_INSTAT1), ENG_REG8(RG8_ATTR2INDW) = ((ENATBX_##A) | 0x20), ENG_REG8(RG8_ATTR2DATR))

/* read and write from SEQUENCER indexed registers */
#define SEQW(A,B)(ENG_REG16(RG16_SEQIND) = ((ENSEQX_##A) | ((B) << 8)))
#define SEQR(A)  (ENG_REG8(RG8_SEQIND) = (ENSEQX_##A), ENG_REG8(RG8_SEQDAT))

/* read and write from PCI GRAPHICS indexed registers */
#define GRPHW(A,B)(ENG_REG16(RG16_GRPHIND) = ((ENGRPHX_##A) | ((B) << 8)))
#define GRPHR(A)  (ENG_REG8(RG8_GRPHIND) = (ENGRPHX_##A), ENG_REG8(RG8_GRPHDAT))

/* read and write from the acceleration engine registers */
#define ACCR(A)    (ENG_REG32(ENACC_##A))
#define ACCW(A,B)  (ENG_REG32(ENACC_##A)=B)

//old:
/* read and write from maven (<= G400) */
#define MAVR(A)     (i2c_maven_read (ENMAV_##A ))
#define MAVW(A,B)   (i2c_maven_write(ENMAV_##A ,B))
#define MAVRW(A)    (i2c_maven_read (ENMAV_##A )|(i2c_maven_read(ENMAV_##A +1)<<8))
#define MAVWW(A,B)  (i2c_maven_write(ENMAV_##A ,B &0xFF),i2c_maven_write(ENMAV_##A +1,B >>8))

/* VIA 2D Engine definitions */
#define VIA_MMIO_BLTBASE        0x200000

/* H2 Engine Registers */
#define VIA_REG_GECMD           0x000
#define VIA_REG_GEMODE          0x004
#define VIA_REG_GESTATUS        0x004
#define VIA_REG_SRCPOS          0x008
#define VIA_REG_DSTPOS          0x00C
#define VIA_REG_DIMENSION       0x010
#define VIA_REG_FGCOLOR         0x018
#define VIA_REG_BGCOLOR         0x01C
#define VIA_REG_SRCBASE         0x030
#define VIA_REG_DSTBASE         0x034
#define VIA_REG_PITCH           0x038

/* H6 / M1 Engine Registers (Chrome9: VX800, VX855, VX900) */
#define VIA_REG_GECMD_M1        0x000
#define VIA_REG_GEMODE_M1       0x004
#define VIA_REG_GESTATUS_M1     0x004
#define VIA_REG_PITCH_M1        0x008
#define VIA_REG_DIMENSION_M1    0x00C
#define VIA_REG_DSTPOS_M1       0x010
#define VIA_REG_DSTBASE_M1      0x014
#define VIA_REG_SRCPOS_M1       0x018
#define VIA_REG_SRCBASE_M1      0x01C
#define VIA_REG_MONOPATFGC_M1   0x058

/* Engine status register, and H5/M1 (Chrome9: VX800, VX855, VX900) busy
 * bits. Address is relative to the MMIO BAR base (NOT VIA_MMIO_BLTBASE) -
 * see BLTR_M1/BLTW_M1 below. Taken from OpenChrome's via_regs.h. */
#define VIA_REG_STATUS          0x400
#define VIA_CMD_RGTR_BUSY_H5    0x00000010
#define VIA_2D_ENG_BUSY_H5      0x00000002

/* GEC Commands */
#define VIA_GEC_NOOP            0x00000000
#define VIA_GEC_BLT             0x00000001
#define VIA_GEC_FIXCOLOR_PAT    0x00002000
#define VIA_GEC_DECY            0x00004000
#define VIA_GEC_DECX            0x00008000
#define VIA_GEC_CLIP_ENABLE     0x00001000
#define VIA_PITCH_ENABLE        0x80000000

/* Raster Operations (ROP) */
#define VIA_ROP_S               0xCC
#define VIA_ROP_P               0xF0

/* Color Depth (GEMODE) */
#define VIA_GEM_8bpp            0x00000000
#define VIA_GEM_16bpp           0x00000100
#define VIA_GEM_32bpp           0x00000300

/* Macro for accessing 2D Engine (H2 generation and older) */
#define BLTR(A)   (ENG_REG32(VIA_MMIO_BLTBASE + (A)))
#define BLTW(A,B) (ENG_REG32(VIA_MMIO_BLTBASE + (A)) = (B))

/* Macro for accessing the H6/M1 2D Engine (Chrome9: VX800, VX855, VX900).
 * Unlike the H2 engine above, the H6/M1 command registers live at the base
 * of the MMIO BAR, NOT at VIA_MMIO_BLTBASE (+0x200000) - confirmed against
 * OpenChrome's viaFlushPCI(), which writes H6/M1 registers via a plain
 * VIASETREG(offset, value) on the base MMIO mapping; the separate BltBase
 * mapping is never dereferenced by any EXA (2D) code path there. */
#define BLTR_M1(A)   (ENG_REG32(A))
#define BLTW_M1(A,B) (ENG_REG32(A) = (B))
