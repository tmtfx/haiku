/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 *
 * The hardware bring-up sequence, register access protocol, and
 * microcode loading implemented below are ported from the
 * OpenSoundSystem v4.2 driver for this chipset,
 * kernel/drv/oss_ymf7xx/oss_ymf7xx.c, which carries this notice:
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
 *
 * This chip is not a classic AC'97 bus-master DMA controller: it is
 * Yamaha's DS-1/DS-1E wavetable/mixing DSP. PCM playback and capture are
 * carried on hardware "voice slots" processed once per internal timer
 * tick by an on-chip microsequencer (loaded from ymf7xx_ucode.h at
 * attach time), not by a classic buffer-descriptor-list DMA engine. The
 * sequencing below -- PCI reset, microcode upload, DSP start
 * confirmation, AC'97 bring-up, voice bank programming, and the
 * timer-tick interrupt -- is ported from the OpenSoundSystem v4.2 (BSD)
 * driver for this chipset (kernel/drv/oss_ymf7xx/oss_ymf7xx.c,
 * Copyright (C) 4Front Technologies 1996-2008, BSD licensed), which is
 * the only known-working reference implementation for this hardware.
 *
 * Assumes a little-endian (x86) host throughout -- the OSS reference's
 * LSWAP() byte-swap-for-big-endian-hosts macro is therefore omitted.
 *
 * Out of scope (see the project plan): S/PDIF, AC3 passthrough, the
 * legacy MPU-401/OPL3 FM I/O decode, and the secondary AC'97 codec.
 * The "effect bus 2/3" setup below is *not* part of that deferred scope,
 * even though it lives in the same hardware subsystem -- the OSS
 * reference programs it unconditionally before every playback stream,
 * AC3 or not, which indicates the native playback path depends on it
 * being live. If silence is the first bring-up result, this is the
 * first thing to double check with real hardware in hand.
 */

#include <KernelExport.h>
#include <PCI.h>
#include <driver_settings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ymf7xx.h"
#include "ymf7xx_ucode.h"
#include "debug.h"
#include "config.h"
#include "util.h"
#include "io.h"
#include <fcntl.h>
#include <unistd.h>
#include "ac97.h"

status_t init_hardware(void);
status_t init_driver(void);
void uninit_driver(void);
const char ** publish_devices(void);
device_hooks * find_device(const char *);
int32 ymf7xx_int(void *arg);
status_t ymf7xx_init(ymf7xx_dev * card);

pci_module_info	*pci;

int32 num_cards;
ymf7xx_dev cards[NUM_CARDS];
int32 num_names;
char * names[NUM_CARDS*20+1];

extern device_hooks multi_hooks;

ymf7xx_settings current_settings = {
	48000,	// sample rate
	512,	// buffer frames
	4,	// buffer count
};


static bool
is_ymf7xx(uint16 vendor, uint16 device)
{
	if (vendor != YAMAHA_VENDOR_ID)
		return false;
	switch (device) {
		case YAMAHA_YMF724_ID:
		case YAMAHA_YMF734_ID:
		case YAMAHA_YMF740_ID:
		case YAMAHA_YMF740C_ID:
		case YAMAHA_YMF724F_ID:
		case YAMAHA_YMF744_ID:
		case YAMAHA_YMF754_ID:
			return true;
	}
	return false;
}


static bool
is_ds1e(uint16 device)
{
	switch (device) {
		case YAMAHA_YMF724F_ID:
		case YAMAHA_YMF740C_ID:
		case YAMAHA_YMF744_ID:
		case YAMAHA_YMF754_ID:
			return true;
	}
	return false;
}


/* ymf7xx memory management */

static ymf7xx_mem *
ymf7xx_mem_new(ymf7xx_dev *card, size_t size)
{
	ymf7xx_mem *mem;

	if ((mem = malloc(sizeof(*mem))) == NULL)
		return (NULL);

	mem->area = alloc_mem(&mem->phy_base, &mem->log_base, size,
		"ymf7xx buffer", true);
	mem->size = size;
	if (mem->area < B_OK) {
		free(mem);
		return NULL;
	}
	return mem;
}


static void
ymf7xx_mem_delete(ymf7xx_mem *mem)
{
	if (mem->area > B_OK)
		delete_area(mem->area);
	free(mem);
}


static void *
ymf7xx_mem_alloc(ymf7xx_dev *card, size_t size)
{
	ymf7xx_mem *mem;

	mem = ymf7xx_mem_new(card, size);
	if (mem == NULL)
		return (NULL);

	LIST_INSERT_HEAD(&(card->mems), mem, next);

	return mem;
}


static void
ymf7xx_mem_free(ymf7xx_dev *card, void *ptr)
{
	ymf7xx_mem *mem;

	LIST_FOREACH(mem, &card->mems, next) {
		if (mem->log_base != ptr)
			continue;
		LIST_REMOVE(mem, next);

		ymf7xx_mem_delete(mem);
		break;
	}
}


