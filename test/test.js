var assert = require('assert');

var lzf = require('../index.js');

var lorem =
    'Lorem ipsum dolor sit amet, consectetur adipiscing elit.' +
    'Curabitur volutpat, nulla nec egestas semper,' +
    'ante dui tristique nibh, quis feugiat.';

var loremBuffer = Buffer.from(lorem);
var loremCompressed = lzf.compress(loremBuffer);
var loremDecompressed = lzf.decompress(loremCompressed);

// check default output buffer
assert.equal(loremDecompressed.toString(), lorem);

// check minimum output buffer
assert.equal(lzf.decompress(loremCompressed, loremBuffer.length).toString(), lorem);

// check error on too small buffer
try {
    lzf.decompress(loremCompressed, loremBuffer.length - 1);
    assert.fail('exception should be thrown for too small buffer');
} catch (e) {}

// roundtrip: incompressible random data
var random = require('crypto').randomBytes(256 * 1024);
assert.ok(lzf.decompress(lzf.compress(random), random.length).equals(random));

// roundtrip: highly compressible data
var zeros = Buffer.alloc(1024 * 1024);
var zerosCompressed = lzf.compress(zeros);
assert.ok(zerosCompressed.length < zeros.length / 10);
assert.ok(lzf.decompress(zerosCompressed, zeros.length).equals(zeros));

// non-buffer input throws TypeError
assert.throws(function () { lzf.compress('not a buffer'); }, TypeError);
assert.throws(function () { lzf.decompress('not a buffer'); }, TypeError);

console.log('test ok');
