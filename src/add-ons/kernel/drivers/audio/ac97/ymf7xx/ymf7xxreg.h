/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Register map and hardware bank layouts transcribed from the Yamaha
 * DS-1/DS-1E register map as documented in OpenSoundSystem v4.2 (BSD),
 * kernel/drv/oss_ymf7xx/ymf7xx.h (Copyright (C) 4Front Technologies
 * 1996-2008, released under the BSD license).
 */
#ifndef _YMF7XXREG_H_
#define _YMF7XXREG_H_

/* Global Control Register */
#define YMF7XX_INTERRUPT_FLAG		0x04
#define YMF7XX_ACTIVITY			0x06
#define YMF7XX_GLOBAL_CONTROL		0x08

/* Timer Control Register */
#define YMF7XX_TIMER_CONTROL		0x10
#define YMF7XX_TIMER_COUNT			0x12

/* SPDIF Control Register (unused: SPDIF is out of scope for this driver) */
#define YMF7XX_SPDIFOUT_CONTROL		0x18
#define YMF7XX_SPDIFOUT_STATUS		0x1c
#define YMF7XX_SPDIFIN_CONTROL		0x34
#define YMF7XX_SPDIFIN_STATUS		0x38

/* EEPROM Control Register */
#define YMF7XX_EEPROM_CONTROL		0x2c

/* AC3 Control Register (unused: AC3 passthrough is out of scope) */
#define YMF7XX_AC3_DATA				0x40
#define YMF7XX_AC3_ADDRESS			0x42
#define YMF7XX_AC3_STATUS			0x44

/* AC97 Control Register */
#define YMF7XX_AC97_CMD_DATA		0x60
#define YMF7XX_AC97_CMD_ADDRESS		0x62
#define YMF7XX_AC97_STATUS_DATA		0x64
#define YMF7XX_AC97_STATUS_ADDRESS	0x66
#define YMF7XX_AC97_SEC_STATUS_DATA	0x68
#define YMF7XX_AC97_SEC_STATUS_ADDR	0x6A
#define YMF7XX_AC97_SEC_CONFIG		0x70

/* Volume/bus routing registers */
#define YMF7XX_LEGACY_OUTPUT		0x80
#define YMF7XX_NATIVE_DAC			0x84
#define YMF7XX_ZVOUTVOL				0x88
#define YMF7XX_AC3_OUTPUT			0x8c
#define YMF7XX_ADC_OUTPUT			0x90
#define YMF7XX_LEGACY_LOOPBACK		0x94
#define YMF7XX_NATIVE_LOOPBACK		0x98
#define YMF7XX_ZVLOOPVOL			0x9c
#define YMF7XX_AC3_LOOPBACK			0xa0
#define YMF7XX_ADC_LOOPBACK			0xa4
#define YMF7XX_NATIVE_ADC_INPUT		0xa8
#define YMF7XX_NATIVE_REC_INPUT		0xac
#define YMF7XX_BUF441OUTVOL			0xb0
#define YMF7XX_BUF441LOOPVOL		0xb4
#define YMF7XX_SPDIFOUTVOL			0xb8
#define YMF7XX_SPDIFLOOPVOL			0xbc

/* Sampling Rate Control Register (ADC/record path only; playback rate is
 * set per-voice via PLAY_BANK.PgDelta, not through a register) */
#define YMF7XX_ADC_SAMPLING_RATE	0xc0
#define YMF7XX_REC_SAMPLING_RATE	0xc4
#define YMF7XX_ADC_FORMAT			0xc8
#define YMF7XX_REC_FORMAT			0xcc

