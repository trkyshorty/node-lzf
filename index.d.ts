/// <reference types="node" />

declare namespace lzf {
    /**
     * Compresses a Buffer or Uint8Array with LZF and returns a new Buffer.
     * Throws TypeError on invalid input (not a Buffer/Uint8Array, empty, or
     * larger than 1 GiB). Runs synchronously on the calling thread.
     */
    function compress(data: Uint8Array): Buffer;

    /**
     * Compresses a Buffer or Uint8Array with LZF on the libuv thread pool
     * without blocking the event loop. Rejects with the same errors
     * compress() throws.
     */
    function compressAsync(data: Uint8Array): Promise<Buffer>;

    /**
     * Decompresses LZF-compressed data (a Buffer or Uint8Array) into a new
     * Buffer. expectedLength is the exact (or an upper bound of the)
     * decompressed size, between 1 and 1 GiB. Throws TypeError/RangeError on
     * invalid arguments and Error when the input is corrupted or
     * expectedLength is too small.
     */
    function decompress(data: Uint8Array, expectedLength: number): Buffer;

    /**
     * Decompresses on the libuv thread pool without blocking the event
     * loop. Rejects with the same errors decompress() throws.
     */
    function decompressAsync(data: Uint8Array, expectedLength: number): Promise<Buffer>;
}

export = lzf;
