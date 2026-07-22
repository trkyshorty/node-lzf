'use strict';

/* Benchmarks lzf against node's built-in zlib (deflateRaw level 1 and 6).
 * Zero external dependencies; datasets are deterministic so runs are
 * comparable across machines. Usage: npm run bench */

const zlib = require('node:zlib');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const lzf = require('../index.js');

/* Deterministic yet truly incompressible bytes: AES-CTR keystream. */
function prandomBytes(size, seed) {
    const key = crypto.createHash('md5').update(String(seed)).digest();
    const cipher = crypto.createCipheriv('aes-128-ctr', key, Buffer.alloc(16));
    return cipher.update(Buffer.alloc(size));
}

function jsonLike(size, seed) {
    const rows = [];
    let s = seed >>> 0;
    const rnd = () => ((s = (s * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
    while (rows.length * 90 < size) {
        rows.push({
            x: +(rnd() * 2000).toFixed(1),
            y: +(rnd() * 2000).toFixed(1),
            action: ['move', 'attack', 'loot', 'skill'][(rnd() * 4) | 0],
            delay: (rnd() * 500) | 0
        });
    }
    return Buffer.from(JSON.stringify(rows)).subarray(0, size);
}

function binaryPacket(size, seed) {
    const b = prandomBytes(size, seed);
    for (let i = 0; i < size; i++) if (i % 16 < 6) b[i] = i % 16; // repeated header fields
    return b;
}

const datasets = [
    ['text 100KB', fs.readFileSync(path.join(__dirname, 'data', '100000.txt'))],
    ['json 64KB', jsonLike(64 * 1024, 1)],
    ['json 2MB', jsonLike(2 * 1024 * 1024, 2)],
    ['binary 4KB', binaryPacket(4 * 1024, 3)],
    ['binary 64KB', binaryPacket(64 * 1024, 4)],
    ['random 1MB', prandomBytes(1024 * 1024, 5)],
    ['zeros 1MB', Buffer.alloc(1024 * 1024)]
];

const codecs = [
    {
        name: 'lzf',
        compress: (d) => lzf.compress(d),
        decompress: (c, len) => lzf.decompress(c, len)
    },
    {
        name: 'zlib-1',
        compress: (d) => zlib.deflateRawSync(d, { level: 1 }),
        decompress: (c) => zlib.inflateRawSync(c)
    },
    {
        name: 'zlib-6',
        compress: (d) => zlib.deflateRawSync(d, { level: 6 }),
        decompress: (c) => zlib.inflateRawSync(c)
    }
];

function bench(fn, minMs) {
    for (let i = 0; i < 10; i++) fn(); // warmup
    const start = process.hrtime.bigint();
    let iterations = 0;
    let elapsed = 0n;
    do {
        fn();
        iterations++;
        elapsed = process.hrtime.bigint() - start;
    } while (elapsed < BigInt(minMs) * 1000000n);
    return Number(elapsed) / 1e6 / iterations; // ms per op
}

function mbps(bytes, ms) {
    return (bytes / 1024 / 1024 / (ms / 1000)).toFixed(0);
}

const minMs = Number(process.env.BENCH_MS) || 300;

console.log(
    'dataset'.padEnd(13) +
        'codec'.padEnd(9) +
        'ratio'.padStart(7) +
        'comp ms'.padStart(10) +
        'comp MB/s'.padStart(11) +
        'decomp ms'.padStart(11) +
        'decomp MB/s'.padStart(13)
);

for (const [name, data] of datasets) {
    for (const codec of codecs) {
        const compressed = codec.compress(data);
        const ratio = (compressed.length / data.length) * 100;
        const compressMs = bench(() => codec.compress(data), minMs);
        const decompressMs = bench(() => codec.decompress(compressed, data.length), minMs);
        console.log(
            name.padEnd(13) +
                codec.name.padEnd(9) +
                (ratio.toFixed(0) + '%').padStart(7) +
                compressMs.toFixed(3).padStart(10) +
                mbps(data.length, compressMs).padStart(11) +
                decompressMs.toFixed(3).padStart(11) +
                mbps(data.length, decompressMs).padStart(13)
        );
    }
}
