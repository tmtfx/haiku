/*
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 * Copyright (C) 1998-2000 by Alan Hourihane, Wigan, England
 * Copyright (C) 2000 Mike Chapman <mike@paranoia.com>
 * Copyright (C) 2000 Juanjo Santamarta <santamarta@ctv.es>
 * Copyright (C) 2000 Mitani Hiroshi <hmitani@drl.mei.co.jp>
 * Copyright (C) 2000 David Thomas <davtom@dream.org.uk>
 * Copyright (C) 2003-2004 Sung-Ching Lin, Ademar Reis
 * Copyright (C) 2003-2004 Eric Anholt <anholt@FreeBSD.org>
 * Copyright (C) 2004-2009 Nan-Hsing Chang <nan_chang@xgitech.com>
 * Copyright (C) 2004-2009 Jong Lin (XGI Technology Inc.)
 * Copyright (C) 2004-2009 Can-Ru Yeou (XGI Technology Inc.)
 * Copyright (C) 2004-2009 Hsuan, Vicent, kuku (XGI Technology Inc.)
 * Copyright (C) 2000 Mark Vojkovich
 * Copyright (C) 2000 Jonathan Bian <jonathan.bian@intel.com>
 * Copyright (C) 2005-2024 Adam Jackson <ajax@redhat.com>
 * Copyright (C) 2005-2024 Alan Barrett <apb@NetBSD.org>
 * Copyright (C) 2005-2024 Alan Coopersmith <alan.coopersmith@oracle.com>
 * Copyright (C) 2005-2024 Alberto Bursi <starshipeleven@outlook.com>
 * Copyright (C) 2005-2024 Chris Ball <cjb@laptop.org>
 * Copyright (C) 2005-2024 Connor Behan <connor.behan@gmail.com>
 * Copyright (C) 2005-2024 Dave Airlie <airlied@redhat.com>
 * Copyright (C) 2005-2024 Egbert Eich <eich@freedesktop.org>
 * Copyright (C) 2005-2024 Emil Velikov <emil.velikov@collabora.com>
 * Copyright (C) 2005-2024 Enrico Weigelt <info@metux.net>
 * Copyright (C) 2005-2024 Fernando Carrijo <fcarrijo@yahoo.com.br>
 * Copyright (C) 2005-2024 Gaetan Nadon <memsize@videotron.ca>
 * Copyright (C) 2005-2024 Ian Romanick <idr@us.ibm.com>
 * Copyright (C) 2005-2024 Jakub Bogusz <qboosh@pld-linux.org>
 * Copyright (C) 2005-2024 James Cloos <cloos@jhcloos.com>
 * Copyright (C) 2005-2024 Jeremy Huddleston <jeremyhu@apple.com>
 * Copyright (C) 2005-2024 Jesse Adkins <jesserayadkins@gmail.com>
 * Copyright (C) 2005-2024 Johannes Obermayr <johannesobermayr@gmx.de>
 * Copyright (C) 2005-2024 Peter Hutterer <peter.hutterer@who-t.net>
 * Copyright (C) 2005-2024 Rémi Cardona <remi@gentoo.org>
 * Copyright (C) 2005-2024 Stefan Dirsch <sndirsch@novell.com>
 * Copyright (C) 2005-2024 Thomas Klausner <wiz@NetBSD.org>
 * Copyright (C) 2005-2024 Trevor Woerner <twoerner@gmail.com>
 * Copyright (C) 2005-2024 Tristan Plumb <freedesktop@trstn.net>
 * Copyright (C) 2004-2009 eXtreme Graphics Innovation Corp, Inc.
 * Copyright (C) 1999-2004 The XFree86 Project, Inc.
 * Copyright (C) 2000-2003 ATI Technologies Inc.
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef XGI_DRIVER_INTERFACE_H
#define XGI_DRIVER_INTERFACE_H

#include <Accelerant.h>
#include <Drivers.h>
#include <GraphicsDefs.h>
#include <PCI.h>
#include <OS.h>
#include <edid.h>
#include <video_overlay.h>

#define XGI_DRIVER_NAME            "xgi"
#define XGI_ACCELERANT_NAME        "xgi.accelerant"
#define XGI_PRIVATE_DATA_MAGIC     0x58474931  /* 'XGI1' */

#define PCI_VENDOR_XGI             0x18CA

