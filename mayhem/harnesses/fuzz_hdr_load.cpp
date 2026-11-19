// Radiance HDR reader: GetMetadataFromHDRMemory + LoadFromHDRMemory (header parse, RLE scanlines).
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    TexMetadata meta{};
    GetMetadataFromHDRMemory(data, size, meta);
    ScratchImage img;
    LoadFromHDRMemory(data, size, &meta, img);
    return 0;
}
