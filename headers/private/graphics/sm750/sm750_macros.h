/* registers definitions and macros for access to them */

/* Standard PCI Configuration Registers */
#define SM750_PCI_DEVID        0x00 // Vendor & Device ID
#define SM750_PCI_CMD_STAT     0x04 // Command & Status Register
#define SM750_PCI_CLASS        0x08 // Revision ID & Class Code
#define SM750_PCI_HEADER       0x0c // Cache Line Size, Latency Timer, Header Type

/* Base Address Registers (BARs) */
#define SM750_PCI_BAR0_REGS    0x10 // MMIO Registers (Standard SM750: 2MB)
#define SM750_PCI_BAR1_FB      0x14 // Frame Buffer (Video Memory)
#define SM750_PCI_BAR2         0x18 // Generalmente non usato su SM750
#define SM750_PCI_BAR3         0x1c 
#define SM750_PCI_BAR4         0x20 
#define SM750_PCI_BAR5         0x24 

/* Subsystem Identifiers */
#define SM750_PCI_SUBSYSID     0x2c // Subsystem Vendor & ID

/* Expansion ROM */
#define SM750_PCI_ROMBASE      0x30 // Video BIOS ROM Base Address

/* Capability Pointer */
#define SM750_PCI_CAPPTR       0x34 // Capabilities Pointer (es. per Power Management)

/* Interrupts */
#define SM750_PCI_INTERRUPT    0x3c // Interrupt Line & Pin

/* SM750 Specific PCI Config (Scratch / Power) */
#define SM750_PCI_VGA_CTRL     0x54 // Controllo abilitazione VGA
#define SM750_PCI_SCRATCH      0x58 // Registro scratch per uso del driver
#define SM750_PCI_PM_CTRL      0x60 // Power Management Control (se presente)

/* Interrupt Control (SM750 System Control Module) 
 * Per leggere lo stato: SYS_R(INT_STATUS)
 * Per abilitare il VBlank: SYS_W(INT_MASK, SM750_INT_VBLANK_CRT1)
 */
#define SM750_SYS_INT_STATUS     0x00000020 // Registro di stato (quali interrupt sono scattati)
#define SM750_SYS_INT_MASK       0x00000024 // Registro di maschera (quali interrupt abilitare)

/* Definizioni dei bit per VBlank (da usare con le macro SYS_R / SYS_W) */
#define SM750_INT_VBLANK_CRT1    (1 << 0)   // Bit per il VBlank del Canale primario (CRT)
#define SM750_INT_VBLANK_PANEL   (1 << 1)   // Bit per il VBlank del Canale secondario (Panel)

/* --- Registri di Controllo Motore 2D (SM750 GE) --- */
#define SM750_GE_SOURCE          0x10000 // Sorgente (Offset)
#define SM750_GE_DESTINATION     0x10004 // Destinazione (Offset)
#define SM750_GE_DIMENSION       0x10008 // Larghezza e Altezza
#define SM750_GE_CONTROL         0x1000C // Comando (BitBlt, Fill, ecc.)
#define SM750_GE_PITCH           0x10010 // Pitch sorgente e destinazione
#define SM750_GE_FOREGROUND      0x10014 // Colore primo piano
#define SM750_GE_BACKGROUND      0x10018 // Colore sfondo
#define SM750_GE_STRETCH         0x1001C // Parametri di stretch
#define SM750_GE_COLOR_COMPARE   0x10020 // Color Key
#define SM750_GE_CLIP_TL         0x10024 // Clipping Top-Left
#define SM750_GE_CLIP_BR         0x10028 // Clipping Bottom-Right
#define SM750_GE_MONO_PATTERN_L  0x1002C // Pattern Monocromatico (Low)
#define SM750_GE_MONO_PATTERN_H  0x10030 // Pattern Monocromatico (High)
#define SM750_GE_STATUS          0x10034 // Stato del motore (Busy/Idle)

/* --- ROP (Raster Operations) --- */
/* SM750 usa un codice ROP a 8 bit (es. 0xCC per copia, 0xF0 per pattern) */
#define SM750_GE_ROP_CODE         0x1000C // Parte del registro di controllo

/* --- Clipping Registers --- */
#define SM750_GE_CLIP_TL          0x10024 // Top-Left (X in 31:16, Y in 15:0)
#define SM750_GE_CLIP_BR          0x10028 // Bottom-Right

/* --- Pattern & Color Registers --- */
#define SM750_GE_COLOR_0          0x10014 // Foreground Color (Pattern/Rect Fill)
#define SM750_GE_COLOR_1          0x10018 // Background Color
#define SM750_GE_MONO_PAT_L       0x1002C // Mono Pattern 64-bit (Low)
#define SM750_GE_MONO_PAT_H       0x10030 // Mono Pattern 64-bit (High)