/* Supported XGI Device IDs */
#define PCI_CHIP_XGIXG20           0x0020  /* Volari Z7 / Z9 */
#define PCI_CHIP_XGIXG21           0x0021  /* Volari Z9s / Z9m */
#define PCI_CHIP_XGIXG27           0x0027  /* Volari Z11 / Z11M */
#define PCI_CHIP_XGIXG40           0x0040  /* Volari V3XT / V5 / V8 */
#define PCI_CHIP_XGIXG41           0x0041  /* Volari V3 / V5 */
#define PCI_CHIP_XGIXG42           0x0042  /* Volari XG42 */
#define PCI_CHIP_XGIXG47           0x0047  /* Volari 8300 (XP10) */

enum xgi_chip_type {
    XGI_UNKNOWN = 0,
    XGI_XG20,
    XGI_XG21,
    XGI_XG27,
    XGI_XG40,
    XGI_XG41,
    XGI_XG42,
    XGI_XG47
};

/* IOCTL operations */
enum {
    XGI_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
    XGI_DEVICE_NAME,
    XGI_GET_EDID,
    XGI_GET_PIO,
    XGI_SET_PIO
};

struct XgiGetSetPIO {
    uint32  magic;
    uint16  port;
    uint8   value;
};

struct XgiGetPrivateData {
    uint32  magic;
    area_id sharedInfoArea;
};

/* Driver settings */
typedef struct {
    char    accelerant[B_FILE_NAME_LENGTH];
    bool    hardcursor;
    bool    turboqueue;
} xgi_settings;

#ifdef __cplusplus
struct Benaphore {
    sem_id  sem;
    int32   count;

    status_t Init(const char* name) {
        count = 0;
        sem = create_sem(0, name);
        return sem < 0 ? sem : B_OK;
    }

    status_t Acquire() {
        if (atomic_add(&count, 1) > 0)
            return acquire_sem(sem);
        return B_OK;
    }

    status_t Lock() {
        return Acquire();
    }

    status_t Release() {
        if (atomic_add(&count, -1) > 1)
            return release_sem(sem);
        return B_OK;
    }

    void Destroy() {
        if (sem >= 0)
            delete_sem(sem);
        sem = -1;
    }
};
#else
typedef struct {
    sem_id  sem;
    int32   count;
} Benaphore;
#endif

/* Shared information block between kernel driver and accelerant */
struct SharedInfo {
    uint16          vendorID;
    uint16          deviceID;
    uint8           revision;
    uint8           bus;
    uint8           device;
    uint8           function;
    uint32          chipType;
    char            chipName[32];
    char            deviceName[B_PATH_NAME_LENGTH];

    /* BAR 1: MMIO registers */
    area_id         regsArea;
    vuint8*         regs;
    phys_addr_t     regsPCI;
    uint32          regsSize;

    /* BAR 0: Framebuffer (Video RAM) */
    area_id         frameBufferArea;
    uint8*          frameBuffer;
    phys_addr_t     frameBufferPCI;
    uint32          frameBufferSize;

    /* BAR 2: Relocated I/O ports (optional) */
    uint16          ioBase;
    uint16          ioSize;

    /* Display modes */
    area_id         modeArea;
    uint32          modeCount;
    display_mode    currentMode;
    display_mode    preferredMode;
    frame_buffer_config frameBufferConfig;
    uint32          dpmsMode;

    uint32          maxPixelClock;
    uint32          colorSpaceCount;
    color_space     colorSpaces[3];

    /* Bootloader framebuffer information */
    bool            hasBootMode;
    uint16          bootWidth;
    uint16          bootHeight;
    uint16          bootDepth;
    uint32          bootBytesPerRow;

    /* EDID */
    bool            hasEdid;
    edid1_info      edidInfo;

    /* Hardware Cursor */
    uint32          cursorOffset;      /* Offset in VRAM */
    uint32          cursorBufferSize;  /* Typically 1024 bytes */
    struct {
        uint16      hotX;
        uint16      hotY;
        int16       x;
        int16       y;
        uint16      width;
        uint16      height;
        bool        visible;
    } cursor;

    /* 2D Engine */
    struct {
        Benaphore   lock;
        uint32      count;
    } engine;

    /* Video Overlay */
    struct {
        uint32          token;
        overlay_buffer  buffers[4];
        bool            bufferAllocated[4];
        uint32          bufferOffset[4];
        uint32          bufferSize[4];
        bool            active;
    } overlay;

    xgi_settings    settings;
    bool            accelerantInUse;
};

#endif /* XGI_DRIVER_INTERFACE_H */
