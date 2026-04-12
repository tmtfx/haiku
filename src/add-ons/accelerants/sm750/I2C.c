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
}

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
}

status_t 
sm750_read_edid(bool is_panel, uint8* buffer) 
{
	vuint32 *regs = gInfo->regs;
	
    uint32 scl, sda;
    if (is_panel) { scl = 28; sda = 29; }
    else { scl = 30; sda = 31; }

    // 1. SBLOCCO GPIO (Fondamentale!)
    // Forza i pin a funzionare come I/O standard e non come funzioni speciali
    uint32 gpio_ctrl = SM750_REG32(0x1000C);
    gpio_ctrl &= ~((1 << scl) | (1 << sda));
    SM750_WREG32(0x1000C, gpio_ctrl);

    // 2. TENTATIVI DI BUSSARE (Retry loop)
    bool monitor_ready = false;
    for (int retry = 0; retry < 3; retry++) {
        i2c_start(scl, sda);
        if (i2c_write_byte(scl, sda, 0xA0)) {
            monitor_ready = true;
            break;
        }
        i2c_stop(scl, sda);
        snooze(10000); // Aspetta 10ms prima di riprovare
        debug_printf("SM750: Monitor non risponde a 0xA0 (tentativo %d/3)...\n", retry + 1);
    }

    if (!monitor_ready) {
        debug_printf("SM750: EDID fallito - monitor assente o bus I2C bloccato.\n");
        return B_ERROR;
    }
    
    // 3. SE RISPONDE, CONTINUA
    i2c_write_byte(scl, sda, 0x00); // Offset di memoria
    i2c_start(scl, sda);            // Restart
    i2c_write_byte(scl, sda, 0xA1); // Modalità lettura
    
    for (int i = 0; i < 128; i++) {
        buffer[i] = i2c_read_byte(scl, sda, i < 127);
    }
    
    i2c_stop(scl, sda);
    return B_OK;
}
