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
    // pin deve essere tra 0 e 31
    return (SM750_REG32(SM750_GPIO_DATA) & (1 << pin)) != 0;
}

static void 
set_gpio_pin(uint32 pin, bool high) 
{
    vuint32 *regs = gInfo->regs;
    uint32 val = SM750_REG32(SM750_GPIO_DATA); 
    if (high) val |= (1U << pin);
    else val &= ~(1U << pin);
    SM750_WREG32(SM750_GPIO_DATA, val);
    // Un piccolissimo delay hardware per far propagare il segnale nel chip
    (void)SM750_REG32(SM750_GPIO_DATA); 
}

// Questa è la chiave per i monitor lenti
static void 
secure_set_scl_high(uint32 scl) 
{
    set_gpio_pin(scl, true);
    // Clock Stretching: aspetta che il pin sia effettivamente alto
    int timeout = 1000;
    while (!get_gpio_pin(scl) && --timeout > 0) snooze(1);
}

static void 
set_sda_direction(uint32 sda, bool output) 
{
	vuint32 *regs = gInfo->regs;
    uint32 dir = SM750_REG32(SM750_GPIO_DIRECTION);
    if (output) dir |= (1 << sda);
    else dir &= ~(1 << sda);
    SM750_WREG32(SM750_GPIO_DIRECTION, dir);
}
static void i2c_start(uint32 scl, uint32 sda) {
    set_sda_direction(sda, true); // Sempre SDA in uscita per lo Start
    set_gpio_pin(sda, true);  
    set_gpio_pin(scl, true);
    snooze(10);
    set_gpio_pin(sda, false); // SDA cade mentre SCL è alto: START
    snooze(10);
    set_gpio_pin(scl, false);
    snooze(10);
}

static void i2c_stop(uint32 scl, uint32 sda) {
    set_sda_direction(sda, true);
    set_gpio_pin(sda, false); 
    snooze(10);
    set_gpio_pin(scl, true);  
    snooze(10);
    set_gpio_pin(sda, true);  // SDA sale mentre SCL è alto: STOP
    snooze(10);
}

static bool 
i2c_write_byte(uint32 scl, uint32 sda, uint8 byte) 
{
    set_sda_direction(sda, true);
    for (int i = 7; i >= 0; i--) {
        set_gpio_pin(sda, (byte >> i) & 1);
        snooze(10);
        secure_set_scl_high(scl); // Usiamo la versione sicura
        snooze(10);
        set_gpio_pin(scl, false);
        snooze(10);
    }

    // Leggi ACK
    set_sda_direction(sda, false);
    snooze(10);
    secure_set_scl_high(scl);
    snooze(10);
    bool ack = !get_gpio_pin(sda); // Il monitor tira SDA a zero
    set_gpio_pin(scl, false);
    snooze(10);
    return ack;
}

static uint8 
i2c_read_byte(uint32 scl, uint32 sda, bool send_ack) 
{
    uint8 byte = 0;
    set_sda_direction(sda, false); 
    for (int i = 7; i >= 0; i--) {
        snooze(10);
        secure_set_scl_high(scl);
        snooze(10);
        if (get_gpio_pin(sda)) byte |= (1 << i);
        set_gpio_pin(scl, false);
        snooze(10);
    }

    // Invia ACK/NACK
    set_sda_direction(sda, true);
    set_gpio_pin(sda, !send_ack); 
    snooze(10);
    secure_set_scl_high(scl);
    snooze(10);
    set_gpio_pin(scl, false);
    set_gpio_pin(sda, true); // Rilascia SDA
    snooze(10);
    
    return byte;
}

status_t 
sm750_read_edid(uint8* buffer) 
{
    // 1. Definiamo i pin in base a quanto visto (Pin 30=SCL, 31=SDA)
    // Se is_panel è vero, potrebbero essere diversi, ma per ora restiamo sui CRT standard
    uint32 scl = 30;
    uint32 sda = 31;

    debug_printf("SM750_ACC: Inizio Bit-Banging EDID su SCL:%d SDA:%d\n", scl, sda);

    // Inizializziamo lo stato dei GPIO (già fatto in init_chip, ma meglio essere sicuri)
    set_sda_direction(sda, true);
    set_gpio_pin(scl, true);
    set_gpio_pin(sda, true);
    snooze(1000); // Un piccolo respiro prima di iniziare

    // --- FASE 1: SET OFFSET 0 ---
    i2c_start(scl, sda);
    
    // Indirizzo 0xA0 (50h << 1 + Write)
    if (!i2c_write_byte(scl, sda, 0xA0)) {
        i2c_stop(scl, sda);
        debug_printf("SM750_ACC: ERROR - Nessun ACK all'indirizzo 0xA0 (Monitor assente?)\n");
        return B_DEVICE_NOT_FOUND;
    }

    // Offset 0x00 (vogliamo leggere dal primo byte dell'EDID)
    if (!i2c_write_byte(scl, sda, 0x00)) {
        i2c_stop(scl, sda);
        debug_printf("SM750_ACC: ERROR - No ACK durante il settaggio dell'offset\n");
        return B_ERROR;
    }
    
    // Molti dispositivi DDC preferiscono uno Stop/Start invece di un Repeated Start
    i2c_stop(scl, sda);
    snooze(100);

    // --- FASE 2: LETTURA DATI ---
    i2c_start(scl, sda);
    
    // Indirizzo 0xA1 (50h << 1 + Read)
    if (!i2c_write_byte(scl, sda, 0xA1)) {
        i2c_stop(scl, sda);
        debug_printf("SM750_ACC: ERROR - No ACK all'indirizzo di lettura 0xA1\n");
        return B_ERROR;
    }

    // Leggiamo 128 byte
    for (int i = 0; i < 128; i++) {
        // L'ultimo byte deve inviare un NACK (false) per dire al monitor di fermarsi
        buffer[i] = i2c_read_byte(scl, sda, (i < 127));
    }

    i2c_stop(scl, sda);

    // --- FASE 3: VERIFICA HEADER ---
    // L'header EDID è standard: 00 FF FF FF FF FF FF 00
    if (buffer[0] == 0x00 && buffer[1] == 0xFF && buffer[2] == 0xFF) {
        debug_printf("SM750_ACC: SUCCESSO! EDID letto via Bit-Banging.\n");
        debug_printf("SM750_ACC: Vendor ID: %02x%02x\n", buffer[8], buffer[9]);
        return B_OK;
    }

    debug_printf("SM750_ACC: Header EDID non valido: %02x %02x %02x... Prova a ricontrollare i collegamenti.\n", 
                  buffer[0], buffer[1], buffer[2]);

    return B_ERROR;
}
/*
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
    }*/
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
/* ripristinare se i2c funziona
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
