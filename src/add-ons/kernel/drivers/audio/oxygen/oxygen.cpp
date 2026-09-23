/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <Drivers.h>

#include <util/kernel_cpp.h>

#include "cmi8788.h"

#define ROUNDUP(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

// Funzione di scrittura SPI per i DAC PCM1796 della Xonar D2X
void xonar_d2_pcm1796_write(oxygen_t *chip, uint8_t codec, uint8_t reg, uint8_t value)
{
    /* Mappa la coppia di canali multicanale alla linea Chip Select SPI corretta */
    static const uint8_t codec_map[4] = {
        0, 1, 2, 4
    };

#if ENABLE_VERBOSE_LOGS
    dprintf("cmi8788: [SPI WRITE] Codec %u, Reg %u -> Valore 0x%02x\n", codec, reg, value);
#endif

    uint32_t timeout = 1000;
    while ((oxygen_read8(chip, OXYGEN_SPI_CONTROL) & OXYGEN_SPI_BUSY) && --timeout > 0) {
        snooze(10);
    }

    // Scrive i dati (formato 16 bit: Indirizzo nel Data2, Dato nel Data1)
    oxygen_write8(chip, OXYGEN_SPI_DATA1, value);
    oxygen_write8(chip, OXYGEN_SPI_DATA2, reg);
    
    // Configura il controllo SPI: attiva il trigger, seleziona il codec, latch clock alto
    uint8_t control = OXYGEN_SPI_TRIGGER | 
                      ((codec_map[codec] << OXYGEN_SPI_CODEC_SHIFT) & OXYGEN_SPI_CODEC_MASK) |
                      OXYGEN_SPI_CEN_LATCH_CLOCK_HI |
                      OXYGEN_SPI_CLOCK_160 |
                      OXYGEN_SPI_DATA_LENGTH_2; // Lunghezza dati a 2 byte
                      
    oxygen_write8(chip, OXYGEN_SPI_CONTROL, control);

    timeout = 1000;
    while ((oxygen_read8(chip, OXYGEN_SPI_CONTROL) & OXYGEN_SPI_BUSY) && --timeout > 0) {
        snooze(10);
    }
}

// Funzione per impostare la frequenza di campionamento e il clock hardware (Asus Xonar D2X)
void xonar_d2_set_sample_rate(oxygen_t *chip, uint32_t rate)
{
    dprintf("oxygen: Impostazione frequenza di campionamento a %u Hz...\n", rate);

    // 1. Selezione del cristallo di clock corretto (22.5792 MHz per famiglia 44.1k, 24.576 MHz per famiglia 48k)
    uint8_t misc = oxygen_read8(chip, OXYGEN_MISC);
    if (rate == 44100 || rate == 88200 || rate == 176400) {
        misc |= OXYGEN_MISC_CRYSTAL_27; // Seleziona il cristallo da 22.5792 MHz (etichettato 27 in reference)
    } else {
        misc &= ~OXYGEN_MISC_CRYSTAL_MASK; // Seleziona il cristallo da 24.576 MHz
    }
    oxygen_write8(chip, OXYGEN_MISC, misc);

    snooze(1000); // Attendi stabilità del Master Clock (1ms)

    // 2. Calcola i bit della frequenza CMI8788, divisore MCLK e oversampling del DAC
    uint16_t oxygen_rate_val = OXYGEN_RATE_48000;
    uint16_t mclk_val = OXYGEN_I2S_MCLK(MCLK_512);
    uint8_t pcm1796_os = PCM1796_OS_128;

    switch (rate) {
        case 32000:
            oxygen_rate_val = OXYGEN_RATE_32000;
            break;
        case 44100:
            oxygen_rate_val = OXYGEN_RATE_44100;
            break;
        case 48000:
            oxygen_rate_val = OXYGEN_RATE_48000;
            break;
        case 64000:
            oxygen_rate_val = OXYGEN_RATE_64000;
            mclk_val = OXYGEN_I2S_MCLK(MCLK_128); // double speed rates use MCLK 128
            pcm1796_os = PCM1796_OS_64;
            break;
        case 88200:
            oxygen_rate_val = OXYGEN_RATE_88200;
            mclk_val = OXYGEN_I2S_MCLK(MCLK_128);
            pcm1796_os = PCM1796_OS_64;
            break;
        case 96000:
            oxygen_rate_val = OXYGEN_RATE_96000;
            mclk_val = OXYGEN_I2S_MCLK(MCLK_128);
            pcm1796_os = PCM1796_OS_64;
            break;
        case 176400:
            oxygen_rate_val = OXYGEN_RATE_176400;
            mclk_val = OXYGEN_I2S_MCLK(MCLK_128); // quad speed rates
            pcm1796_os = PCM1796_OS_64;
            break;
        case 192000:
            oxygen_rate_val = OXYGEN_RATE_192000;
            mclk_val = OXYGEN_I2S_MCLK(MCLK_128);
            pcm1796_os = PCM1796_OS_64;
            break;
    }

    // Aggiorna OXYGEN_I2S_MULTICH_FORMAT (lasciando inalterati formato, bit e master)
    uint16_t i2s_format = oxygen_read16(chip, OXYGEN_I2S_MULTICH_FORMAT);
    i2s_format &= ~(OXYGEN_I2S_RATE_MASK | OXYGEN_I2S_MCLK_MASK);
    i2s_format |= (oxygen_rate_val | mclk_val);
    oxygen_write16(chip, OXYGEN_I2S_MULTICH_FORMAT, i2s_format);

    // 3. Configura via SPI l'oversampling rate corretto sui 4 DAC esterni PCM1796
    for (int i = 0; i < 4; i++) {
        xonar_d2_pcm1796_write(chip, i, PCM1796_REG_CONTROL_3, pcm1796_os);
    }

    chip->sample_rate = rate;
}

