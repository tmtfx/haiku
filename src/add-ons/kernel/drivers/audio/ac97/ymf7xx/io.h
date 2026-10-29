/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Pirati Del Frico
 * Distributed under the terms of the MIT License.
 */
#ifndef _IO_H_
#define _IO_H_

#include "config.h"

uint8  ymf7xx_reg_read_8(device_config *config, uint32 regno);
uint16 ymf7xx_reg_read_16(device_config *config, uint32 regno);
uint32 ymf7xx_reg_read_32(device_config *config, uint32 regno);

void ymf7xx_reg_write_8(device_config *config, uint32 regno, uint8 value);
void ymf7xx_reg_write_16(device_config *config, uint32 regno, uint16 value);
void ymf7xx_reg_write_32(device_config *config, uint32 regno, uint32 value);

uint16 ymf7xx_codec_read(device_config *config, uint8 regno);
void ymf7xx_codec_write(device_config *config, uint8 regno, uint16 value);

#endif
