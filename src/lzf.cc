/* node-lzf (C) 2011 Ian Babrou <ibobrik@gmail.com>  */

#include <node_buffer.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

#include "nan.h"

#include "lzf/lzf.h"


using namespace v8;
using namespace node;


// Handle<Value> ThrowNodeError(const char* what = NULL) {
//     return Nan::ThrowError(Exception::Error(Nan::New<String>(what)));
// }
NAN_METHOD(compress) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    Local<Value> bufferIn  = info[0];
    size_t bytesIn         = Buffer::Length(bufferIn);
    char * dataPointer     = Buffer::Data(bufferIn);
    size_t bytesCompressed = bytesIn + (bytesIn / 16) + 64 + 3;
    char * bufferOut       = (char*) malloc(bytesCompressed);

    if (!bufferOut) {
        return Nan::ThrowError("LZF malloc failed!");
    }

    unsigned result = lzf_compress(dataPointer, bytesIn, bufferOut, bytesCompressed);

    if (!result) {
        free(bufferOut);
        return Nan::ThrowError("Compression failed");
    }

    // Optional: shrink allocation
    char* finalBuffer = (char*) malloc(result);
    memcpy(finalBuffer, bufferOut, result);
    free(bufferOut);

    info.GetReturnValue().Set(
        Nan::NewBuffer(finalBuffer, result, [](char* data, void*) {
            free(data);
        }, nullptr).ToLocalChecked()
    );
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

    char * bufferOut = (char*) malloc(bytesUncompressed);
    if (!bufferOut) {
        return Nan::ThrowError("LZF malloc failed!");
    }

    unsigned result = lzf_decompress(Buffer::Data(bufferIn), Buffer::Length(bufferIn), bufferOut, bytesUncompressed);

    if (!result) {
        free(bufferOut);
        return Nan::ThrowError("Decompression failed");
    }

    char* finalBuffer = (char*) malloc(result);
    memcpy(finalBuffer, bufferOut, result);
    free(bufferOut);

    info.GetReturnValue().Set(
        Nan::NewBuffer(finalBuffer, result, [](char* data, void*) {
            free(data);
        }, nullptr).ToLocalChecked()
    );
}

extern "C" void
init (Handle<Object> target) {
    Nan::SetMethod(target, "compress", compress);
    Nan::SetMethod(target, "decompress", decompress);
}

NODE_MODULE(lzf, init)