// Inizializzazione hardware specifica per la Xonar D2X
void
xonar_d2_init(oxygen_t *chip)
{
    dprintf("oxygen: Configurazione registri globali e GPIO per ASUS Xonar D2X...\n");

    // Configura GPIO come output: MUTE (GPIO 8) e ALT loopback (GPIO 7)
    uint16_t control = oxygen_read16(chip, OXYGEN_GPIO_CONTROL);
    control |= (XONAR_D2_GPIO_MUTE | XONAR_D2_GPIO_ALT);
    // Assicura che GPIO 5 (ext power detection) rimanga impostato come input
    control &= ~XONAR_D2X_EXT_POWER;
    oxygen_write16(chip, OXYGEN_GPIO_CONTROL, control);

    // Abilita la rilevazione dell'input sul GPIO 5 impostandone la maschera di interrupt (16-bit)
    // Questo attiva fisicamente il buffer di input hardware sul controller C-Media!
    uint16_t gpio_int = oxygen_read16(chip, OXYGEN_GPIO_INTERRUPT_MASK);
    gpio_int |= XONAR_D2X_EXT_POWER;
    oxygen_write16(chip, OXYGEN_GPIO_INTERRUPT_MASK, gpio_int);

    snooze(100); // Piccola pausa per la stabilizzazione elettrica

    // Controlla l'alimentazione esterna con un breve debounce per evitare falsi negativi all'avvio.
    uint16_t gpio_status = 0;
    int powered_samples = 0;
    for (int i = 0; i < 5; i++) {
        gpio_status = oxygen_read16(chip, OXYGEN_GPIO_DATA);
        if (gpio_status & XONAR_D2X_EXT_POWER)
            powered_samples++;
        snooze(1000);
    }
    if (powered_samples == 0) {
        dprintf("oxygen: ATTENZIONE! Nessuna alimentazione esterna floppy a 4 pin rilevata!\n");
    } else {
        dprintf("oxygen: Alimentazione esterna rilevata correttamente.\n");
    }

    // Attiva il MUTE hardware durante il setup iniziale per evitare "pop" sonori
    uint16_t data = oxygen_read16(chip, OXYGEN_GPIO_DATA);
    data &= ~XONAR_D2_GPIO_MUTE; // Mute attivo (basso)
    data &= ~XONAR_D2_GPIO_ALT;  // Loopback analogico spento
    oxygen_write16(chip, OXYGEN_GPIO_DATA, data);

    // Inizializzazione dei 4 chip Burr-Brown PCM1796 via SPI
    for (int i = 0; i < 4; i++) {
        // Control 1: Formato I2S standard a 24/32-bit, soft mute disattivato, caricamento volume automatico (ATLD)
        xonar_d2_pcm1796_write(chip, i, PCM1796_REG_CONTROL_1, PCM1796_DMF_DISABLED | PCM1796_FMT_24_I2S | PCM1796_ATLD);
        
        // Control 2: Filtro Sharp Roll-off e velocità di transizione attenuazione rapida (ATS_1)
        xonar_d2_pcm1796_write(chip, i, PCM1796_REG_CONTROL_2, PCM1796_FLT_SHARP | PCM1796_ATS_1);
        
        // Imposta l'attenuazione iniziale a 0 (volume massimo iniziale, o gestito dal mixer)
        xonar_d2_pcm1796_write(chip, i, PCM1796_REG_ATTN_L, 0xff);
        xonar_d2_pcm1796_write(chip, i, PCM1796_REG_ATTN_R, 0xff);
    }

    // Rilascia il MUTE (porta il pin GPIO alto) ora che i DAC sono pronti
    data |= XONAR_D2_GPIO_MUTE;
    oxygen_write16(chip, OXYGEN_GPIO_DATA, data);

    dprintf("oxygen: Xonar D2X inizializzata con successo.\n");
}