/* per-voice bank field computation, ported from
 * SetFormatField/SetPgDeltaField/SetLpfKField/SetLpfQField in
 * oss_ymf7xx.c */

static uint32
ymf7xx_format_field(ymf7xx_stream *stream)
{
	uint32 val = 0;

	if (!stream->b16)
		val |= 0x80000000;
	if (stream->channels > 1)
		val |= 0x00010000;
	return val;
}


static uint32
ymf7xx_pgdelta_field(ymf7xx_stream *stream)
{
	uint32 x = (stream->sample_rate * (1 << 15) + 187) / 375;
	return (x * (1 << 6)) & 0x7FFFFF00;
}


static uint32
ymf7xx_lpfk_field(ymf7xx_stream *stream)
{
	static const uint32 freq[8] =
		{ 100, 2000, 8000, 11025, 16000, 22050, 32000, 48000 };
	static const uint32 lpfk[8] =
		{ 0x0057, 0x06aa, 0x18B2, 0x2093, 0x2b9a, 0x35a1, 0x3eaa, 0x4000 };
	uint32 sr = stream->sample_rate, i;

	if (sr == 44100)
		return 0x4646 << 16;
	for (i = 0; i < 8; i++) {
		if (sr <= freq[i])
			return lpfk[i] << 16;
	}
	return lpfk[7] << 16;
}


static uint32
ymf7xx_lpfq_field(ymf7xx_stream *stream)
{
	static const uint32 freq[8] =
		{ 100, 2000, 8000, 11025, 16000, 22050, 32000, 48000 };
	static const uint32 lpfq[8] =
		{ 0x3528, 0x34a7, 0x3202, 0x3177, 0x3139, 0x31c9, 0x33d0, 0x4000 };
	uint32 sr = stream->sample_rate, i;

	if (sr == 44100)
		return 0x370A << 16;
	for (i = 0; i < 8; i++) {
		if (sr <= freq[i])
			return lpfq[i] << 16;
	}
	return lpfq[7] << 16;
}


/*
 * Programs one playback voice bank pair (A and B, kept identical -- the
 * two banks are a hardware ping-pong the driver does not otherwise
 * manage). is_right selects which native-bus channel this voice feeds;
 * for mono streams both channels are fed by the single voice. Ported
 * from SetupPlayBank() in oss_ymf7xx.c.
 */
static void
ymf7xx_setup_play_bank(ymf7xx_stream *stream, bool is_right,
	ymf7xx_play_bank *bankA, ymf7xx_play_bank *bankB)
{
	memset(bankA, 0, sizeof(ymf7xx_play_bank));

	bankA->Format = ymf7xx_format_field(stream);
	bankA->EgGain = bankA->EgGainEnd = 0x40000000;

	if (stream->channels == 1) {
		bankA->LchGain = bankA->LchGainEnd = 0x40000000;
		bankA->RchGain = bankA->RchGainEnd = 0x40000000;
		bankA->Effect2Gain = bankA->Effect2GainEnd = 0x40000000;
		bankA->Effect3Gain = bankA->Effect3GainEnd = 0x40000000;
	} else if (is_right) {
		bankA->RchGain = bankA->RchGainEnd = 0x40000000;
		bankA->Effect2Gain = bankA->Effect2GainEnd = 0x40000000;
	} else {
		bankA->LchGain = bankA->LchGainEnd = 0x40000000;
		bankA->Effect3Gain = bankA->Effect3GainEnd = 0x40000000;
	}

	bankA->LoopDefault = 0;
	bankA->NumOfFrames = 0;
	bankA->LoopCount = 0;
	bankA->PgStart = 0;
	bankA->PgLoop = 0;

	bankA->PgBase = (uint32)stream->buffer->phy_base;
	bankA->PgLoopEnd = (uint32)(stream->buffer->size / stream->dacfmt);

	bankA->PgDelta = bankA->PgDeltaEnd = ymf7xx_pgdelta_field(stream);
	bankA->LpfK = bankA->LpfKEnd = ymf7xx_lpfk_field(stream);
	bankA->LpfQ = ymf7xx_lpfq_field(stream);

	memcpy(bankB, bankA, sizeof(ymf7xx_play_bank));
}


/*
 * Sets up the two playback voices (L, and R if stereo) for this stream
 * at fixed hardware voice-slot indices 1 (L) and 2 (R), and publishes
 * their physical addresses into the pointer table -- but does not yet
 * mark them active; that happens in ymf7xx_stream_start(). Mirrors
 * SetupPlaySlot()/SetupPlayBank() in oss_ymf7xx.c, specialized to the
 * single stereo stream this driver needs (OSS multiplexes up to 8
 * virtual devices across 20 slots; we only ever need the first one).
 */
