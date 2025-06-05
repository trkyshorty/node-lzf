/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */
/* node-lzf (C) 2025 Maintained Türkay Tanrikulu <trky.shorty@gmail.com>  */

#include <node_buffer.h>
#include "nan.h"
#include "lzf/lzf.h"

using namespace v8;
using namespace node;

NAN_METHOD(compress) {
    if (info.Length() < 1 || !info[0]->IsObject() || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowTypeError("First argument must be a Buffer");
    }

    Local<Object> inputBuf = Nan::To<Object>(info[0]).ToLocalChecked();
    char* inputData = Buffer::Data(inputBuf);
    size_t inputLen = Buffer::Length(inputBuf);

    size_t maxOutputLen = inputLen + (inputLen / 16) + 64 + 3;
    std::unique_ptr<char[]> output(new char[maxOutputLen]);

    unsigned int compressedLen = lzf_compress(inputData, inputLen, output.get(), maxOutputLen);
    if (compressedLen == 0) {
        return Nan::ThrowError("Compression failed");
    }

    info.GetReturnValue().Set(Nan::CopyBuffer(output.get(), compressedLen).ToLocalChecked());
}

NAN_METHOD(decompress) {
    if (info.Length() < 1 || !info[0]->IsObject() || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowTypeError("First argument must be a Buffer");
    }

    Local<Object> inputBuf = Nan::To<Object>(info[0]).ToLocalChecked();
    char* inputData = Buffer::Data(inputBuf);
    size_t inputLen = Buffer::Length(inputBuf);

    size_t expectedOutLen = 1024 * 1024 * 100;
    if (info.Length() > 1 && info[1]->IsNumber()) {
        expectedOutLen = Nan::To<uint32_t>(info[1]).FromJust();
    }

    std::unique_ptr<char[]> output(new char[expectedOutLen]);

    unsigned int decompressedLen = lzf_decompress(inputData, inputLen, output.get(), expectedOutLen);
    if (decompressedLen == 0) {
        return Nan::ThrowError("Decompression failed");
    }

    info.GetReturnValue().Set(Nan::CopyBuffer(output.get(), decompressedLen).ToLocalChecked());
}

NAN_MODULE_INIT(init) {
    Nan::SetMethod(target, "compress", compress);
    Nan::SetMethod(target, "decompress", decompress);
}

NODE_MODULE(lzf, init)