/* PCI Native Audio Control Register (the DS-1 engine control block) */
#define YMF7XX_STATUS				0x100
#define YMF7XX_CONTROL_SELECT		0x104
#define YMF7XX_MODE					0x108
#define YMF7XX_SAMPLE_COUNT			0x10c
#define YMF7XX_NUM_OF_SAMPLES		0x110
#define YMF7XX_CONFIG				0x114
#define YMF7XX_PLAY_CNTRL_SIZE		0x140
#define YMF7XX_REC_CNTRL_SIZE		0x144
#define YMF7XX_EFF_CNTRL_SIZE		0x148
#define YMF7XX_WORK_SIZE			0x14c
#define YMF7XX_MAP_OF_REC			0x150
#define YMF7XX_MAP_OF_EFFECTS		0x154
#define YMF7XX_PLAY_CNTRL_BASE		0x158
#define YMF7XX_REC_CNTRL_BASE		0x15c
#define YMF7XX_EFF_CNTRL_BASE		0x160
#define YMF7XX_WORK_BASE			0x164

/* Microsequencer instruction RAM */
#define YMF7XX_DSP_INST_RAM			0x1000
#define YMF7XX_CONTROL_INST_RAM		0x4000

/* MMIO window size (BAR0) */
#define YMF7XX_MAP_LENGTH			0x8000

/* DMA control-table areas, sized/placed as in the OSS reference driver */
#define YMF7XX_PLAY_TABLE_SIZE		0x1000	/* play pointer table + banks */
#define YMF7XX_EFF_TABLE_SIZE		1024	/* effect bank storage */
#define YMF7XX_REC_TABLE_SIZE		1024	/* record bank storage */
#define YMF7XX_EFF_BUF_SIZE			(2 * 8192)	/* effect scratch buffer */
#define YMF7XX_PLAY_SLOT_COUNT		20	/* total hw voice slots, tab[0] */
#define YMF7XX_PLAY_BANK_STRIDE		0x120	/* bytes between banks in slt area */

/*
 * Per-voice playback state block. This struct is walked directly by the
 * on-chip microsequencer as a flat array of 32-bit words at a fixed
 * stride -- field order, count, and width must match exactly. Do not
 * reorder, add, or remove fields.
 */
typedef struct {
	uint32	Format;
	uint32	LoopDefault;
	uint32	PgBase;
	uint32	PgLoop;
	uint32	PgLoopEnd;
	uint32	PgLoopFrac;
	uint32	PgDeltaEnd;
	uint32	LpfKEnd;
	uint32	EgGainEnd;
	uint32	LchGainEnd;
	uint32	RchGainEnd;
	uint32	Effect1GainEnd;
	uint32	Effect2GainEnd;
	uint32	Effect3GainEnd;
	uint32	LpfQ;
	uint32	Status;
	uint32	NumOfFrames;
	uint32	LoopCount;
	uint32	PgStart;
	uint32	PgStartFrac;
	uint32	PgDelta;
	uint32	LpfK;
	uint32	EgGain;
	uint32	LchGain;
	uint32	RchGain;
	uint32	Effect1Gain;
	uint32	Effect2Gain;
	uint32	Effect3Gain;
	uint32	LpfDelay1;
	uint32	LpfDelay2;
} ymf7xx_play_bank;

/* Per-slot capture state block (ADC or S/PDIF-in; we only use the ADC
 * slot, see ymf7xx_setup_rec_slot() in ymf7xx.c). Same byte-exact
 * requirement as ymf7xx_play_bank. */
typedef struct {
	uint32	PgBase;
	uint32	PgLoopEndAdr;
	uint32	PgStartAdr;
	uint32	NumOfLoops;
} ymf7xx_rec_bank;

/* Effect-bus state block. We only set up the "effect 2/3" pair
 * (banks 7-10 in the vendor numbering) because the native playback path
 * requires them to be live even when no S/PDIF/AC3 passthrough is used
 * -- see the comment above ymf7xx_setup_effect_slot() in ymf7xx.c. */
typedef struct {
	uint32	PgBase;
	uint32	PgLoopEnd;
	uint32	PgStart;
	uint32	Temp;
} ymf7xx_effect_bank;

#endif /* _YMF7XXREG_H_ */
