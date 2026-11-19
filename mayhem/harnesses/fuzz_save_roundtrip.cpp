// Load (DDS/TGA/HDR) -> SaveToDDSMemory / SaveToTGAMemory / SaveToHDRMemory -> reload the DDS blob
// (the writers in DirectXTexDDS/TGA/HDR.cpp). Byte 0 picks the loader, bytes 1-4 its flags,
// bytes 5-8 the DDS_FLAGS used when saving.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    unsigned which = r.byte();
    uint32_t lflags = r.u32();
    auto sflags = static_cast<DDS_FLAGS>(r.u32() & 0x1FFFF & ~DDS_FLAGS_ALLOW_LARGE_FILES);
    TexMetadata meta{};
    ScratchImage img;
    if (FAILED(fuzz::load_any(which, lflags, r.p, r.n, meta, img))) return 0;
    meta = img.GetMetadata();
    if (!fuzz::small_enough(meta)) return -1;

    Blob blob;
    if (SUCCEEDED(SaveToDDSMemory(img.GetImages(), img.GetImageCount(), meta, sflags, blob))) {
        TexMetadata m2{};
        ScratchImage back;
        LoadFromDDSMemory(blob.GetConstBufferPointer(), blob.GetBufferSize(), DDS_FLAGS_NONE, &m2, back);
    }
    const Image* first = img.GetImage(0, 0, 0);
    if (!first) return 0;
    Blob tga;
    if (SUCCEEDED(SaveToTGAMemory(*first, TGA_FLAGS_NONE, tga, &meta))) {
        ScratchImage back;
        LoadFromTGAMemory(tga.GetConstBufferPointer(), tga.GetBufferSize(), TGA_FLAGS_NONE, nullptr, back);
    }
    Blob hdr;
    if (SUCCEEDED(SaveToHDRMemory(*first, hdr))) {
        ScratchImage back;
        LoadFromHDRMemory(hdr.GetConstBufferPointer(), hdr.GetBufferSize(), nullptr, back);
    }
    return 0;
}
