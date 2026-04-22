const crypto = require('crypto');

// --- CONFIGURAZIONE (Deve essere identica al test C++) ---
// Test con tutti zeri come nel tuo codice C++
const key = Buffer.alloc(16, 0); 
const iv = Buffer.alloc(12, 0);
const plaintext = "A"; // Cambia in "" per test stringa vuota

function validate() {
    console.log("--- BCrypto GCM External Validator ---");
    console.log("Input:", plaintext === "" ? "(vuoto)" : plaintext);
    console.log("Key:", key.toString('hex'));
    console.log("IV:", iv.toString('hex'));
    console.log("---------------------------------------");

    // 1. Calcolo Standard NIST via OpenSSL
    const cipher = crypto.createCipheriv('aes-128-gcm', key, iv);
    
    // Node.js/OpenSSL di default usa un tag di 16 byte
    let ciphertext = cipher.update(plaintext, 'utf8');
    cipher.final(); // GCM final non aggiunge dati ma calcola il tag
    
    const expectedCiphertext = ciphertext.toString('hex');
    const expectedTag = cipher.getAuthTag().toString('hex');

    console.log("RISULTATI ATTESI (Standard):");
    console.log("Ciphertext (hex): " + expectedCiphertext);
    console.log("Tag (hex):        " + expectedTag);
    console.log("---------------------------------------");
    
    // 2. Debug per aiutarti a capire Haiku
    // In GCM, il primo blocco di dati viene XORato con E(K, J0 + 1)
    // Calcoliamo il keystream del primo blocco per confrontarlo con i tuoi dprintf
    const ctr0 = Buffer.alloc(16, 0);
    iv.copy(ctr0);
    ctr0[15] = 2; // J0 + 1 (GCM inizia a cifrare dal contatore 2)
    
    const ecb = crypto.createCipheriv('aes-128-ecb', key, null);
    ecb.setAutoPadding(false);
    const keystream = ecb.update(ctr0).toString('hex');
    
    console.log("DEBUG INTERNO:");
    console.log("Keystream atteso (J0+1): " + keystream);
}

validate();
