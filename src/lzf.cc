/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */
/* node-lzf (C) 2025 Maintained Türkay Tanrikulu <trky.shorty@gmail.com>  */

#include <node_buffer.h>
#include "nan.h"
#include "lzf/lzf.h"

using namespace v8;
using namespace node;

NAN_METHOD(compress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowTypeError("First argument must be a Buffer");
    }

    Local<Object> input = info[0].As<Object>();
    char* inputData = Buffer::Data(input);
    size_t inputLen = Buffer::Length(input);

    size_t maxOut = inputLen + (inputLen / 16) + 64 + 3;
    char* outBuf = new char[maxOut];

    unsigned int outLen = lzf_compress(inputData, inputLen, outBuf, maxOut);
    if (outLen == 0) {
        delete[] outBuf;
        return Nan::ThrowError("Compression failed");
    }

    info.GetReturnValue().Set(Nan::NewBuffer(outBuf, outLen, [](char* data, void*) {
        delete[] data;
    }, nullptr).ToLocalChecked());
}

NAN_METHOD(decompress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowTypeError("First argument must be a Buffer");
    }

    Local<Object> input = info[0].As<Object>();
    char* inputData = Buffer::Data(input);
    size_t inputLen = Buffer::Length(input);

    size_t expectedLen = 1024 * 1024 * 10;
    if (info.Length() > 1 && info[1]->IsNumber()) {
        expectedLen = Nan::To<uint32_t>(info[1]).FromJust();
    }

    char* outBuf = new char[expectedLen];
    unsigned int outLen = lzf_decompress(inputData, inputLen, outBuf, expectedLen);

    if (outLen == 0) {
        delete[] outBuf;
        return Nan::ThrowError("Decompression failed");
    }

    info.GetReturnValue().Set(Nan::NewBuffer(outBuf, outLen, [](char* data, void*) {
        delete[] data;
    }, nullptr).ToLocalChecked());
}


NAN_MODULE_INIT(init) {
    Nan::SetMethod(target, "compress", compress);
    Nan::SetMethod(target, "decompress", decompress);
}

NODE_MODULE(lzf, init)