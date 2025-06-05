#include <node_buffer.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

#include "nan.h"
#include "lzf/lzf.h"

using namespace v8;
using namespace node;

NAN_METHOD(compress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    char* dataIn = Buffer::Data(info[0]);
    uint32_t lenIn = Buffer::Length(info[0]);

    uint32_t maxCompressedLen = lenIn + (lenIn / 16) + 64 + 3;

    // Allocate Node.js-managed buffer
    auto maybeBuf = Nan::NewBuffer(maxCompressedLen);
    if (maybeBuf.IsEmpty()) {
        return Nan::ThrowError("Failed to allocate output buffer");
    }

    Local<Object> outBuf = maybeBuf.ToLocalChecked();
    char* outData = Buffer::Data(outBuf);

    unsigned int compressedLen = lzf_compress(dataIn, lenIn, outData, maxCompressedLen);
    if (compressedLen == 0) {
        return Nan::ThrowError("Compression failed");
    }

    // Slice buffer to actual compressed length (no copy)
    info.GetReturnValue().Set(outBuf->Get(Nan::GetCurrentContext(), Nan::New("slice").ToLocalChecked())
        .ToLocalChecked().As<Function>()->Call(Nan::GetCurrentContext(), outBuf, 2,
            new Local<Value>[2]{
                Nan::New(0),
                Nan::New((uint32_t)compressedLen)
            }).ToLocalChecked());
}

NAN_METHOD(decompress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    char* dataIn = Buffer::Data(info[0]);
    uint32_t lenIn = Buffer::Length(info[0]);

    uint32_t expectedOutLen = 999 * 1024 * 1024;
    if (info.Length() > 1 && info[1]->IsNumber()) {
        expectedOutLen = Nan::To<uint32_t>(info[1]).FromJust();
    }

    auto maybeBuf = Nan::NewBuffer(expectedOutLen);
    if (maybeBuf.IsEmpty()) {
        return Nan::ThrowError("Failed to allocate output buffer");
    }

    Local<Object> outBuf = maybeBuf.ToLocalChecked();
    char* outData = Buffer::Data(outBuf);

    unsigned int decompressedLen = lzf_decompress(dataIn, lenIn, outData, expectedOutLen);
    if (decompressedLen == 0) {
        return Nan::ThrowError("Decompression failed");
    }

    // Slice to decompressed size (no copy)
    info.GetReturnValue().Set(outBuf->Get(Nan::GetCurrentContext(), Nan::New("slice").ToLocalChecked())
        .ToLocalChecked().As<Function>()->Call(Nan::GetCurrentContext(), outBuf, 2,
            new Local<Value>[2]{
                Nan::New(0),
                Nan::New((uint32_t)decompressedLen)
            }).ToLocalChecked());
}

extern "C" void init(Local<Object> exports) {
    Nan::SetMethod(exports, "compress", compress);
    Nan::SetMethod(exports, "decompress", decompress);
}

NODE_MODULE(lzf, init)
