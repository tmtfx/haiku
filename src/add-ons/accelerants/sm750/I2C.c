/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include "DriverInterface.h"
#include "sm750_macros.h"
#include "protos.h"

extern accelerant_info *gInfo;

#define PIN_SCL 30
#define PIN_SDA 31

static bool 
get_gpio_pin(uint32 pin) 
{
	vuint32 *regs = gInfo->regs;
	return (SM750_REG32(SM750_GPIO_DATA) & (1 << pin)) != 0;
}

// Funzione critica: segue la logica del driver Linux
// HIGH = INPUT (pull-up resistor alza il segnale)
// LOW = OUTPUT con valore 0
static void 
set_gpio_pin(uint32 pin, bool high) 
{
	vuint32 *regs = gInfo->regs;
	uint32 gpio_dir = SM750_REG32(SM750_GPIO_DIRECTION);
	uint32 gpio_data = SM750_REG32(SM750_GPIO_DATA);
	
	if (high) {
		// HIGH: Imposta come INPUT, il pull-up resistor tira su
		gpio_dir &= ~(1 << pin);
		SM750_WREG32(SM750_GPIO_DIRECTION, gpio_dir);
	} else {
		// LOW: Prima abbassa il segnale, poi imposta come OUTPUT
		gpio_data &= ~(1 << pin);
		SM750_WREG32(SM750_GPIO_DATA, gpio_data);
		gpio_dir |= (1 << pin);
		SM750_WREG32(SM750_GPIO_DIRECTION, gpio_dir);
	}
}

static void i2c_start(uint32 scl, uint32 sda) {
	// Start I2C: SDA scende mentre SCL è alto
	set_gpio_pin(sda, true);
	set_gpio_pin(scl, true);
	set_gpio_pin(sda, false);
}

static void i2c_stop(uint32 scl, uint32 sda) {
	// Stop I2C: SCL sale, poi SDA sale mentre SCL è alto
	set_gpio_pin(scl, true);
	set_gpio_pin(sda, false);
	set_gpio_pin(sda, true);
}

static void sw_i2c_wait(void)
{
	int i, tmp;
	for (i = 0; i < 600; i++) {
		tmp = i;
		tmp += i;
	}
}

static bool i2c_write_byte(uint32 scl, uint32 sda, uint8 byte) 
{
	int i;
	
	// Invia i bit dal più significativo al meno significativo
	for (i = 0; i < 8; i++) {
		// SCL basso
		set_gpio_pin(scl, false);
		
		// Imposta il bit su SDA
		if ((byte & 0x80) != 0)
			set_gpio_pin(sda, true);
		else
			set_gpio_pin(sda, false);
		
		sw_i2c_wait();
		
		// SCL alto (il dispositivo legge il bit)
		set_gpio_pin(scl, true);
		sw_i2c_wait();
		
		// Shifta il byte per il prossimo bit
		byte = byte << 1;
	}
	
	// Preparazione per l'ACK
	set_gpio_pin(scl, false);
	set_gpio_pin(sda, true); // SDA alto (INPUT) per ricevere l'ACK
	
	// Leggi l'ACK
	sw_i2c_wait();
	set_gpio_pin(scl, true);
	sw_i2c_wait();
	
	// Verifica ACK (attendi che SDA sia basso)
	bool ack = false;
	for (i = 0; i < 255; i++) {
		if (!get_gpio_pin(sda)) {
			ack = true;
			break;
		}
		set_gpio_pin(scl, false);
		sw_i2c_wait();
		set_gpio_pin(scl, true);
		sw_i2c_wait();
	}
	
	// Pulisci
	set_gpio_pin(scl, false);
	set_gpio_pin(sda, true);
	
	return ack;
}