static void
ymf7xx_setup_play_slot(ymf7xx_stream *stream)
{
	ymf7xx_dev *card = stream->card;
	device_config *config = &card->config;
	uint32 bank_stride;
	char *slot1, *slot2;

	bank_stride = ymf7xx_reg_read_32(config, YMF7XX_PLAY_CNTRL_SIZE)
		* sizeof(uint32);

	slot1 = (char *)card->slt + (1 * YMF7XX_PLAY_BANK_STRIDE);
	stream->lbank1 = (ymf7xx_play_bank *)slot1;
	stream->lbank2 = (ymf7xx_play_bank *)(slot1 + bank_stride);
	ymf7xx_setup_play_bank(stream, false, stream->lbank1, stream->lbank2);

	if (stream->channels > 1) {
		slot2 = (char *)card->slt + (2 * YMF7XX_PLAY_BANK_STRIDE);
		stream->rbank1 = (ymf7xx_play_bank *)slot2;
		stream->rbank2 = (ymf7xx_play_bank *)(slot2 + bank_stride);
		ymf7xx_setup_play_bank(stream, true, stream->rbank1, stream->rbank2);
	} else {
		stream->rbank1 = stream->rbank2 = NULL;
	}
}


/*
 * Required by the native playback path even with S/PDIF/AC3 disabled --
 * see the file-level comment above. Ported from the non-#if-0'd part of
 * SetupEffectSlot() in oss_ymf7xx.c: only the "effect 2/3" bank pair
 * (banks 7-10 in the vendor numbering) is set up, pointed at a small
 * scratch buffer; the dry-L/R and effect-1 banks (1-6) and the SPDIF/AC3
 * mixer controls are #if 0'd out in the reference too and are skipped
 * here as genuinely out of scope.
 */
static void
ymf7xx_setup_effect_slot(ymf7xx_dev *card)
{
	device_config *config = &card->config;
	uint32 bank_stride = ymf7xx_reg_read_32(config, YMF7XX_EFF_CNTRL_SIZE);
	char *base = (char *)card->effect_table_log;
	ymf7xx_effect_bank *bank7, *bank8, *bank9, *bank10;

	bank7 = (ymf7xx_effect_bank *)(base + bank_stride * 24);
	bank8 = (ymf7xx_effect_bank *)(base + bank_stride * 28);
	bank9 = (ymf7xx_effect_bank *)(base + bank_stride * 32);
	bank10 = (ymf7xx_effect_bank *)(base + bank_stride * 36);

	memset(bank7, 0, sizeof(*bank7));
	memset(bank8, 0, sizeof(*bank8));
	memset(bank9, 0, sizeof(*bank9));
	memset(bank10, 0, sizeof(*bank10));

	bank7->PgBase = bank8->PgBase = (uint32)card->eff_buf_phys + 0 * 8192;
	bank7->PgLoopEnd = bank8->PgLoopEnd = 4096;

	bank9->PgBase = bank10->PgBase = (uint32)card->eff_buf_phys + 1 * 8192;
	bank9->PgLoopEnd = bank10->PgLoopEnd = 4096;

	ymf7xx_reg_write_32(config, YMF7XX_AC97_SEC_CONFIG,
		(ymf7xx_reg_read_32(config, YMF7XX_AC97_SEC_CONFIG) & ~0x0030) | 0x0010);
	ymf7xx_reg_write_32(config, YMF7XX_MAP_OF_EFFECTS, 0x18);	/* effect 2,3 */
	ymf7xx_reg_write_32(config, YMF7XX_MODE,
		ymf7xx_reg_read_32(config, YMF7XX_MODE) | (1 << 30));
}


/*
 * Sets up the fixed analog-ADC capture slot (REC_BANK "banks 3/4" in the
 * vendor numbering; S/PDIF-in capture, "banks 1/2", is out of scope --
 * see MAP_OF_REC usage in ymf7xx_stream_start()). Ported from the
 * devc->spdif_in == 0 branch of SetupRecSlot() in oss_ymf7xx.c.
 */
static void
ymf7xx_setup_rec_slot(ymf7xx_stream *stream)
{
	ymf7xx_dev *card = stream->card;
	device_config *config = &card->config;
	uint32 bank_stride = ymf7xx_reg_read_32(config, YMF7XX_REC_CNTRL_SIZE);
	char *base = (char *)card->rec_table_log;
	uint8 bits = stream->b16 ? 16 : 8;

	stream->recbank3 = (ymf7xx_rec_bank *)(base + bank_stride * 8);
	stream->recbank4 = (ymf7xx_rec_bank *)(base + bank_stride * 12);

	memset(stream->recbank3, 0, sizeof(ymf7xx_rec_bank));
	memset(stream->recbank4, 0, sizeof(ymf7xx_rec_bank));

	ymf7xx_reg_write_8(config, YMF7XX_ADC_FORMAT,
		(uint8)((stream->channels & 0x02) + ((bits & 0x08) >> 3)));
	ymf7xx_reg_write_16(config, YMF7XX_ADC_SAMPLING_RATE,
		(uint16)(48000 * 4096 / stream->sample_rate) - 1);

	stream->recbank3->PgBase = stream->recbank4->PgBase
		= (uint32)stream->buffer->phy_base;
	stream->recbank3->PgLoopEndAdr = stream->recbank4->PgLoopEndAdr
		= (uint32)stream->buffer->size;
}


