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

#define MAX_CARDS 4
static cmi8788_device sCards[MAX_CARDS];
static uint32 gNumCards = 0;
static const char *sDeviceNames[MAX_CARDS + 1];

static uint32_t
sample_rate_to_hz(uint32_t rate)
{
	switch (rate) {
		case B_SR_8000:   return 8000;
		case B_SR_11025:  return 11025;
		case B_SR_12000:  return 12000;
		case B_SR_16000:  return 16000;
		case B_SR_22050:  return 22050;
		case B_SR_24000:  return 24000;
		case B_SR_32000:  return 32000;
		case B_SR_44100:  return 44100;
		case B_SR_48000:  return 48000;
		case B_SR_64000:  return 64000;
		case B_SR_88200:  return 88200;
		case B_SR_96000:  return 96000;
		case B_SR_176400: return 176400;
		case B_SR_192000: return 192000;
		default:          return 48000;
	}
}

static uint32_t
hz_to_sample_rate(uint32_t hz)
{
	switch (hz) {
		case 8000:   return B_SR_8000;
		case 11025:  return B_SR_11025;
		case 12000:  return B_SR_12000;
		case 16000:  return B_SR_16000;
		case 22050:  return B_SR_22050;
		case 24000:  return B_SR_24000;
		case 32000:  return B_SR_32000;
		case 44100:  return B_SR_44100;
		case 48000:  return B_SR_48000;
		case 64000:  return B_SR_64000;
		case 88200:  return B_SR_88200;
		case 96000:  return B_SR_96000;
		case 176400: return B_SR_176400;
		case 192000: return B_SR_192000;
		default:          return B_SR_48000;
	}
}

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
	CALLED();
	dprintf("cmi8788: open() called on %s\n", name);
	
	cmi8788_device *device = NULL;
	for (uint32 i = 0; i < gNumCards; i++) {
		if (strcmp(name, sCards[i].devfs_path) == 0) {
			device = &sCards[i];
			break;
		}
	}

	if (device == NULL)
		return B_DEVICE_NOT_FOUND;
	
	if (!device->initialized) {
		device->dma_user_area = -1;
		device->dma_user_base = NULL;

		// Allinea i comandi PCI: abilita Bus Master e le mappature di memoria/IO (fondamentale per DMA!)
		// Inoltre disabilita il bit PCI_command_int_disable (0x0400) per abilitare fisicamente gli interrupt INTx legacy!
		uint16 pcicmd = (*gPci->read_pci_config)(device->pci_info.bus, 
			device->pci_info.device, device->pci_info.function, PCI_command, 2);
		pcicmd |= PCI_command_master | PCI_command_memory | PCI_command_io;
		pcicmd &= ~0x0400; // Pulisce bit 10 per sbloccare gli interrupt INTx hardware
		(*gPci->write_pci_config)(device->pci_info.bus, 
			device->pci_info.device, device->pci_info.function, PCI_command, 2, pcicmd);

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

    // Preserva i puntatori e i conteggi allocati dallo user-space
    multi_channel_info *user_channels = data->channels;
    int32 request_count = data->request_channel_count;

    // Inizializza i campi singolarmente per non sovrascrivere i puntatori utente
    data->info_size = sizeof(multi_description);
    data->interface_version = 1;
    data->interface_minimum = 1;

    strlcpy(data->friendly_name, "ASUS Xonar D2X", sizeof(data->friendly_name));
    strlcpy(data->vendor_info, "ASUS / C-Media", sizeof(data->vendor_info));

    data->max_cvsr_rate = 0;
    data->min_cvsr_rate = 0;
	
    data->output_channel_count = 10; // 8 analogici + 2 digitali S/PDIF
    data->input_channel_count = 4;   // 2 analogici (Line/Mic) + 2 digitali S/PDIF In
    data->output_bus_channel_count = 10;
    data->input_bus_channel_count = 4;
    data->aux_bus_channel_count = 0;

    data->lock_sources = B_MULTI_LOCK_INTERNAL;
    data->timecode_sources = 0;
    data->interface_flags = B_MULTI_INTERFACE_PLAYBACK | B_MULTI_INTERFACE_RECORD;
    data->start_latency = 30000;
    data->control_panel[0] = '\0';

    int idx = 0;
    
    // 1. Output Analogici (7.1 Surround - 8 canali su mini-jack)
    uint32 out_designations[8] = {
        B_CHANNEL_LEFT, B_CHANNEL_RIGHT,
        B_CHANNEL_CENTER, B_CHANNEL_SUB,
        B_CHANNEL_REARLEFT, B_CHANNEL_REARRIGHT,
        B_CHANNEL_SIDE_LEFT, B_CHANNEL_SIDE_RIGHT
    };

    for (int i = 0; i < 8; i++, idx++) {
        device->channel_infos[idx].channel_id = idx;
        device->channel_infos[idx].kind = B_MULTI_OUTPUT_CHANNEL;
        device->channel_infos[idx].designations = out_designations[i];
        device->channel_infos[idx].connectors = B_CHANNEL_MINI_JACK_STEREO;
    }
    
    // 2. Output Digitali Coassiali (S/PDIF Out - 2 canali sui connettori RCA)
    device->channel_infos[idx].channel_id = idx;
    device->channel_infos[idx].kind = B_MULTI_OUTPUT_CHANNEL;
    device->channel_infos[idx].designations = B_CHANNEL_LEFT;
    device->channel_infos[idx].connectors = B_CHANNEL_COAX_SPDIF;
    idx++;

    device->channel_infos[idx].channel_id = idx;
    device->channel_infos[idx].kind = B_MULTI_OUTPUT_CHANNEL;
    device->channel_infos[idx].designations = B_CHANNEL_RIGHT;
    device->channel_infos[idx].connectors = B_CHANNEL_COAX_SPDIF;
    idx++;

    // 3. Input Analogici (Line-In / Mic - 2 canali su mini-jack)
    device->channel_infos[idx].channel_id = idx;
    device->channel_infos[idx].kind = B_MULTI_INPUT_CHANNEL;
    device->channel_infos[idx].designations = B_CHANNEL_LEFT;
    device->channel_infos[idx].connectors = B_CHANNEL_MINI_JACK_STEREO;
    idx++;

    device->channel_infos[idx].channel_id = idx;
    device->channel_infos[idx].kind = B_MULTI_INPUT_CHANNEL;
    device->channel_infos[idx].designations = B_CHANNEL_RIGHT;
    device->channel_infos[idx].connectors = B_CHANNEL_MINI_JACK_STEREO;
    idx++;

    // 4. Input Digitali Coassiali (S/PDIF In - 2 canali sui connettori RCA)
    device->channel_infos[idx].channel_id = idx;
    device->channel_infos[idx].kind = B_MULTI_INPUT_CHANNEL;
    device->channel_infos[idx].designations = B_CHANNEL_LEFT;
    device->channel_infos[idx].connectors = B_CHANNEL_COAX_SPDIF;
    idx++;

    device->channel_infos[idx].channel_id = idx;
    device->channel_infos[idx].kind = B_MULTI_INPUT_CHANNEL;
    device->channel_infos[idx].designations = B_CHANNEL_RIGHT;
    device->channel_infos[idx].connectors = B_CHANNEL_COAX_SPDIF;
    idx++;

    // Copia i dati all'utente salvaguardando lo spazio allocato
    int32 copy_count = request_count < 14 ? request_count : 14;
    if (user_channels != NULL && copy_count > 0) {
        memcpy(user_channels, device->channel_infos, copy_count * sizeof(multi_channel_info));
    }
    
    data->channels = user_channels;
    data->request_channel_count = request_count;

    // Frequenze supportate dai PCM1796 e dal CMI8788 (Xonar D2X lavora nativamente a 48kHz, 96kHz, 192kHz)
    data->output_rates = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;
    data->input_rates  = B_SR_44100 | B_SR_48000 | B_SR_96000 | B_SR_192000;
    
    // Formato nativo supportato dai DAC PCM1796: 32-bit container / 24-bit audio
    data->output_formats = B_FMT_32BIT;
    data->input_formats  = B_FMT_32BIT;

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

        case B_MULTI_GET_ENABLED_CHANNELS:
        {
            multi_channel_enable *data = (multi_channel_enable *)arg;
            if (data == NULL)
                return B_BAD_VALUE;
                
            data->lock_source = B_MULTI_LOCK_INTERNAL;
            
            // Abilita tutti i 14 canali (10 output e 4 input) per far capire alla preflet di usarli
            for (int32 i = 0; i < 14; i++) {
                B_SET_CHANNEL(data->enable_bits, i, true);
            }
            
            return B_OK;
        }

        case B_MULTI_SET_ENABLED_CHANNELS:
        {
            // Non c'è bisogno di fare nulla, accettiamo la configurazione
            return B_OK;
        }

        case B_MULTI_GET_GLOBAL_FORMAT:
        {
            multi_format_info *data = (multi_format_info *)arg;
            if (data == NULL)
                return B_BAD_VALUE;
                
            data->output_latency = 0;
            data->input_latency = 0;
            data->timecode_kind = 0;
            
            data->output.format = device->format;
            data->output.rate = hz_to_sample_rate(device->sample_rate);
            
            data->input.format = 0;
            data->input.rate = 0;
            
            return B_OK;
        }

        case B_MULTI_SET_GLOBAL_FORMAT:
        {
            multi_format_info *data = (multi_format_info *)arg;
            if (data == NULL)
                return B_BAD_VALUE;

            device->format = data->output.format;
            
            // Converte il bitmask B_SR_ in frequenza grezza Hz per il chip e DAC
            uint32_t raw_hz = sample_rate_to_hz(data->output.rate);
            device->sample_rate = raw_hz;

            // Riconfigura la frequenza di campionamento, i cristalli PLL e l'oversampling dei DAC esterni
            xonar_d2_set_sample_rate(device, raw_hz);
            return B_OK;
        }

        case B_MULTI_GET_BUFFERS:
        {
            multi_buffer_list *data = (multi_buffer_list *)arg;
            if (data == NULL)
                return B_BAD_VALUE;
    
            int32 num_buffers = data->request_playback_buffers > 0 ? data->request_playback_buffers : 2;
            int32 channels = data->request_playback_channels > 0 ? data->request_playback_channels : 8; // 8 canali per la D2X
            uint32 buffer_size_frames = data->request_playback_buffer_size > 0 ? data->request_playback_buffer_size : 1024;
    
            data->return_playback_buffers = num_buffers;
            data->return_playback_channels = channels;
            data->return_playback_buffer_size = buffer_size_frames;
            
            device->channels = channels;
            device->buffer_size_frames = buffer_size_frames;
    
            // Se non è già stata clonata, cloniamo l'area DMA nel team utente corrente
            if (device->dma_user_area < 0) {
                device->dma_user_base = NULL;
                device->dma_user_area = clone_area(
                    "cmi8788_dma_user",
                    &device->dma_user_base,
                    B_ANY_ADDRESS,
                    B_READ_AREA | B_WRITE_AREA,
                    device->dma_area
                );
                if (device->dma_user_area < B_OK) {
                    dprintf("cmi8788: Errore nel clonare l'area DMA per user-space (%s)\n", strerror(device->dma_user_area));
                    return device->dma_user_area;
                }
                dprintf("cmi8788: Area DMA clonata per user-space con successo. Indirizzo utente: %p\n", device->dma_user_base);
            }

            size_t single_buffer_bytes = buffer_size_frames * channels * sizeof(int32);
    
            // Mappatura Interleaved dei buffer clonato per user-space
            for (int b = 0; b < num_buffers; b++) {
                for (int c = 0; c < channels; c++) {
                    data->playback_buffers[b][c].base = (char *)device->dma_user_base 
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

                // Sblocca immediatamente eventuali thread in attesa sul semaforo
                release_sem(device->playback_sem);
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
            
            // 1. Gruppo Master (Usa la stringa di sistema S_OUTPUT)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 100;
                controls[count].flags = B_MULTI_MIX_GROUP;
                controls[count].master = 0;
                controls[count].parent = 0;
                controls[count].string = S_OUTPUT;
                strlcpy(controls[count].name, "Uscite Master", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 1.1 Sotto-Gruppo Canali Frontali (Sotto-gruppo di 100)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 200;
                controls[count].flags = B_MULTI_MIX_GROUP;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Canali Frontali", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 1.2 Sotto-Gruppo Canali Posteriori (Sotto-gruppo di 100)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 300;
                controls[count].flags = B_MULTI_MIX_GROUP;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Canali Posteriori", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 1.3 Sotto-Gruppo Canale Centrale e Subwoofer (Sotto-gruppo di 100)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 400;
                controls[count].flags = B_MULTI_MIX_GROUP;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Canale Centrale e Sub", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 1.4 Sotto-Gruppo Canali Laterali (Sotto-gruppo di 100)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 500;
                controls[count].flags = B_MULTI_MIX_GROUP;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_null;
                strlcpy(controls[count].name, "Canali Laterali", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 2. Master Volume Left (Usa la stringa di sistema S_VOLUME)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 101;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 0; // Master
                controls[count].parent = 100;
                controls[count].string = S_VOLUME;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Master Sinistro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 3. Master Volume Right (Slave agganciato all'ID 101)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 102;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 101; // Aggancia a Left per formare slider Stereo
                controls[count].parent = 100;
                controls[count].string = S_VOLUME;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Master Destro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 4. Frontale L (Codec 0 L, Sotto-gruppo 200)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 103;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 0;
                controls[count].parent = 200;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Sinistro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 5. Frontale R (Codec 0 R, Slave a 103, Sotto-gruppo 200)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 104;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 103;
                controls[count].parent = 200;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Destro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 6. Posteriore L (Codec 1 L, Sotto-gruppo 300)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 105;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 0;
                controls[count].parent = 300;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Sinistro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 7. Posteriore R (Codec 1 R, Slave a 105, Sotto-gruppo 300)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 106;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 105;
                controls[count].parent = 300;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Destro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 8. Centrale (Codec 2 L, Sotto-gruppo 400)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 107;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 0;
                controls[count].parent = 400;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Centrale", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 9. Subwoofer (Codec 2 R, Slave a 107, Sotto-gruppo 400)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 108;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 107;
                controls[count].parent = 400;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Subwoofer", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 10. Laterale L (Codec 3 L, Sotto-gruppo 500)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 109;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 0;
                controls[count].parent = 500;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Sinistro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 11. Laterale R (Codec 3 R, Slave a 109, Sotto-gruppo 500)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 110;
                controls[count].flags = B_MULTI_MIX_GAIN;
                controls[count].master = 109;
                controls[count].parent = 500;
                controls[count].string = S_null;
                controls[count].gain.min_gain = -60.0f;
                controls[count].gain.max_gain = 0.0f;
                controls[count].gain.granularity = 0.5f;
                strlcpy(controls[count].name, "Volume Destro", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 12. Master Mute Toggle (Usa la stringa di sistema S_MUTE)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 111;
                controls[count].flags = B_MULTI_MIX_ENABLE;
                controls[count].master = 0;
                controls[count].parent = 100;
                controls[count].string = S_MUTE;
                strlcpy(controls[count].name, "Mute", sizeof(controls[count].name));
                count++;
            } else {
                count++;
            }

            // 13. DAC Filter Choice (Sharp vs Slow)
            if (controls != NULL && info->control_count > count) {
                controls[count].id = 112;
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
                    // Volume Master Sinistro (restituisce il valore del DAC 0 L)
                    float gain_db = -60.0f + ((float)device->dac_volume[0] * (60.0f / 255.0f));
                    info->values[i].gain = gain_db;
                } else if (id == 102) {
                    // Volume Master Destro (restituisce il valore del DAC 0 R)
                    float gain_db = -60.0f + ((float)device->dac_volume[1] * (60.0f / 255.0f));
                    info->values[i].gain = gain_db;
                } else if (id >= 103 && id <= 110) {
                    // Volume canali specifici (Front L/R, Rear L/R, Center/Sub, Side L/R)
                    int ch = id - 103;
                    float gain_db = -60.0f + ((float)device->dac_volume[ch] * (60.0f / 255.0f));
                    info->values[i].gain = gain_db;
                } else if (id == 111) {
                    info->values[i].enable = device->dac_mute;
                } else if (id == 112) {
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
                    
                    // Master Left: Assegna il volume ai canali Left dei 4 DAC esterni PCM1796 via SPI
                    for (int codec = 0; codec < 4; codec++) {
                        device->dac_volume[codec * 2] = raw_vol;
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_ATTN_L, raw_vol);
                    }
                } else if (id == 102) {
                    float gain_db = info->values[i].gain;
                    if (gain_db < -60.0f) gain_db = -60.0f;
                    if (gain_db > 0.0f) gain_db = 0.0f;
                    uint8_t raw_vol = (uint8_t)((gain_db + 60.0f) * (255.0f / 60.0f));
                    
                    // Master Right: Assegna il volume ai canali Right dei 4 DAC esterni PCM1796 via SPI
                    for (int codec = 0; codec < 4; codec++) {
                        device->dac_volume[codec * 2 + 1] = raw_vol;
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_ATTN_R, raw_vol);
                    }
                } else if (id >= 103 && id <= 110) {
                    float gain_db = info->values[i].gain;
                    if (gain_db < -60.0f) gain_db = -60.0f;
                    if (gain_db > 0.0f) gain_db = 0.0f;
                    uint8_t raw_vol = (uint8_t)((gain_db + 60.0f) * (255.0f / 60.0f));

                    int ch = id - 103;
                    device->dac_volume[ch] = raw_vol;

                    // Scrive via SPI sul rispettivo canale del DAC Burr-Brown
                    int codec = ch / 2;
                    if (ch % 2 == 0) {
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_ATTN_L, raw_vol);
                    } else {
                        xonar_d2_pcm1796_write(device, codec, PCM1796_REG_ATTN_R, raw_vol);
                    }
                } else if (id == 111) {
                    bool mute = info->values[i].enable;
                    cmi8788_set_mute(device, mute);
                } else if (id == 112) {
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
	CALLED();
	if (get_module(B_PCI_MODULE_NAME, (module_info **)&gPci) < B_OK)
		return B_ERROR;

	gNumCards = 0;
	pci_info info;
	int index = 0;
	while ((*gPci->get_nth_pci_info)(index, &info) == B_OK && gNumCards < MAX_CARDS) {
		if (info.vendor_id == CMEDIA_VENDOR_ID && info.device_id == CMI8788_DEVICE_ID) {
			cmi8788_device *device = &sCards[gNumCards];
			memset(device, 0, sizeof(cmi8788_device));
			device->pci_info = info;
			
			// Genera il percorso devfs (es. "audio/hmulti/cmi8788/0")
			sprintf(device->devfs_path, DEVFS_PATH_FORMAT, gNumCards);
			sDeviceNames[gNumCards] = device->devfs_path;
			
			dprintf("cmi8788: Registrata scheda #%" B_PRIu32 " a %s\n", gNumCards, device->devfs_path);
			gNumCards++;
		}
		index++;
	}
	sDeviceNames[gNumCards] = NULL;

	dprintf("cmi8788: Caricati %" B_PRIu32 " device cmi8788 compatibili.\n", gNumCards);
	return B_OK;
}

extern "C" void
uninit_driver(void)
{
	CALLED();
	for (uint32 i = 0; i < gNumCards; i++) {
		cmi8788_device *device = &sCards[i];
		if (device->initialized) {
			oxygen_chip_shutdown(device->mmio_base);
			cmi8788_remove_interrupts(device);
			oxygen_free_dma_buffer(device);
			delete_area(device->mmio_area);
			device->initialized = false;
		}
	}

	if (gPci != NULL)
		put_module(B_PCI_MODULE_NAME);
}

extern "C" const char **
publish_devices(void)
{
	CALLED();
	return sDeviceNames;
}

extern "C" device_hooks *
find_device(const char *name)
{
	return &sDeviceHooks;
}
