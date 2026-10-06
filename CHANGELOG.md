# Changelog

All notable changes to `@trkyshorty/node-lzf` are listed here. Versions follow
[Semantic Versioning](https://semver.org/).

## 2.0.0

### Breaking

- Requires Node.js 20 or later (Node.js 18 is end-of-life).
- Empty input no longer throws: `compress` returns an empty Buffer and an empty
  stream decompresses to an empty Buffer. Code that relied on the
  `Input buffer must not be empty` `TypeError` to reject empty data has to
  check the length itself.
- Input larger than 1 GiB throws `RangeError` (`ERR_OUT_OF_RANGE`) instead of
  `TypeError`.
- Argument error messages were reworded; match on the new `code` property
  instead of the message. The decompression messages still contain
  `corrupted input` and `expected length too small`.

### Fixed

- `compressAsync`/`decompressAsync` no longer read the caller's memory from the
  worker thread. Detaching the input's `ArrayBuffer` during the call
  (`transfer()`, `postMessage`) returned wrong data or crashed the process with
  a use-after-free, and changing the input before the promise settled leaked
  into the result. The input is now copied before the call returns.
- A `DataView` input threw an uncatchable error and aborted the process; it is
  now rejected with a `TypeError`.
- `decompress` with `expectedLength` set to `Infinity` or a huge number relied
  on undefined behavior in C++; it is now range-checked before conversion.
- The compressor could read out of bounds on Windows ARM64 (32-bit offset
  type), and its unaligned 16-bit load was undefined behavior on x86 with
  gcc/clang.
- Compressing a 1-byte input read one byte past the end of the input (an
  upstream liblzf bug found by AddressSanitizer).

### Added

- Every error has a stable `code` property (`ERR_INVALID_ARG_TYPE`,
  `ERR_OUT_OF_RANGE`, `ERR_LZF_OUTPUT_TOO_SMALL`, `ERR_LZF_CORRUPTED_INPUT`,
  `ERR_LZF_ALLOCATION_FAILED`, `ERR_LZF_COMPRESSION_FAILED`). The type
  definitions export `ErrorCode` and `LzfError`.
- `expectedLength` may be `0`, for an empty stream.
- Any TypedArray (`Float64Array`, `Int16Array`, `Float16Array`, ...) is
  accepted and read as its raw bytes; the type definitions accept
  `NodeJS.TypedArray`.
- Prebuilt binaries for Windows arm64, macOS x64 and Alpine Linux (musl) x64.
  Linux prebuilds are tagged with their libc.

### Changed

- `decompress` allocates at most 88× the input size, the largest expansion an
  LZF stream can encode, so an untrusted `expectedLength` cannot make a small
  input allocate up to 1 GiB. A corrupted stream that would exceed this cap
  now reports `ERR_LZF_CORRUPTED_INPUT` rather than `ERR_LZF_OUTPUT_TOO_SMALL`.
- `compressAsync`/`decompressAsync` copy their input (see Fixed), so each call
  in flight holds an extra copy of the input until it settles.

## 1.3.1 - 2026-09-11

### Changed

- A local `npm publish` is refused; releases are published by the CI workflow
  only, so stale local prebuilds cannot reach the registry.

## 1.3.0 - 2026-09-11

### Changed

- Releases are published to npm automatically by CI when the package version
  changes, using npm trusted publishing with provenance.

## 1.2.0 - 2026-09-11

### Added

- TypeScript usage example in the README.

### Changed

- Vendored liblzf builds without compiler warnings.

## 1.1.0 - 2026-07-22

First release as `@trkyshorty/node-lzf`.

### Added

- N-API addon with prebuilt binaries for linux-x64, linux-arm64, win32-x64 and
  darwin-arm64.
- `compressAsync` and `decompressAsync`, running on the libuv thread pool.
- Type definitions, a complete test suite and a benchmark against zlib.

### Changed

- **Breaking:** `decompress` requires `expectedLength` (1 byte to 1 GiB).
  Omitting it used to allocate a 999 MB buffer on every call. This should have
  been a major version.
- Arguments are validated strictly; corrupted input throws instead of crashing.
