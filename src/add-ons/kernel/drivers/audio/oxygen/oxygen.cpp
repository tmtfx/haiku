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
void xonar_d2_pcm1796_write(oxygen_t *chip, uint8_t codec, uint8_t reg, uint8_t value)
{
    uint32_t timeout = 1000;
    while ((oxygen_read8(chip, OXYGEN_SPI_CONTROL) & OXYGEN_SPI_BUSY) && --timeout > 0) {
        // Attento che il bus SPI sia libero
    }

    // Il PCM1796 accetta pacchetti da 16 bit (Indirizzo + Dato) o simili a seconda del codec, 
    // qui impostiamo i registri dati SPI ufficiali:
    oxygen_write8(chip, OXYGEN_SPI_DATA1, reg);
    oxygen_write8(chip, OXYGEN_SPI_DATA2, value);
    
    // Configura il controllo SPI: seleziona il codec (chip select) e attiva il trigger
    uint8_t control = OXYGEN_SPI_TRIGGER | 
                      ((codec << OXYGEN_SPI_CODEC_SHIFT) & OXYGEN_SPI_CODEC_MASK) |
                      OXYGEN_SPI_DATA_LENGTH_2; // Lunghezza dati a 2 bytes
                      
    oxygen_write8(chip, OXYGEN_SPI_CONTROL, control);
}
/* variante
void
xonar_d2_pcm1796_write(oxygen_t *chip, uint8_t dac_mask, uint8_t reg, uint8_t value)
{
    // 1. Attendi che il bus SPI sia libero
    int timeout = 1000;
    while ((*(volatile uint8_t *)(chip->mmio_base + OXYGEN_SPI_STATUS) & OXYGEN_SPI_BUSY) && timeout > 0) {
        snooze(10);
        timeout--;
    }

    // 2. Scrivi il valore del registro nel data register SPI
    *(volatile uint8_t *)(chip->mmio_base + OXYGEN_SPI_DATA) = value;

    // 3. Configura il controllo SPI: 
    // - Indirizzo del registro sul PCM1796 (bit bassi)
    // - Maschera di selezione del chip / DAC (tramite i bit di codec/chipless select)
    // - Trigger di scrittura
    uint8_t control = OXYGEN_SPI_TRIGGER_WRITE | (reg & 0x1f);
    
    // Seleziona quale dei 4 DAC pilotare in base alla maschera (es. bit 0..3)
    // Nel CMI8788 i chip select dei DAC multicanale usano specifici bit di codec
    control |= (dac_mask << OXYGEN_SPI_CODEC_SHIFT);

    *(volatile uint8_t *)(chip->mmio_base + OXYGEN_SPI_CONTROL) = control;

    // Attendi il completamento della transazione SPI
    timeout = 1000;
    while ((*(volatile uint8_t *)(chip->mmio_base + OXYGEN_SPI_STATUS) & OXYGEN_SPI_BUSY) && timeout > 0) {
        snooze(10);
        timeout--;
    }
}*/

// Inizializzazione hardware specifica per la Xonar D2X
void
xonar_d2_init(oxygen_t *chip)
{
    dprintf("oxygen: Configurazione registri globali e GPIO per ASUS Xonar D2X...\n");

    // 1. Configurazione dei GPIO: imposta MUTE e LED come output
    uint16_t control = oxygen_read16(chip, OXYGEN_GPIO_CONTROL);
    control |= (XONAR_D2_GPIO_MUTE | XONAR_D2_GPIO_LED_MASK);
    oxygen_write16(chip, OXYGEN_GPIO_CONTROL, control);

    // 2. Attiva il MUTE hardware durante il setup iniziale per evitare "pop" sonori
    uint16_t data = oxygen_read16(chip, OXYGEN_GPIO_DATA);
    data &= ~XONAR_D2_GPIO_MUTE; // Mute attivo (basso)
    oxygen_write16(chip, OXYGEN_GPIO_DATA, data);

    // 3. Inizializzazione dei 4 chip Burr-Brown PCM1796 via SPI
    // Ciascun PCM1796 gestisce una coppia di canali (7.1 in totale = 4 chip)
    for (int i = 0; i < 4; i++) {
        uint8_t dac_mask = (1 << i);
        
        // Control 1: Impostazioni di default (filtri, deselezione soft mute di reset)
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_CONTROL_1, 0x00);
        
        // Control 2: Formato I2S, 24-bit/32-bit, disattivazione soft mute manuale
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_CONTROL_2, 0x50);
        
        // Imposta l'attenuazione iniziale a 0 (volume massimo iniziale, o gestito dal mixer)
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_ATTN_L, 0xff);
        xonar_d2_pcm1796_write(chip, dac_mask, PCM1796_REG_ATTN_R, 0xff);
    }

    // 4. Rilascia il MUTE (porta il pin GPIO alto) ora che i DAC sono configurati
    data |= XONAR_D2_GPIO_MUTE;
    oxygen_write16(chip, OXYGEN_GPIO_DATA, data);

    dprintf("oxygen: Xonar D2X inizializzata con successo.\n");
}

