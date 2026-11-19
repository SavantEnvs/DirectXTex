// TGA reader: GetMetadataFromTGAMemory + LoadFromTGAMemory (RLE/uncompressed/colour-mapped, TGA 2.0
// extension area). The first byte picks the TGA_FLAGS; the rest is the .tga file.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    auto flags = static_cast<TGA_FLAGS>(r.byte() & 0x3F);
    TexMetadata meta{};
    GetMetadataFromTGAMemory(r.p, r.n, flags, meta);
    ScratchImage img;
    LoadFromTGAMemory(r.p, r.n, flags, &meta, img);
    return 0;
}