static uint8 i2c_read_byte(uint32 scl, uint32 sda, bool send_ack) 
{
	int i;
	uint8 data = 0;
	
	for (i = 7; i >= 0; i--) {
		// SCL basso, SDA alto (INPUT per ricevere)
		set_gpio_pin(scl, false);
		set_gpio_pin(sda, true);
		sw_i2c_wait();
		
		// SCL alto
		set_gpio_pin(scl, true);
		sw_i2c_wait();
		
		// Leggi il bit da SDA
		if (get_gpio_pin(sda))
			data |= (1 << i);
	}
	
	// Invia ACK se richiesto
	if (send_ack) {
		// ACK: tirare SDA basso
		set_gpio_pin(scl, false);
		set_gpio_pin(sda, false);
		sw_i2c_wait();
		set_gpio_pin(scl, true);
		sw_i2c_wait();
	}
	
	// SCL basso e SDA alto
	set_gpio_pin(scl, false);
	set_gpio_pin(sda, true);
	
	return data;
}

static void clean_i2c_bus_error() {
	vuint32 *regs = gInfo->regs;
	uint8 status = SM750_REG8(SM750_I2C_STATUS);
	if (status & 0x04) {
		// Se il Bit 2 (Err) è 1
		debug_printf("SM750_ACC: Rilevato Bus Error (Bit 2), reset in corso...\n");
		SM750_WREG8(SM750_I2C_RESET, 0x00); // Scriviamo 0 per fare Clear
		snooze(10);
	}
}
status_t sm750_read_edid_I2C(uint8* buffer) {
	/* I2C EDID retrivial.
	 * Due to known bug of this cards
	 * the clock stretching prevents data reading
	 * from this I2C module.
	 * As done in other platforms the EDID reading
	 * has been done by simulating an I2C via GPIO
	 * the comments on this source code below keeps
	 * log of what has been discovered by my attempts
	 * to make it work.
	 * If the system has the minimum requirements enabled
	 * (take a look at the code) the I2C module can show you
	 * ACK, Comp and Busy bits, but data registers always
	 * returns 00
	 */
	vuint32 *regs = gInfo->regs;
	// Reset e Pulizia del controller I2C
	//bool is_panel = gInfo->si->card_info.is_panel;
	status_t ret = B_OK;
	
	//LETTURA E BACKUP STATO DELLA GPU
	uint32 PM0_state = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC);
	uint32 PM1_state = SM750_REG32(SM750_SYS_PWR_MODE_1_CLKC);
	uint32 PMC_state = SM750_REG32(SM750_SYS_PWR_MODE_CTRL);
	uint32 P_control = SM750_REG32(SM750_PANEL_CONTROL); // these should not be touched
	uint32 C_control = SM750_REG32(SM750_CRT_CONTROL); // these should not be touched
	uint32 GPIO_ctrl = SM750_REG32(SM750_SYS_GPIO_CTRL);
	uint32 PPLL_ctrl = SM750_REG32(SM750_DISP_PANEL_PLL);
	uint32 CPLL_ctrl = SM750_REG32(SM750_DISP_CRT_PLL);
	uint32 MXCC_PLL_ctrl = SM750_REG32(SM750_DISP_MXCLKC_PLL); //I2C clock is connected to MXCLK PLL Control
	uint32 I2C_ctrl  = SM750_REG32(SM750_I2C_CONTROL); // this should be 0x0 as default
	// Questi non ci interessano
	//SM750_SYS_MISC_CTRL
	//SM750_SYS_CTRL
	
	// IMPOSTAZIONE MINIMALE
	
	// TEST VITA I2C
	// Ripetuti test su quali funzionalità sono necessarie al funzionamento
	// Risultato:
	
	// ok quindi occorre che
	// 1) i power mode siano (0x00005147)
	//	in particolare impostare a 1 i bit:
	//	1 - DMA
	//	2 - Display Controller Clock Control
	//	3 - Local Memory Controller Clock Control
	//	6 - GPIO clock
	//   12 - con 13 a 0: M2XCLK Divided by 2 (default)
	//   15 - con 15 a 0: MCLK Divided by 2 (default)
	// 2) I pll di pannello e crt possono essere spenti
	// 3) registro di controllio GPIO impostato ovviamente per I2C 0xC0000000
	// 4) ovviamente il master clock mxclk pll control deve essere attivo
	//	e non byassato visto che il clock i2c dipende direttamente da esso
	
	// Master Clock PLL
	// assicuriamoci che il clock principale sia attivo e non bypassato,
	// manteniamo la configurazione attiva, il clock I2C tramite divisore 7
	// dipende esclusivamente da questo PLL
	uint32 mxclk = SM750_REG32(SM750_DISP_MXCLKC_PLL);
	mxclk |= (1 << 17);  // Power On
	mxclk &= ~(1 << 18); // No Bypass
	//mxclk |= (3 << 12); // Rallentiamo il PLL per far girare ancora più piano il I2C 
	// per assorbire il ritardo del monitor nella lentezza di lettura del I2C, così da 
	// evitare il clock stretching.
	// Se applichiamo questa modifica la scheda va in protezione.
	//debug_printf("SM750_ACC: Rallento MXCLK. Valore registro 0x70: 0x%08x\n", mxclk);
	SM750_WREG32(SM750_DISP_MXCLKC_PLL, mxclk);
	snooze (10000);
	
	// PLLs
	// proviamo in 3 modalità, la prima che proviamo è disattivando il pll 
	// non usato (attualmente dovrebbe essere alla stessa frequenza)
	//SM750_WREG32(SM750_DISP_PANEL_PLL, is_panel ? PPLL_ctrl : 0 );
	//SM750_WREG32(SM750_DISP_CRT_PLL, is_panel ? 0 : CPLL_ctrl );
	SM750_WREG32(SM750_DISP_PANEL_PLL, 0);
	SM750_WREG32(SM750_DISP_CRT_PLL, 0);
	
	// GPIO/I2C
	SM750_WREG32(SM750_SYS_GPIO_CTRL,0xC0000000); //Bit 30 e 31 a 1 per attivare I2C
	
	// PANEL/CRT CONTROL
	//SM750_WREG32(SM750_PANEL_CONTROL,0);// gInfo->si->card_info.is_panel ? P_control : 0 );
	//SM750_WREG32(SM750_CRT_CONTROL,0);// gInfo->si->card_info.is_panel ? 0 : C_control );
	
	// POWER MODES
	// test tolto dma per vedere se è lui che blocca i dati verso i registri
	SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC,0x00005146); // solo i bit 8 per il clock I2C, 6 GPIO, bit 1 e 2 per Local Memory Controller Clock Control, Display Controller Clock Control
	SM750_WREG32(SM750_SYS_PWR_MODE_1_CLKC,0x00005146); // solo i bit 8 per il clock I2C, 6 GPIO, bit 1 e 2 per Local Memory Controller Clock Control, Display Controller Clock Control
	SM750_WREG32(SM750_SYS_PWR_MODE_CTRL,0x00000008); // bit 3 Oscillator input control abilitato, ACPI off, selezione power mode 0
	snooze (100);
	
	// I2C
	SM750_WREG8(SM750_I2C_CONTROL, 0x00); // Disable controller
	snooze(100);
	clean_i2c_bus_error();
	snooze(1000); 
	SM750_WREG8(SM750_I2C_CONTROL, 0x01); // Enable controller
	snooze(1000); 
	//FINE IMPOSTAZIONE MINIMALE
	
	//uint8 info = SM750_REG8(SM750_I2C_STATUS);
	//debug_printf("Stato dell'i2c all'inizio dopo il reset: 0x%02x\n",info);

	// --- FASE 1: SET OFFSET (Indirizziamo il byte 0 dell'EDID) ---
	// Aspettiamo che il bus non sia occupato
	int timeout = 1000;
	while ((SM750_REG8(SM750_I2C_STATUS) & 0x01) && --timeout > 0) snooze(10);
	if ((SM750_REG8(SM750_I2C_STATUS) & 0x01)) {
		debug_printf("SM750_ACC: ERROR - Timeout scaduto ma il bus è ancora bloccato.\n");
		ret = B_BUSY;
		goto finalize;
	}

	SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x00); // Byte Count: 0 (significa 1 byte)
	SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA0); // Slave Addr 0x50 + Write (Bit 0 = 0)
	SM750_WREG8(0x10044, 0x00); // Scriviamo 0x00 nel primo registro dati (offset EDID)
	//SM750_WREG8(SM750_I2C_CONTROL, 0x45); // Control: Enable=1, Start=1 (0x05) // 0x45 per Repeated Start Enabled=1, Start=1, Enable=1
	SM750_WREG8(SM750_I2C_CONTROL, 0x01); // STOP (Bit 2 = 0)	-------------*
	SM750_WREG8(SM750_I2C_CONTROL, 0x05); // START			   -------------*

	// Attesa completamento (Bit 3: Comp) // Transfer: 0 in progress 1 completed
	timeout = 1000;
	while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);
	uint8 info = SM750_REG8(SM750_I2C_STATUS);
	if (!(info & 0x08)) {
		debug_printf("SM750_ACC: ERROR - Timeout scaduto senza bit COMP! Bus bloccato.\nSM750_ACC: Valore del registro di stato del I2C: 0x%02x\n", info);
		// Qui dovresti resettare o uscire, non continuare!
		ret = B_TIMED_OUT;
		goto finalize;
	}
	SM750_WREG8(SM750_I2C_CONTROL, 0x01); // Forza STOP		  --------------*
	snooze(1000); // Aspetta un millisecondo intero			  --------------*

	// CONTROLLO CRITICO: Abbiamo ricevuto l'ACK dal monitor?
	info = SM750_REG8(SM750_I2C_STATUS);
	//debug_printf("Stato dell'i2c per vedere se l'ACK è arrivato: 0x%02x\n",status);
	if (!(info & 0x02)) {
		// Bit 1: Ack
		debug_printf("SM750_ACC: Monitor non ha risposto (No ACK) al Set Offset. Status: 0x%02x\n", info);
		ret = B_DEVICE_NOT_FOUND;
		goto finalize;
	}
	if (info & 0x04) clean_i2c_bus_error();
	// --- FASE 2: LETTURA MASSIVA (128 byte in blocchi da 16) ---
	for (int i = 0; i < 8; i++) {
	SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x0F); // 16 byte
	SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA1); // Read
	SM750_WREG8(SM750_I2C_CONTROL, 0x05);	// Start

	// Attesa COMP
	timeout = 1000;
	while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);
	info = SM750_REG8(SM750_I2C_STATUS);
	debug_printf("SM750_ACC: Stato I2C dopo l'invio di 0xA1 0x%02x\n", info);
	if (!(info & 0x08)) {
		debug_printf("SM750_ACC: ERROR - Timeout scaduto senza bit COMP nella lettura 0xA1! \nSM750_ACC: Bus bloccato: Valore del registro di stato del I2C: 0x%02x\n", info);
		// Qui dovresti resettare o uscire, non continuare!
		ret = B_TIMED_OUT;
		goto finalize;
	} else {
		debug_printf("SM750_ACC: operazione di lettura su 0xA1 completata\n");
		if (info & 0x01) {
			debug_printf("SM750_ACC: ma i2c è ancora BUSY, invio STOP\n");
			SM750_WREG8(SM750_I2C_CONTROL, 0x01); // Riportiamo il Bit 2 a 0 (Stop) ma teniamo Enable=1
			snooze(500);
			info = SM750_REG8(SM750_I2C_STATUS);
		debug_printf("SM750_ACC: Stato I2C dopo l'eventuale stop 0x%02x\n", info);
		}
	}
	

	// UN PICCOLO RESPIRO (fondamentale per alcuni bridge PCI)
	snooze(500);
	
	uint32 test44 = SM750_REG32(0x10044);
	uint32 test48 = SM750_REG32(0x10048);
	uint8  testByte44 = SM750_REG8(0x10044);

	debug_printf("DEBUG: REG32(44)=0x%08x, REG32(48)=0x%08x, REG8(44)=0x%02x\n", 
		  test44, test48, testByte44);

	// LETTURA A 32-BIT (REG32)
	// Se il chip mappa i dati come una riga di memoria, 
	// l'accesso a 32-bit è più "stabile".
	for (int j = 0; j < 4; j++) {
		uint32 val32 = SM750_REG32(0x10044 + (j * 4));
	
		if (val32 != 0) {
			debug_printf("SM750_ACC: BINGO! Dati trovati: 0x%08x\n", val32);
		}

		buffer[(i * 16) + (j * 4) + 0] = (uint8)(val32 & 0xFF);
		buffer[(i * 16) + (j * 4) + 1] = (uint8)((val32 >> 8) & 0xFF);
		buffer[(i * 16) + (j * 4) + 2] = (uint8)((val32 >> 16) & 0xFF);
		buffer[(i * 16) + (j * 4) + 3] = (uint8)((val32 >> 24) & 0xFF);
	}
	// LETTURA A 8-BIT (REG8)
	/*
	for (int j = 0; j < 16; j++) {
		uint8 val8 = SM750_REG8(0x10044 + j);
		// Se vedi qualcosa di diverso da 00 o FF, festeggiamo
		if (val8 != 0x00 && val8 != 0xFF) {
			debug_printf("SM750_ACC: TROVATO! Byte %d = 0x%02x\n", (i*16)+j, val8);
		}
		buffer[(i * 16) + j] = val8;
	}*/
	// LETTURA A 8-BIT FIFO (REG8) (ripetuta sullo stesso registro)
	//for (int j = 0; j < 16; j++) {
	//	uint8 val8 = SM750_REG8(0x10044); // NIENTE + j
	//	buffer[(i * 16) + j] = val8;
	//}
	
	// STOP
	SM750_WREG8(SM750_I2C_CONTROL, 0x01);
	snooze(50);
	}
   

	// --- VERIFICA FINALE ---
	// L'EDID standard inizia sempre con 00 FF FF FF FF FF FF 00
	if (buffer[0] == 0x00 && buffer[1] == 0xFF && buffer[2] == 0xFF) {
		debug_printf("SM750_ACC: EDID letto con successo! Header valido.\n");
		goto finalize;
	}
	
	debug_printf("SM750_ACC: EDID Raw Header errato:\n		   %02x %02x %02x %02x\n		   %02x %02x %02x %02x\n		   %02x %02x %02x %02x\n		   %02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8],buffer[9], buffer[10], buffer[11],buffer[12], buffer[13], buffer[14], buffer[15]);
	ret = B_ERROR;
finalize:
	SM750_WREG32(SM750_DISP_MXCLKC_PLL,MXCC_PLL_ctrl);
	SM750_WREG32(SM750_DISP_PANEL_PLL,PPLL_ctrl);
	SM750_WREG32(SM750_DISP_CRT_PLL,CPLL_ctrl);
	SM750_WREG32(SM750_SYS_GPIO_CTRL,GPIO_ctrl);
	SM750_WREG32(SM750_PANEL_CONTROL,P_control);
	SM750_WREG32(SM750_CRT_CONTROL,C_control);
	SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, PM0_state);
	SM750_WREG32(SM750_SYS_PWR_MODE_1_CLKC, PM1_state);
	SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, PMC_state);
	SM750_WREG8(SM750_I2C_CONTROL, I2C_ctrl); // Disable controller, alternatively set to 0x0
	return ret;
}



