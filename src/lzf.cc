/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */
/* node-lzf (C) 2025 Maintained Türkay Tanrikulu <trky.shorty@gmail.com>  */

#include <napi.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "lzf/lzf.h"

namespace {

/* Hard caps so a bogus length argument can never trigger a multi-GB
 * allocation. 1 GiB is far above any realistic LZF payload. */
constexpr size_t kMaxInputLength = 1024u * 1024u * 1024u;
constexpr size_t kMaxOutputLength = 1024u * 1024u * 1024u;

/* Worst-case compressed size for an input of n bytes (same formula the
 * original node-lzf used; slightly above the theoretical n + n/32 + 1). */
inline size_t CompressBound(size_t n) {
    return n + (n / 16) + 64 + 3;
}

/* Largest output any LZF stream of n bytes can decode to. The densest token
 * is a long back reference: 3 input bytes (control, length, offset) that
 * expand to 7 + 255 + 2 = 264 output bytes, i.e. 88x. Literal runs and short
 * back references expand less, so no valid or corrupted stream can outgrow
 * this. Capping the allocation with it means an untrusted expectedLength
 * cannot make a tiny input allocate up to 1 GiB. */
constexpr size_t kMaxExpansion = 88;

inline size_t DecompressBound(size_t n) {
    return n > SIZE_MAX / kMaxExpansion ? SIZE_MAX : n * kMaxExpansion;
}

/* ---- errors ------------------------------------------------------------- */

enum class ErrorKind { kError, kTypeError, kRangeError };

/* Every error carries a stable `code` so callers never have to match on the
 * message. The strings are static, so a Failure can be filled on a worker
 * thread and turned into a JS error later on the main thread. */
struct Failure {
    ErrorKind kind = ErrorKind::kError;
    const char* code = nullptr;
    const char* message = nullptr;
};

constexpr Failure kInvalidData{ErrorKind::kTypeError, "ERR_INVALID_ARG_TYPE",
                               "The \"data\" argument must be a Buffer or TypedArray"};
constexpr Failure kDataTooLarge{ErrorKind::kRangeError, "ERR_OUT_OF_RANGE",
                                "The \"data\" argument exceeds the 1 GiB limit"};
constexpr Failure kInvalidLengthType{ErrorKind::kTypeError, "ERR_INVALID_ARG_TYPE",
                                     "The \"expectedLength\" argument must be a number"};
constexpr Failure kLengthNotInteger{ErrorKind::kRangeError, "ERR_OUT_OF_RANGE",
                                    "The \"expectedLength\" argument must be an integer"};
constexpr Failure kLengthOutOfRange{ErrorKind::kRangeError, "ERR_OUT_OF_RANGE",
                                    "The \"expectedLength\" argument must be between 0 and 1 GiB"};
constexpr Failure kAllocationFailed{ErrorKind::kError, "ERR_LZF_ALLOCATION_FAILED",
                                    "Failed to allocate memory"};
constexpr Failure kCompressionFailed{ErrorKind::kError, "ERR_LZF_COMPRESSION_FAILED",
                                     "Compression failed"};
constexpr Failure kOutputTooSmall{ErrorKind::kError, "ERR_LZF_OUTPUT_TOO_SMALL",
                                  "Decompression failed: expected length too small"};
constexpr Failure kCorruptedInput{ErrorKind::kError, "ERR_LZF_CORRUPTED_INPUT",
                                  "Decompression failed: corrupted input"};

Napi::Value MakeError(Napi::Env env, const Failure& failure) {
    napi_value code;
    napi_value message;
    napi_value error = nullptr;
    napi_create_string_utf8(env, failure.code, NAPI_AUTO_LENGTH, &code);
    napi_create_string_utf8(env, failure.message, NAPI_AUTO_LENGTH, &message);
    switch (failure.kind) {
        case ErrorKind::kTypeError: napi_create_type_error(env, code, message, &error); break;
        case ErrorKind::kRangeError: napi_create_range_error(env, code, message, &error); break;
        case ErrorKind::kError: napi_create_error(env, code, message, &error); break;
    }
    return Napi::Value(env, error);
}

Napi::Value Throw(Napi::Env env, const Failure& failure) {
    napi_throw(env, MakeError(env, failure));
    return env.Undefined();
}

Napi::Value RejectedPromise(Napi::Env env, const Failure& failure) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(MakeError(env, failure));
    return deferred.Promise();
}

/* ---- codec -------------------------------------------------------------- */

struct RawResult {
    char* data = nullptr;
    size_t length = 0;
};

/* Compresses into a malloc'd buffer shrunk to the exact result size. An
 * empty input yields an empty result (lzf_compress itself rejects it). */
