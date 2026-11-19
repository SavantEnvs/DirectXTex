// libjpeg glue (Auxiliary/DirectXTexJPEG.cpp, ENABLE_LIBJPEG_SUPPORT): GetMetadataFromJPEGFile +
// LoadFromJPEGFile. Byte 0 picks the JPEG_FLAGS; the rest is the .jpg file.
#include "fuzz_common.h"
#include "DirectXTexJPEG.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    auto flags = static_cast<JPEG_FLAGS>(r.byte() & 0x1);
    fuzz::TempFile f(r.p, r.n, ".jpg");
    TexMetadata meta{};
    GetMetadataFromJPEGFile(f.wpath.c_str(), flags, meta);
    ScratchImage img;
    LoadFromJPEGFile(f.wpath.c_str(), flags, &meta, img);
    return 0;
}
