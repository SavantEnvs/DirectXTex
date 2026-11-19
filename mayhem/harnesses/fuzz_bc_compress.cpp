// Load -> Compress to BC1..BC5 (BC.cpp / BC4BC5.cpp encoders, DirectXTexCompress.cpp). BC6H/BC7
// encoders are excluded: seconds per small image is too slow for fuzzing. Byte 0 picks the loader,
// bytes 1-4 its flags, byte 5 the BC format, bytes 6-9 the TEX_COMPRESS flags.
#include "fuzz_common.h"
using namespace DirectX;

static const DXGI_FORMAT kBC[] = {
    DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM_SRGB,
    DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_SNORM,
    DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_SNORM,
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    unsigned which = r.byte();
    uint32_t lflags = r.u32();
    DXGI_FORMAT fmt = kBC[r.byte() % 10];
    // TEX_COMPRESS_PARALLEL is left out: built without OpenMP.
    auto cflags = static_cast<TEX_COMPRESS_FLAGS>(r.u32() & 0x30F00FFu);
    TexMetadata meta{};
    ScratchImage img;
    if (!fuzz::load_plain(which, lflags, r.p, r.n, meta, img)) return -1;
    ScratchImage out;
    if (SUCCEEDED(Compress(img.GetImages(), img.GetImageCount(), meta, fmt, cflags, TEX_THRESHOLD_DEFAULT, out))) {
        ScratchImage back;
        Decompress(out.GetImages(), out.GetImageCount(), out.GetMetadata(), DXGI_FORMAT_UNKNOWN, back);
    }
    return 0;
}
