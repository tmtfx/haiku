/*
 * Copyright 1992-2003, Alan Hourihane. All rights reserved.
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alan Hourihane <alanh@fairlite.demon.co.uk>
 *		Fabio Tomat <f.t.public@gmail.com>
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"
#include <unistd.h>
#include <ddc.h>
#include <edid.h>

static uint8
read_crtc_reg_pio(uint8 index)
{
	TridentGetSetPIO gsp;
	gsp.magic = TRIDENT_PRIVATE_DATA_MAGIC;
	gsp.size = 1;
	gsp.offset = 0x3D4;
	gsp.value = index;
	ioctl(gInfo.deviceFileDesc, TRIDENT_SET_PIO, &gsp, sizeof(gsp));
	gsp.offset = 0x3D5;
	ioctl(gInfo.deviceFileDesc, TRIDENT_GET_PIO, &gsp, sizeof(gsp));
	return (uint8)gsp.value;
}

static void
write_crtc_reg_pio(uint8 index, uint8 value)
{
	TridentGetSetPIO gsp;
	gsp.magic = TRIDENT_PRIVATE_DATA_MAGIC;
	gsp.size = 1;
	gsp.offset = 0x3D4;
	gsp.value = index;
	ioctl(gInfo.deviceFileDesc, TRIDENT_SET_PIO, &gsp, sizeof(gsp));
	gsp.offset = 0x3D5;
	gsp.value = value;
	ioctl(gInfo.deviceFileDesc, TRIDENT_SET_PIO, &gsp, sizeof(gsp));
}

static status_t
GetI2CSignals(void* cookie, int* clock, int* data)
{
	(void)cookie;
	uint8 value = read_crtc_reg_pio(I2C);
	*clock = (value & 0x02) != 0; // Bit 1
	*data = (value & 0x01) != 0;  // Bit 0
	debug_printf("Trident_I2C: READ CR%02X=0x%02X -> SCL(clock)=%d, SDA(data)=%d\n",
		I2C, value, *clock, *data);
	return B_OK;
}

static status_t
SetI2CSignals(void* cookie, int clock, int data)
{
	(void)cookie;
	uint8 value = 0x0C;
	
	if (clock)
		value |= 0x02;
	if (data)
		value |= 0x01;

	uint8 old_val = read_crtc_reg_pio(I2C);
	write_crtc_reg_pio(I2C, value);
	uint8 new_val = read_crtc_reg_pio(I2C);
	debug_printf("Trident_I2C: WRITE CR%02X Target=0x%02X (SCL=%d, SDA=%d) | Old=0x%02X -> Readback=0x%02X\n",
		I2C, value, clock, data, old_val, new_val);
	return B_OK;
}

bool
GetEdidInfoI2C(edid1_info* edid)
{
	i2c_bus bus;
	bus.cookie = NULL;
	bus.set_signals = &SetI2CSignals;
	bus.get_signals = &GetI2CSignals;
	ddc2_init_timing(&bus);

	// Assicura che MMIO sia abilitato e i registri estesi sbloccati
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	uint8 oldI2C = read_crtc_reg_pio(I2C);
	debug_printf("Trident_I2C: --- START Dynamic EDID I2C Read (Original CR%02X = 0x%02X) ---\n", 
		I2C, oldI2C);

	bool success = (ddc2_read_edid1(&bus, edid, NULL, NULL) == B_OK);

	write_crtc_reg_pio(I2C, oldI2C);
	
	debug_printf("Trident_I2C: --- END EDID I2C Read Result = %s (Restored CR%02X = 0x%02X) ---\n",
		success ? "SUCCESS" : "FAILED", I2C, read_crtc_reg_pio(I2C));

	return success;
}
