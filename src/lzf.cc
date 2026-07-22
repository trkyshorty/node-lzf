/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */
/* node-lzf (C) 2025 Maintained Türkay Tanrikulu <trky.shorty@gmail.com>  */

#include <napi.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

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

struct RawResult {
    char* data = nullptr;
    size_t length = 0;
};

/* Compresses into a malloc'd buffer shrunk to the exact result size.
 * Returns false and fills `error` on failure (buffer already freed). */
bool DoCompress(const char* input, size_t inputLength, RawResult* out, std::string* error) {
    size_t bound = CompressBound(inputLength);
    char* buffer = static_cast<char*>(malloc(bound));
    if (buffer == nullptr) {
        *error = "Failed to allocate output buffer";
        return false;
    }

    unsigned int compressedLength = lzf_compress(
        input, static_cast<unsigned int>(inputLength),
        buffer, static_cast<unsigned int>(bound));

    if (compressedLength == 0) {
        free(buffer);
        *error = "Compression failed";
        return false;
    }

    char* shrunk = static_cast<char*>(realloc(buffer, compressedLength));
    out->data = shrunk != nullptr ? shrunk : buffer;
    out->length = compressedLength;
    return true;
}

/* Decompresses into a malloc'd buffer of expectedLength, shrunk to the
 * actual result size. Distinguishes "expected length too small" (E2BIG)
 * from corrupted input via errno set by lzf_decompress. */
