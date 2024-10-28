/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */

#include <node_buffer.h>
#include <vector>

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

    Local<Value> bufferIn = info[0];
    size_t bytesIn = Buffer::Length(bufferIn);
    char *dataPointer = Buffer::Data(bufferIn);

    // Use vector instead of malloc
    std::vector<char> bufferOut(bytesIn + 100);

    unsigned result = lzf_compress(dataPointer, bytesIn, bufferOut.data(), bufferOut.size());

    if (!result) {
        return Nan::ThrowError("Compression failed, probably too small buffer");
    }

    // Resize vector to the actual size of compressed data
    bufferOut.resize(result);

    Nan::MaybeLocal<Object> BufferOut = Nan::NewBuffer(bufferOut.data(), bufferOut.size());

    info.GetReturnValue().Set(BufferOut.ToLocalChecked());
}

NAN_METHOD(decompress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    Local<Value> bufferIn = info[0];

    size_t bytesUncompressed = 999 * 1024 * 1024; // Max size V8 supports

    if (info.Length() > 1 && info[1]->IsNumber()) { // Accept dest buffer size
        bytesUncompressed = Nan::To<uint32_t>(info[1]).FromJust();
    }

    // Use vector instead of malloc
    std::vector<char> bufferOut(bytesUncompressed);

    unsigned result = lzf_decompress(Buffer::Data(bufferIn), Buffer::Length(bufferIn), bufferOut.data(), bufferOut.size());

    if (!result) {
        return Nan::ThrowError("Decompression failed, probably too small buffer");
    }

    // Resize vector to the actual size of decompressed data
    bufferOut.resize(result);

    Nan::MaybeLocal<Object> BufferOut = Nan::NewBuffer(bufferOut.data(), bufferOut.size());

    info.GetReturnValue().Set(BufferOut.ToLocalChecked());
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
