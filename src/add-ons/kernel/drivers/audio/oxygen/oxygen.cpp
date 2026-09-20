/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <Drivers.h>


// Prototipo della funzione I2C definita sopra
status_t oxygen_i2c_write(addr_t mmio_base, uint8 device_addr, uint8 reg, uint16 data);

// Registri di controllo globali OxygenHD
#define OXYGEN_FUNCTION_CONTROL		CNc 0x6a // Esempio offset registro funzionale
#define OXYGEN_GPI_DATA			0xe0

status_t
oxygen_chip_init(addr_t mmio_base)
{
	dprintf("oxygen: Inizializzazione registri CMI8788 in corso...\n");

	// 1. Reset e configurazione iniziale dei canali DMA e interrupt
	// (Qui andranno configurate le maschere di interrupt globali)

	// 2. Inizializzazione del DAC CS4398 via I2C (Indirizzo tipico 0x98 o simili per Xonar DX)
	// Esempio di invio configurazione volume/mute iniziale al DAC
	status_t status = oxygen_i2c_write(mmio_base, 0x98, 0x01, 0x0000);
	if (status < B_OK) {
		dprintf("oxygen: Attenzione - Impossibile comunicare con il DAC via I2C\n");
	}

	dprintf("oxygen: Chip CMI8788 inizializzato con successo.\n");
	return B_OK;
}

void
oxygen_chip_shutdown(addr_t mmio_base)
{
	dprintf("oxygen: Arresto del chip CMI8788...\n");
	// Disattiva i flussi DMA e metti in muto le uscite
}
status_t
oxygen_init_dma_buffer(cmi8788_device *device, size_t size)
{
	// Allineiamo la dimensione alla pagina
	size = ROUNDUP(size, B_PAGE_SIZE);
	device->dma_buffer_size = size;

	// Creiamo un'area di memoria contigua fisica per il DMA
	physical_entry entry;
	device->dma_area = create_area("cmi8788_dma_buffer", &device->dma_pub_base,
		B_ANY_KERNEL_ADDRESS, size, B_CONTIGUOUS, B_READ_AREA | B_WRITE_AREA);

	if (device->dma_area < B_OK) {
		dprintf("oxygen: Errore nella creazione dell'area DMA (%s)\n", strerror(device->dma_area));
		return device->dma_area;
	}

	// Otteniamo l'indirizzo fisico da passare al controller PCI/DMA del CMI8788
	get_memory_map(device->dma_pub_base, size, &entry, 1);
	device->dma_phy_base = entry.address;

	dprintf("oxygen: Buffer DMA allocato. Virtuale: %p, Fisico: 0x%lx, Dimensione: %lu\n",
		device->dma_pub_base, (unsigned long)device->dma_phy_base, size);

	return B_OK;
}

void
oxygen_free_dma_buffer(cmi8788_device *device)
{
	if (device->dma_area >= B_OK) {
		delete_area(device->dma_area);
		device->dma_area = -1;
		device->dma_pub_base = NULL;
	}
}


status_t
cmi8788_setup_interrupts(cmi8788_device *device)
{
	// Ottieni la linea IRQ assegnata dal BIOS/PCI bus manager
	uint8 irq = device->pci_info.u.h0.interrupt_line;
	
	dprintf("cmi8788: Installazione interrupt su IRQ %u\n", irq);
	
	status_t status = install_io_interrupt_handler(irq, cmi8788_interrupt, (void *)device, 0);
	if (status < B_OK) {
		dprintf("cmi8788: Errore installazione interrupt handler (%s)\n", strerror(status));
		return status;
	}

	// Abilita gli interrupt per il playback nel registro di maschera del chip
	uint8 mask = *(volatile uint8 *)(device->mmio_base + OXYGEN_INTERRUPT_MASK);
	mask |= OXYGEN_INT_PLAYBACK;
	*(volatile uint8 *)(device->mmio_base + OXYGEN_INTERRUPT_MASK) = mask;

	return B_OK;
}

void
cmi8788_remove_interrupts(cmi8788_device *device)
{
	uint8 irq = device->pci_info.u.h0.interrupt_line;
	
	// Disattiva le maschere
	*(volatile uint8 *)(device->mmio_base + OXYGEN_INTERRUPT_MASK) = 0;
	
	// Rimuovi l'handler dal kernel
	remove_io_interrupt_handler(irq, cmi8788_interrupt, (void *)device);
}
