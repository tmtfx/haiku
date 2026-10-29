/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 *
 * Register access is a single 32 KB memory-mapped BAR0 window (no I/O
 * port mode on this chipset); the AC'97 codec access protocol (the
 * AC97_CMD and AC97_STATUS register busy-wait) is ported from
 * ac97_read()/ac97_write() in OpenSoundSystem v4.2's oss_ymf7xx.c,
 * which carries this notice:
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
#include <KernelExport.h>
#include <OS.h>
#include "io.h"
#include "ymf7xxreg.h"
#include "debug.h"
#include <PCI.h>

extern pci_module_info  *pci;

uint8
ymf7xx_reg_read_8(device_config *config, uint32 regno)
{
	return *(volatile uint8 *)(((char *)config->bar0_log) + regno);
}

uint16
ymf7xx_reg_read_16(device_config *config, uint32 regno)
{
	return *(volatile uint16 *)(((char *)config->bar0_log) + regno);
}

uint32
ymf7xx_reg_read_32(device_config *config, uint32 regno)
{
	return *(volatile uint32 *)(((char *)config->bar0_log) + regno);
}

void
ymf7xx_reg_write_8(device_config *config, uint32 regno, uint8 value)
{
	*(volatile uint8 *)(((char *)config->bar0_log) + regno) = value;
}

void
ymf7xx_reg_write_16(device_config *config, uint32 regno, uint16 value)
{
	*(volatile uint16 *)(((char *)config->bar0_log) + regno) = value;
}

void
ymf7xx_reg_write_32(device_config *config, uint32 regno, uint32 value)
{
	*(volatile uint32 *)(((char *)config->bar0_log) + regno) = value;
}

/*
 * AC'97 codec access. The chip exposes the codec through a command/status
 * register pair rather than a direct bus-master link: a read sets the
 * high bit of AC97_CMD_ADDRESS and busy-waits on bit 15 of
 * AC97_STATUS_ADDRESS clearing, then reads AC97_STATUS_DATA; a write sets
 * both AC97_CMD_ADDRESS and AC97_CMD_DATA and waits the same way. This is
 * a tight spin (no snooze()) matching the OSS reference exactly -- this
 * path can run before interrupts/threads are safely usable during early
 * bring-up, so it must not sleep.
 */
#define YMF7XX_AC97_BUSY_SPINS 1000

uint16
ymf7xx_codec_read(device_config *config, uint8 regno)
{
	int count;

	ymf7xx_reg_write_16(config, YMF7XX_AC97_CMD_ADDRESS, regno | 0x8000);

	for (count = 0; count < YMF7XX_AC97_BUSY_SPINS; count++) {
		if ((ymf7xx_reg_read_16(config, YMF7XX_AC97_STATUS_ADDRESS) >> 15) == 0)
			return ymf7xx_reg_read_16(config, YMF7XX_AC97_STATUS_DATA);
	}

	PRINT(("codec read timed out (reg %#x)\n", regno));
	return 0xffff;
}

void
ymf7xx_codec_write(device_config *config, uint8 regno, uint16 value)
{
	int count;

	ymf7xx_reg_write_16(config, YMF7XX_AC97_CMD_ADDRESS, regno);
	ymf7xx_reg_write_16(config, YMF7XX_AC97_CMD_DATA, value);

	for (count = 0; count < YMF7XX_AC97_BUSY_SPINS; count++) {
		if ((ymf7xx_reg_read_16(config, YMF7XX_AC97_STATUS_ADDRESS) >> 15) == 0)
			return;
	}

	PRINT(("codec write timed out (reg %#x)\n", regno));
}