bool DoDecompress(const char* input, size_t inputLength, size_t expectedLength, RawResult* out,
                  std::string* error) {
    char* buffer = static_cast<char*>(malloc(expectedLength));
    if (buffer == nullptr) {
        *error = "Failed to allocate output buffer";
        return false;
    }

    errno = 0;
    unsigned int decompressedLength = lzf_decompress(
        input, static_cast<unsigned int>(inputLength),
        buffer, static_cast<unsigned int>(expectedLength));

    if (decompressedLength == 0) {
        int cause = errno;
        free(buffer);
        *error = cause == E2BIG ? "Decompression failed: expected length too small"
                                : "Decompression failed: corrupted input";
        return false;
    }

    if (decompressedLength < expectedLength) {
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
    return Napi::Buffer<char>::NewOrCopy(
        env, result->data, result->length,
        [](Napi::Env, char* data) { free(data); });
}

/* ---- argument validation ------------------------------------------------ */

bool ValidateInputBuffer(const Napi::CallbackInfo& info, std::string* error) {
    if (info.Length() < 1 || !info[0].IsBuffer()) {
        *error = "First argument must be a Buffer";
        return false;
    }
    size_t length = info[0].As<Napi::Buffer<char>>().Length();
    if (length == 0) {
        *error = "Input buffer must not be empty";
        return false;
    }
    if (length > kMaxInputLength) {
        *error = "Input buffer exceeds 1 GiB limit";
        return false;
    }
    return true;
}

/* expectedLength is required: the legacy default silently allocated a
 * 999 MB buffer per call, which is a footgun rather than a feature. */
bool ValidateExpectedLength(const Napi::CallbackInfo& info, size_t* expectedLength,
                            std::string* error, bool* rangeError) {
    *rangeError = false;
    if (info.Length() < 2 || !info[1].IsNumber()) {
        *error = "Second argument (expectedLength) must be a number";
        return false;
    }
    double raw = info[1].As<Napi::Number>().DoubleValue();
    if (raw != raw /* NaN */ || raw != static_cast<double>(static_cast<int64_t>(raw))) {
        *rangeError = true;
        *error = "expectedLength must be an integer";
        return false;
    }
    int64_t value = static_cast<int64_t>(raw);
    if (value <= 0 || static_cast<uint64_t>(value) > kMaxOutputLength) {
        *rangeError = true;
        *error = "expectedLength must be between 1 and 1 GiB";
        return false;
    }
    *expectedLength = static_cast<size_t>(value);
    return true;
}

/* ---- async workers ------------------------------------------------------ */

class CompressWorker : public Napi::AsyncWorker {
  public:
    CompressWorker(Napi::Env env, Napi::Buffer<char> input)
        : Napi::AsyncWorker(env),
          deferred_(Napi::Promise::Deferred::New(env)),
          inputRef_(Napi::Persistent(input.As<Napi::Object>())),
          data_(input.Data()),
          length_(input.Length()) {}

    Napi::Promise Promise() { return deferred_.Promise(); }

  protected:
    void Execute() override {
        std::string error;
        if (!DoCompress(data_, length_, &result_, &error)) SetError(error);
    }

    void OnOK() override { deferred_.Resolve(WrapResult(Env(), &result_)); }

    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

  private:
    Napi::Promise::Deferred deferred_;
    Napi::ObjectReference inputRef_;  // keeps the input buffer alive while the worker runs
    const char* data_;
    size_t length_;
    RawResult result_;
};

class DecompressWorker : public Napi::AsyncWorker {
  public:
    DecompressWorker(Napi::Env env, Napi::Buffer<char> input, size_t expectedLength)
        : Napi::AsyncWorker(env),
          deferred_(Napi::Promise::Deferred::New(env)),
          inputRef_(Napi::Persistent(input.As<Napi::Object>())),
          data_(input.Data()),
          length_(input.Length()),
          expectedLength_(expectedLength) {}

    Napi::Promise Promise() { return deferred_.Promise(); }

  protected:
    void Execute() override {
        std::string error;
        if (!DoDecompress(data_, length_, expectedLength_, &result_, &error)) SetError(error);
    }

    void OnOK() override { deferred_.Resolve(WrapResult(Env(), &result_)); }

    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

  private:
    Napi::Promise::Deferred deferred_;
    Napi::ObjectReference inputRef_;
    const char* data_;
    size_t length_;
    size_t expectedLength_;
    RawResult result_;
};

/* ---- sync API ----------------------------------------------------------- */

Napi::Value Compress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::string error;
    if (!ValidateInputBuffer(info, &error)) {
        Napi::TypeError::New(env, error).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<char> input = info[0].As<Napi::Buffer<char>>();
    RawResult result;
    if (!DoCompress(input.Data(), input.Length(), &result, &error)) {
        Napi::Error::New(env, error).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return WrapResult(env, &result);
}

Napi::Value Decompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::string error;
    bool rangeError = false;
    size_t expectedLength = 0;
    if (!ValidateInputBuffer(info, &error)) {
        Napi::TypeError::New(env, error).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!ValidateExpectedLength(info, &expectedLength, &error, &rangeError)) {
        if (rangeError) Napi::RangeError::New(env, error).ThrowAsJavaScriptException();
        else Napi::TypeError::New(env, error).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<char> input = info[0].As<Napi::Buffer<char>>();
    RawResult result;
    if (!DoDecompress(input.Data(), input.Length(), expectedLength, &result, &error)) {
        Napi::Error::New(env, error).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return WrapResult(env, &result);
}

/* ---- async API (libuv thread pool; never blocks the event loop) --------- */

Napi::Value RejectedPromise(Napi::Env env, const Napi::Error& error) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(error.Value());
    return deferred.Promise();
}

Napi::Value CompressAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::string error;
    if (!ValidateInputBuffer(info, &error)) {
        return RejectedPromise(env, Napi::TypeError::New(env, error));
    }

    auto* worker = new CompressWorker(env, info[0].As<Napi::Buffer<char>>());
    Napi::Promise promise = worker->Promise();
    worker->Queue();
    return promise;
}

Napi::Value DecompressAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::string error;
    bool rangeError = false;
    size_t expectedLength = 0;
    if (!ValidateInputBuffer(info, &error)) {
        return RejectedPromise(env, Napi::TypeError::New(env, error));
    }
    if (!ValidateExpectedLength(info, &expectedLength, &error, &rangeError)) {
        if (rangeError) return RejectedPromise(env, Napi::RangeError::New(env, error));
        return RejectedPromise(env, Napi::TypeError::New(env, error));
    }

    auto* worker = new DecompressWorker(env, info[0].As<Napi::Buffer<char>>(), expectedLength);
    Napi::Promise promise = worker->Promise();
    worker->Queue();
    return promise;
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
