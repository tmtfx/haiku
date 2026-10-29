/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 *
 * Hardware behavior ported from the OpenSoundSystem v4.2 driver for
 * this chipset, kernel/drv/oss_ymf7xx/{oss_ymf7xx.c,ymf7xx.h}, which
 * carries this notice:
 *
 *   Copyright (c) 1996-2008, 4Front Technologies
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *    1 Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *    2 Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 *   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 *   OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *   SUCH DAMAGE.
 */
#ifndef _DEV_PCI_YMF7XX_H_
#define _DEV_PCI_YMF7XX_H_

#include <Drivers.h>
#include <SupportDefs.h>
#include <OS.h>
#include <PCI.h>
#include "ymf7xxreg.h"
#include "config.h"
#include "queue.h"
#include "hmulti_audio.h"
#include "multi.h"


#define YAMAHA_VENDOR_ID		0x1073
#define YAMAHA_YMF724_ID		0x0004
#define YAMAHA_YMF734_ID		0x0005
#define YAMAHA_YMF740_ID		0x000a
#define YAMAHA_YMF740C_ID		0x000c
#define YAMAHA_YMF724F_ID		0x000d
#define YAMAHA_YMF744_ID		0x0010
#define YAMAHA_YMF754_ID		0x0012

#define VERSION "Version alpha 1, Copyright (c) 2026 Pirati Del Frico, compiled on " __DATE__ " " __TIME__
#define DRIVER_NAME "ymf7xx"
#define FRIENDLY_NAME "Yamaha DS-XG"
#define AUTHOR "Pirati Del Frico"

#define	YMF7XX_USE_PLAY		(1 << 0)
#define	YMF7XX_USE_RECORD	(1 << 1)
#define YMF7XX_STATE_STARTED	(1 << 0)

/*
 * ymf7xx memory management
 */

typedef struct _ymf7xx_mem {
	LIST_ENTRY(_ymf7xx_mem) next;
	void		*log_base;
	phys_addr_t	phy_base;
	area_id		area;
	size_t		size;
} ymf7xx_mem;

/*
 * Streams
 */

typedef struct _ymf7xx_stream {
	struct _ymf7xx_dev	*card;
	uint8				use;
	uint8				state;
	uint8				b16;
	uint32				sample_rate;
	uint8				channels;
	uint32				bufframes;
	uint8				bufcount;

	LIST_ENTRY(_ymf7xx_stream)	next;

	void				(*inth)(void *);
	void				*inthparam;

	ymf7xx_mem			*buffer;
	uint16				blksize;	/* bytes per fragment */
	uint16				trigblk;	/* fragment on which to trigger inth */
	uint16				blkmod;		/* modulo value to wrap trigblk */
	uint8				dacfmt;		/* bytes per frame */

	/* playback: one voice per channel (L, and R if stereo), each voice
	 * has two hardware ping-pong banks that are always kept identical */
	ymf7xx_play_bank	*lbank1, *lbank2;
	ymf7xx_play_bank	*rbank1, *rbank2;

	/* record: fixed ADC slot (banks 3/4 in the vendor numbering) */
	ymf7xx_rec_bank		*recbank3, *recbank4;

	/* effect bus 2/3, required by the native playback path even with
	 * S/PDIF/AC3 passthrough disabled -- see ymf7xx_setup_effect_slot() */
	ymf7xx_effect_bank	*effbank7, *effbank8, *effbank9, *effbank10;

	/* multi_audio */
	volatile int64		frames_count;	/* for play or record */
	volatile bigtime_t	real_time;		/* for play or record */
	volatile int32		buffer_cycle;	/* for play or record */
	int32				first_channel;
	bool				update_needed;
} ymf7xx_stream;

/*
 * Devices
 */

typedef struct _ymf7xx_dev {
	char		name[DEVNAME];	/* used for resources */
	pci_info	info;
	device_config config;

	/* play table: pointer table (devc->tab equivalent) + bank storage */
	void		*play_table_log;
	phys_addr_t	play_table_phys;
	area_id		play_table_area;
	uint32		*tab;			/* pointer table, tab[0] = slot count */
	void		*slt;			/* bank storage area, within play_table_log */
	phys_addr_t	slt_phys;

	/* effect table: bank storage for the effect bus */
	void		*effect_table_log;
	phys_addr_t	effect_table_phys;
	area_id		effect_table_area;

	/* effect scratch buffer the effect banks point at */
	void		*eff_buf_log;
	phys_addr_t	eff_buf_phys;
	area_id		eff_buf_area;

	/* record table: bank storage for the ADC/S-PDIF-in capture path */
	void		*rec_table_log;
	phys_addr_t	rec_table_phys;
	area_id		rec_table_area;

	sem_id		buffer_ready_sem;

	LIST_HEAD(, _ymf7xx_stream) streams;
	LIST_HEAD(, _ymf7xx_mem) mems;

	ymf7xx_stream	*pstream;
	ymf7xx_stream	*rstream;

	/* multi_audio */
	multi_dev	multi;
} ymf7xx_dev;


#define YMF7XX_SETTINGS "ymf7xx.settings"

typedef struct {
	uint32	sample_rate;
	uint32	buffer_frames;
	int32	buffer_count;
} ymf7xx_settings;

extern ymf7xx_settings current_settings;

extern int32 num_cards;
extern ymf7xx_dev cards[NUM_CARDS];

status_t ymf7xx_stream_set_audioparms(ymf7xx_stream *stream, uint8 channels,
			     uint8 b16, uint32 sample_rate);
status_t ymf7xx_stream_commit_parms(ymf7xx_stream *stream);
status_t ymf7xx_stream_get_nth_buffer(ymf7xx_stream *stream, uint8 chan, uint8 buf,
					char** buffer, size_t *stride);
void ymf7xx_stream_start(ymf7xx_stream *stream, void (*inth) (void *), void *inthparam);
void ymf7xx_stream_halt(ymf7xx_stream *stream);
ymf7xx_stream *ymf7xx_stream_new(ymf7xx_dev *card, uint8 use, uint32 bufframes, uint8 bufcount);
void ymf7xx_stream_delete(ymf7xx_stream *stream);

#endif /* _DEV_PCI_YMF7XX_H_ */
