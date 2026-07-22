/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */
/* node-lzf (C) 2025 Maintained Türkay Tanrikulu <trky.shorty@gmail.com>  */

#include <napi.h>
#include "lzf/lzf.h"

namespace {

Napi::Value Compress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "First argument must be a Buffer").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<char> input = info[0].As<Napi::Buffer<char>>();
    size_t lenIn = input.Length();

    size_t maxCompressedLen = lenIn + (lenIn / 16) + 64 + 3;
    Napi::Buffer<char> output = Napi::Buffer<char>::New(env, maxCompressedLen);

    unsigned int compressedLen = lzf_compress(
        input.Data(), (unsigned int)lenIn,
        output.Data(), (unsigned int)maxCompressedLen);

    if (compressedLen == 0) {
        Napi::Error::New(env, "Compression failed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    /* Copy to an exact-sized buffer instead of returning a slice view, so the
     * oversized scratch allocation is not kept alive by the returned Buffer. */
    return Napi::Buffer<char>::Copy(env, output.Data(), compressedLen);
}

Napi::Value Decompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "First argument must be a Buffer").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<char> input = info[0].As<Napi::Buffer<char>>();

    size_t expectedOutLen = 999 * 1024 * 1024;
    if (info.Length() > 1 && info[1].IsNumber()) {
        expectedOutLen = info[1].As<Napi::Number>().Uint32Value();
    }

    Napi::Buffer<char> output = Napi::Buffer<char>::New(env, expectedOutLen);

    unsigned int decompressedLen = lzf_decompress(
        input.Data(), (unsigned int)input.Length(),
        output.Data(), (unsigned int)expectedOutLen);

    if (decompressedLen == 0) {
        Napi::Error::New(env, "Decompression failed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if ((size_t)decompressedLen == expectedOutLen) {
        return output;
    }

    return Napi::Buffer<char>::Copy(env, output.Data(), decompressedLen);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("compress", Napi::Function::New(env, Compress));
    exports.Set("decompress", Napi::Function::New(env, Decompress));
    return exports;
}

}  // namespace

NODE_API_MODULE(lzf, Init)
