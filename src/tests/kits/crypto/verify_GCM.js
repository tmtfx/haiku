const crypto = require('crypto');

const key = Buffer.from('0102030405060708091011121314151617181920212223242526272829303132', 'hex');
const iv = Buffer.from('aabbccddeeff001122334455', 'hex');
const tag = Buffer.from('b53178d0085d03b12f92d9f3ef43f96b', 'hex');
const ciphertext = require('fs').readFileSync('test_data.enc');

try {
    const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv);
    decipher.setAuthTag(tag);
    let decrypted = decipher.update(ciphertext, null, 'utf8');
    decrypted += decipher.final('utf8');
    console.log("SUCCESS! Contenuto:", decrypted);
} catch (e) {
    console.error("FALLITO:", e.message);
}