/* ymf7xx stream functions */

status_t
ymf7xx_stream_set_audioparms(ymf7xx_stream *stream, uint8 channels,
     uint8 b16, uint32 sample_rate)
{
	uint8 sample_size, frame_size;
	LOG(("ymf7xx_stream_set_audioparms\n"));

	if ((stream->channels == channels) &&
		(stream->b16 == b16) &&
		(stream->sample_rate == sample_rate))
		return B_OK;

	if (stream->buffer)
		ymf7xx_mem_free(stream->card, stream->buffer->log_base);

	stream->b16 = b16;
	stream->sample_rate = sample_rate;
	stream->channels = channels;

	sample_size = stream->b16 + 1;
	frame_size = sample_size * stream->channels;
	stream->dacfmt = frame_size;

	stream->buffer = ymf7xx_mem_alloc(stream->card,
		stream->bufframes * frame_size * stream->bufcount);

	stream->trigblk = 0;
	stream->blkmod = stream->bufcount;
	stream->blksize = stream->bufframes * frame_size;

	return B_OK;
}


status_t
ymf7xx_stream_commit_parms(ymf7xx_stream *stream)
{
	LOG(("ymf7xx_stream_commit_parms\n"));

	if (stream->use & YMF7XX_USE_RECORD) {
		ymf7xx_setup_rec_slot(stream);
	} else {
		ymf7xx_setup_effect_slot(stream->card);
		ymf7xx_setup_play_slot(stream);

		/* ymf7xx_dsp_init() leaves the native-DAC output bus muted
		 * (NATIVE_DAC = 0) as part of its startup sequence; it must be
		 * unmuted here, matching ymf7xx_audio_prepare_for_output() in
		 * oss_ymf7xx.c, or every voice plays silently through a
		 * zero-gain bus. (AC3 passthrough, which keeps this bus muted
		 * and routes through S/PDIF instead, is out of scope here.) */
		ymf7xx_reg_write_32(&stream->card->config, YMF7XX_NATIVE_DAC, 0xFFFFFFFF);
	}

	return B_OK;
}


status_t
ymf7xx_stream_get_nth_buffer(ymf7xx_stream *stream, uint8 chan, uint8 buf,
					char** buffer, size_t *stride)
{
	uint8 sample_size, frame_size;
	LOG(("ymf7xx_stream_get_nth_buffer\n"));

	sample_size = stream->b16 + 1;
	frame_size = sample_size * stream->channels;

	*buffer = (char *)stream->buffer->log_base
		+ (buf * stream->bufframes * frame_size) + chan * sample_size;
	*stride = frame_size;

	return B_OK;
}


/* returns the fragment index the hardware is currently playing/recording,
 * by reading the live position field out of the voice/ADC bank -- there
 * is no "buffer half/full" interrupt bit on this hardware, see the ISR */
static uint32
ymf7xx_stream_curblk(ymf7xx_stream *stream)
{
	uint32 pos;

	if (stream->use & YMF7XX_USE_RECORD)
		pos = stream->recbank3->PgStartAdr / stream->blksize;
	else
		pos = stream->lbank1->PgStart / stream->bufframes;

	if (pos >= stream->blkmod)
		pos = 0;
	return pos;
}


void
ymf7xx_stream_start(ymf7xx_stream *stream, void (*inth) (void *), void *inthparam)
{
	ymf7xx_dev *card = stream->card;
	device_config *config = &card->config;
	uint32 l_index = 1, r_index = 2;
	LOG(("ymf7xx_stream_start\n"));

	stream->inth = inth;
	stream->inthparam = inthparam;
	stream->state |= YMF7XX_STATE_STARTED;

	if (stream->use & YMF7XX_USE_RECORD) {
		ymf7xx_reg_write_32(config, YMF7XX_MAP_OF_REC, 0x2);	/* ADC slot */
		ymf7xx_reg_write_32(config, YMF7XX_CONTROL_SELECT, 0);
		ymf7xx_reg_write_32(config, YMF7XX_MODE,
			ymf7xx_reg_read_32(config, YMF7XX_MODE) | 0x00000003);
	} else {
		card->tab[l_index] = (uint32)card->slt_phys
			+ (l_index * YMF7XX_PLAY_BANK_STRIDE);
		if (stream->channels > 1)
			card->tab[r_index] = (uint32)card->slt_phys
				+ (r_index * YMF7XX_PLAY_BANK_STRIDE);

		ymf7xx_reg_write_32(config, YMF7XX_CONTROL_SELECT, 1);
		ymf7xx_reg_write_32(config, YMF7XX_MODE,
			ymf7xx_reg_read_32(config, YMF7XX_MODE) | 0x00000003);
	}
}


