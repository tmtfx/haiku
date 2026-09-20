/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <Drivers.h>
#include <PCI.h>
#include <hmulti_audio.h>

#include "cmi8788.h"

static pci_module_info *gPci;


static cmi8788_device sDataDevice;

status_t
cmi8788_map_registers(cmi8788_device *device)
{
	// Ottieni l'indirizzo fisico della BAR della memoria (BAR 0 o BAR 1 a seconda del layout)
	addr_t mmio_paddr = device->pci_info.u.h0.base_registers[0];
	size_t mmio_size = device->pci_info.u.h0.base_register_sizes[0];

	device->mmio_area = map_physical_memory(
		"cmi8788_mmio",
		(phys_addr_t)mmio_paddr,
		mmio_size,
		B_ANY_KERNEL_ADDRESS,
		B_READ_AREA | B_WRITE_AREA,
		(void **)&device->mmio_base
	);

	if (device->mmio_area < B_OK) {
		dprintf("cmi8788: Errore nella mappatura della memoria MMIO (%s)\n", strerror(device->mmio_area));
		return device->mmio_area;
	}

	dprintf("cmi8788: Memoria MMIO mappata a 0x%lx (fisica: 0x%lx, dimensione: %lu)\n",
		device->mmio_base, mmio_paddr, mmio_size);

	return B_OK;
}
// Funzione ISR (Interrupt Service Routine) richiamata dal kernel all'hardware interrupt
int32
cmi8788_interrupt(void *data)
{
	cmi8788_device *device = (cmi8788_device *)data;
	
	// Leggi lo stato degli interrupt dal registro MMIO del CMI8788
	uint8 status = *(volatile uint8 *)(device->mmio_base + OXYGEN_INTERRUPT_STATUS);
	
	if (status == 0)
		return B_UNHANDLED_INTERRUPT; // L'interrupt non appartiene a questa scheda

	// Pulisci i flag di interrupt scrivendoli indietro
	*(volatile uint8 *)(device->mmio_base + OXYGEN_INTERRUPT_STATUS) = status;

	if (status & OXYGEN_INT_PLAYBACK) {
		// Notifica al framework multi_audio di Haiku che un frammento DMA è stato riprodotto
		// e che possiamo riempire il successivo blocco del ring buffer.
		// (In Haiku si usa una notifica tramite condition variable o blocco semaforo)
	}

	return B_HANDLED_INTERRUPT;
}


static int32
cmi8788_open(const char *name, uint32 flags, void **cookie)
{
	dprintf("cmi8788: open() called\n");
	
	cmi8788_device *device = &sDataDevice;
	
	// Se non è ancora stato inizializzato, mappiamo i registri e avviamo il chip
	if (!device->initialized) {
		pci_info info;
		int index = 0;
		bool found = false;

		while ((*gPci->get_nth_pci_info)(index, &info) == B_OK) {
			if (info.vendor_id == CMEDIA_VENDOR_ID && info.device_id == CMI8788_DEVICE_ID) {
				device->pci_info = info;
				found = true;
				break;
			}
			index++;
		}

		if (!found)
			return B_DEVICE_NOT_FOUND;

		status_t status = cmi8788_map_registers(device);
		if (status < B_OK)
			return status;

		// Alloca il buffer DMA circolare (es. 16 KB)
		status = oxygen_init_dma_buffer(device, 16384);
		if (status < B_OK) {
			delete_area(device->mmio_area);
			return status;
		}

		// Configura gli interrupt IRQ
		status = cmi8788_setup_interrupts(device);
		if (status < B_OK) {
			oxygen_free_dma_buffer(device);
			delete_area(device->mmio_area);
			return status;
		}
		
		// Inizializza il chip e i DAC esterni via I2C
		status = oxygen_chip_init(device);
		if (status < B_OK) {
			cmi8788_remove_interrupts(device);
			oxygen_free_dma_buffer(device);
			delete_area(device->mmio_area);
			return status;
		}

		device->initialized = true;
	}

	*cookie = device;
	return B_OK;
}

static int32
cmi8788_close(void *cookie)
{
	dprintf("cmi8788: close() called\n");
	return B_OK;
}

static int32
cmi8788_free(void *cookie)
{
	return B_OK;
}