/* --- BitBlt (Blit) Registers --- */
#define SM750_GE_SRC_ADDR         0x10000 // Indirizzo sorgente (offset in memoria video)
#define SM750_GE_DST_ADDR         0x10004 // Indirizzo destinazione
#define SM750_GE_PITCH            0x10010 // Pitch (Src in 31:16, Dst in 15:0)
#define SM750_GE_DIMENSION        0x10008 // Dimensione (Width in 31:16, Height in 15:0)
#define SM750_GE_SRC_XY           0x10040 // Coordinate X,Y sorgente (se usate)
#define SM750_GE_DST_XY           0x10044 // Coordinate X,Y destinazione

/* --- Status & FIFO --- */
#define SM750_GE_STATUS           0x10034 // Sostituisce i vari FIFOFREE

/* System Control Registers (SM750) 
 * Sostituisce PWRUP/Coldstart
 */
#define SM750_SYS_MISC_CTRL       0x00000004 // Controllo generale e Power up
#define SM750_SYS_BOOTSTRAP       0x0000000C // Info dai jumper/strap (sostituisce STRAPINFO)
#define SM750_SYS_PLL_CTRL        0x00000010 // Controllo principale PLL
#define SM750_SYS_DRAM_CTRL       0x00000030 // Configurazione memoria (sostituisce PFB_CONFIG)

/* Clock Control 
 * Sostituisce COREPLL/MEMPLL
 */
#define SM750_SYS_MCLK_CTRL       0x00000038 // Memory Clock
#define SM750_SYS_SCLK_CTRL       0x0000003C // System Clock
#define SM750_SYS_M2XCLK_CTRL     0x00000040 // Master Display Clock

/* VGA Legacy (Mappati in MMIO) */
#define VG_MISC_W                 0x000C03C2
#define VG_MISC_R                 0x000C03CC
#define VG_SEQ_IND                0x000C03C4
#define VG_SEQ_DAT                0x000C03C5
#define VG_GRPH_IND               0x000C03CE
#define VG_GRPH_DAT               0x000C03CF

/* Palette RAM (Primary & Secondary) */
#define SM750_DISP_PALETTE_RAM    0x00080400 // Palette primaria (CRT)
#define SM750_DISP_PALETTE_RAM2   0x00080800 // Palette secondaria (Panel)

/* --- Primary Display (CRT) --- 
 * Sostituisce CRTC1/2, CURSOR, PWR
 */
#define SM750_DISP_CRT_CTRL       0x00080000 // Controllo e timing (sostituisce FUNCSEL)
#define SM750_DISP_CRT_FB_ADDR    0x0008000C // Indirizzo inizio schermo (sostituisce FBSTADD)
#define SM750_DISP_CRT_FB_WIDTH   0x00080010 // Larghezza riga (Pitch)
#define SM750_DISP_CRT_CUR_ADDR   0x000800F0 // Indirizzo Cursore (sostituisce CURADD)
#define SM750_DISP_CRT_CUR_CTRL   0x000800F4 // Configurazione Cursore

/* --- Secondary Display (Panel) --- */
#define SM750_DISP_PANEL_CTRL     0x00080200 // Controllo e timing
#define SM750_DISP_PANEL_FB_ADDR  0x00080204 // Indirizzo inizio schermo 2
#define SM750_DISP_PANEL_FB_WIDTH 0x00080208
#define SM750_DISP_PANEL_PWR      0x00080218 // Accensione LCD (sostituisce PANEL_PWR)
#define SM750_DISP_PANEL_CUR_ADDR 0x00080230 // Indirizzo Cursore 2
#define SM750_DISP_PANEL_CUR_CTRL 0x00080234

/* --- CRT Display Timings (Primary) --- */
#define SM750_CRT_H_TOTAL_ACTIVE      0x00080000 // Total (31:16) | Active (15:0)
#define SM750_CRT_H_SYNC              0x00080004 // Width (31:16) | Start (15:0)
#define SM750_CRT_V_TOTAL_ACTIVE      0x00080008 // Total (31:16) | Active (15:0)
#define SM750_CRT_V_SYNC              0x0008000C // Height (31:16) | Start (15:0)
#define SM750_CRT_CONTROL             0x00080010 // Polarità Sync, Abilitazione, ecc.

/* --- Panel Display Timings (Secondary) --- */
#define SM750_PANEL_H_TOTAL_ACTIVE    0x00080200
#define SM750_PANEL_H_SYNC            0x00080204
#define SM750_PANEL_V_TOTAL_ACTIVE    0x00080208
#define SM750_PANEL_V_SYNC            0x0008020C
#define SM750_PANEL_CONTROL           0x00080210

/* CRTC Indices (SM750_CRTCX_...) */
#define SM750_CRTCX_HTOTAL      0x00
#define SM750_CRTCX_HDISPE      0x01
#define SM750_CRTCX_HBLANKS     0x02
#define SM750_CRTCX_HBLANKE     0x03
#define SM750_CRTCX_HSYNCS      0x04
#define SM750_CRTCX_HSYNCE      0x05
#define SM750_CRTCX_VTOTAL      0x06
#define SM750_CRTCX_OVERFLOW    0x07
#define SM750_CRTCX_VSYNCS      0x10
#define SM750_CRTCX_VSYNCE      0x11
#define SM750_CRTCX_VDISPE      0x12
#define SM750_CRTCX_PITCH       0x13

