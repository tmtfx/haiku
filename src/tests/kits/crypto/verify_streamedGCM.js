const crypto = require('crypto');

const key = Buffer.from('0102030405060708090a0b0c0d0e0f10', 'hex');
const iv = Buffer.from('afbeadde0102030405060708', 'hex');
const tag = Buffer.from('2aef95dedfc475a9bafc5946bbbec95e', 'hex');
const ciphertext = require('fs').readFileSync('test_data.enc');

try {
    const decipher = crypto.createDecipheriv('aes-128-gcm', key, iv);
    decipher.setAuthTag(tag);
    let decrypted = decipher.update(ciphertext, null, 'utf8');
    decrypted += decipher.final('utf8');
    console.log("SUCCESS! Contenuto:", decrypted);
} catch (e) {
    console.error("FALLITO:", e.message);
}
