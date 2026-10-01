/// <reference types="node" />

declare namespace lzf {
    /**
     * Input accepted by every function: a Buffer or any other TypedArray,
     * read as its raw bytes. DataView and ArrayBuffer are rejected.
     */
    type Input = NodeJS.TypedArray;

    /** Value of the `code` property on every error the module throws. */
    type ErrorCode =
        /** TypeError: `data` is not a TypedArray or `expectedLength` is not a number. */
        | 'ERR_INVALID_ARG_TYPE'
        /** RangeError: `data` exceeds 1 GiB or `expectedLength` is not an integer in 0..1 GiB. */
        | 'ERR_OUT_OF_RANGE'
        /** Error: the decompressed data does not fit in `expectedLength` bytes. */
        | 'ERR_LZF_OUTPUT_TOO_SMALL'
        /** Error: the input is not a valid LZF stream. */
        | 'ERR_LZF_CORRUPTED_INPUT'
        /** Error: the output (or the async input copy) could not be allocated. */
        | 'ERR_LZF_ALLOCATION_FAILED'
        /** Error: liblzf reported a compression failure (not expected in practice). */
        | 'ERR_LZF_COMPRESSION_FAILED';

    /** Shape of the errors thrown (sync) or rejected (async) by this module. */
    interface LzfError extends Error {
        code: ErrorCode;
    }

    /**
     * Compresses the input with LZF and returns a new Buffer. An empty input
     * returns an empty Buffer. Throws TypeError for input that is not a
     * TypedArray and RangeError for input larger than 1 GiB. Runs
     * synchronously on the calling thread.
     */
    function compress(data: Input): Buffer;

    /**
     * Compresses on the libuv thread pool without blocking the event loop.
     * The input is copied before the call returns, so it may be reused right
     * away. Rejects with the same errors compress() throws.
     */
    function compressAsync(data: Input): Promise<Buffer>;

    /**
     * Decompresses LZF-compressed data into a new Buffer. expectedLength is
     * the exact (or an upper bound of the) decompressed size, an integer
     * between 0 and 1 GiB. Throws TypeError/RangeError on invalid arguments
     * and Error when the input is corrupted or expectedLength is too small.
     */
    function decompress(data: Input, expectedLength: number): Buffer;

    /**
     * Decompresses on the libuv thread pool without blocking the event loop.
     * The input is copied before the call returns, so it may be reused right
     * away. Rejects with the same errors decompress() throws.
     */
    function decompressAsync(data: Input, expectedLength: number): Promise<Buffer>;
}

export = lzf;
