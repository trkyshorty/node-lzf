'use strict';

const { test } = require('node:test');
const assert = require('node:assert');
const crypto = require('node:crypto');

const lzf = require('../index.js');

/* Deterministic pseudo-random buffer so failures are reproducible. */
function prandomBytes(size, seed) {
    const out = Buffer.allocUnsafe(size);
    let s = seed >>> 0;
    for (let i = 0; i < size; i++) {
        s = (s * 1103515245 + 12345) & 0x7fffffff;
        out[i] = s & 0xff;
    }
    return out;
}

/* Compressible but non-trivial data: hex text with repeated blocks. */
function compressibleBytes(size, seed) {
    const block = prandomBytes(64, seed).toString('hex'); // 128 chars
    return Buffer.from(block.repeat(Math.ceil(size / block.length)).slice(0, size));
}

const lorem =
    'Lorem ipsum dolor sit amet, consectetur adipiscing elit.' +
    'Curabitur volutpat, nulla nec egestas semper,' +
    'ante dui tristique nibh, quis feugiat.';

test('roundtrip: lorem text', () => {
    // 150 bytes of mostly-unique text: LZF may not shrink it, but the
    // roundtrip contract must hold regardless.
    const data = Buffer.from(lorem);
    const compressed = lzf.compress(data);
    assert.strictEqual(lzf.decompress(compressed, data.length).toString(), lorem);
});

test('roundtrip: expectedLength larger than actual output is fine', () => {
    const data = Buffer.from(lorem);
    const compressed = lzf.compress(data);
    const out = lzf.decompress(compressed, data.length + 1000);
    assert.ok(out.equals(data));
});

test('roundtrip: single byte', () => {
    const data = Buffer.from([0x42]);
    assert.ok(lzf.decompress(lzf.compress(data), 1).equals(data));
});

test('roundtrip: incompressible random data (multiple sizes)', () => {
    for (const size of [1, 2, 33, 1024, 65536, 1024 * 1024]) {
        const data = crypto.randomBytes(size);
        const compressed = lzf.compress(data);
        assert.ok(lzf.decompress(compressed, size).equals(data), `size=${size}`);
    }
});

test('roundtrip: highly compressible data', () => {
    const zeros = Buffer.alloc(1024 * 1024);
    const compressed = lzf.compress(zeros);
    assert.ok(compressed.length < zeros.length / 10, 'zeros should compress >10x');
    assert.ok(lzf.decompress(compressed, zeros.length).equals(zeros));
});

test('roundtrip: 5MB payload', () => {
    const data = compressibleBytes(5 * 1024 * 1024, 777);
    const compressed = lzf.compress(data);
    assert.ok(lzf.decompress(compressed, data.length).equals(data));
});

test('fuzz: 300 deterministic buffers roundtrip', () => {
    for (let i = 0; i < 300; i++) {
        const size = 1 + ((i * 2654435761) % 16384);
        const data = i % 2 === 0 ? prandomBytes(size, i + 1) : compressibleBytes(size, i + 1);
        const compressed = lzf.compress(data);
        const out = lzf.decompress(compressed, size);
        assert.ok(out.equals(data), `fuzz i=${i} size=${size}`);
    }
});

test('compress: invalid inputs throw TypeError', () => {
    assert.throws(() => lzf.compress(), TypeError);
    assert.throws(() => lzf.compress('not a buffer'), TypeError);
    assert.throws(() => lzf.compress(123), TypeError);
    assert.throws(() => lzf.compress(Buffer.alloc(0)), TypeError);
});

test('decompress: invalid input buffer throws TypeError', () => {
    assert.throws(() => lzf.decompress(), TypeError);
    assert.throws(() => lzf.decompress('not a buffer', 10), TypeError);
    assert.throws(() => lzf.decompress(Buffer.alloc(0), 10), TypeError);
});

test('decompress: expectedLength is required and validated', () => {
    const compressed = lzf.compress(Buffer.from(lorem));
    assert.throws(() => lzf.decompress(compressed), TypeError); // missing
    assert.throws(() => lzf.decompress(compressed, 'x'), TypeError); // wrong type
    assert.throws(() => lzf.decompress(compressed, 0), RangeError);
    assert.throws(() => lzf.decompress(compressed, -1), RangeError);
    assert.throws(() => lzf.decompress(compressed, 1.5), RangeError);
    assert.throws(() => lzf.decompress(compressed, NaN), RangeError);
    assert.throws(() => lzf.decompress(compressed, 2 * 1024 * 1024 * 1024), RangeError); // > 1 GiB
});