void
ymf7xx_stream_halt(ymf7xx_stream *stream)
{
	ymf7xx_dev *card = stream->card;
	device_config *config = &card->config;
	LOG(("ymf7xx_stream_halt\n"));

	stream->state &= ~YMF7XX_STATE_STARTED;

	if (stream->use & YMF7XX_USE_RECORD) {
		ymf7xx_reg_write_32(config, YMF7XX_MAP_OF_REC, 0x0);
	} else {
		card->tab[1] = 0;
		card->tab[2] = 0;
	}
}


ymf7xx_stream *
ymf7xx_stream_new(ymf7xx_dev *card, uint8 use, uint32 bufframes, uint8 bufcount)
{
	ymf7xx_stream *stream;
	cpu_status status;
	LOG(("ymf7xx_stream_new\n"));

	stream = malloc(sizeof(ymf7xx_stream));
	if (stream == NULL)
		return (NULL);
	memset(stream, 0, sizeof(ymf7xx_stream));
	stream->card = card;
	stream->use = use;
	stream->state = !YMF7XX_STATE_STARTED;
	stream->bufframes = bufframes;
	stream->bufcount = bufcount;

	status = lock();
	LIST_INSERT_HEAD((&card->streams), stream, next);
	unlock(status);

	return stream;
}


void
ymf7xx_stream_delete(ymf7xx_stream *stream)
{
	cpu_status status;
	LOG(("ymf7xx_stream_delete\n"));

	ymf7xx_stream_halt(stream);

	if (stream->buffer)
		ymf7xx_mem_free(stream->card, stream->buffer->log_base);

	status = lock();
	LIST_REMOVE(stream, next);
	unlock(status);

	free(stream);
}


/* ymf7xx interrupt
 *
 * There is a single global interrupt source: bit 31 of STATUS, raised
 * once per internal timer tick after the microsequencer has processed
 * all active voice/ADC banks. There is no per-stream interrupt bit, so
 * every open stream is checked on every tick, mirroring the
 * for-each-portc sweep in ymf7xxintr() in oss_ymf7xx.c.
 */
int32
ymf7xx_int(void *arg)
{
	ymf7xx_dev *card = arg;
	device_config *config = &card->config;
	ymf7xx_stream *stream;
	bool gotone = false;
	uint32 status, curblk;

	status = ymf7xx_reg_read_32(config, YMF7XX_STATUS);
	if ((status & 0x80000000) == 0)
		return B_UNHANDLED_INTERRUPT;

	LIST_FOREACH(stream, &card->streams, next) {
		if ((stream->state & YMF7XX_STATE_STARTED) == 0)
			continue;
		if ((stream->use & YMF7XX_USE_PLAY) && stream->lbank1 == NULL)
			continue;
		if ((stream->use & YMF7XX_USE_RECORD) && stream->recbank3 == NULL)
			continue;

		curblk = ymf7xx_stream_curblk(stream);
		if (curblk == stream->trigblk)
			continue;

		stream->trigblk = curblk;
		gotone = true;

		if (stream->inth)
			stream->inth(stream->inthparam);
	}

	ymf7xx_reg_write_32(config, YMF7XX_STATUS, 0x80000000);
	ymf7xx_reg_write_32(config, YMF7XX_MODE,
		ymf7xx_reg_read_32(config, YMF7XX_MODE) | 0x00000002);

	return gotone ? B_INVOKE_SCHEDULER : B_HANDLED_INTERRUPT;
}


/*
 * Uploads a microcode blob -- "len" 32-bit words from src -- into
 * on-chip instruction RAM starting at "addr". Ported verbatim from
 * install_ucode() in oss_ymf7xx.c.
 */
static void
ymf7xx_install_ucode(device_config *config, uint32 addr, const unsigned int *src,
	uint32 len)
{
	uint32 i;

	for (i = 0; i < len; i++) {
		ymf7xx_reg_write_32(config, addr, src[i]);
		addr += 4;
	}
}


/*
 * Resets the DS-1/DS-1E engine, uploads microcode, confirms the DSP
 * actually started, and attaches the AC'97 codec. Ported from the
 * non-audio-engine part of init_ymf7xx() in oss_ymf7xx.c. Returns B_OK
 * only if the DSP confirmed it started -- callers must not proceed to
 * programming voice/ADC banks otherwise.
 */