// Allinea la firma a quella dichiarata in cmi8788.h (oppure aggiorna l'header se preferisci passare il puntatore al chip)
status_t
oxygen_chip_init(oxygen_t *chip)
{
	xonar_d2_init(chip);
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

// Funzione di scrittura sicura sui registri GPIO del CMI8788
void cmi8788_gpio_set(cmi8788_device *device, uint16 data, uint16 mask)
{
    // Leggi lo stato attuale dei GPIO
    uint16 ctrl = *(volatile uint16 *)(device->mmio_base + OXYGEN_GPIO_CONTROL);
    // Assicurati che i pin siano configurati come output (bit a 1 nella control mask)
    *(volatile uint16 *)(device->mmio_base + OXYGEN_GPIO_CONTROL) = ctrl | mask;

    uint16 val = *(volatile uint16 *)(device->mmio_base + OXYGEN_GPIO_DATA);
    val = (val & ~mask) | (data & mask);
    *(volatile uint16 *)(device->mmio_base + OXYGEN_GPIO_DATA) = val;
}

void cmi8788_set_mute(cmi8788_device *device, bool mute)
{
    // Sulla Xonar D2, impostare il bit XONAR_D2_GPIO_MUTE attiva/disattiva il relè
    uint16 data = mute ? 0x0000 : XONAR_D2_GPIO_MUTE;
    cmi8788_gpio_set(device, data, XONAR_D2_GPIO_MUTE);
}
void cmi8788_spi_write(cmi8788_device *device, uint8 codec_mask, uint8 reg, uint8 value)
{
    // 1. Attendi che il bus SPI sia libero
    int timeout = 1000;
    while ((*(volatile uint8 *)(device->mmio_base + OXYGEN_SPI_STATUS) & OXYGEN_SPI_BUSY) && timeout > 0) {
        snooze(10);
        timeout--;
    }

    // 2. Scrivi i dati nel registro dati SPI (formato tipico: indirizzo/valore o comando codec)
    *(volatile uint8 *)(device->mmio_base + OXYGEN_SPI_DATA) = value;
    
    // 3. Configura il controllo SPI (includendo la maschera del DAC target e il registro)
    // Nota: il CMI8788 permette di selezionare quale DAC indirizzare tramite i bit di selezione nel registro SPI control
    uint8 control = codec_mask | (reg & 0x1f); 
    *(volatile uint8 *)(device->mmio_base + OXYGEN_SPI_CONTROL) = control | OXYGEN_SPI_TRIGGER_WRITE;
}

void xonar_d2_init_dacs(cmi8788_device *device)
{
    // Inizializza i registri dei PCM1796 (es. Control 1 e 2, azzeramento attenuazione)
    // Ipotizzando una maschera che seleziona tutti i DAC o iterando sui chip
    for (int codec = 0; codec < 4; codec++) {
        uint8 mask = (1 << codec); // Selezione chip select via SPI sul CMI8788
        cmi8788_spi_write(device, mask, PCM1796_REG_CONTROL_1, 0x00); // Impostazioni default
        cmi8788_spi_write(device, mask, PCM1796_REG_CONTROL_2, 0x10); // Soft mute disattivato, 24-bit/I2S
        cmi8788_spi_write(device, mask, PCM1796_REG_ATTN_L, 0xff);    // Volume massimo (attenuazione 0)
        cmi8788_spi_write(device, mask, PCM1796_REG_ATTN_R, 0xff);
    }
}
