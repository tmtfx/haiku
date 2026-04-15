/* registers definitions and macros for access to them */

#define SM750_PRIVATE_DATA_MAGIC 0x750DA7A1
/* Standard PCI Configuration Registers */
#define SM750_PCI_DEVID        0x00 // Vendor & Device ID
#define SM750_PCI_CMD_STAT     0x04 // Command & Status Register
#define SM750_PCI_CLASS        0x08 // Revision ID & Class Code
#define SM750_PCI_LT           0x0D // Latency Timer
#define SM750_LINEAR_FB_BAR    0x10 // Linear Frame Buffer Base Address Register
#define SM750_BAR_MMA          0x14 // Base Address Register for Memory Map Address

#define SM750_64_VGA_BA           0x18 // 64K VGA 0xA0000/0xB0000 Base Address
#define SM750_32_VGA_BA           0x1C // 32K VGA 0xB8000 Base Address
#define SM750_VGA_IO_3CX_BA       0x20 // VGA I/O Ports 0x3Cx Base Address
#define SM750_VGA_IO_3DX_3BX_BA   0x24 // VGA I/O Ports 0x3Dx/0x3Bx Base Address
#define SM750_PCI_SUBSYSID     0x2C
/* Expansion ROM */
#define SM750_PCI_ROMBASE      0x30
/* Capability Pointer */
#define SM750_PCI_CAPPTR       0x34 // Capabilities Pointer (es. per Power Management)
/* Interrupts */
#define SM750_PCI_INTERRUPT    0x3C // Interrupt Line & Pin
#define SM750_PWR_DWN_CAP_REG  0x40
#define SM750_PWR_DWN_CAP_DATA 0x44

/* Base Address Registers (BARs) */
/* NOPE
#define SM750_PCI_BAR0_REGS    0x10 // MMIO Registers (Standard SM750: 2MB)
#define SM750_PCI_BAR1_FB      0x14 // Frame Buffer (Video Memory)
#define SM750_PCI_BAR2         0x18 // Generalmente non usato su SM750
#define SM750_PCI_BAR3         0x1c 
#define SM750_PCI_BAR4         0x20 
#define SM750_PCI_BAR5         0x24 */

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
// --------------------------------- DA VERIFICARE -------------------------*/
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
/* -------------------------------- FINO QUI ----------------------------------*/
/* System Control Registers (SM750) 
 * Sostituisce PWRUP/Coldstart
 */
#define SM750_SYS_CTRL            0x00000000 // SYSTEM CONTROL
#define SM750_SYS_MISC_CTRL       0x00000004 // Controllo generale e Power up
#define SM750_SYS_GPIO_CTRL       0x00000008 // Controllo per GPIO
#define SM750_SYS_BOOTSTRAP       0x0000000C // Info dai jumper/strap (sostituisce STRAPINFO)
#define SM750_SYS_PLL_CTRL        0x00000010 // Controllo principale PLL
#define SM750_SYS_DRAM_CTRL       0x00000030 // Configurazione memoria (sostituisce PFB_CONFIG)
#define SM750_SYS_DEVID           0x00000054 // Device Id
#define SM750_SYS_PLL_CLKC        0x00000058 // PLL Clock Count ONLY FOR TEST PURPOSES

/* Clock Control */
#define SM750_SYS_CUR_CLK_STATUS  0x00000040 // Power management current clock status
#define SM750_SYS_PWR_MODE_0_CLKC 0x00000044 // Power Mode 0 Clock Control
#define SM750_SYS_PWR_MODE_1_CLKC 0x00000048 // Power Mode 0 Clock Control
#define SM750_SYS_PWR_MODE_CTRL   0x0000004C // Power Mode Control
/* Scratch data */
#define SM750_SYS_SCRATCH_DATA    0x0000006C // Scratch Data

#define SM750_SYS_VGA_CONFIG      0x00000088 // VGA Configuration register

/* VGA Legacy (Mappati in MMIO) */
#define VG_MISC_W                 0x000C03C2
#define VG_MISC_R                 0x000C03CC
#define VG_SEQ_IND                0x000C03C4
#define VG_SEQ_DAT                0x000C03C5
#define VG_GRPH_IND               0x000C03CE
//#define VG_GRPH_DAT               0x000C03CF

/* Palette RAM */
#define SM750_DISP_PALETTE_RAM       0x00080400 // Primary (PANEL)
#define SM750_DISP_PALETTE_RAM2      0x00080C00 // Secondary (CRT)

/* Display PLL */
#define SM750_DISP_PANEL_PLL         0x0000005C // Primary Display PLL
#define SM750_DISP_CRT_PLL           0x00000060 // Secondary Display PLL

/* --- PANEL (Primary) --- */
#define SM750_DISP_PANEL_FB_ADDR     0x0008000C // Primary Display FB Address
#define SM750_DISP_PANEL_FB_WIDTH    0x00080010 // Primary Display FB Offset/Window Width

