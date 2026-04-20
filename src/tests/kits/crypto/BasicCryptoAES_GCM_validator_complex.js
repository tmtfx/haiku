const crypto = require('crypto');
const readline = require('readline');

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

// Configurazione fissa (deve corrispondere al tuo test C++)
const key = Buffer.from('01020304050607080910111213141516', 'hex');
const iv = Buffer.from('99887766554433221100aabb', 'hex');
const plaintext = "Haiku OS - GCM Test Message";

console.log("--- BCrypto GCM External Validator ---");
console.log("Configurazione: AES-128-GCM");
console.log("Messaggio atteso:", plaintext);
console.log("---------------------------------------");

rl.question('Incolla il Tag generato da Haiku: ', (inputTag) => {
    const cleanTag = inputTag.trim().toLowerCase();

    try {
        // 1. Calcoliamo cosa si aspetta Node.js (Standard NIST)
        const cipher = crypto.createCipheriv('aes-128-gcm', key, iv);
        let ciphertext = cipher.update(plaintext, 'utf8');
        ciphertext = Buffer.concat([ciphertext, cipher.final()]);
        const expectedTag = cipher.getAuthTag().toString('hex');

        console.log("\nAnalisi in corso...");
        console.log("Tag atteso (Node):   " + expectedTag);
        console.log("Tag fornito (Haiku): " + cleanTag);

        if (cleanTag === expectedTag) {
            console.log("\n✅ SUCCESSO! La logica One-Shot di Haiku è Standard-Compliant.");
        } else {
            console.log("\n❌ FALLITO! I Tag non corrispondono.");
            console.log("Suggerimento: Controlla l'endianness del blocco delle lunghezze (64-bit Big Endian).");
        }

    } catch (err) {
        console.error("\n[!] Errore durante la cifratura in Node:", err.message);
    }

    rl.close();
});
