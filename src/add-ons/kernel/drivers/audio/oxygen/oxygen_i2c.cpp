/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>

// Registri I2C del CMI8788 (OxygenHD)
#define OXYGEN_I2C_DATA		0x88
#define OXYGEN_I2C_ADDRESS	0x89
#define OXYGEN_I2C_CONTROL	0x8a
#define   OXYGEN_I2C_DIRECTION_IV	(1 << 0)
#define   OXYGEN_I2C_bus_SPEED_FAST	(1 << 1)
#define   OXYGEN_I2C_TRIGGER		(1 << 2)
#define   OXYGEN_I2C_BUSY			(1 << 2)

// Funzione interna per attendere che il bus I2C sia libero
static bool
oxygen_i2c_wait(addr_t mmio_base)
{
	int timeout = 1000;
	while (timeout-- > 0) {
		uint8 ctrl = *(volatile uint8 *)(mmio_base + OXYGEN_I2C_CONTROL);
		if (!(ctrl & OXYGEN_I2C_BUSY))
			return true;
		spin(10);
	}
	dprintf("oxygen_i2c: Timeout attesa bus I2C\n");
	return false;
}

status_t
oxygen_i2c_write(addr_t mmio_base, uint8 device_addr, uint8 reg, uint16 data)
{
	if (!oxygen_i2c_wait(mmio_base))
		return B_TIMED_OUT;

	// Imposta i dati da inviare (es. registro e valore per DAC esterni)
	*(volatile uint8 *)(mmio_base + OXYGEN_I2C_DATA) = reg;
	// Sulla Xonar DX spesso si invia un pacchetto a 16 o 24 bit a seconda del chip
	*(volatile uint8 *)(mmio_base + OXYGEN_I2C_ADDRESS) = device_addr;

	// Avvia la transazione I2C
	uint8 control = OXYGEN_I2C_TRIGGER | OXYGEN_I2C_bus_SPEED_FAST;
	*(volatile uint8 *)(mmio_base + OXYGEN_I2C_CONTROL) = control;

	if (!oxygen_i2c_wait(mmio_base))
		return B_TIMED_OUT;

	return B_OK;
}
