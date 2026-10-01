## node-lzf

[LZF](https://software.schmorp.de/pkg/liblzf.html) compression library for Node.js.

LZF advantages:

- Very fast compression speeds, rivaling a straight copy loop, especially for decompression which is basically at (unoptimized) memcpy-speed.
- Mediocre compression ratios - you can usually expect about 40-50% compression for typical binary data.
- Easy to use (just compress and decompress, no state attached).
- Any liblzf build decodes the output, whatever options it was compressed with.
- Freely usable (BSD-type license).

### Install

```bash
npm install @trkyshorty/node-lzf
```

Requires Node.js 20 or later. The module is N-API based and ships prebuilt
binaries (`prebuilds/`) for these platforms, so no compiler toolchain is
needed on them:

| OS      | Architectures                         |
| ------- | ------------------------------------- |
| Linux   | x64, arm64 (glibc); x64 (musl/Alpine) |
| Windows | x64, arm64                            |
| macOS   | arm64, x64                            |

On any other platform (Alpine on arm64, for example) it falls back to
compiling from source via `node-gyp-build`, which requires a C++ toolchain
and Python.

### Usage

```javascript
const lzf = require('@trkyshorty/node-lzf');

const data = Buffer.from('some data to compress');

// sync — runs on the calling thread
const compressed = lzf.compress(data);
const restored = lzf.decompress(compressed, data.length);

// async — runs on the libuv thread pool, never blocks the event loop
const compressed2 = await lzf.compressAsync(data);
const restored2 = await lzf.decompressAsync(compressed2, data.length);
```

LZF streams do not store the original size, so store `data.length` next to
the compressed bytes (a length prefix in your framing, for example) and pass
it back to `decompress`.

#### TypeScript

Type definitions are bundled, no `@types` package is needed. The module is
CommonJS (`export =`), so import it as a namespace — or as a default import
when `esModuleInterop` is enabled:

```typescript
import * as lzf from '@trkyshorty/node-lzf';
// with "esModuleInterop": true you can also write:
// import lzf from '@trkyshorty/node-lzf';

const data: Buffer = Buffer.from('some data to compress');

// sync
const compressed: Buffer = lzf.compress(data);
const restored: Buffer = lzf.decompress(compressed, data.length);

// async
async function roundtrip(input: Buffer): Promise<Buffer> {
    const packed = await lzf.compressAsync(input);
    return lzf.decompressAsync(packed, input.length);
}

// errors carry a typed code
try {
    lzf.decompress(compressed, 1);
} catch (err) {
    if ((err as lzf.LzfError).code === 'ERR_LZF_OUTPUT_TOO_SMALL') {
        // ...
    }
}
```

### API

Every function accepts a `Buffer` or any other TypedArray (`Uint8Array`,
`Float64Array`, ...), read as its raw bytes, and always returns a `Buffer`.
`DataView` and `ArrayBuffer` are rejected; wrap an `ArrayBuffer` in a
`Uint8Array` first. Inputs are limited to 1 GiB.

#### `compress(data): Buffer`

Returns an LZF-compressed Buffer sized exactly to the result. An empty input
returns an empty Buffer. Incompressible input can grow slightly (up to
~104%).

#### `decompress(data, expectedLength): Buffer`

Returns the decompressed Buffer. `expectedLength` is **required**: pass the
exact decompressed size, or an upper bound — the result is shrunk to the
actual size. It must be an integer between 0 and 1 GiB. An empty input
returns an empty Buffer.

The decompressor is safe on untrusted input: a corrupted stream throws
instead of reading or writing out of bounds. The output allocation never
exceeds 88× the input size (the largest expansion an LZF stream can encode),
however large `expectedLength` is. Still check `expectedLength` against a
limit that fits your application when it comes from an untrusted source.

> **v1.1.0 note:** older versions allowed omitting `expectedLength` and
> silently allocated a 999 MB scratch buffer per call — that default has
> been removed.

#### `compressAsync(data): Promise<Buffer>` / `decompressAsync(data, expectedLength): Promise<Buffer>`

Same semantics as the sync variants, but the (de)compression runs on the
libuv thread pool and the returned promise rejects with the errors the sync
variants would throw. The input is copied before the call returns, so it may
be modified, reused or transferred right away. Prefer these for payloads
larger than a few hundred KB on latency-sensitive servers.

#### Errors

Every error has a stable `code` property; match on it rather than on the
message.

| `code`                       | Class        | Cause                                                                   |
| ---------------------------- | ------------ | ----------------------------------------------------------------------- |
| `ERR_INVALID_ARG_TYPE`       | `TypeError`  | `data` is not a TypedArray, or `expectedLength` is not a number         |
| `ERR_OUT_OF_RANGE`           | `RangeError` | `data` exceeds 1 GiB, or `expectedLength` is not an integer in 0..1 GiB |
| `ERR_LZF_OUTPUT_TOO_SMALL`   | `Error`      | the decompressed data does not fit in `expectedLength` bytes            |
| `ERR_LZF_CORRUPTED_INPUT`    | `Error`      | the input is not a valid LZF stream                                     |
| `ERR_LZF_ALLOCATION_FAILED`  | `Error`      | the output, or the async input copy, could not be allocated             |
| `ERR_LZF_COMPRESSION_FAILED` | `Error`      | liblzf reported a compression failure (not expected in practice)        |

#### Output stability

Compressed output is a valid LZF stream decodable by any liblzf build. The
exact compressed bytes are not guaranteed to be identical across calls
(liblzf's hash table is intentionally left uninitialized for speed) — only
the roundtrip contract holds.

### Benchmarks

`npm run bench` compares against node's built-in zlib (`deflateRaw`,
levels 1 and 6) on deterministic datasets. Numbers below from a Windows
x64 machine, Node 24 (`ratio` = compressed/original; lower is better):

```text
dataset      codec      ratio   comp ms  comp MB/s  decomp ms  decomp MB/s
text 100KB   lzf          40%     0.158        433      0.094          726
text 100KB   zlib-1       30%     0.354        194      0.122          562
text 100KB   zlib-6       27%     1.421         48      0.120          570
json 64KB    lzf          37%     0.039        917      0.037          963
json 64KB    zlib-1       24%     0.122        291      0.054          658
json 64KB    zlib-6       19%     0.486         73      0.049          722
json 2MB     lzf          36%     1.837        617      1.156          980
json 2MB     zlib-1       24%     4.383        259      1.511          750
json 2MB     zlib-6       18%    17.541         65      1.311          865
binary 4KB   lzf          79%     0.008        509      0.004          904
binary 4KB   zlib-1       72%     0.039        100      0.011          346
binary 64KB  lzf          76%     0.127        493      0.049         1286
binary 64KB  zlib-1       71%     0.660         95      0.127          492
random 1MB   lzf         103%     2.816        355      0.415         2410
random 1MB   zlib-1      100%    15.080         66      0.385         2598
zeros 1MB    lzf           1%     0.273       3660      1.531          653
zeros 1MB    zlib-1        0%     0.350       2861      0.473         2115
```

In short: LZF compresses 2–10× faster than zlib at its fastest level, at
the cost of a worse ratio. Pick LZF when compression latency matters more
than size (hot network paths); pick zlib/brotli for cold storage.

### Development

`npm test` runs the suite against whichever binary `node-gyp-build` resolves:
`build/Release` when it exists, otherwise the local `prebuilds/` — which are
git-ignored and may predate your latest `src/` change. After changing the C++
sources (`src/`, `binding.gyp`) run `npm run test:local` instead: it rebuilds
`build/Release` (a C++ toolchain is required) and runs the tests against that
fresh build. Delete `build/` to test the prebuilds again. Builds use the
`node-gyp` devDependency rather than the copy bundled with npm, which is too
old to find Visual Studio 2026 on Node 20.

The `test` GitHub Actions workflow runs on every push and pull request: it
builds from source and runs the suite on Linux, Windows and macOS with
Node 20, 22 and 24, and once more on Linux under AddressSanitizer and
UndefinedBehaviorSanitizer.

#### Releasing

Releases are fully automatic: add the changes to `CHANGELOG.md`, bump
`version` in `package.json` and push to `master`. If that version is not on
npm yet, the `prebuild` workflow builds and tests every platform
(`npm run build:prebuilds` runs `prebuildify --napi --strip` for the current
platform), publishes the package with the merged `prebuilds/` via npm trusted
publishing (with provenance) and tags the commit `v<version>`. Pushes that
keep the version publish nothing; a manual run always builds and tests. A
local `npm publish` is refused (`prepublishOnly`), so stale local
`prebuilds/` can never reach the registry.

---

### Authors

- Ian Babrou (`ibobrik@gmail.com`) — original author
- Türkay Tanrikulu (`trky.shorty@gmail.com`) — fork maintainer

### License

BSD-2-Clause, see [LICENSE](LICENSE). Bundles [liblzf](https://software.schmorp.de/pkg/liblzf.html)
by Marc Alexander Lehmann (BSD-2-Clause / GPL dual-licensed).