bool DoCompress(const char* input, size_t inputLength, RawResult* out, Failure* failure) {
    if (inputLength == 0) return true;

    size_t bound = CompressBound(inputLength);
    char* buffer = static_cast<char*>(malloc(bound));
    if (buffer == nullptr) {
        *failure = kAllocationFailed;
        return false;
    }

    unsigned int compressedLength = lzf_compress(
        input, static_cast<unsigned int>(inputLength),
        buffer, static_cast<unsigned int>(bound));

    if (compressedLength == 0) {
        free(buffer);
        *failure = kCompressionFailed;
        return false;
    }

    char* shrunk = static_cast<char*>(realloc(buffer, compressedLength));
    out->data = shrunk != nullptr ? shrunk : buffer;
    out->length = compressedLength;
    return true;
}

/* Decompresses into a malloc'd buffer of at most expectedLength bytes, shrunk
 * to the actual result size. Distinguishes "expected length too small"
 * (E2BIG) from corrupted input via errno set by lzf_decompress. An empty
 * input decodes to an empty result, the inverse of DoCompress. */
bool DoDecompress(const char* input, size_t inputLength, size_t expectedLength, RawResult* out,
                  Failure* failure) {
    if (inputLength == 0) return true;
    if (expectedLength == 0) {
        /* every LZF token produces at least one byte */
        *failure = kOutputTooSmall;
        return false;
    }

    size_t capacity = std::min(expectedLength, DecompressBound(inputLength));
    char* buffer = static_cast<char*>(malloc(capacity));
    if (buffer == nullptr) {
        *failure = kAllocationFailed;
        return false;
    }

    errno = 0;
    unsigned int decompressedLength = lzf_decompress(
        input, static_cast<unsigned int>(inputLength),
        buffer, static_cast<unsigned int>(capacity));

    if (decompressedLength == 0) {
        int cause = errno;
        free(buffer);
        /* E2BIG below the expansion bound cannot come from a real stream, but
         * report it as corruption rather than blaming expectedLength. */
        *failure = cause == E2BIG && capacity == expectedLength ? kOutputTooSmall : kCorruptedInput;
        return false;
    }

    if (decompressedLength < capacity) {
        char* shrunk = static_cast<char*>(realloc(buffer, decompressedLength));
        if (shrunk != nullptr) buffer = shrunk;
    }
    out->data = buffer;
    out->length = decompressedLength;
    return true;
}

/* Wraps a malloc'd result as a Buffer without copying when the runtime
 * supports external buffers (plain Node does); falls back to copy+free. */
Napi::Value WrapResult(Napi::Env env, RawResult* result) {
    if (result->length == 0) {
        free(result->data);
        return Napi::Buffer<char>::New(env, 0);
    }
    return Napi::Buffer<char>::NewOrCopy(
        env, result->data, result->length,
        [](Napi::Env, char* data) { free(data); });
}

/* ---- argument validation ------------------------------------------------ */

struct Input {
    const char* data = nullptr;
    size_t length = 0;
};

/* Accepts any TypedArray (Buffer, Uint8Array, Float64Array, ...) and reads its
 * bytes. DataView and ArrayBuffer are rejected. napi_get_buffer_info reports
 * the view's byte length straight from V8, so element types node-addon-api
 * does not know (Float16Array) are measured correctly too. */
bool GetInput(const Napi::CallbackInfo& info, Input* input, Failure* failure) {
    if (info.Length() < 1 || !info[0].IsTypedArray()) {
        *failure = kInvalidData;
        return false;
    }
    void* data = nullptr;
    size_t length = 0;
    if (napi_get_buffer_info(info.Env(), info[0], &data, &length) != napi_ok) {
        *failure = kInvalidData;
        return false;
    }
    if (length > kMaxInputLength) {
        *failure = kDataTooLarge;
        return false;
    }
    input->data = static_cast<const char*>(data);
    input->length = length;
    return true;
}

/* expectedLength is required: the legacy default silently allocated a
 * 999 MB buffer per call, which is a footgun rather than a feature. The
 * range is checked on the double before any integer conversion, because
 * converting an out-of-range double (Infinity, 1e300) is undefined. */
bool GetExpectedLength(const Napi::CallbackInfo& info, size_t* expectedLength, Failure* failure) {
    if (info.Length() < 2 || !info[1].IsNumber()) {
        *failure = kInvalidLengthType;
        return false;
    }
    double raw = info[1].As<Napi::Number>().DoubleValue();
    if (!std::isfinite(raw) || std::trunc(raw) != raw) {
        *failure = kLengthNotInteger;
        return false;
    }
    if (raw < 0 || raw > static_cast<double>(kMaxOutputLength)) {
        *failure = kLengthOutOfRange;
        return false;
    }
    *expectedLength = static_cast<size_t>(raw);
    return true;
}

