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
    if (info.Length() < 1 || !info[0]->IsObject() || !node::Buffer::HasInstance(info[0])) {
        return Nan::ThrowTypeError("First argument must be a Buffer");
    }

    Local<Object> inBuf = Nan::To<Object>(info[0]).ToLocalChecked();
    char* dataIn = node::Buffer::Data(inBuf);
    size_t lenIn = node::Buffer::Length(inBuf);

    size_t maxCompressedLen = lenIn + (lenIn / 16) + 64 + 3;
    auto maybeBuf = Nan::NewBuffer(maxCompressedLen);
    if (maybeBuf.IsEmpty()) {
        return Nan::ThrowError("Failed to allocate output buffer");
    }

    Local<Object> outBuf = maybeBuf.ToLocalChecked();
    char* outData = node::Buffer::Data(outBuf);

    unsigned int compressedLen = lzf_compress(dataIn, lenIn, outData, maxCompressedLen);
    if (compressedLen == 0) {
        return Nan::ThrowError("Compression failed");
    }

    // Avoid .Get + .Call + new[]
    Local<Function> sliceFn = outBuf->Get(Nan::GetCurrentContext(), Nan::New("slice").ToLocalChecked())
        .ToLocalChecked().As<Function>();
    Local<Value> args[] = {
        Nan::New(0),
        Nan::New((uint32_t)compressedLen)
    };
    Local<Value> sliced = sliceFn->Call(Nan::GetCurrentContext(), outBuf, 2, args).ToLocalChecked();

    info.GetReturnValue().Set(sliced);
}

NAN_METHOD(decompress) {
    if (info.Length() < 1 || !info[0]->IsObject() || !node::Buffer::HasInstance(info[0])) {
        return Nan::ThrowTypeError("First argument must be a Buffer");
    }

    Local<Object> inBuf = Nan::To<Object>(info[0]).ToLocalChecked();
    char* dataIn = node::Buffer::Data(inBuf);
    size_t lenIn = node::Buffer::Length(inBuf);

    size_t expectedOutLen = 999 * 1024 * 1024;
    if (info.Length() > 1 && info[1]->IsNumber()) {
        expectedOutLen = Nan::To<uint32_t>(info[1]).FromJust();
    }

    auto maybeBuf = Nan::NewBuffer(expectedOutLen);
    if (maybeBuf.IsEmpty()) {
        return Nan::ThrowError("Failed to allocate output buffer");
    }

    Local<Object> outBuf = maybeBuf.ToLocalChecked();
    char* outData = node::Buffer::Data(outBuf);

    unsigned int decompressedLen = lzf_decompress(dataIn, lenIn, outData, expectedOutLen);
    if (decompressedLen == 0) {
        return Nan::ThrowError("Decompression failed");
    }

    Local<Function> sliceFn = outBuf->Get(Nan::GetCurrentContext(), Nan::New("slice").ToLocalChecked())
        .ToLocalChecked().As<Function>();
    Local<Value> args[] = {
        Nan::New(0),
        Nan::New((uint32_t)decompressedLen)
    };
    Local<Value> sliced = sliceFn->Call(Nan::GetCurrentContext(), outBuf, 2, args).ToLocalChecked();

    info.GetReturnValue().Set(sliced);
}

NAN_MODULE_INIT(init) {
    Nan::SetMethod(target, "compress", compress);
    Nan::SetMethod(target, "decompress", decompress);
}

NODE_MODULE(lzf, init)