/* Sequencer Indices (SM750_SEQX_...) */
#define SM750_SEQX_RESET        0x00
#define SM750_SEQX_CLKMODE      0x01
#define SM750_SEQX_MEMMODE      0x04

/* Graphics Indices (SM750_GRPHX_...) */
#define SM750_GRPHX_MODE        0x05
#define SM750_GRPHX_MISC        0x06
#define SM750_GRPHX_BITMASK     0x08

/* Attribute Indices (SM750_ATBX_...) */
#define SM750_ATBX_MODECTL      0x10
#define SM750_ATBX_COLSEL       0x14


/* --- Core Access Macros (Memory Mapped IO) --- */
#define SM750_REG8(r_)  ((vuint8  *)regs)[(r_)]
#define SM750_REG16(r_) ((vuint16 *)regs)[(r_) >> 1]
#define SM750_REG32(r_) ((vuint32 *)regs)[(r_) >> 2]

/* --- PCI Config Space (via ioctl) --- */
#define CFGR(A)   (eng_pci_access.offset=SM750_PCI_##A, ioctl(fd,ENG_GET_PCI, &eng_pci_access,sizeof(eng_pci_access)), eng_pci_access.value)
#define CFGW(A,B) (eng_pci_access.offset=SM750_PCI_##A, eng_pci_access.value = B, ioctl(fd,ENG_SET_PCI,&eng_pci_access,sizeof(eng_pci_access)))

/* --- Graphic Engine (2D Acceleration) --- */
#define GE_R(A)      (SM750_REG32(SM750_GE_##A))
#define GE_W(A,B)    (SM750_REG32(SM750_GE_##A) = (B))

/* --- Display Control (Panel/CRT) --- */
#define DISP_R(A)    (SM750_REG32(SM750_DISP_##A))
#define DISP_W(A,B)  (SM750_REG32(SM750_DISP_##A) = (B))

/* --- Video Control (Overlay/Scaler) --- */
#define VID_R(A)     (SM750_REG32(SM750_VIDEO_##A))
#define VID_W(A,B)   (SM750_REG32(SM750_VIDEO_##A) = (B))

/* --- System & Clock Control --- */
#define SYS_R(A)     (SM750_REG32(SM750_SYS_##A))
#define SYS_W(A,B)   (SM750_REG32(SM750_SYS_##A) = (B))

/* --- VGA Legacy Registers (Indexed) --- */

/* CRTC (Cathode Ray Tube Controller) */
#define CRTCW(A,B)   (SM750_REG16(VG_CRTC_IND) = ((SM750_CRTCX_##A) | ((B) << 8)))
#define CRTCR(A)     (SM750_REG8(VG_CRTC_IND) = (SM750_CRTCX_##A), SM750_REG8(VG_CRTC_DAT))

/* Sequencer */
#define SEQW(A,B)    (SM750_REG16(VG_SEQ_IND) = ((SM750_SEQX_##A) | ((B) << 8)))
#define SEQR(A)      (SM750_REG8(VG_SEQ_IND) = (SM750_SEQX_##A), SM750_REG8(VG_SEQ_DAT))

/* Graphics Controller */
#define GRPHW(A,B)   (SM750_REG16(VG_GRPH_IND) = ((SM750_GRPHX_##A) | ((B) << 8)))
#define GRPHR(A)     (SM750_REG8(VG_GRPH_IND) = (SM750_GRPHX_##A), SM750_REG8(VG_GRPH_DAT))

/* Attribute Controller */
#define ATBW(A,B)    (SM750_REG8(VG_INSTAT1), SM750_REG8(VG_ATTR_INDW) = ((SM750_ATBX_##A) | 0x20), SM750_REG8(VG_ATTR_DATW) = (B))
#define ATBR(A)      (SM750_REG8(VG_INSTAT1), SM750_REG8(VG_ATTR_INDW) = ((SM750_ATBX_##A) | 0x20), SM750_REG8(VG_ATTR_DATR))

/* I2C (Software/Bit-banging per DDC/EDID) */
#define I2CW(A,B)    (SM750_REG32(SM750_I2C_##A) = (B))
#define I2CR(A)      (SM750_REG32(SM750_I2C_##A))

/* Macro per attendere che il motore grafico sia libero (Idle) */
#define GE_WAIT_IDLE() \
    while (GE_R(STATUS) & 0x00000001) { /* attesa attiva */ }

/* Macro per attendere spazio nella FIFO (SM750 ha 16 slot) */
#define GE_WAIT_FIFO(slots) \
    while (((GE_R(STATUS) >> 8) & 0x1F) < (slots)) { /* attesa */ }
