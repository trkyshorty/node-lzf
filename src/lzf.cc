/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */

#include <node_buffer.h>
#include <nan.h>
#include "lzf/lzf.h"

using namespace v8;
using namespace node;

inline void ThrowNodeError(const char* what) {
    Nan::ThrowError(Nan::New<String>(std::string(what) + "- Error: " + std::to_string(errno)).ToLocalChecked());
}

NAN_METHOD(compress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    Local<Value> bufferIn = info[0];
    size_t bytesIn = Buffer::Length(bufferIn);
    char* dataPointer = Buffer::Data(bufferIn);
    
    size_t bytesCompressed = bytesIn + 100;
    Nan::MaybeLocal<Object> BufferOut = Nan::NewBuffer(bytesCompressed);

    if (BufferOut.IsEmpty()) {
        return Nan::ThrowError("Buffer allocation failed!");
    }

    char* bufferOut = node::Buffer::Data(BufferOut.ToLocalChecked());

    unsigned result = lzf_compress(dataPointer, bytesIn, bufferOut, bytesCompressed);

    if (!result) {
        return ThrowNodeError("Compression failed, probably too small buffer");
    }

    info.GetReturnValue().Set(Nan::NewBuffer(bufferOut, result).ToLocalChecked());
}

NAN_METHOD(decompress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    Local<Value> bufferIn = info[0];
    size_t bytesUncompressed = 999 * 1024 * 1024;

    if (info.Length() > 1 && info[1]->IsNumber()) {
        bytesUncompressed = Nan::To<uint32_t>(info[1]).FromJust();
    }

    Nan::MaybeLocal<Object> BufferOut = Nan::NewBuffer(bytesUncompressed);

    if (BufferOut.IsEmpty()) {
        return Nan::ThrowError("Buffer allocation failed!");
    }

    char* bufferOut = node::Buffer::Data(BufferOut.ToLocalChecked());

    unsigned result = lzf_decompress(Buffer::Data(bufferIn), Buffer::Length(bufferIn), bufferOut, bytesUncompressed);

    if (!result) {
        return ThrowNodeError("Decompression failed, probably too small buffer");
    }

    info.GetReturnValue().Set(Nan::NewBuffer(bufferOut, result).ToLocalChecked());
}

extern "C" void init(Local<Object> exports, Local<Value> module, Local<Context> context) {
    Nan::HandleScope scope;

    if (!exports->IsObject() || exports->IsNull()) {
        Nan::ThrowTypeError("Target object is not valid");
        return;
    }

    Nan::SetMethod(exports.As<Object>(), "compress", compress);
    Nan::SetMethod(exports.As<Object>(), "decompress", decompress);
}

NODE_MODULE(lzf, init)