status_t sm750_read_edid(uint8* buffer)
{
	// READ EDID with software simulated I2C via GPIO
	// Segue l'implementazione del driver Linux staging
	vuint32 *regs = gInfo->regs;
	status_t ret = B_OK;
	uint32 PM0_state = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC);
	uint32 PM1_state = SM750_REG32(SM750_SYS_PWR_MODE_1_CLKC);
	uint32 PMC_state = SM750_REG32(SM750_SYS_PWR_MODE_CTRL);
	uint32 GPIO_ctrl = SM750_REG32(SM750_SYS_GPIO_CTRL);
	uint32 GPIO_dir = SM750_REG32(SM750_GPIO_DIRECTION);
	uint32 PPLL_ctrl = SM750_REG32(SM750_DISP_PANEL_PLL);
	uint32 CPLL_ctrl = SM750_REG32(SM750_DISP_CRT_PLL);
	
	// Definisci i pin GPIO (Pin 30=SCL, 31=SDA)
	uint32 scl = PIN_SCL;
	uint32 sda = PIN_SDA;
	
	// Abilita GPIO MUX disattivando I2C hardware sui pin 30 e 31
	uint32 gpio_mux = SM750_REG32(SM750_SYS_GPIO_CTRL);
	gpio_mux &= ~(1 << scl);
	gpio_mux &= ~(1 << sda);
	SM750_WREG32(SM750_SYS_GPIO_CTRL, gpio_mux);
	
	// Abilita il clock GPIO
	SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, 0x00005046);
	SM750_WREG32(SM750_SYS_PWR_MODE_1_CLKC, 0x00005046);
	SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, 0x00000008);
	snooze(100);
	
	// Pulisci le linee I2C con 9 cicli di stop
	for (int i = 0; i < 9; i++)
		i2c_stop(scl, sda);
	
	snooze(1000);

	debug_printf("SM750_ACC: Inizio Bit-Banging EDID su SCL:%d SDA:%d\n", scl, sda);

	// --- FASE 1: SET OFFSET 0 ---
	i2c_start(scl, sda);
	
	// Indirizzo 0xA0 (50h << 1 + Write)
	if (!i2c_write_byte(scl, sda, 0xA0)) {
		i2c_stop(scl, sda);
		debug_printf("SM750_ACC: ERROR - Nessun ACK all'indirizzo 0xA0 (Monitor assente?)\n");
		ret = B_DEVICE_NOT_FOUND;
		goto finalize;
	}

	// Offset 0x00 (vogliamo leggere dal primo byte dell'EDID)
	if (!i2c_write_byte(scl, sda, 0x00)) {
		i2c_stop(scl, sda);
		debug_printf("SM750_ACC: ERROR - No ACK durante il settaggio dell'offset\n");
		ret = B_ERROR;
		goto finalize;
	}

	// --- FASE 2: LETTURA DATI ---
	i2c_start(scl, sda);
	
	// Indirizzo 0xA1 (50h << 1 + Read)
	if (!i2c_write_byte(scl, sda, 0xA1)) {
		i2c_stop(scl, sda);
		debug_printf("SM750_ACC: ERROR - No ACK all'indirizzo di lettura 0xA1\n");
		ret = B_ERROR;
		goto finalize;
	}

	// Leggiamo 128 byte
	for (int i = 0; i < 128; i++) {
		// L'ultimo byte invia NACK (false) per terminare la lettura
		buffer[i] = i2c_read_byte(scl, sda, (i < 127));
	}

	i2c_stop(scl, sda);

	// --- FASE 3: VERIFICA HEADER ---
	// L'header EDID è standard: 00 FF FF FF FF FF FF 00
	if (buffer[0] == 0x00 && buffer[1] == 0xFF && buffer[2] == 0xFF) {
		debug_printf("SM750_ACC: SUCCESSO! EDID letto via Bit-Banging.\n");
		debug_printf("SM750_ACC: Vendor ID: %02x%02x\n", buffer[8], buffer[9]);
		ret = B_OK;
		goto finalize;
	}

	debug_printf("SM750_ACC: Header EDID non valido: %02x %02x %02x... Prova a ricontrollare i collegamenti.\n", 
		  buffer[0], buffer[1], buffer[2]);

	ret = B_ERROR;
finalize:
	SM750_WREG32(SM750_DISP_PANEL_PLL, PPLL_ctrl);
	SM750_WREG32(SM750_DISP_CRT_PLL, CPLL_ctrl);
	SM750_WREG32(SM750_SYS_GPIO_CTRL, GPIO_ctrl);
	SM750_WREG32(SM750_GPIO_DIRECTION, GPIO_dir);
	SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, PM0_state);
	SM750_WREG32(SM750_SYS_PWR_MODE_1_CLKC, PM1_state);
	SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, PMC_state);
	return ret;
}