/* --- PANEL Display Timings (Primary) --- */
#define SM750_PANEL_H_TOTAL_ACTIVE   0x00080024 // Pag 143: Primary Horizontal Total
#define SM750_PANEL_H_SYNC           0x00080028 // Pag 143: Primary Horizontal Sync
#define SM750_PANEL_V_TOTAL_ACTIVE   0x0008002C // Pag 143: Primary Vertical Total
#define SM750_PANEL_V_SYNC           0x00080030 // Pag 143: Primary Vertical Sync
#define SM750_PANEL_CONTROL          0x00080000 // Pag 143: Primary Display Control
#define SM750_PANEL_CURRENT_LINE     0x00080034 // Pag 143: Primary Display Control


/* Video Control */
#define SM750_DISP_PANEL_VIDEO_DISP_CTRL      0x00080040
#define SM750_DISP_PANEL_VIDEO_FB0_ADDR       0x00080044
#define SM750_DISP_PANEL_VIDEO_FB_WIDTH       0x00080048
#define SM750_DISP_PANEL_VIDEO_FB0_LAST_ADDR  0x0008004C
#define SM750_DISP_PANEL_VIDEO_PL_TL_POS      0x00080050
#define SM750_DISP_PANEL_VIDEO_PL_BR_POS      0x00080054
#define SM750_DISP_PANEL_VIDEO_SCALE          0x00080058
#define SM750_DISP_PANEL_VIDEO_INIT_SCALE     0x0008005C
#define SM750_DISP_PANEL_VIDEO_YUV_CONST      0x00080060
#define SM750_DISP_PANEL_VIDEO_FB1_ADDR       0x00080064
#define SM750_DISP_PANEL_VIDEO_FB1_LAST_ADDR  0x00080068

/* Video Alpha Control */
#define SM750_DISP_PANEL_ALPHA_CTRL          0x00080080
#define SM750_DISP_PANEL_ALPHA_FB_ADDR       0x00080084
#define SM750_DISP_PANEL_ALPHA_FB_WIDTH      0x00080088
#define SM750_DISP_PANEL_ALPHA_FB_LAST_ADDR  0x0008008C
#define SM750_DISP_PANEL_ALPHA_PL_TL_POS     0x00080090
#define SM750_DISP_PANEL_ALPHA_PL_BR_POS     0x00080094
#define SM750_DISP_PANEL_ALPHA_SCALE         0x00080098
#define SM750_DISP_PANEL_ALPHA_INIT_SCALE    0x0008009C
#define SM750_DISP_PANEL_ALPHA_CHROMA_KEY    0x000800A0
// 0x000800A4 - 0x000800C0 VIDEO ALPHA COLOR LOOKUP
 

/* Cursor PANEL (Primary) */
#define SM750_DISP_PANEL_CUR_ADDR             0x000800F0 
#define SM750_DISP_PANEL_CUR_POS              0x000800F4 // Location
#define SM750_DISP_PANEL_CUR_COLOR12          0x000800F8
#define SM750_DISP_PANEL_CUR_COLOR3           0x000800FC
#define SM750_DISP_PANEL_CUR_ALPHA_CTRL       0x00080100
#define SM750_DISP_PANEL_CUR_ALPHA_FB_ADDR    0x00080104
#define SM750_DISP_PANEL_CUR_ALPHA_FB_WIDTH   0x00080108
#define SM750_DISP_PANEL_CUR_ALPHA_PL_TL_POS  0x0008010C
#define SM750_DISP_PANEL_CUR_ALPHA_PL_BR_POS  0x00080110
#define SM750_DISP_PANEL_CUR_ALPHA_CHROMA_KEY 0x00080114
// 0x00080118 - 0x00080134 ALPHA COLOR LOOKUP

/* --- CRT (Secondary) --- */
#define SM750_DISP_CRT_FB_ADDR       0x00080204 // Secondary Display FB Address
#define SM750_DISP_CRT_FB_WIDTH      0x00080208 // Secondary Display FB Offset/Window Width

/* --- CRT Display Timings (Secondary) --- */
#define SM750_CRT_H_TOTAL_ACTIVE     0x0008020C // Pag 143: Secondary Horizontal Total
#define SM750_CRT_H_SYNC             0x00080210 // Pag 143: Secondary Horizontal Sync
#define SM750_CRT_V_TOTAL_ACTIVE     0x00080214 // Pag 143: Secondary Vertical Total
#define SM750_CRT_V_SYNC             0x00080218 // Pag 143: Secondary Vertical Sync
#define SM750_CRT_CONTROL            0x00080200 // Pag 143: Secondary Display Control
#define SM750_CRT_CURRENT_LINE       0x00080220
#define SM750_CRT_MONITOR_DETECT     0x00080224

/* Cursor CRT (Secondary) */
#define SM750_DISP_CRT_CUR_ADDR      0x00080230 // Indirizzo Cursore 2
#define SM750_DISP_CRT_CUR_POS       0x00080234 // Location
#define SM750_DISP_CRT_CUR_COLOR12   0x00080238 // Colore 1 e 2
#define SM750_DISP_CRT_CUR_COLOR3    0x0008023C // Colore 3