static status_t
ymf7xx_dsp_init(ymf7xx_dev *card)
{
	device_config *config = &card->config;
	int32 i;
	uint32 fEnd;

	ymf7xx_reg_write_32(config, YMF7XX_NATIVE_DAC, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_MODE, 0x00010000);
	ymf7xx_reg_write_32(config, YMF7XX_MODE, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_MAP_OF_REC, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_MAP_OF_EFFECTS, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_PLAY_CNTRL_BASE, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_REC_CNTRL_BASE, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_EFF_CNTRL_BASE, 0x00000000);
	ymf7xx_reg_write_32(config, YMF7XX_CONTROL_SELECT, 1);
	ymf7xx_reg_write_32(config, YMF7XX_GLOBAL_CONTROL,
		ymf7xx_reg_read_32(config, YMF7XX_GLOBAL_CONTROL) & ~0x0007);
	ymf7xx_reg_write_32(config, YMF7XX_ZVOUTVOL, 0xFFFFFFFF);
	ymf7xx_reg_write_32(config, YMF7XX_ZVLOOPVOL, 0xFFFFFFFF);
	ymf7xx_reg_write_32(config, YMF7XX_SPDIFOUTVOL, 0xFFFFFFFF);
	ymf7xx_reg_write_32(config, YMF7XX_SPDIFLOOPVOL, 0x3FFF3FFF);

	/* the codec may not be ready yet right after reset */
	if (ymf7xx_codec_read(config, 0x02) == 0xFFFF) {
		for (i = 0; i < 100; i++) {
			if (ymf7xx_codec_read(config, 0x02) != 0xFFFF)
				break;
		}
	}

	ymf7xx_install_ucode(config, YMF7XX_DSP_INST_RAM, dsp, dsp_size);
	if (config->is_ds1e)
		ymf7xx_install_ucode(config, YMF7XX_CONTROL_INST_RAM, cntrl1E, cntrl1E_size);
	else
		ymf7xx_install_ucode(config, YMF7XX_CONTROL_INST_RAM, cntrl, cntrl_size);

	ymf7xx_reg_write_32(config, YMF7XX_CONFIG, 1);

	/* extra reset to init the mixers */
	ymf7xx_codec_write(config, 0x02, 0x0000);
	ymf7xx_codec_write(config, 0x18, 0x0808);

	/* confirm the DSP actually started: this loop waits for the busy
	 * bit to drop, not to rise -- porting it the other way around is a
	 * correctness bug, not just a style nit */
	i = 10000;
	do {
		if (i-- <= 0) {
			PRINT(("DSP init timed out\n"));
			return B_TIMED_OUT;
		}
		fEnd = ymf7xx_reg_read_32(config, YMF7XX_CONTROL_SELECT) & 0x1;
	} while (fEnd == 0x1);

	PRINT(("codec attach\n"));
	ac97_attach(&config->ac97, (codec_reg_read)ymf7xx_codec_read,
		(codec_reg_write)ymf7xx_codec_write, config,
		config->subvendor_id, config->subsystem_id);

	return B_OK;
}


/*
 * Allocates the play/effect/record control-data areas and tells the
 * hardware where they live. These are separate from the actual PCM
 * sample buffers (allocated per-stream in ymf7xx_stream_set_audioparms).
 * alloc_mem() already forces B_32_BIT_CONTIGUOUS, which is what this
 * hardware's plain 32-bit physical table-base registers require.
 * Ported from the table-allocation part of init_ymf7xx() in
 * oss_ymf7xx.c.
 */
static status_t
ymf7xx_alloc_tables(ymf7xx_dev *card)
{
	device_config *config = &card->config;

	card->play_table_area = alloc_mem(&card->play_table_phys,
		&card->play_table_log, YMF7XX_PLAY_TABLE_SIZE, "ymf7xx play table", false);
	if (card->play_table_area < B_OK)
		return B_ERROR;
	card->tab = (uint32 *)card->play_table_log;
	card->slt = (char *)card->play_table_log + 0x100;
	card->slt_phys = card->play_table_phys + 0x100;
	card->tab[0] = YMF7XX_PLAY_SLOT_COUNT;

	card->effect_table_area = alloc_mem(&card->effect_table_phys,
		&card->effect_table_log, YMF7XX_EFF_TABLE_SIZE, "ymf7xx effect table", false);
	if (card->effect_table_area < B_OK)
		return B_ERROR;

	card->eff_buf_area = alloc_mem(&card->eff_buf_phys, &card->eff_buf_log,
		YMF7XX_EFF_BUF_SIZE, "ymf7xx effect scratch", false);
	if (card->eff_buf_area < B_OK)
		return B_ERROR;

	card->rec_table_area = alloc_mem(&card->rec_table_phys,
		&card->rec_table_log, YMF7XX_REC_TABLE_SIZE, "ymf7xx rec table", false);
	if (card->rec_table_area < B_OK)
		return B_ERROR;

	ymf7xx_reg_write_32(config, YMF7XX_PLAY_CNTRL_BASE, (uint32)card->play_table_phys);
	ymf7xx_reg_write_32(config, YMF7XX_EFF_CNTRL_BASE, (uint32)card->effect_table_phys);
	ymf7xx_reg_write_32(config, YMF7XX_REC_CNTRL_BASE, (uint32)card->rec_table_phys);

	return B_OK;
}


