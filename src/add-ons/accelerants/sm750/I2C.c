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
/* per gpio ma noi abbiamo i2c integrato
static void 
set_gpio_pin(uint32 pin, bool high) 
{
	vuint32 *regs = gInfo->regs;
    // pin deve essere tra 0 e 31
    uint32 val = SM750_REG32(SM750_GPIO_DATA_LOW); 
    if (high) val |= (1 << pin);
    else val &= ~(1 << pin);
    SM750_WREG32(SM750_GPIO_DATA_LOW, val);
}

static bool 
get_gpio_pin(uint32 pin) 
{
	vuint32 *regs = gInfo->regs;
    // pin deve essere tra 0 e 31
    return (SM750_REG32(SM750_GPIO_DATA_LOW) & (1 << pin)) != 0;
}

static void 
set_sda_direction(uint32 sda, bool output) 
{
	vuint32 *regs = gInfo->regs;
    uint32 dir = SM750_REG32(SM750_GPIO_DIR_LOW);
    if (output) dir |= (1 << sda);
    else dir &= ~(1 << sda);
    SM750_WREG32(SM750_GPIO_DIR_LOW, dir);
}*/
/* bit-banging... ma noi abbiamo un controller i2c integrato cribbio!
static void i2c_start(uint32 scl, uint32 sda) {
    set_gpio_pin(sda, true);  set_gpio_pin(scl, true);
    snooze(5);
    set_gpio_pin(sda, false); snooze(5);
    set_gpio_pin(scl, false);
}

static void i2c_stop(uint32 scl, uint32 sda) {
    set_gpio_pin(sda, false); snooze(5);
    set_gpio_pin(scl, true);  snooze(5);
    set_gpio_pin(sda, true);  snooze(5);
}

static bool 
i2c_write_byte(uint32 scl, uint32 sda, uint8 byte) 
{
    // Assicuriamoci che SDA sia in uscita
    set_sda_direction(sda, true);

    for (int i = 7; i >= 0; i--) {
        set_gpio_pin(sda, (byte >> i) & 1);
        snooze(20);
        set_gpio_pin(scl, true);
        snooze(20);
        set_gpio_pin(scl, false);
        snooze(20);
    }

    // Leggi ACK: rilascia SDA (diventa input)
    set_sda_direction(sda, false);
    set_gpio_pin(scl, true); 
    snooze(20);
    
    // Se il monitor abbassa SDA, l'ACK è valido (logica negativa)
    bool ack = !get_gpio_pin(sda);
    
    set_gpio_pin(scl, false);
    snooze(20);
    return ack;
}

static uint8 
i2c_read_byte(uint32 scl, uint32 sda, bool send_ack) 
{
    uint8 byte = 0;
    
    // SDA in ingresso per leggere dal monitor
    set_sda_direction(sda, false); 
    
    for (int i = 7; i >= 0; i--) {
        snooze(20);
        set_gpio_pin(scl, true); 
        snooze(20);
        if (get_gpio_pin(sda)) 
            byte |= (1 << i);
        set_gpio_pin(scl, false);
    }

    // Invia ACK/NACK: SDA torna in uscita
    set_sda_direction(sda, true);
    set_gpio_pin(sda, !send_ack); // ACK = 0, NACK = 1
    snooze(20);
    set_gpio_pin(scl, true);  
    snooze(20);
    set_gpio_pin(scl, false);
    set_gpio_pin(sda, true); // Rilascia SDA
    
    return byte;
}*/
/*

status_t 
sm750_read_edid(uint8* buffer) 
{
	vuint32 *regs = gInfo->regs;
    // 1. Reset e Pulizia del controller I2C
    SM750_WREG8(SM750_I2C_CONTROL, 0x00); // Disable controller
    snooze(10);
    SM750_WREG8(SM750_I2C_RESET, 0x04); // Reset (Bit 2 = 1)
    snooze(10);
    SM750_WREG8(SM750_I2C_RESET, 0x00); // Ritorna in modalità normale
    snooze(1000); 
    SM750_WREG8(SM750_I2C_CONTROL, 0x01); // Enable controller
    
    uint8 info = SM750_REG8(SM750_I2C_STATUS);
    debug_printf("Stato dell'i2c all'inizio dopo il reset: 0x%02x\n",info);

    // --- FASE 1: SET OFFSET (Indirizziamo il byte 0 dell'EDID) ---
    // Aspettiamo che il bus non sia occupato
    int timeout = 1000;
    while ((SM750_REG8(SM750_I2C_STATUS) & 0x01) && --timeout > 0) snooze(10);
    if ((SM750_REG8(SM750_I2C_STATUS) & 0x01) {
        debug_printf("SM750_ACC: ERROR - Timeout scaduto ma il bus è ancora bloccato.\n");
        //return B_ BUSY
    }

    SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA0); // Slave Addr 0x50 + Write (Bit 0 = 0)
    SM750_WREG8(0x10044, 0x00); // Scriviamo 0x00 nel primo registro dati (offset EDID)
    SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x00); // Byte Count: 0 (significa 1 byte)
    SM750_WREG8(SM750_I2C_CONTROL, 0x05); // Control: Enable=1, Start=1 (0x05)

    // Attesa completamento (Bit 3: Comp)
    timeout = 1000;
    while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);
    if (!(SM750_REG8(SM750_I2C_STATUS) & 0x08)) {
        debug_printf("SM750_ACC: ERROR - Timeout scaduto senza bit COMP! Bus bloccato.\n");
        // Qui dovresti resettare o uscire, non continuare!
        //return B_TIMED_OUT;
    }

    // CONTROLLO CRITICO: Abbiamo ricevuto l'ACK dal monitor?
    uint8 status = SM750_REG8(SM750_I2C_STATUS);
    debug_printf("Stato dell'i2c per vedere se l'ACK è arrivato: 0x%02x\n",status);
    if (!(status & 0x02)) { // Bit 1: Ack
        debug_printf("SM750_ACC: Monitor non ha risposto (No ACK) al Set Offset. Status: 0x%02x\n", status);
        return B_DEVICE_NOT_FOUND;
    }

    // --- FASE 2: LETTURA MASSIVA (128 byte in blocchi da 16) ---
    for (int i = 0; i < 8; i++) {
        // Aspetta che il bus sia libero
        timeout = 1000;
        while ((SM750_REG8(SM750_I2C_STATUS) & 0x01) && --timeout > 0) snooze(10);

        SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA1); // Slave Addr 0x50 + Read (Bit 0 = 1)
        SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x0F); // Byte Count: 15 (significa 16 byte)
        
        // Invece di 0x05 (Start + Enable), prova 0x45 (Repeat Start + Start + Enable)
        // Bit 6 = 1, Bit 2 = 1, Bit 0 = 1 => 0100 0101 = 0x45
        SM750_WREG8(SM750_I2C_CONTROL, 0x45);

        // Aspetta il completamento del blocco da 16 byte
        timeout = 1000;
        while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);

        // Verifica ACK anche qui
        status = SM750_REG8(SM750_I2C_STATUS);
        if (!(status & 0x02)) {
            debug_printf("SM750_ACC: Perso ACK durante lettura blocco %d. Status: 0x%02x\n", i, status);
            return B_ERROR;
        }

        // Copia dei dati dal buffer MMIO (0x10044 a 0x10053)
        for (int j = 0; j < 16; j++) {
            buffer[(i * 16) + j] = SM750_REG8(0x10044 + j);
        }
    }

    // --- VERIFICA FINALE ---
    // L'EDID standard inizia sempre con 00 FF FF FF FF FF FF 00
    if (buffer[0] == 0x00 && buffer[1] == 0xFF && buffer[2] == 0xFF) {
        debug_printf("SM750_ACC: EDID letto con successo! Header valido.\n");
        return B_OK;
    }
    
    debug_printf("SM750_ACC: EDID Raw Header errato: %02x %02x %02x...\n", buffer[0], buffer[1], buffer[2]);
    //return B_ERROR;
    return B_OK;
}*/
static void clean_i2c_bus_error() {
	vuint32 *regs = gInfo->regs;
    uint8 status = SM750_REG8(SM750_I2C_STATUS);
    if (status & 0x04) { // Se il Bit 2 (Err) è 1
        debug_printf("SM750_ACC: Rilevato Bus Error (Bit 2), reset in corso...\n");
        SM750_WREG8(SM750_I2C_RESET, 0x00); // Scriviamo 0 per fare Clear
        snooze(10);
    }
}