/* ---------------------------- DA VERIFICARE ------------------------- */
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
/* ---------------------------- FINO QUI ------------------------- */

#define SM750_GPIO_DATA        0x010000
#define SM750_GPIO_DIRECTION   0x010008
#define SM750_GPIO_INT_SETUP   0x010010
#define SM750_GPIO_INT_STATUS  0x010014 // READ
#define SM750_GPIO_INT_RESET   0x010014 // WRITE (DATASHEET use same address)

#define SM750_I2C_BYTE_COUNT   0x010040
#define SM750_I2C_CONTROL      0x010041
#define SM750_I2C_STATUS       0x010042 // Status if read
#define SM750_I2C_RESET        0x010042 // Same address as status, but write access for bit 2 (third)
#define SM750_I2C_SLAVE_ADDR   0x010043
// From  0x010044 to 0x010053 I2C DATA
/* There are 16 I2C Data registers that hold the data to be written 
 * to or read from the I2C Slave. These registers can be accessed in
 * 8-bit, 16-bit, or 32-bit mode for very fast FIFO transfer.
 */

#define SM750_MIN_VCO  240000  // 240 MHz
#define SM750_MAX_VCO  480000  // 480 MHz (più sicuro per evitare jitter) (potrebbe arrivare fino a 1GHz)


/* --- 2D Graphics Engine Registers --- */
#define SM750_2D_SOURCE              0x00100000 // X, Y source
#define SM750_2D_DESTINATION         0x00100004 // X, Y destination
#define SM750_2D_DIMENSION           0x00100008 // Width, Height
#define SM750_2D_CONTROL             0x0010000C // ROP, Command, Direction
#define SM750_2D_PITCH               0x00100010 // Source & Dest Pitch
#define SM750_2D_FOREGROUND          0x00100014
#define SM750_2D_BACKGROUND          0x00100018
#define SM750_2D_STRETCH             0x0010001C // Stretch & Format
#define SM750_2D_COLOR_COMPARE       0x00100020
#define SM750_2D_COLOR_COMPARE_MASK  0x00100024
#define SM750_2D_MASK                0x00100028
#define SM750_2D_CLIP_TL             0x0010002C // Clip Top Left
#define SM750_2D_CLIP_BR             0x00100030 // Clip Bottom Right
#define SM750_2D_MONO_PATTERN_LOW    0x00100034
#define SM750_2D_MONO_PATTERN_HIGH   0x00100038
#define SM750_2D_WINDOW_WIDTH        0x0010003C
#define SM750_2D_SOURCE_BASE         0x00100040
#define SM750_2D_DEST_BASE           0x00100044
#define SM750_2D_ALPHA               0x00100048
#define SM750_2D_WRAP                0x0010004C
#define SM750_2D_STATUS              0x00100050 // Bit 31: Busy, Bit 30: Empty

/* --- CSC (Color Space Conversion) Registers --- */
#define SM750_CSC_SOURCE_BASE        0x001000C8
#define SM750_CSC_CONST              0x001000CC
#define SM750_CSC_SOURCE_X           0x001000D0
#define SM750_CSC_SOURCE_Y           0x001000D4
#define SM750_CSC_U_SOURCE_BASE_YUV420  0x001000D8
#define SM750_CSC_V_SOURCE_BASE_YUV420  0x001000DC
#define SM750_CSC_SOURCE_DIMENSION   0x001000E0
#define SM750_CSC_SOURCE_PITCH       0x001000E4
#define SM750_CSC_DESTINATION        0x001000E8
#define SM750_CSC_DEST_DIMENSION     0x001000EC
#define SM750_CSC_DEST_PITCH         0x001000F0
#define SM750_CSC_SCALE_FACTOR       0x001000F4
#define SM750_CSC_DESTINATION_BASE   0x001000F8
#define SM750_CSC_CONTROL            0x001000FC
// ... aggiungi gli altri se ti servono per il video, ma questi sono i principali

#define WAIT_2D_IDLE() \
    while (SM750_REG32(SM750_2D_STATUS) & (1 << 31)) { snooze(1); }


/* --- Core Access Macros (Memory Mapped IO) --- */
#define SM750_REG8(r_)  ((vuint8  *)regs)[(r_)]
#define SM750_WREG8(r_, v_)  (((vuint8  *)regs)[(r_)] = (v_))

#define SM750_REG16(r_) ((vuint16 *)regs)[(r_) >> 1]
#define SM750_WREG16(r_, v_) (((vuint16 *)regs)[(r_) >> 1] = (v_))

#define SM750_REG32(r_) ((vuint32 *)regs)[(r_) >> 2]
#define SM750_WREG32(r_, v_) (((vuint32 *)regs)[(r_) >> 2] = (v_))

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
