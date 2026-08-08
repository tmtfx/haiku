/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <PCI.h>
#include <string.h>
#include <SupportDefs.h>
#include <boot_item.h>

#include <frame_buffer_console.h>
#include <stdlib.h>

#include "DriverInterface.h"
#include "sm750_macros.h"
#include "common_modes.h"

#ifdef IS_PIRATI_BUILD
#include "sm750_logo.h"
#else
#include "professional_sm750_logo.h"
#endif

extern pci_module_info *pci;

#define VESA_EDID_BOOT_INFO "vesa_edid/v1"
#define VESA_MODES_BOOT_INFO "vesa_modes/v1"


typedef struct BIOSState bios_state;

struct bios_regs {
    uint32 eax; uint32 ebx; uint32 ecx; uint32 edx;
    uint32 edi; uint32 esi; uint32 ebp; uint32 eflags;
    uint32 ds;  uint32 es;  uint32 fs;  uint32 gs;
};
typedef struct bios_regs bios_regs;

typedef struct {
    module_info info;
    status_t  (*prepare)(bios_state** _state);
    status_t  (*interrupt)(bios_state* state, uint8 vector, bios_regs* regs);
    void      (*finish)(bios_state* state);
    void* (*allocate_mem)(bios_state* state, size_t size);
    uint32    (*physical_address)(bios_state* state, void* virtualAddress);
    void* (*virtual_address)(bios_state* state, uint32 physicalAddress);
} sm750_bios_module_info; 

#define B_BIOS_MODULE_NAME "generic/bios/v1"

static status_t
GetEdidFromBIOS(edid1_raw* edidRaw)
{
    sm750_bios_module_info* biosModule;
    status_t status = get_module(B_BIOS_MODULE_NAME, (module_info**)&biosModule);
    if (status != B_OK) {
        dprintf("SM750: Unable loading the BIOS module: 0x%" B_PRIx32 "\n", status);
        return status;
    }

    bios_state* state;
    status = biosModule->prepare(&state);
    if (status != B_OK) {
        dprintf("SM750: bios_prepare() failed\n");
        put_module(B_BIOS_MODULE_NAME);
        return status;
    }

    bios_regs regs = {};
    regs.eax = 0x4f15; // VBE Function: Report DDC Capabilities
    regs.ebx = 0;
    //dprintf("SM750: Check DDC Capabilities EAX=0x%x, EBX=0x%x\n", regs.eax, regs.ebx);
    
    status = biosModule->interrupt(state, 0x10, &regs);
    
    // Does BIOS support the function (0x4f = Success)
    if (status == B_OK && (regs.eax & 0xffff) == 0x4f) {
        edid1_raw* edid = (edid1_raw*)biosModule->allocate_mem(state, sizeof(edid1_raw));
        if (edid == NULL) {
            status = B_NO_MEMORY;
        } else {
            regs.eax = 0x4f15;
            regs.ebx = 1;  // func: Read EDID
            regs.ecx = 0;
            regs.edx = 0;
            regs.es  = (uint16)((addr_t)edid >> 4);
            regs.edi = (uint16)((addr_t)edid & 0x0f);

            status = biosModule->interrupt(state, 0x10, &regs);
            
            if (status == B_OK && (regs.eax & 0xffff) == 0x4f) {
                memcpy(edidRaw, edid, sizeof(edid1_raw));
                //dprintf("SM750: EDID correctly read through BIOS interrupt!\n");
            } else {
                dprintf("SM750: BIOS failed reading the EDID (EAX=0x%x)\n", regs.eax);
                status = B_NOT_SUPPORTED;
            }
        }
    } else {
        dprintf("SM750: DDC not supported by Video BIOS\n");
        status = B_NOT_SUPPORTED;
    }

    biosModule->finish(state);
    put_module(B_BIOS_MODULE_NAME);
    return status;
}

static const color_space kFallbackSpaces[] = {
    B_RGB32,
    B_RGB16,
    B_CMAP8
};