status_t 
sm750_read_edid(uint8* buffer) 
{
	vuint32 *regs = gInfo->regs;
    // 1. Reset e Pulizia del controller I2C
    SM750_WREG8(SM750_I2C_CONTROL, 0x00); // Disable controller
    snooze(10);
    clean_i2c_bus_error();
    snooze(1000); 
    SM750_WREG8(SM750_I2C_CONTROL, 0x01); // Enable controller
    
    uint8 info = SM750_REG8(SM750_I2C_STATUS);
    debug_printf("Stato dell'i2c all'inizio dopo il reset: 0x%02x\n",info);

    // --- FASE 1: SET OFFSET (Indirizziamo il byte 0 dell'EDID) ---
    // Aspettiamo che il bus non sia occupato
    int timeout = 1000;
    while ((SM750_REG8(SM750_I2C_STATUS) & 0x01) && --timeout > 0) snooze(10);
    if ((SM750_REG8(SM750_I2C_STATUS) & 0x01)) {
        debug_printf("SM750_ACC: ERROR - Timeout scaduto ma il bus è ancora bloccato.\n");
        //return B_ BUSY
    }

    SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA0); // Slave Addr 0x50 + Write (Bit 0 = 0)
    SM750_WREG8(0x10044, 0x00); // Scriviamo 0x00 nel primo registro dati (offset EDID)
    SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x00); // Byte Count: 0 (significa 1 byte)
    SM750_WREG8(SM750_I2C_CONTROL, 0x05); // Control: Enable=1, Start=1 (0x05)

    // Attesa completamento (Bit 3: Comp)
    timeout = 1000;
    while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);
    if (!(SM750_REG8(SM750_I2C_STATUS) & 0x08)) {
        debug_printf("SM750_ACC: ERROR - Timeout scaduto senza bit COMP! Bus bloccato.\n");
        // Qui dovresti resettare o uscire, non continuare!
        //return B_TIMED_OUT;
    }

    // CONTROLLO CRITICO: Abbiamo ricevuto l'ACK dal monitor?
    uint8 status = SM750_REG8(SM750_I2C_STATUS);
    debug_printf("Stato dell'i2c per vedere se l'ACK è arrivato: 0x%02x\n",status);
    if (!(status & 0x02)) { // Bit 1: Ack
        debug_printf("SM750_ACC: Monitor non ha risposto (No ACK) al Set Offset. Status: 0x%02x\n", status);
        return B_DEVICE_NOT_FOUND;
    }
    
    clean_i2c_bus_error();
    // --- FASE 2: LETTURA MASSIVA (128 byte in blocchi da 16) ---
    for (int i = 0; i < 8; i++) {
        SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x0F); // 16 byte
        SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA1); // Read
        SM750_WREG8(SM750_I2C_CONTROL, 0x05);    // Start

        // Attesa COMP
        timeout = 1000;
        while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);

        // UN PICCOLO RESPIRO (fondamentale per alcuni bridge PCI)
        snooze(100);

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
    
        // STOP
        SM750_WREG8(SM750_I2C_CONTROL, 0x11);
        snooze(50);
    }
    /*for (int i = 0; i < 8; i++) {
        // 1. Pulizia stato precedente
        SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x0F); 
        SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA1); 
        SM750_WREG8(SM750_I2C_CONTROL, 0x05); // START
        
        uint8 pre_read_status = SM750_REG8(SM750_I2C_STATUS);
        debug_printf("SM750_ACC: Blocco %d, Stato Pre-Lettura: 0x%02x\n", i, pre_read_status);

        // 2. Configurazione "inversa" (Byte Count prima)
        SM750_WREG8(SM750_I2C_BYTE_COUNT, 0x0F); 
        SM750_WREG8(SM750_I2C_SLAVE_ADDR, 0xA1); 
    
        // 3. Start (Proviamo 0x05 standard, se fallisce useremo 0x45)
        // SM750_WREG8(SM750_I2C_CONTROL, 0x05);

        // 4. Attesa COMP con debug reale
        timeout = 1000;
        while (!(SM750_REG8(SM750_I2C_STATUS) & 0x08) && --timeout > 0) snooze(10);
    
        uint8 post_read_status = SM750_REG8(SM750_I2C_STATUS);
        debug_printf("SM750_ACC: Blocco %d, Stato Post-Lettura: 0x%02x\n", i, post_read_status);
*/
        // 5. Lettura dei dati one-shot
        /*
        for (int j = 0; j < 16; j++) {
            buffer[(i * 16) + j] = SM750_REG8(0x10044 + j);
        }
        */
        // 5. Lettura dei dati con "doppio colpo"
        /*
        for (int j = 0; j < 16; j++) {
            // Lettura a vuoto: forza il controller a presentare il dato
            (void)SM750_REG8(0x10044 + j); 
            
            // Lettura reale: salviamo questo valore
            uint8 val = SM750_REG8(0x10044 + j);
            buffer[(i * 16) + j] = val;
            
            // Debug per il primo blocco: se leggiamo qualcosa di diverso da 00, lo vogliamo sapere!
            if (i == 0 && val != 0) {
                debug_printf("SM750_ACC: Miracolo! Byte %d = 0x%02x\n", j, val);
            }
        }*/
        // 5. Lettura dati a 32-bit
        /*
        for (int j = 0; j < 4; j++) {
            uint32 val32 = SM750_REG32(0x10044 + (j * 4));
        
            if (val32 != 0) {
                debug_printf("SM750_ACC: BINGO! Dati trovati: 0x%08x\n", val32);
            }

            // Distribuiamo i byte nel buffer
            buffer[(i * 16) + (j * 4) + 0] = (uint8)(val32 & 0xFF);
            buffer[(i * 16) + (j * 4) + 1] = (uint8)((val32 >> 8) & 0xFF);
            buffer[(i * 16) + (j * 4) + 2] = (uint8)((val32 >> 16) & 0xFF);
            buffer[(i * 16) + (j * 4) + 3] = (uint8)((val32 >> 24) & 0xFF);
        }*/
        // 5. svuotamento FIFO
        /*
        for (int j = 0; j < 16; j++) {
            // Aspettiamo che il bit 0 (Busy/Empty) si calmi
            int retry = 100;
            while ((SM750_REG8(SM750_I2C_STATUS) & 0x01) && --retry > 0) snooze(1);

            uint8 val = SM750_REG8(0x10044); // LEGGIAMO SEMPRE 0x10044
            buffer[(i * 16) + j] = val;

            if (val != 0) {
                 debug_printf("SM750_ACC: DATO TROVATO a 0x10044! Byte %d = 0x%02x\n", (i*16)+j, val);
            }
        }*/
        // Diciamo al controller: "Ho finito il blocco, chiudi la comunicazione"
/*
        SM750_WREG8(SM750_I2C_CONTROL, 0x11); // 0x10 (STOP) + 0x01 (ENABLE)
        timeout = 100;
        while ((SM750_REG8(SM750_I2C_STATUS) & 0x01) && --timeout > 0) snooze(1);
    }*/

    // --- VERIFICA FINALE ---
    // L'EDID standard inizia sempre con 00 FF FF FF FF FF FF 00
    if (buffer[0] == 0x00 && buffer[1] == 0xFF && buffer[2] == 0xFF) {
        debug_printf("SM750_ACC: EDID letto con successo! Header valido.\n");
        return B_OK;
    }
    
    debug_printf("SM750_ACC: EDID Raw Header errato: %02x %02x %02x...\n", buffer[0], buffer[1], buffer[2]);
    //return B_ERROR;
    return B_OK;
}
