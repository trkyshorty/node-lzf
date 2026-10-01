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

test('roundtrip: 1-3 byte inputs in exactly sized allocations', async () => {
    // Small Buffers live in Node's shared pool, so reading past them stays in
    // mapped memory. The async API copies the input into an allocation of the
    // exact size, which lets AddressSanitizer see an over-read: liblzf used to
    // read one byte past a 1-byte input.
    for (const size of [1, 2, 3]) {
        const data = Buffer.from('xyz'.slice(0, size));
        const compressed = await lzf.compressAsync(data);
        assert.ok(lzf.decompress(compressed, size).equals(data), `size=${size}`);
        assert.ok((await lzf.decompressAsync(compressed, size)).equals(data), `size=${size}`);
    }
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

/* assert.throws / assert.rejects matcher for an error class plus its code. */
function lzfError(type, code) {
    return (err) => err instanceof type && err.code === code;
}

const invalidType = lzfError(TypeError, 'ERR_INVALID_ARG_TYPE');
const outOfRange = lzfError(RangeError, 'ERR_OUT_OF_RANGE');
const corrupted = lzfError(Error, 'ERR_LZF_CORRUPTED_INPUT');
const tooSmall = lzfError(Error, 'ERR_LZF_OUTPUT_TOO_SMALL');

test('compress: invalid inputs throw TypeError', () => {
    assert.throws(() => lzf.compress(), invalidType);
    assert.throws(() => lzf.compress('not a buffer'), invalidType);
    assert.throws(() => lzf.compress(123), invalidType);
    assert.throws(() => lzf.compress([1, 2, 3]), invalidType);
});

test('decompress: invalid input buffer throws TypeError', () => {
    assert.throws(() => lzf.decompress(), invalidType);
    assert.throws(() => lzf.decompress('not a buffer', 10), invalidType);
});

test('decompress: expectedLength is required and validated', () => {
    const compressed = lzf.compress(Buffer.from(lorem));
    assert.throws(() => lzf.decompress(compressed), invalidType); // missing
    assert.throws(() => lzf.decompress(compressed, 'x'), invalidType); // wrong type
    assert.throws(() => lzf.decompress(compressed, 10n), invalidType); // BigInt is not a number
    assert.throws(() => lzf.decompress(compressed, -1), outOfRange);
    assert.throws(() => lzf.decompress(compressed, 1.5), outOfRange);
    assert.throws(() => lzf.decompress(compressed, NaN), outOfRange);
    assert.throws(() => lzf.decompress(compressed, 2 * 1024 * 1024 * 1024), outOfRange); // > 1 GiB
    // non-finite and huge values must be rejected before any integer
    // conversion (converting them is undefined behavior in C++)
    assert.throws(() => lzf.decompress(compressed, Infinity), outOfRange);
    assert.throws(() => lzf.decompress(compressed, -Infinity), outOfRange);
    assert.throws(() => lzf.decompress(compressed, 1e300), outOfRange);
    assert.throws(() => lzf.decompress(compressed, -1e300), outOfRange);
    assert.throws(() => lzf.decompress(compressed, Number.MAX_SAFE_INTEGER), outOfRange);
});

test('decompress: too-small expectedLength throws with a specific message', () => {
    const data = Buffer.from(lorem);
    const compressed = lzf.compress(data);
    assert.throws(() => lzf.decompress(compressed, data.length - 1), /too small/);
    assert.throws(() => lzf.decompress(compressed, data.length - 1), tooSmall);
    // 0 is a valid length, but a non-empty stream always decodes to at least one byte
    assert.throws(() => lzf.decompress(compressed, 0), tooSmall);
});

test('decompress: corrupted input throws instead of crashing', () => {
    // back-reference before the start of output: ctrl 0xe0 needs 2 more bytes
    assert.throws(() => lzf.decompress(Buffer.from([0xe0, 0x00, 0x00]), 64), /corrupted/);
    assert.throws(() => lzf.decompress(Buffer.from([0xe0, 0x00, 0x00]), 64), corrupted);
    // literal run claiming 32 bytes with no payload behind it
    assert.throws(() => lzf.decompress(Buffer.from([0x1f]), 64), corrupted);
    // truncated back-reference (control byte only)
    assert.throws(() => lzf.decompress(Buffer.from([0x20]), 64), corrupted);
    // truncated valid stream
    const compressed = lzf.compress(Buffer.from(lorem.repeat(10)));
    assert.throws(() => lzf.decompress(compressed.subarray(0, 5), lorem.length * 10), corrupted);
});

test('empty input: roundtrips to an empty Buffer', async () => {
    const empty = Buffer.alloc(0);
    const compressed = lzf.compress(empty);
    assert.ok(Buffer.isBuffer(compressed));
    assert.strictEqual(compressed.length, 0);
    assert.strictEqual(lzf.decompress(compressed, 0).length, 0);
    // an empty stream decodes to nothing whatever the expected length
    assert.strictEqual(lzf.decompress(empty, 1024).length, 0);
    assert.strictEqual((await lzf.compressAsync(empty)).length, 0);
    assert.strictEqual((await lzf.decompressAsync(empty, 0)).length, 0);
    assert.strictEqual(lzf.compress(new Uint8Array(0)).length, 0);
});

test('decompress: a loose expectedLength does not limit maximum-expansion streams', () => {
    // The output allocation is capped at 88x the input (the densest LZF token
    // is a 3-byte back reference producing 264 bytes). Streams at that ratio
    // must still decode fully when expectedLength is far above the output.
    const oneGiB = 1024 * 1024 * 1024;
    const tokens = 1000;
    const parts = [Buffer.from([0x00, 0x61])]; // literal "a"
    for (let i = 0; i < tokens; i++) parts.push(Buffer.from([0xe0, 0xff, 0x00])); // 264 x "a"
    const stream = Buffer.concat(parts);
    const expected = 1 + 264 * tokens;
    for (const expectedLength of [expected, expected + 1, oneGiB]) {
        const out = lzf.decompress(stream, expectedLength);
        assert.strictEqual(out.length, expected, `expectedLength=${expectedLength}`);
        assert.ok(out.equals(Buffer.alloc(expected, 0x61)));
    }
    assert.throws(() => lzf.decompress(stream, expected - 1), tooSmall);

    // real compressor output close to the maximum ratio
    const zeros = Buffer.alloc(1024 * 1024);
    assert.ok(lzf.decompress(lzf.compress(zeros), oneGiB).equals(zeros));
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
    await assert.rejects(lzf.compressAsync(), invalidType);
    await assert.rejects(lzf.compressAsync('nope'), invalidType);
    await assert.rejects(lzf.decompressAsync(), invalidType);
    await assert.rejects(lzf.decompressAsync('not a buffer', 10), invalidType);
    await assert.rejects(lzf.decompressAsync(Buffer.from([1, 0])), invalidType); // missing
    await assert.rejects(lzf.decompressAsync(Buffer.from([1, 0]), 'x'), invalidType); // wrong type
    await assert.rejects(lzf.decompressAsync(Buffer.from([1, 0]), -1), outOfRange);
    await assert.rejects(lzf.decompressAsync(Buffer.from([1, 0]), Infinity), outOfRange);
    await assert.rejects(lzf.decompressAsync(Buffer.from([1, 0]), 0), tooSmall);
    await assert.rejects(lzf.decompressAsync(Buffer.from([0xe0, 0x00, 0x00]), 64), /corrupted/);
    await assert.rejects(lzf.decompressAsync(Buffer.from([0xe0, 0x00, 0x00]), 64), corrupted);
    const data = Buffer.from(lorem);
    await assert.rejects(lzf.decompressAsync(lzf.compress(data), data.length - 1), /too small/);
    await assert.rejects(lzf.decompressAsync(lzf.compress(data), data.length - 1), tooSmall);
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

/* Large enough that the worker is still running when the test touches the
 * input right after the call, so the pre-copy behavior fails reliably. */
const inFlightSize = 8 * 1024 * 1024;

test('async: input may be mutated right after the call', async () => {
    // The async API copies its input before returning, so writes made while
    // the worker runs must not leak into the result.
    const data = compressibleBytes(inFlightSize, 55);
    const snapshot = Buffer.from(data);
    const compressing = lzf.compressAsync(data);
    data.fill(0xaa);
    const compressed = await compressing;
    assert.ok(lzf.decompress(compressed, snapshot.length).equals(snapshot));

    const decompressing = lzf.decompressAsync(compressed, snapshot.length);
    compressed.fill(0);
    assert.ok((await decompressing).equals(snapshot));
});

test('async: detaching the input during the call is safe', async () => {
    // Detaching (transfer, postMessage) used to free or hand over the memory
    // the worker was still reading: wrong output, or a segfault when the
    // memory was released.
    const detachers = [
        (u8) => new Uint8Array(structuredClone(u8.buffer, { transfer: [u8.buffer] })).fill(0xaa)
    ];
    if (typeof ArrayBuffer.prototype.transfer === 'function') {
        detachers.push((u8) => u8.buffer.transfer(16)); // shrinks: releases the old memory
    }
    for (const detach of detachers) {
        const data = compressibleBytes(inFlightSize, 66);
        const snapshot = Buffer.from(data);
        const u8 = new Uint8Array(snapshot);
        const compressing = lzf.compressAsync(u8);
        detach(u8);
        assert.strictEqual(u8.byteLength, 0); // detached
        const compressed = await compressing;
        assert.ok(lzf.decompress(compressed, snapshot.length).equals(snapshot));

        const packed = new Uint8Array(compressed);
        const decompressing = lzf.decompressAsync(packed, snapshot.length);
        detach(packed);
        assert.ok((await decompressing).equals(snapshot));
    }
});

test('input: a plain Uint8Array is accepted like a Buffer', async () => {
    const data = compressibleBytes(4096, 42);
    const u8 = new Uint8Array(data);
    const compressed = lzf.compress(u8);
    assert.ok(Buffer.isBuffer(compressed));
    assert.ok(lzf.decompress(new Uint8Array(compressed), data.length).equals(data));
    const asyncCompressed = await lzf.compressAsync(u8);
    assert.ok((await lzf.decompressAsync(new Uint8Array(asyncCompressed), data.length)).equals(data));
    // a view with a non-zero byteOffset must be read from its own offset
    const padded = new Uint8Array(data.length + 7);
    padded.set(data, 7);
    assert.ok(lzf.decompress(lzf.compress(padded.subarray(7)), data.length).equals(data));
});

test('input: any TypedArray is read as its raw bytes', async () => {
    const data = compressibleBytes(4096, 7);
    const types = [
        Int8Array,
        Uint8ClampedArray,
        Int16Array,
        Uint16Array,
        Int32Array,
        Float32Array,
        Float64Array,
        BigInt64Array
    ];
    if (typeof Float16Array === 'function') types.push(Float16Array); // unknown to node-addon-api
    for (const Type of types) {
        const view = new Type(data.buffer.slice(data.byteOffset, data.byteOffset + data.length));
        assert.strictEqual(view.byteLength, data.length);
        const compressed = lzf.compress(view);
        assert.ok(lzf.decompress(compressed, data.length).equals(data), Type.name);
        assert.ok((await lzf.decompressAsync(await lzf.compressAsync(view), data.length)).equals(data), Type.name);
    }
});

test('input: DataView and ArrayBuffer are rejected with TypeError', async () => {
    // A DataView used to pass the Buffer check and then abort the whole
    // process instead of throwing.
    const view = new DataView(new ArrayBuffer(16));
    const raw = new ArrayBuffer(16);
    for (const input of [view, raw]) {
        assert.throws(() => lzf.compress(input), TypeError);
        assert.throws(() => lzf.decompress(input, 64), TypeError);
        await assert.rejects(lzf.compressAsync(input), TypeError);
        await assert.rejects(lzf.decompressAsync(input, 64), TypeError);
    }
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
