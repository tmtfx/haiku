/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <Drivers.h>

//#include <stdlib.h>
//#include <errno.h>

//#include <SupportDefs.h>
#include <util/kernel_cpp.h>
//#include <unistd.h>
//#include <stdio.h>
//#include <string.h>
//#include <errno.h>

#include "cmi8788.h"

#define ROUNDUP(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

// Funzione di scrittura SPI per i DAC PCM1796 della Xonar D2X
static void
xonar_d2_pcm1796_write(oxygen_t *chip, uint8_t codec_mask, uint8_t reg, uint8_t value)
{
    int timeout = 100;
    
    while ((oxygen_read8(chip, OXYGEN_SPI_STATUS) & 0x01) && --timeout > 0) {
        // spin-wait breve nel kernel
    }

    uint16_t spi_data = ((reg & 0x1f) << 8) | value;
    oxygen_write16(chip, OXYGEN_SPI_DATA, spi_data);
    
    uint8_t control_val = 0x01 | (codec_mask & 0x0f) << 4;
    oxygen_write8(chip, OXYGEN_SPI_CONTROL, control_val);
}

// Inizializzazione hardware specifica per la Xonar D2X
void
xonar_d2_init(oxygen_t *chip)
{
    uint16_t control;
    uint16_t data;

    control = oxygen_read16(chip, OXYGEN_GPIO_CONTROL);
    control |= (XONAR_D2_GPIO_MUTE | XONAR_D2_GPIO_LED_MASK);
    oxygen_write16(chip, OXYGEN_GPIO_CONTROL, control);

    data = oxygen_read16(chip, OXYGEN_GPIO_DATA);
    data &= ~XONAR_D2_GPIO_MUTE;
    oxygen_write16(chip, OXYGEN_GPIO_DATA, data);

    for (int i = 0; i < 4; i++) {
        uint8_t dac_mask = (1 << i);
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_CONTROL_1, 0x00); 
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_CONTROL_2, 0x50); 
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_ATTN_L, 0xff);
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_ATTN_R, 0xff);
    }

    data |= XONAR_D2_GPIO_MUTE;
    oxygen_write16(chip, OXYGEN_GPIO_DATA, data);
}

// Allinea la firma a quella dichiarata in cmi8788.h (oppure aggiorna l'header se preferisci passare il puntatore al chip)
status_t
oxygen_chip_init(oxygen_t *chip)
{
    dprintf("oxygen: Inizializzazione registri CMI8788 per ASUS Xonar D2X...\n");

    // Esegue l'inizializzazione specifica dei DAC PCM1796 e dei GPIO della D2X
    xonar_d2_init(chip);

    dprintf("oxygen: Chip CMI8788 e Xonar D2X inizializzati con successo.\n");
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
	size = ROUNDUP(size, B_PAGE_SIZE); //same as: size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
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