test('decompress: too-small expectedLength throws with a specific message', () => {
    const data = Buffer.from(lorem);
    const compressed = lzf.compress(data);
    assert.throws(() => lzf.decompress(compressed, data.length - 1), /too small/);
});

test('decompress: corrupted input throws instead of crashing', () => {
    // back-reference before the start of output: ctrl 0xe0 needs 2 more bytes
    assert.throws(() => lzf.decompress(Buffer.from([0xe0, 0x00, 0x00]), 64), /corrupted/);
    // literal run claiming 32 bytes with no payload behind it
    assert.throws(() => lzf.decompress(Buffer.from([0x1f]), 64), /corrupted/);
    // truncated back-reference (control byte only)
    assert.throws(() => lzf.decompress(Buffer.from([0x20]), 64), /corrupted/);
    // truncated valid stream
    const compressed = lzf.compress(Buffer.from(lorem.repeat(10)));
    assert.throws(() => lzf.decompress(compressed.subarray(0, 5), lorem.length * 10));
});

test('decompress: garbage input never reads out of bounds', () => {
    // Corrupt-but-plausible streams must never crash: either throw or
    // produce a nonsensical result that stays within bounds. 200 tries.
    for (let i = 0; i < 200; i++) {
        const garbage = prandomBytes(1 + (i % 512), 9000 + i);
        try {
            const out = lzf.decompress(garbage, 4096);
            assert.ok(out.length <= 4096);
        } catch (e) {
            assert.ok(e instanceof Error);
        }
    }
});

/* NOTE: liblzf's hash table is deliberately left uninitialized (stack
 * garbage), so compressed BYTES are not guaranteed identical across calls
 * or threads — only the roundtrip contract holds. Tests therefore never
 * assert byte-equality between two compress() outputs. */

test('async: compressAsync/decompressAsync roundtrip', async () => {
    const data = compressibleBytes(2 * 1024 * 1024, 1234);
    const compressed = await lzf.compressAsync(data);
    const out = await lzf.decompressAsync(compressed, data.length);
    assert.ok(out.equals(data));
    // async output must also be decodable by the sync path and vice versa
    assert.ok(lzf.decompress(compressed, data.length).equals(data));
    assert.ok((await lzf.decompressAsync(lzf.compress(data), data.length)).equals(data));
});

test('async: rejects with the same errors as sync', async () => {
    await assert.rejects(lzf.compressAsync('nope'), TypeError);
    await assert.rejects(lzf.compressAsync(Buffer.alloc(0)), TypeError);
    await assert.rejects(lzf.decompressAsync(Buffer.from([1, 0]), 0), RangeError);
    await assert.rejects(lzf.decompressAsync(Buffer.from([0xe0, 0x00, 0x00]), 64), /corrupted/);
    const data = Buffer.from(lorem);
    await assert.rejects(lzf.decompressAsync(lzf.compress(data), data.length - 1), /too small/);
});

test('async: many concurrent operations', async () => {
    const jobs = [];
    for (let i = 0; i < 50; i++) {
        const data = i % 2 === 0 ? prandomBytes(20000 + i, i) : compressibleBytes(20000 + i, i);
        jobs.push(
            lzf
                .compressAsync(data)
                .then((c) => lzf.decompressAsync(c, data.length))
                .then((out) => assert.ok(out.equals(data), `concurrent i=${i}`))
        );
    }
    await Promise.all(jobs);
});

test('async: input buffer mutated after await does not corrupt result', async () => {
    // The worker holds the input alive by reference (no copy). We verify
    // that mutating the input AFTER awaiting does not affect the returned
    // buffer (the result owns its own memory).
    const data = compressibleBytes(65536, 55);
    const snapshot = Buffer.from(data);
    const compressed = await lzf.compressAsync(data);
    data.fill(0);
    const out = await lzf.decompressAsync(compressed, snapshot.length);
    assert.ok(out.equals(snapshot));
});

test('wire format: decodes a hand-crafted liblzf stream', () => {
    // Decompression is stateless, so a fixed vector is safe here:
    // [literal run of 3: "abc"] + [backref len=3 off=3] => "abcabc"
    const stream = Buffer.from([0x02, 0x61, 0x62, 0x63, 0x20, 0x02]);
    assert.strictEqual(lzf.decompress(stream, 6).toString(), 'abcabc');
    // and an RLE-style overlapping backref: "a" then copy 15 from off=1 => 16×a
    const rle = Buffer.from([0x00, 0x61, 0xe0, 0x06, 0x00]);
    assert.strictEqual(lzf.decompress(rle, 16).toString(), 'a'.repeat(16));
});
