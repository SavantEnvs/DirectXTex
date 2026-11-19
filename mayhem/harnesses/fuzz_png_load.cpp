// libpng glue (Auxiliary/DirectXTexPNG.cpp, ENABLE_LIBPNG_SUPPORT): GetMetadataFromPNGFile +
// LoadFromPNGFile. Byte 0 picks the PNG_FLAGS; the rest is the .png file.
#include "fuzz_common.h"
#include "DirectXTexPNG.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    auto flags = static_cast<PNG_FLAGS>(r.byte() & 0x67);
    fuzz::TempFile f(r.p, r.n, ".png");
    TexMetadata meta{};
    GetMetadataFromPNGFile(f.wpath.c_str(), flags, meta);
    ScratchImage img;
    LoadFromPNGFile(f.wpath.c_str(), flags, &meta, img);
    return 0;
}
