/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 */
#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "ac97.h"

#define NUM_CARDS 3
#define DEVNAME 32

typedef struct {
	phys_addr_t	bar0_phys;
	void		*bar0_log;
	area_id		bar0_area;
	uint32		irq;

	ushort		subvendor_id;
	ushort		subsystem_id;

	/* true for YMF724F/740C/744/754 ("DS-1E"): these need the "1E"
	 * controller microcode instead of the plain DS-1 one. */
	bool		is_ds1e;

	ac97_dev	*ac97;
} device_config;

#endif
