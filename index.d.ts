/// <reference types="node" />

declare namespace lzf {
    /**
     * Compresses a Buffer with LZF. Throws TypeError on invalid input
     * (non-Buffer, empty, or larger than 1 GiB). Runs synchronously on
     * the calling thread.
     */
    function compress(data: Buffer): Buffer;

    /**
     * Compresses a Buffer with LZF on the libuv thread pool without
     * blocking the event loop. Rejects with the same errors compress()
     * throws.
     */
    function compressAsync(data: Buffer): Promise<Buffer>;

    /**
     * Decompresses an LZF-compressed Buffer. expectedLength is the exact
     * (or an upper bound of the) decompressed size, between 1 and 1 GiB.
     * Throws TypeError/RangeError on invalid arguments and Error when the
     * input is corrupted or expectedLength is too small.
     */
    function decompress(data: Buffer, expectedLength: number): Buffer;

    /**
     * Decompresses on the libuv thread pool without blocking the event
     * loop. Rejects with the same errors decompress() throws.
     */
    function decompressAsync(data: Buffer, expectedLength: number): Promise<Buffer>;
}

export = lzf;