static void
ymf7xx_free_tables(ymf7xx_dev *card)
{
	if (card->play_table_area > B_OK)
		delete_area(card->play_table_area);
	if (card->effect_table_area > B_OK)
		delete_area(card->effect_table_area);
	if (card->eff_buf_area > B_OK)
		delete_area(card->eff_buf_area);
	if (card->rec_table_area > B_OK)
		delete_area(card->rec_table_area);
}


/*	ymf7xx driver functions */


/* detect presence of our hardware */
status_t
init_hardware(void)
{
	int ix = 0;
	pci_info info;
	status_t err = ENODEV;

	if (get_module(B_PCI_MODULE_NAME, (module_info **)&pci))
		return ENOSYS;

	while ((*pci->get_nth_pci_info)(ix, &info) == B_OK) {
		if (is_ymf7xx(info.vendor_id, info.device_id)) {
			LOG_CREATE();
			PRINT(("init_hardware()\n"));
			err = B_OK;
		}
		ix++;
	}

	put_module(B_PCI_MODULE_NAME);

	return err;
}


static void
make_device_names(ymf7xx_dev * card)
{
	sprintf(card->name, "audio/hmulti/ymf7xx/%ld", card-cards+1);
	names[num_names++] = card->name;

	names[num_names] = NULL;
}


status_t
ymf7xx_init(ymf7xx_dev * card)
{
	LIST_INIT(&(card->streams));
	LIST_INIT(&(card->mems));

	return B_OK;
}


static status_t
ymf7xx_setup(ymf7xx_dev * card)
{
	status_t err;
	unsigned char cmd, pci_reset;

	PRINT(("ymf7xx_setup(%p)\n", card));

	make_device_names(card);

	card->config.subvendor_id = card->info.u.h0.subsystem_vendor_id;
	card->config.subsystem_id = card->info.u.h0.subsystem_id;
	card->config.bar0_phys = card->info.u.h0.base_registers[0];
	card->config.irq = card->info.u.h0.interrupt_line;
	card->config.is_ds1e = is_ds1e(card->info.device_id);

	PRINT(("%s deviceid = %#04x chiprev = %x bar0 at %#" B_PRIxPHYSADDR "\n",
		card->name, card->info.device_id, card->info.revision,
		card->config.bar0_phys));

	if (card->config.bar0_phys == 0) {
		PRINT(("BAR0 not assigned by BIOS\n"));
		return B_ERROR;
	}

	/* reset dance on PCI config byte 0x48, only performed if bits 0-1
	 * are already set (matches the OSS reference exactly); this and the
	 * legacy I/O setup below are pure config-space operations and must
	 * happen before enabling memory space / mapping BAR0, matching the
	 * order in oss_ymf7xx_attach() in oss_ymf7xx.c */
	pci_reset = (*pci->read_pci_config)(card->info.bus, card->info.device,
		card->info.function, 0x48, 1);
	if (pci_reset & 0x03) {
		(*pci->write_pci_config)(card->info.bus, card->info.device,
			card->info.function, 0x48, 1, pci_reset & 0xFC);
		(*pci->write_pci_config)(card->info.bus, card->info.device,
			card->info.function, 0x48, 1, pci_reset | 0x03);
		(*pci->write_pci_config)(card->info.bus, card->info.device,
			card->info.function, 0x48, 1, pci_reset & 0xFC);
	}

	/* legacy MPU-401/OPL3 FM I/O decode left disabled -- out of scope */
	(*pci->write_pci_config)(card->info.bus, card->info.device,
		card->info.function, 0x40, 2, 0x0004);
	(*pci->write_pci_config)(card->info.bus, card->info.device,
		card->info.function, 0x42, 2, 0x0000);

	/* enable PCI memory space + bus mastering */
	cmd = (*pci->read_pci_config)(card->info.bus, card->info.device,
		card->info.function, PCI_command, 2);
	(*pci->write_pci_config)(card->info.bus, card->info.device,
		card->info.function, PCI_command, 2,
		cmd | PCI_command_master | PCI_command_memory);

	card->config.bar0_area = map_mem(&card->config.bar0_log,
		card->config.bar0_phys, YMF7XX_MAP_LENGTH, "ymf7xx mmio");
	if (card->config.bar0_area < B_OK) {
		PRINT(("mapping of mmio space failed\n"));
		return B_ERROR;
	}

	/* the interrupt handler must be installed before ymf7xx_dsp_init(),
	 * which writes CONFIG=1 and starts the timer tick that raises it --
	 * matches the ordering in oss_ymf7xx_attach() in oss_ymf7xx.c */
	PRINT(("installing interrupt : %" B_PRIu32 "\n", card->config.irq));
	err = install_io_interrupt_handler(card->config.irq, ymf7xx_int, card, 0);
	if (err != B_OK) {
		PRINT(("failed to install interrupt\n"));
		delete_area(card->config.bar0_area);
		return err;
	}

	err = ymf7xx_dsp_init(card);
	if (err != B_OK) {
		remove_io_interrupt_handler(card->config.irq, ymf7xx_int, card);
		delete_area(card->config.bar0_area);
		return err;
	}

	err = ymf7xx_alloc_tables(card);
	if (err != B_OK) {
		ac97_detach(card->config.ac97);
		remove_io_interrupt_handler(card->config.irq, ymf7xx_int, card);
		ymf7xx_free_tables(card);
		delete_area(card->config.bar0_area);
		return err;
	}

	if ((err = ymf7xx_init(card)) != B_OK)
		return err;

	PRINT(("init_driver done\n"));

	return err;
}