// Funzione di descrizione delle capacità della D2X per il framework multi_audio di Haiku
status_t
cmi8788_get_capabilities(cmi8788_device *device, multi_description *data)
{
    if (data == NULL)
        return B_BAD_VALUE;

    memset(data, 0, sizeof(multi_description));

    data->info_size = sizeof(multi_description);
    data->interface_version = 1;
    data->interface_minimum = 1;

    strlcpy(data->friendly_name, "ASUS Xonar D2X / CMI8788", sizeof(data->friendly_name));
    strlcpy(data->vendor_info, "ASUS / Burr-Brown PCM1796", sizeof(data->vendor_info));

    // La Xonar D2X gestisce 8 canali in output (7.1) e 8 canali in input tramite i Burr-Brown
    data->output_channel_count = 8;
    data->input_channel_count = 8;
    data->output_bus_channel_count = 0;
    data->input_bus_channel_count = 0;
    data->aux_bus_channel_count = 0;

    // Frequenze supportate dai PCM1796 e dal CMI8788 (fino a 192kHz)
    data->output_rates = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;
    data->input_rates  = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;

    // Formato nativo supportato dai DAC PCM1796 (32-bit container / 24-bit audio)
    data->output_formats = B_FMT_32BIT;
    data->input_formats  = B_FMT_32BIT;

    return B_OK;
}
/* OK
static status_t
cmi8788_get_capabilities(cmi8788_device *device, multi_description *data)
{
    if (data == NULL)
        return B_BAD_VALUE;

    memset(data, 0, sizeof(multi_description));

    data->info_size = sizeof(multi_description);
    data->interface_version = 1;
    data->interface_minimum = 1;

    strlcpy(data->friendly_name, "ASUS Xonar DX / CMI8788", sizeof(data->friendly_name));
    strlcpy(data->vendor_info, "C-Media / ASUS", sizeof(data->vendor_info));

    data->output_channel_count = 8;
    data->input_channel_count = 2;
    data->output_bus_channel_count = 0;
    data->input_bus_channel_count = 0;
    data->aux_bus_channel_count = 0;

    // Usa i prefissi corretti B_SR_
    data->output_rates = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;
    data->input_rates  = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;

    // Usa B_FMT_32BIT suggerito dal compilatore
    data->output_formats = B_FMT_32BIT; 
    data->input_formats  = B_FMT_32BIT;

    return B_OK;
}*/
static int32
cmi8788_control(void *cookie, uint32 op, void *arg, size_t length)
{
    cmi8788_device *device = (cmi8788_device *)cookie;
    if (device == NULL)
        return B_BAD_VALUE;

    switch (op) {
        case B_MULTI_GET_DESCRIPTION:
            return cmi8788_get_capabilities(device, (multi_description *)arg);

case B_MULTI_GET_BUFFERS:
{
    multi_buffer_list *data = (multi_buffer_list *)arg;
    
    int32 num_buffers = data->request_playback_buffers > 0 ? data->request_playback_buffers : 2;
    int32 channels = data->request_playback_channels > 0 ? data->request_playback_channels : 8; // 8 canali per la D2X
    uint32 buffer_size_frames = data->request_playback_buffer_size > 0 ? data->request_playback_buffer_size : 1024;
    
    data->return_playback_buffers = num_buffers;
    data->return_playback_channels = channels;
    data->return_playback_buffer_size = buffer_size_frames;
    
    size_t chunk_size = buffer_size_frames * channels * sizeof(int32);
    
    for (int b = 0; b < num_buffers; b++) {
        for (int c = 0; c < channels; c++) {
            data->playback_buffers[b][c].base = (char *)device->dma_pub_base 
                + (b * chunk_size) + (c * buffer_size_frames * sizeof(int32));
            data->playback_buffers[b][c].stride = chunk_size;
        }
    }
    
    data->return_record_buffers = 0;
    data->return_record_channels = 0;
    data->return_record_buffer_size = 0;
    
    return B_OK;
}
        case B_MULTI_BUFFER_EXCHANGE:
        {
            multi_buffer_info *data = (multi_buffer_info *)arg;
            (void)data; // Evita il warning di variabile non usata
            // Gestione dello scambio dei buffer audio
            return B_OK;
        }

        case B_MULTI_BUFFER_FORCE_STOP:
            // Stop forzato dello streaming
            return B_OK;

        default:
            return B_BAD_VALUE;
    }
}

static int32
cmi8788_read(void *cookie, off_t position, void *buffer, size_t *_numBytes)
{
	*_numBytes = 0;
	return B_NOT_ALLOWED;
}

static int32
cmi8788_write(void *cookie, off_t position, const void *buffer, size_t *_numBytes)
{
	*_numBytes = 0;
	return B_NOT_ALLOWED;
}

static device_hooks sDeviceHooks = {
	cmi8788_open,
	cmi8788_close,
	cmi8788_free,
	cmi8788_control,
	cmi8788_read,
	cmi8788_write,
	NULL,    // select
	NULL,    // deselect
	NULL,    // read_vnet
	NULL     // write_vnet
};

const char *gDeviceNames[] = {
	"audio/raw/cmi8788/1",    // Nome standard per i device audio grezzi su Haiku
	NULL
};

status_t
init_hardware(void)
{
	pci_info info;
	int index = 0;
	
	if (get_module(B_PCI_MODULE_NAME, (module_info **)&gPci) < B_OK)
		return B_ERROR;

	status_t result = B_DEVICE_NOT_FOUND;
	while ((*gPci->get_nth_pci_info)(index, &info) == B_OK) {
		if (info.vendor_id == CMEDIA_VENDOR_ID && info.device_id == CMI8788_DEVICE_ID) {
			dprintf("cmi8788: Trovata scheda audio CMI8788 compatibile!\n");
			result = B_OK;
			break;
		}
		index++;
	}

	put_module(B_PCI_MODULE_NAME);
	return result;
}

status_t
init_driver(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info **)&gPci) < B_OK)
		return B_ERROR;
		
	dprintf("cmi8788: Driver caricato con successo.\n");
	return B_OK;
}

void
uninit_driver(void)
{
	cmi8788_device *device = &sDataDevice;
	if (device->initialized) {
		oxygen_chip_shutdown(device->mmio_base);
		cmi8788_remove_interrupts(device);
		oxygen_free_dma_buffer(device);
		delete_area(device->mmio_area);
		device->initialized = false;
	}

	if (gPci != NULL)
		put_module(B_PCI_MODULE_NAME);
}
const char **
publish_devices(void)
{
	return gDeviceNames;
}

device_hooks *
find_device(const char *name)
{
	return &sDeviceHooks;
}
