/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef CMI8788_H
#define CMI8788_H

#include <KernelExport.h>
#include <Drivers.h>
#include <audio_driver.h>
#include <OS.h>
#include <PCI.h>
#include <cstring>
#include <Errors.h>

// Forward declaration o inclusione di multi_audio.h se necessario
//#include "multi_audio.h"

#define CMEDIA_VENDOR_ID    0x13f6
#define CMI8788_DEVICE_ID   0x8788

// Registri di interrupt del CMI8788
#define OXYGEN_INTERRUPT_STATUS		0x02
#define OXYGEN_INTERRUPT_MASK		0x03
#define   OXYGEN_INT_PLAYBACK		(1 << 0)
#define   OXYGEN_INT_CAPTURE		(1 << 1)

// Registri di controllo globali OxygenHD
#define OXYGEN_FUNCTION_CONTROL		0x6a // Esempio offset registro funzionale
// Registri di controllo CMI8788 (Oxygen HD)
#define OXYGEN_GPIO_DATA            0x0e
#define OXYGEN_GPIO_CONTROL         0x0f
#define OXYGEN_SPI_CONTROL          0x30
#define OXYGEN_SPI_DATA             0x31
#define OXYGEN_SPI_STATUS           0x32

// Mappatura GPIO specifica per la Xonar D2 / D2X
// La D2X utilizza i GPIO per pilotare i relè di protezione e i circuiti di muting analogico
#define XONAR_D2_GPIO_MUTE          0x0001  // Relè di muting generale d'uscita
#define XONAR_D2_GPIO_LED_MASK      0x00f0  // Maschera per i LED di stato/connettori della D2X

// Configurazione SPI per i DAC Burr-Brown PCM1796
// La Xonar D2X ha 4 chip PCM1796 indirizzati tramite chip select (CS) o ID specifici sul bus SPI
#define PCM1796_REG_CONTROL_1       0x01
#define PCM1796_REG_CONTROL_2       0x02
#define PCM1796_REG_ATTN_L          0x03
#define PCM1796_REG_ATTN_R          0x04

// Definizione della struct principale del device che ora manca a oxygen.cpp
typedef struct cmi8788_device {
    struct pci_info     pci_info;
    area_id             mmio_area;
    addr_t              mmio_base;
    bool                initialized;
    
    // Campi per il buffer DMA
    area_id             dma_area;
    void*               dma_pub_base;
    phys_addr_t         dma_phy_base;
    size_t              dma_buffer_size;
} cmi8788_device;

typedef cmi8788_device oxygen_t;

// Funzioni inline di accesso ai registri MMIO del CMI8788
static inline uint8_t
oxygen_read8(oxygen_t *chip, uint32_t reg)
{
    return *(volatile uint8_t *)(chip->mmio_base + reg);
}

static inline void
oxygen_write8(oxygen_t *chip, uint32_t reg, uint8_t value)
{
    *(volatile uint8_t *)(chip->mmio_base + reg) = value;
}

static inline uint16_t
oxygen_read16(oxygen_t *chip, uint32_t reg)
{
    return *(volatile uint16_t *)(chip->mmio_base + reg);
}

static inline void
oxygen_write16(oxygen_t *chip, uint32_t reg, uint16_t value)
{
    *(volatile uint16_t *)(chip->mmio_base + reg) = value;
}

static inline uint32_t
oxygen_read32(oxygen_t *chip, uint32_t reg)
{
    return *(volatile uint32_t *)(chip->mmio_base + reg);
}

static inline void
oxygen_write32(oxygen_t *chip, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(chip->mmio_base + reg) = value;
}




// Prototipi delle funzioni definite in oxygen.cpp (o file correlati)
status_t oxygen_chip_init(oxygen_t *chip);
void oxygen_chip_shutdown(addr_t mmio_base);
status_t oxygen_init_dma_buffer(cmi8788_device *device, size_t size);
void oxygen_free_dma_buffer(cmi8788_device *device);
status_t cmi8788_setup_interrupts(cmi8788_device *device);
void cmi8788_remove_interrupts(cmi8788_device *device);
status_t oxygen_i2c_write(addr_t mmio_base, uint8 device_addr, uint8 reg, uint16 data);
int32 cmi8788_interrupt(void *data);

#endif /* CMI8788_H */
