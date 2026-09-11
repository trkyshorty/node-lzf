## node-lzf

[LZF](http://oldhome.schmorp.de/marc/liblzf.html) compression library for nodejs.

LZF advantages:

- Small code size (less then 500 lines including header files and docs).
- Very fast compression speeds, rivaling a straight copy loop, especially for decompression which is basically at (unoptimized) memcpy-speed. Compression speed can be increased by 20% by sacrificing a few percent of compression ratio.
- Mediocre compression ratios - you can usually expect about 40-50% compression for typical binary data
- Easy to use (just two functions, no state attached)
- Highly portable (written in C)
- Tunable, see the file lzfP.h in the distribution, to tailor liblzf to your needs. The generated compressed blocks can be decompressed by any liblzf version regardless of the options used to compress.
- Freely usable (BSD-type-license)

### Install

```bash
npm install @trkyshorty/node-lzf
```

The module is N-API based and ships prebuilt binaries (`prebuilds/`) for
linux-x64, linux-arm64, win32-x64 and darwin-arm64 — no compiler toolchain
is needed on those platforms. On other platforms it falls back to compiling
from source via `node-gyp-build` (requires a C++ toolchain).

Prebuilds are produced by the `prebuild` GitHub Actions workflow
(`npm run prebuild` runs `prebuildify --napi --strip` for the current
platform). Before `npm publish`, download the merged `prebuilds` artifact
from the workflow run and place it in the package root as `prebuilds/`.

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
```

### API

TypeScript definitions are bundled (`index.d.ts`).

#### `compress(data: Buffer): Buffer`

Returns an LZF-compressed Buffer sized exactly to the result. Throws
`TypeError` if `data` is not a Buffer, is empty, or exceeds 1 GiB.
Note: incompressible input can grow slightly (up to ~104%).

#### `decompress(data: Buffer, expectedLength: number): Buffer`

Returns the decompressed Buffer. `expectedLength` is **required** — pass the
exact decompressed size (or an upper bound); the result is shrunk to the
actual size. Throws `TypeError`/`RangeError` for invalid arguments
(`expectedLength` must be an integer between 1 and 1 GiB) and `Error` with
a descriptive message when the input is corrupted (`corrupted input`) or
`expectedLength` is too small (`expected length too small`). The
decompressor is safe on untrusted input: corrupt streams throw instead of
reading or writing out of bounds.

> **v1.1.0 note:** older versions allowed omitting `expectedLength` and
> silently allocated a 999 MB scratch buffer per call — that default has
> been removed.

#### `compressAsync(data: Buffer): Promise<Buffer>` / `decompressAsync(data: Buffer, expectedLength: number): Promise<Buffer>`

Same semantics as the sync variants, but the (de)compression runs on the
libuv thread pool and the returned promise rejects with the errors the sync
variants would throw. Prefer these for payloads larger than a few hundred
KB on latency-sensitive servers.

Compressed output is a valid LZF stream decodable by any liblzf build.
The exact compressed bytes are not guaranteed to be identical across calls
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

---

### Authors

- Ian Babrou (`ibobrik@gmail.com`) — original author
- Türkay Tanrikulu (`trky.shorty@gmail.com`) — fork maintainer

### License

BSD-2-Clause, see [LICENSE](LICENSE). Bundles [liblzf](http://oldhome.schmorp.de/marc/liblzf.html)
by Marc Alexander Lehmann (BSD-2-Clause / GPL dual-licensed).