static status_t create_mode_list(shared_info* si) {
    uint32 vesa_count = 0;
    while (vesa_dmt_table[vesa_count].width != 0) {
        vesa_count++;
    }
    uint32 total_modes = vesa_count * 3;
    
    size_t size = (total_modes * sizeof(display_mode) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
    
    display_mode* kernel_list = (display_mode*)malloc(size);
    if (kernel_list == NULL) return B_NO_MEMORY;
    memset(kernel_list, 0, size);

    uint32 count = 0;

    for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
        const vesa_timing_t* vesa = &vesa_dmt_table[i];
        
        for (int s = 0; s < 3; s++) {
            display_mode* dm = &kernel_list[count];
            
            dm->timing.pixel_clock = vesa->pixel_clock;
            dm->timing.h_display    = vesa->width;
            dm->timing.h_sync_start = vesa->h_sync_start;
            dm->timing.h_sync_end   = vesa->h_sync_end;
            dm->timing.h_total      = vesa->h_total;
            dm->timing.v_display    = vesa->height;
            dm->timing.v_sync_start = vesa->v_sync_start;
            dm->timing.v_sync_end   = vesa->v_sync_end;
            dm->timing.v_total      = vesa->v_total;
            dm->timing.flags        = vesa->flags;

            dm->space = kFallbackSpaces[s];
            dm->virtual_width  = vesa->width;
            dm->virtual_height = vesa->height;
            dm->h_display_start = 0;
            dm->v_display_start = 0;
            dm->flags = 0;

            count++;
        }
    }

    display_mode* user_list = NULL;
    area_id m_area = create_area("sm750 modes", (void **)&user_list,
        B_ANY_KERNEL_ADDRESS, size, B_FULL_LOCK, 
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

    if (m_area < 0) {
        free(kernel_list);
        return m_area;
    }

    if (user_memcpy(user_list, kernel_list, size) != B_OK) {
        dprintf("SM750: user_memcpy failed in create_mode_list!\n");
    }

    free(kernel_list);

    si->mode_count = count;
    si->mode_list_area = m_area;

    //dprintf("SM750: create_mode_list completed. # of Modes: %u\n", count);
    return B_OK;
}

static void draw_logo(DeviceInfo *di, display_mode* dm) {
    if (!di || !dm) return;

    void* fb_virt = NULL;
    area_id fb_area = -1;
    uint32 fb_size = di->pci.u.h0.base_register_sizes[0];
    if (fb_size == 0)
        return;

    fb_size = (fb_size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
    fb_area = map_physical_memory("sm750_fb_init_logo",
        di->pci.u.h0.base_registers[0], fb_size, B_ANY_KERNEL_ADDRESS,
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &fb_virt);
    if (fb_area < B_OK || fb_virt == NULL)
        return;

    shared_info *si = di->si;
    uint32* fb = (uint32*)fb_virt;
    
    uint32 screenWidth = dm->virtual_width;
    uint32 screenHeight = dm->virtual_height;
    
    uint32 bytesPerRow = si->fbc.bytes_per_row;
    if (bytesPerRow == 0)
        bytesPerRow = screenWidth * 4; 

    uint32 fbPitch = bytesPerRow / 4; 

    uint32 logoW = sm750_logo_width;
    uint32 logoH = sm750_logo_height;

    int32 startX = (int32)((screenWidth - logoW) / 2);
    int32 startY = (int32)((screenHeight - logoH) / 2);

    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;

    for (uint32 y = 0; y < logoH && (startY + (int32)y) < (int32)screenHeight; y++) {
        for (uint32 x = 0; x < logoW && (startX + (int32)x) < (int32)screenWidth; x++) {
            uint32 fbIndex = (uint32)((startY + (int32)y) * (int32)fbPitch + (startX + (int32)x));
            fb[fbIndex] = sm750_logo[y * logoW + x];
        }
    }

    delete_area(fb_area);
}

static void sm750_init_interrupts(DeviceInfo* info) 
{
	vuint32* regs = info->regs;

	// MASTER CLEARING: 
	// 1 clear the bit (register 0x20)
	// 31:5 Reserved, 4 ZV1 Port, 3 ZV0 Port, 2 CRT VSYNC, 1 Panel VSYNC, 0 VGA VSYNC
	SM750_WREG32(SM750_SYS_RAW_INT_CLEAR, 0x0000001F);

	// 2D ENGINE CLEARING (0x100050)
	// Datasheet says: "Write 0 to clear"
	uint32 engineStatus = SM750_REG32(SM750_2D_STATUS);
	engineStatus &= ~0x00000003; // clear bit 0 (2D) and 1 (CSC)
	SM750_WREG32(SM750_2D_STATUS, engineStatus);

	// DMA CLEARING (0x0D0020)
	uint32 dmaStatus = SM750_REG32(SM750_DMA_ABORT_INTERRUPT);
	SM750_WREG32(SM750_DMA_ABORT_INTERRUPT, dmaStatus); 

	// V-SYNC CLEARING (Panel and CRT)
	// Look for "Clear" bits in 0x080000 registers (Panel)
	// and 0x080200 (CRT).
	// NO: vertical-sync bit (11) of CRT 80200 is read-only
	uint32 panelStatus = SM750_REG32(SM750_PANEL_CONTROL);
	panelStatus &= ~(1 << 11); // 0 to VSYNC
	//panelStatus |= (1 << 11); // 1 to VSYNC
	panelStatus |= 0x0000006; // enable primary graphics plane and 32bit format
	SM750_WREG32(SM750_PANEL_CONTROL, panelStatus); 

	// PWM CLEARING
	// bit 3 PWM Interrupt Pending. In order to clear a pending interrupt, write a “1” in
	// the IP bit.

	uint32 pwmstat = SM750_REG32(SM750_PWM0);
	SM750_WREG32(SM750_PWM0,pwmstat|(1<<3));
	pwmstat = SM750_REG32(SM750_PWM1);
	SM750_WREG32(SM750_PWM1,pwmstat|(1<<3));
	pwmstat = SM750_REG32(SM750_PWM2);
	SM750_WREG32(SM750_PWM2,pwmstat|(1<<3));
	
	// Write mask even if the datasheet indicates it's read-only!
	SM750_WREG32(SM750_SYS_INT_MASK,0xFE0013FF);
	// but it seems writable and soon afer it enalbes the interrupt! <------ !!!
	
	info->si->irq_enabled = 1;
}

/* --- sm750_init_chip --- */
void sm750_init_chip(DeviceInfo *di) {
    if (!di || !di->regs) return;

    vuint32 *regs = di->regs;
    shared_info *si = di->si;
    uint32 device_info = SM750_REG32(SM750_SYS_DEVID);
    si->card_info.chip_id = (uint16)(device_info >> 16);
    uint16 device_id = (uint16)(device_info >> 16);
    uint8 revision = (uint8)(device_info & 0xFF);
    dprintf("SM750: Chip detected. ID: 0x%04X, Revision: 0x%02X\n", device_id, revision);
    
    sm750_init_interrupts(di);
    
    edid1_raw local_edid;
    edid1_raw* boot_edid = (edid1_raw*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);

    if (boot_edid != NULL) {
        memcpy(&si->vesa_edid_raw, boot_edid, sizeof(edid1_raw));
        si->card_info.has_edid_vesa = true;
    } else {
        if (GetEdidFromBIOS(&local_edid) == B_OK) {
        	memcpy(&si->vesa_edid_raw, &local_edid, sizeof(edid1_raw));
            si->card_info.has_edid_vesa = true;
        } else {
        	si->card_info.has_edid_vesa = false;
        }
    }
    
    if (si->settings.force_CRT) {
        si->card_info.is_panel = false;
        si->card_info.active_outputs = 2; // Forza CRT
        dprintf("SM750: User override enabled! Forcing CRT branch (Analog).\n");
    } 
    else if (si->settings.force_Panel) {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1; // Forza PANEL
        dprintf("SM750: User override enabled! Forcing PANEL branch (Digital).\n");
    } 
    else if (si->card_info.has_edid_vesa) {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1;
        dprintf("SM750: EDID detected -> Using PANEL branch.\n");
    } 
    else {
        // Fallback: no file, no EDID. 
        // Set the default based on your crt card.
        si->card_info.is_panel = false; 
        si->card_info.active_outputs = 2;
        dprintf("SM750: EDID not detected -> Using CRT branch.\n");
    }
    
    // --- WAKE UP the CHIP (Power Mode 0) ---
    // --- unlock clocks (Power Mode 0) ---
    uint32 mode0_gate = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC); // 0x000044
    // GPIO (6), 2D (3), Display (2), Memory (1) e DMA (0)
    mode0_gate |= (1 << 6) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0); //attiviamo clock DMA, Local memory, Display , 2D, CSC, GPIO, I2C
    //mode0_gate &= ~(1 << 6); // As we are not able to use direction register for GPIO the way it's needed, force to 0 GPIO clock
    //mode0_gate &= ~(1 << 8); // As hardware I2C is broken forece to 0 I2C clock
    mode0_gate &= ~(1 << 5); // Disable ZV
    mode0_gate &= ~(1 << 7); // Disable SSP
    mode0_gate &= ~(1 << 9); // Disable PWM
    if (si->card_info.is_panel) {
        mode0_gate &= ~(1 << 10); // Shut down VGA clock if PANEL is used
        mode0_gate |= (1 << 8);   // Activate I2C/DDC for Panel
    } else {
        mode0_gate |= (1 << 10);  // Activate VGA il clock for CRT!
        mode0_gate &= ~(1 << 8);  // Shut down hardware I2C if no needed
    }
    SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, mode0_gate); // 0x000044
    //dprintf("SM750: Power Mode 0 Clock Gate set to: 0x%08x\n", mode0_gate);
    
    uint32 vga_mode = SM750_REG32(SM750_SYS_VGA_CONFIG);
    vga_mode |= (1 << 2); //switch CLOCK from vga to primary panel
    SM750_WREG32(SM750_SYS_VGA_CONFIG, vga_mode);
    snooze(100);
    vga_mode = SM750_REG32(SM750_SYS_VGA_CONFIG);

    // --- POWER MODE SELECTION AND OSCILLATOR ACTIVATION ---
    //Power Mode Control
    uint32 pwr_ctrl = SM750_REG32(SM750_SYS_PWR_MODE_CTRL); // 0x00004C
    pwr_ctrl &= ~0x00000003; // Force Mode 0 (bit 1:0 = 00)
    pwr_ctrl |= (1 << 3);    // Ensure the oscillator is up
    SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, pwr_ctrl); // 0x00004C
    snooze(1000); // clock stabilization
    
    // --- ENABLE BUS AND MEMORY (Register 0x00) ---
    uint32 sys_ctrl = SM750_REG32(SM750_SYS_CTRL); // 0x00
    // Remove isolation (bit 0,1,2,3) to connect RAM and video output
    sys_ctrl &= ~0x0000000F; 
    // (DPMS on, bit 31:30 = 00)
    sys_ctrl &= ~(3U << 30);
    SM750_WREG32(SM750_SYS_CTRL, sys_ctrl); //0x000000
    
    // --- NOW LET'S DO THE DETECTION (After enabling the buses) ---
    // Various attempts have been made to avoid using the detection logic
    // used earlier. Reading SM750_SYS_CTRL with if (!(sys_ctrl & (1 << 3))) indicates
    // crt even if the card has HDMI.
    // Even reading SM750_CRT_MONITOR_DETECT doesn't provide a reliable value. It detects a monitor on
    // port 0 even if it's not enabled. Basically, 2 of its bits detect the presence
    // of the monitor (25 and 27), and 2 more bits enable them (24 and 26). By default, with HDMI,
    // a monitor is detected on port 0, but it's not enabled. Not reliable
    // Even using the default panel control and crt control values ​​doesn't work:
    // The panel branch is always activated, even on cards with only VGA ports -> CRT branch
    // The only difference between the two cards is that the card using the PANEL (HDMI) branch
    // can obtain the EDID from the BIOS, which means the CRT uses the standard DDC lines (VGA pins 12 and 15).
    // While HDMI uses the dedicated I2C channel (which we can't use), we know:
    // If the bootloader passed us an EDID, we know it's a card with an HDMI port.
    // Otherwise, we assume it's a card with only CRT ports.
    

    uint32 misc_ctrl = SM750_REG32(SM750_SYS_MISC_CTRL); // 0x000004
    // Ensure that:
    // The bit 6 (Rst) is 1 (Normal Operation)
    // The bit 0 (E) is 0 (Enable local memory)
    // Force RA (bit 5) to 0 to keep pages active (Performance UP)
    misc_ctrl &= ~(1 << 5);
    misc_ctrl |= (1 << 6);  // Force Normal Mode
    misc_ctrl &= ~(1 << 0); // Force Enable Memory
    // Try loosening the refresh (bit 26:25) from 00 to 01 
    // (Refresh every 16x100 instead of 8x100)
    misc_ctrl &= ~(3 << 25);
    misc_ctrl |= (1 << 25);
    SM750_WREG32(SM750_SYS_MISC_CTRL, misc_ctrl); // 0x000004
    
    // Memory detection
    uint32 mem_size_code = (misc_ctrl >> 12) & 0x03; // Bit 13:12
    uint32 detected_mem;

    switch (mem_size_code) {
        case 0: detected_mem = 16 * 1024 * 1024; break;
        case 1: detected_mem = 32 * 1024 * 1024; break;
        case 2: detected_mem = 64 * 1024 * 1024; break;
        case 3: detected_mem = 8  * 1024 * 1024; break;
        default: detected_mem = 8 * 1024 * 1024; break;
    }

    si->card_info.mem_size = detected_mem;
    if (si->card_info.mem_size > 12 * 1024 * 1024)
        si->card_info.max_desktop_mem = 12 * 1024 * 1024;
    else
        si->card_info.max_desktop_mem = si->card_info.mem_size - (2 * 1024 * 1024);
    dprintf("SM750: Detected VRAM memory (from Reg 0x000004): %u MB\n", detected_mem / (1024*1024));
    
    bool showLogo = false;
    display_mode *dm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    if (bi) {
        uint32 bpp = 32;
        si->real_framebuffer_size = bi->width * bi->height * (bpp / 8);
        
        bool found = false;

        for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
            if (vesa_dmt_table[i].width == bi->width && vesa_dmt_table[i].height == bi->height) {
            
                dm->timing.pixel_clock = vesa_dmt_table[i].pixel_clock;
                dm->timing.h_display    = vesa_dmt_table[i].width;
                dm->timing.h_sync_start = vesa_dmt_table[i].h_sync_start;
                dm->timing.h_sync_end   = vesa_dmt_table[i].h_sync_end;
                dm->timing.h_total      = vesa_dmt_table[i].h_total;
            
                dm->timing.v_display    = vesa_dmt_table[i].height;
                dm->timing.v_sync_start = vesa_dmt_table[i].v_sync_start;
                dm->timing.v_sync_end   = vesa_dmt_table[i].v_sync_end;
                dm->timing.v_total      = vesa_dmt_table[i].v_total;
                dm->timing.flags        = vesa_dmt_table[i].flags;
            
                found = true;
                showLogo = true;
                break;
            }
        }
        if (!found) {
            dprintf("SM750: WARNING! Boot resolution not in our table. Using 1024x768 fallback\n");

            dm->timing.pixel_clock = 65000;
            dm->timing.h_display    = 1024;
            dm->timing.h_sync_start = 1048;
            dm->timing.h_sync_end   = 1184;
            dm->timing.h_total      = 1344;
    
            dm->timing.v_display    = 768;
            dm->timing.v_sync_start = 771;
            dm->timing.v_sync_end   = 777;
            dm->timing.v_total      = 806;
            dm->timing.flags        = 0;
    
            dm->virtual_width = 1024;
            dm->virtual_height = 768;
            dm->h_display_start = 0;
            dm->v_display_start = 0;
        }

        if (bi && bi->depth <= 8) dm->space = B_CMAP8;
        else if (bi && bi->depth <= 16) dm->space = B_RGB16;
        else dm->space = B_RGB32;
        dm->virtual_width = dm->timing.h_display;
        dm->virtual_height = dm->timing.v_display;
    }
        
    // VGA BYPASS (Needed for linear framebuffer/native mode)
    uint32 display_reg = si->card_info.is_panel ? SM750_PANEL_CONTROL : SM750_CRT_CONTROL; // 0x080000 : 0x080200
    uint32 notdisplay_reg = si->card_info.is_panel ? SM750_CRT_CONTROL : SM750_PANEL_CONTROL; // 0x080200 : 0x080000
    uint32 ctrl = SM750_REG32(display_reg);
    uint32 nctrl = SM750_REG32(notdisplay_reg);
    

    // 32-bit Format (Bit 1:0 = 10) - same for both paths
    ctrl &= ~0x00000003;
    ctrl |= 0x00000002;
    nctrl &= ~0x00000003;
    nctrl |= 0x00000002;


    // Enable Plane and Timing (Bit 2 e 8) - same for both
    ctrl |= (1 << 2) | (1 << 8);
    nctrl |= (1 << 2) | (1 << 8);

    // Data Source selection
    if (si->card_info.is_panel) {
        // Register 0x80000: Bit 29:28. "Panel Data" (00)
        ctrl &= ~(3U << 28); // Panel Data for Primary Display
        nctrl &= ~(3U << 18); // Panel Data for Secondary Display
    
        // Extra for LCD: Enable control signals (Bit 27, 26, 25, 24)
        ctrl |= (1 << 27) | (1 << 26) | (1 << 25) | (1 << 24);
        
    } else {
        // Register 0x80200: Bit 19:18. "CRT Data" (10)
        ctrl &= ~(3U << 18);
        ctrl |= (2U << 18);  // CRT Data for Secondary Display
        nctrl &= ~(3U << 28); 
        nctrl |= (2U << 28); // Secondary Display Data for Primary Disaply
        nctrl &= ~(1 << 2); // disable primary graphic plane, otherwise a ruined image appears on screen
        // Exercise note: by deactivating the CRT plane and leaving the PANEL plane active, the desktop appears on the monitor 
        // but is defective with horizontal lines that translate the image.
        // Either my board doesn't support the PANEL branch, or the PANEL branch isn't configured correctly, or there's something
        // else I need to investigate...
    
        // No Blank for CRT (Bit 10)
        ctrl &= ~(1 << 10);
    }
    SM750_WREG32(display_reg, ctrl);
    SM750_WREG32(notdisplay_reg, nctrl);
    
    si->card_info.f_ref = 14.31818f; // datasheet says 14.318, old NTSC value
    si->card_info.max_sclk = 130000; // Typical value (130MHz)
    si->card_info.max_mclk = 145000; // 145MHz from datasheet
    si->card_info.max_pclk = 300000; // 300MHz (DAC limit)
    
    create_mode_list(si);
    
    uint32 bpp = 0;
    switch (dm->space) {
        case B_RGB32: case B_RGBA32: bpp = 32; break;
        case B_RGB16: bpp = 16; break;
        default: bpp = 8; break;
    }
    
    si->fbc.frame_buffer = NULL;
    si->fbc.frame_buffer_dma = (void *)(addr_t)di->pci.u.h0.base_registers[0]; // it is si->framebuffer_pci
    si->fbc.bytes_per_row = dm->timing.h_display * (bpp / 8);
    si->fbc2 = si->fbc; // To be safe, we also copy the configuration to the other output
    
    if (showLogo) {
    	display_mode *dm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    	draw_logo(di, dm);
    }
    snooze(2000000); // 2 seconds of glory
}