status_t
oxygen_chip_init(oxygen_t *chip)
{
    dprintf("oxygen: Inizializzazione globale del chip C-Media CMI8788...\n");
    
    // Inizializza i valori di mixer di default nello stato interno
    for (int i = 0; i < 8; i++) {
        chip->dac_volume[i] = 255; // Nessuna attenuazione, volume massimo
    }
    chip->dac_mute = false;
    chip->dac_filter = 0; // Sharp Roll-off
    chip->playing = false;
    chip->current_playback_buffer = 0;
    
    // Inizializza formato e frequenza di campionamento di default
    chip->format = B_FMT_32BIT;
    chip->sample_rate = 48000;

    // Crea il semaforo per il ping-pong del buffer
    chip->playback_sem = create_sem(0, "cmi8788_playback_sem");
    if (chip->playback_sem < 0) {
        dprintf("oxygen: Errore creazione semaforo playback\n");
        return chip->playback_sem;
    }

    // Forza reset dei codec esterni e attiva il controller SPI
    oxygen_write8_masked(chip, OXYGEN_FUNCTION,
                         OXYGEN_FUNCTION_RESET_CODEC | OXYGEN_FUNCTION_SPI | OXYGEN_FUNCTION_ENABLE_SPI_4_5,
                         OXYGEN_FUNCTION_RESET_CODEC | OXYGEN_FUNCTION_2WIRE_SPI_MASK | OXYGEN_FUNCTION_ENABLE_SPI_4_5);

    // Arresta i canali DMA prima della configurazione
    oxygen_write8(chip, OXYGEN_DMA_STATUS, 0);
    oxygen_write8(chip, OXYGEN_DMA_PAUSE, 0);
    
    // Configura i canali di riproduzione: 8 canali per il buffer DMA multicanale
    oxygen_write8(chip, OXYGEN_PLAY_CHANNELS,
                  OXYGEN_PLAY_CHANNELS_8 | OXYGEN_DMA_A_BURST_8 | OXYGEN_DMA_MULTICH_BURST_8);

    // Formato dati playback: 32-bit (container) per i DAC della Xonar D2X
    oxygen_write8(chip, OXYGEN_PLAY_FORMAT,
                  (OXYGEN_FORMAT_32 << OXYGEN_MULTICH_FORMAT_SHIFT));

    // Configura formato I2S della multicanale: 48kHz, Master, 32-bit, formato I2S, MCLK 512
    oxygen_write16(chip, OXYGEN_I2S_MULTICH_FORMAT,
                   OXYGEN_RATE_48000 | OXYGEN_I2S_FORMAT_I2S | OXYGEN_I2S_MCLK(MCLK_512) |
                   OXYGEN_I2S_BITS_32 | OXYGEN_I2S_MASTER | OXYGEN_I2S_BCLK_64);

    // Routing di riproduzione multicanale: mappa i flussi I2S direttamente ai DAC esterni
    oxygen_write16(chip, OXYGEN_PLAY_ROUTING,
                   OXYGEN_PLAY_MULTICH_I2S_DAC | OXYGEN_PLAY_SPDIF_SPDIF |
                   (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
                   (1 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
                   (2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
                   (3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT));

    // Esegui l'inizializzazione della scheda Xonar D2X
    xonar_d2_init(chip);

    return B_OK;
}

void
oxygen_chip_shutdown(addr_t mmio_base)
{
    dprintf("oxygen: Arresto del chip CMI8788...\n");
    
    // Mettere a muto l'uscita tramite GPIO per evitare pop allo spegnimento (16-bit)
    uint16_t data = *(volatile uint16_t *)(mmio_base + OXYGEN_GPIO_DATA);
    data &= ~XONAR_D2_GPIO_MUTE;
    *(volatile uint16_t *)(mmio_base + OXYGEN_GPIO_DATA) = data;

    // Disattiva flussi DMA
    *(volatile uint8_t *)(mmio_base + OXYGEN_DMA_STATUS) = 0;
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
    if (device->playback_sem >= 0) {
        delete_sem(device->playback_sem);
        device->playback_sem = -1;
    }
}

status_t
cmi8788_setup_interrupts(cmi8788_device *device)
{
	uint8 irq = device->pci_info.u.h0.interrupt_line;
	
	dprintf("cmi8788: Installazione interrupt su IRQ %u\n", irq);
	
	status_t status = install_io_interrupt_handler(irq, cmi8788_interrupt, (void *)device, 0);
	if (status < B_OK) {
		dprintf("cmi8788: Errore installazione interrupt handler (%s)\n", strerror(status));
		return status;
	}

	// Abilita gli interrupt per la multicanale nel registro di maschera del chip (16-bit)
	uint16 mask = oxygen_read16(device, OXYGEN_INTERRUPT_MASK);
	mask |= OXYGEN_CHANNEL_MULTICH;
	oxygen_write16(device, OXYGEN_INTERRUPT_MASK, mask);

	return B_OK;
}

void
cmi8788_remove_interrupts(cmi8788_device *device)
{
	uint8 irq = device->pci_info.u.h0.interrupt_line;
	
	// Disattiva le maschere (16-bit)
	oxygen_write16(device, OXYGEN_INTERRUPT_MASK, 0);
	
	// Rimuovi l'handler dal kernel
	remove_io_interrupt_handler(irq, cmi8788_interrupt, (void *)device);
}

// Funzione di scrittura sicura sui registri GPIO del CMI8788
void cmi8788_gpio_set(cmi8788_device *device, uint16 data, uint16 mask)
{
    // Leggi lo stato attuale dei GPIO (16-bit)
    uint16 ctrl = oxygen_read16(device, OXYGEN_GPIO_CONTROL);
    // Assicurati che i pin siano configurati come output (bit a 1 nella control mask)
    oxygen_write16(device, OXYGEN_GPIO_CONTROL, ctrl | mask);

    uint16 val = oxygen_read16(device, OXYGEN_GPIO_DATA);
    val = (val & ~mask) | (data & mask);
    oxygen_write16(device, OXYGEN_GPIO_DATA, val);
}

void cmi8788_set_mute(cmi8788_device *device, bool mute)
{
    uint16 data = mute ? 0x0000 : XONAR_D2_GPIO_MUTE;
    cmi8788_gpio_set(device, data, XONAR_D2_GPIO_MUTE);
    device->dac_mute = mute;
}
