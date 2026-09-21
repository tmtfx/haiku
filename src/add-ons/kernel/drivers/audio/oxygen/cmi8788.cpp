/*
 * Copyright 2026, Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <Drivers.h>
#include <PCI.h>
#include <hmulti_audio.h>

#include "cmi8788.h"

// Costanti hmulti_audio private per i controlli mixer
#define B_MULTI_MIX_GROUP     0x10
#define B_MULTI_MIX_GAIN      0x2
#define B_MULTI_MIX_ENABLE    0x8

#define CALLED() dprintf("CMI8788: CALLED %s\n", __FUNCTION__)

int32 api_version = B_CUR_DRIVER_API_VERSION;

pci_module_info *gPci;
cmi8788_device sDataDevice;

static status_t
cmi8788_map_registers(cmi8788_device *device)
{
	CALLED();
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
	CALLED();
    cmi8788_device *device = (cmi8788_device *)data;
    
    // Leggi lo stato degli interrupt dal registro MMIO del CMI8788
    uint16_t status = oxygen_read16(device, OXYGEN_INTERRUPT_STATUS);
    if (status == 0 || status == 0xffff)
        return B_UNHANDLED_INTERRUPT;
    
    if (status & OXYGEN_CHANNEL_MULTICH) {
        // Pulisci l'interrupt scrivendo 1 sul canale multicanale
        oxygen_write16(device, OXYGEN_INTERRUPT_STATUS, OXYGEN_CHANNEL_MULTICH);
        
        if (device->playing) {
            // Avanza il buffer cycle (ping-pong)
            device->current_playback_buffer = (device->current_playback_buffer + 1) % 2;
            // Sblocca il thread in attesa (B_MULTI_BUFFER_EXCHANGE)
            release_sem_etc(device->playback_sem, 1, B_DO_NOT_RESCHEDULE);
        }
    }

    return B_HANDLED_INTERRUPT;
}

static int32
cmi8788_open(const char *name, uint32 flags, void **cookie)
{
	dprintf("cmi8788: open() called\n");
	
	cmi8788_device *device = &sDataDevice;
	
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

		// Alloca il buffer DMA circolare (128 KB per ospitare 8 canali a 32-bit interleaved con latenza ridotta)
		status = oxygen_init_dma_buffer(device, 131072);
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
		
		// Inizializza il chip e i DAC esterni via SPI
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

status_t
cmi8788_get_capabilities(cmi8788_device *device, multi_description *data)
{
	CALLED();
    if (data == NULL)
        return B_BAD_VALUE;

    memset(data, 0, sizeof(multi_description));

    data->info_size = sizeof(multi_description);
    data->interface_version = 1;
    data->interface_minimum = 1;

    strlcpy(data->friendly_name, "ASUS Xonar D2X / CMI8788", sizeof(data->friendly_name));
    strlcpy(data->vendor_info, "ASUS / Burr-Brown PCM1796", sizeof(data->vendor_info));

    // La Xonar D2X gestisce 8 canali in output (7.1)
    data->output_channel_count = 8;
    data->input_channel_count = 0;
    data->output_bus_channel_count = 0;
    data->input_bus_channel_count = 0;
    data->aux_bus_channel_count = 0;

    // Frequenze supportate dai PCM1796 e dal CMI8788 (Xonar D2X lavora nativamente a 48kHz, 96kHz, 192kHz)
    data->output_rates = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;
    data->input_rates  = 0;

    // Formato nativo supportato dai DAC PCM1796: 32-bit container / 24-bit audio
    data->output_formats = B_FMT_32BIT;
    data->input_formats  = 0;

    return B_OK;
}

static int32
cmi8788_control(void *cookie, uint32 op, void *arg, size_t length)
{
	CALLED();
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
            
            device->channels = channels;
            device->buffer_size_frames = buffer_size_frames;
    
            size_t single_buffer_bytes = buffer_size_frames * channels * sizeof(int32);
    
            // Mappatura Interleaved dei buffer per DMA del CMI8788
            for (int b = 0; b < num_buffers; b++) {
                for (int c = 0; c < channels; c++) {
                    data->playback_buffers[b][c].base = (char *)device->dma_pub_base 
                        + (b * single_buffer_bytes) + (c * sizeof(int32));
                    data->playback_buffers[b][c].stride = channels * sizeof(int32);
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
            
            if (!device->playing) {
                // Avvia i registri DMA del CMI8788
                oxygen_write32(device, OXYGEN_DMA_MULTICH_ADDRESS, device->dma_phy_base);
                oxygen_write32(device, OXYGEN_DMA_MULTICH_COUNT, (device->dma_buffer_size / 4) - 1);
                
                // Imposta TCOUNT per generare interrupt a metà buffer (ping-pong)
                size_t period_size_bytes = device->dma_buffer_size / 2;
                oxygen_write32(device, OXYGEN_DMA_MULTICH_TCOUNT, (period_size_bytes / 4) - 1);
                
                device->playing = true;
                device->current_playback_buffer = 0;
                
                // Abilita il DMA multicanale
                uint8_t dma_status = oxygen_read8(device, OXYGEN_DMA_STATUS);
                dma_status |= OXYGEN_CHANNEL_MULTICH;
                oxygen_write8(device, OXYGEN_DMA_STATUS, dma_status);
            }
            
            // Attendi interrupt dal thread hardware
            status_t status = acquire_sem(device->playback_sem);
            if (status < B_OK)
                return status;
                
            data->playback_buffer_cycle = device->current_playback_buffer;
            data->played_real_time = system_time();
            data->played_frames_count += device->buffer_size_frames;
            
            data->record_buffer_cycle = 0;
            data->recorded_real_time = system_time();
            data->recorded_frames_count = 0;
            
            return B_OK;
        }

        case B_MULTI_BUFFER_FORCE_STOP:
        {
            if (device->playing) {
                device->playing = false;
                
                // Disabilita il canale DMA multicanale
                uint8_t dma_status = oxygen_read8(device, OXYGEN_DMA_STATUS);
                dma_status &= ~OXYGEN_CHANNEL_MULTICH;
                oxygen_write8(device, OXYGEN_DMA_STATUS, dma_status);
            }
            return B_OK;
        }

        case B_MULTI_LIST_MIX_CONTROLS:
        {
            multi_mix_control_info *info = (multi_mix_control_info *)arg;
            if (info == NULL)
                return B_BAD_VALUE;
                
            int32 count = 0;
            multi_mix_control *controls = info->controls;
            
            // 1. Gruppo Master
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 100;
                controls[count].flags = B_MULTI_MIX_GROUP;
                controls[count].master = 0;
                controls[count].parent = 0;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Uscite Master", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 2. Master Volume Slider
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 101;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Riproduzione", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 3. Master Mute Toggle
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 102;
                controls[count].flags = B_MULTI_MIX_ENABLE;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Mute", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 4. DAC Filter Choice (Sharp vs Slow)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 103;
                controls[count].flags = B_MULTI_MIX_ENABLE; 
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Filtro DAC Sharp", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            info->control_count = count;
            return B_OK;
        }

        case B_MULTI_GET_MIX:
        {
            multi_mix_value_info *info = (multi_mix_value_info *)arg;
            if (info == NULL)
                return B_BAD_VALUE;
                
            for (int32 i = 0; i < info->item_count; i++) {
                int32 id = info->values[i].id;
                if (id == 101) {
                    // Restituisce volume left/right (mappato su dac_volume[0] e [1])
                    float gain_db = -60.0f + ((float)device->dac_volume[0] * (60.0f / 255.0f));
                    info->values[i].gain = gain_db;
                } else if (id == 102) {
                    info->values[i].enable = device->dac_mute;
                } else if (id == 103) {
                    info->values[i].enable = (device->dac_filter == 0);
                }
            }
            return B_OK;
        }

        case B_MULTI_SET_MIX:
        {
            multi_mix_value_info *info = (multi_mix_value_info *)arg;
            if (info == NULL)
                return B_BAD_VALUE;
                
            for (int32 i = 0; i < info->item_count; i++) {
                int32 id = info->values[i].id;
                if (id == 101) {
                    float gain_db = info->values[i].gain;
                    if (gain_db < -60.0f) gain_db = -60.0f;
                    if (gain_db > 0.0f) gain_db = 0.0f;
                    uint8_t raw_vol = (uint8_t)((gain_db + 60.0f) * (255.0f / 60.0f));
                    
                    // Assegna a tutti gli 8 canali per la riproduzione uniforme
                    for (int ch = 0; ch < 8; ch++) {
                        device->dac_volume[ch] = raw_vol;
                    }
                    
                    // Applica l'attenuazione (0..255) ai 4 DAC PCM1796 via SPI
                    for (int codec = 0; codec < 4; codec++) {
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_ATTN_L, raw_vol);
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_ATTN_R, raw_vol);
                    }
                } else if (id == 102) {
                    bool mute = info->values[i].enable;
                    cmi8788_set_mute(device, mute);
                } else if (id == 103) {
                    bool sharp = info->values[i].enable;
                    device->dac_filter = sharp ? 0 : 1;
                    uint8_t filter_reg = sharp ? PCM1796_FLT_SHARP : PCM1796_FLT_SLOW;
                    
                    // Imposta il filtro Sharp/Slow Roll-off sui 4 DAC
                    for (int codec = 0; codec < 4; codec++) {
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_CONTROL_2, filter_reg | PCM1796_ATS_1);
                    }
                }
            }
            return B_OK;
        }

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

device_hooks sDeviceHooks = {
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
	//"audio/raw/cmi8788/1",
	"audio/hmulti/cmi8788/1",
	NULL
};

extern "C" status_t
init_hardware(void)
{
	CALLED();
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

extern "C" status_t
init_driver(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info **)&gPci) < B_OK)
		return B_ERROR;
		
	dprintf("cmi8788: Driver caricato con successo.\n");
	return B_OK;
}

extern "C" void
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

extern "C" const char **
publish_devices(void)
{
	return gDeviceNames;
}

extern "C" device_hooks *
find_device(const char *name)
{
	return &sDeviceHooks;
}