status_t
init_driver(void)
{
	void *settings_handle;
	pci_info info;
	int ix = 0;
	status_t err;
	num_cards = 0;

	PRINT(("init_driver()\n"));

	settings_handle = load_driver_settings(YMF7XX_SETTINGS);
	if (settings_handle != NULL) {
		const char *item;
		char *end;
		uint32 value;

		item = get_driver_parameter(settings_handle, "sample_rate", "48000", "48000");
		value = strtoul(item, &end, 0);
		if (*end == '\0')
			current_settings.sample_rate = value;

		item = get_driver_parameter(settings_handle, "buffer_frames", "512", "512");
		value = strtoul(item, &end, 0);
		if (*end == '\0')
			current_settings.buffer_frames = value;

		item = get_driver_parameter(settings_handle, "buffer_count", "4", "4");
		value = strtoul(item, &end, 0);
		if (*end == '\0')
			current_settings.buffer_count = value;

		unload_driver_settings(settings_handle);
	}

	if (get_module(B_PCI_MODULE_NAME, (module_info **) &pci))
		return ENOSYS;

	while ((*pci->get_nth_pci_info)(ix++, &info) == B_OK) {
		if (!is_ymf7xx(info.vendor_id, info.device_id))
			continue;

		if (num_cards == NUM_CARDS) {
			PRINT(("Too many ymf7xx cards installed!\n"));
			break;
		}
		memset(&cards[num_cards], 0, sizeof(ymf7xx_dev));
		cards[num_cards].info = info;
		if ((err = (*pci->reserve_device)(info.bus, info.device, info.function,
			DRIVER_NAME, &cards[num_cards])) < B_OK) {
			dprintf("%s: failed to reserve_device(%d, %d, %d,): %s\n",
				DRIVER_NAME, info.bus, info.device, info.function,
				strerror(err));
			continue;
		}
		if (ymf7xx_setup(&cards[num_cards])) {
			PRINT(("Setup of ymf7xx %" B_PRId32 " failed\n", num_cards+1));
			(*pci->unreserve_device)(info.bus, info.device, info.function,
				DRIVER_NAME, &cards[num_cards]);
		} else {
			num_cards++;
		}
	}
	if (!num_cards) {
		PRINT(("no cards\n"));
		put_module(B_PCI_MODULE_NAME);
		PRINT(("no suitable cards found\n"));
		return ENODEV;
	}

	return B_OK;
}


static void
ymf7xx_shutdown(ymf7xx_dev *card)
{
	PRINT(("shutdown(%p)\n", card));

	ymf7xx_reg_write_32(&card->config, YMF7XX_CONFIG, 0);
	ymf7xx_reg_write_32(&card->config, YMF7XX_MODE, 0);

	ac97_detach(card->config.ac97);

	remove_io_interrupt_handler(card->config.irq, ymf7xx_int, card);

	ymf7xx_free_tables(card);
	delete_area(card->config.bar0_area);
}


void
uninit_driver(void)
{
	int ix, cnt = num_cards;
	num_cards = 0;

	PRINT(("uninit_driver()\n"));
	for (ix=0; ix<cnt; ix++) {
		ymf7xx_shutdown(&cards[ix]);
		(*pci->unreserve_device)(cards[ix].info.bus,
			cards[ix].info.device, cards[ix].info.function,
			DRIVER_NAME, &cards[ix]);
	}
	memset(&cards, 0, sizeof(cards));
	put_module(B_PCI_MODULE_NAME);
}


const char **
publish_devices(void)
{
	int ix = 0;
	PRINT(("publish_devices()\n"));

	for (ix=0; names[ix]; ix++) {
		PRINT(("publish %s\n", names[ix]));
	}
	return (const char **)names;
}


device_hooks *
find_device(const char * name)
{
	int ix;

	PRINT(("find_device(%s)\n", name));

	for (ix=0; ix<num_cards; ix++) {
		if (!strcmp(cards[ix].name, name)) {
			return &multi_hooks;
		}
	}
	PRINT(("find_device(%s) failed\n", name));
	return NULL;
}

int32	api_version = B_CUR_DRIVER_API_VERSION;