/* ---- async workers ------------------------------------------------------ */

/* The async API copies the input on the calling thread before queueing. A
 * pointer into the caller's memory is not safe to keep: the ArrayBuffer can
 * be detached or shrunk (transfer(), postMessage, resize()) while the worker
 * runs, which freed the memory under it. The copy also means the caller may
 * reuse or mutate the input as soon as the call returns. */
class OwnedInput {
  public:
    OwnedInput() = default;
    OwnedInput(const OwnedInput&) = delete;
    OwnedInput& operator=(const OwnedInput&) = delete;
    ~OwnedInput() { free(data_); }

    bool CopyFrom(const Input& input) {
        if (input.length == 0) return true;
        data_ = static_cast<char*>(malloc(input.length));
        if (data_ == nullptr) return false;
        memcpy(data_, input.data, input.length);
        length_ = input.length;
        return true;
    }

    const char* data() const { return data_; }
    size_t length() const { return length_; }

  private:
    char* data_ = nullptr;
    size_t length_ = 0;
};

class CodecWorker : public Napi::AsyncWorker {
  public:
    explicit CodecWorker(Napi::Env env)
        : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)) {}

    OwnedInput& input() { return input_; }
    Napi::Promise Promise() { return deferred_.Promise(); }

  protected:
    /* Failures are reported through failure_ rather than SetError so the
     * rejection keeps its error class and code. */
    void OnOK() override {
        if (failed_) deferred_.Reject(MakeError(Env(), failure_));
        else deferred_.Resolve(WrapResult(Env(), &result_));
    }

    OwnedInput input_;
    RawResult result_;
    Failure failure_;
    bool failed_ = false;

  private:
    Napi::Promise::Deferred deferred_;
};

class CompressWorker : public CodecWorker {
  public:
    using CodecWorker::CodecWorker;

  protected:
    void Execute() override {
        failed_ = !DoCompress(input_.data(), input_.length(), &result_, &failure_);
    }
};

class DecompressWorker : public CodecWorker {
  public:
    DecompressWorker(Napi::Env env, size_t expectedLength)
        : CodecWorker(env), expectedLength_(expectedLength) {}

  protected:
    void Execute() override {
        failed_ = !DoDecompress(input_.data(), input_.length(), expectedLength_, &result_,
                                &failure_);
    }

  private:
    size_t expectedLength_;
};

/* Copies the input into the worker and queues it, or deletes the worker and
 * returns a rejected promise when the copy cannot be allocated. */
Napi::Value QueueWorker(Napi::Env env, CodecWorker* worker, const Input& input) {
    if (!worker->input().CopyFrom(input)) {
        delete worker;
        return RejectedPromise(env, kAllocationFailed);
    }
    Napi::Promise promise = worker->Promise();
    worker->Queue();
    return promise;
}

/* ---- sync API ----------------------------------------------------------- */

Napi::Value Compress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    Input input;
    Failure failure;
    if (!GetInput(info, &input, &failure)) return Throw(env, failure);

    RawResult result;
    if (!DoCompress(input.data, input.length, &result, &failure)) return Throw(env, failure);
    return WrapResult(env, &result);
}

Napi::Value Decompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    Input input;
    size_t expectedLength = 0;
    Failure failure;
    if (!GetInput(info, &input, &failure)) return Throw(env, failure);
    if (!GetExpectedLength(info, &expectedLength, &failure)) return Throw(env, failure);

    RawResult result;
    if (!DoDecompress(input.data, input.length, expectedLength, &result, &failure)) {
        return Throw(env, failure);
    }
    return WrapResult(env, &result);
}

/* ---- async API (libuv thread pool; never blocks the event loop) --------- */

Napi::Value CompressAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    Input input;
    Failure failure;
    if (!GetInput(info, &input, &failure)) return RejectedPromise(env, failure);

    return QueueWorker(env, new CompressWorker(env), input);
}

Napi::Value DecompressAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    Input input;
    size_t expectedLength = 0;
    Failure failure;
    if (!GetInput(info, &input, &failure)) return RejectedPromise(env, failure);
    if (!GetExpectedLength(info, &expectedLength, &failure)) return RejectedPromise(env, failure);

    return QueueWorker(env, new DecompressWorker(env, expectedLength), input);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("compress", Napi::Function::New(env, Compress));
    exports.Set("decompress", Napi::Function::New(env, Decompress));
    exports.Set("compressAsync", Napi::Function::New(env, CompressAsync));
    exports.Set("decompressAsync", Napi::Function::New(env, DecompressAsync));
    return exports;
}

}  // namespace

NODE_API_MODULE(lzf, Init)
