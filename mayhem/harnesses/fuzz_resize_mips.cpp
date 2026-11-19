// Load -> Resize -> GenerateMipMaps / GenerateMipMaps3D / ScaleMipMapsAlphaForCoverage
// (DirectXTexResize.cpp, DirectXTexMipmaps.cpp, filters.h). Byte 0 picks the loader, bytes 1-4
// its flags, bytes 5-6 the new width/height, bytes 7-10 the TEX_FILTER flags, byte 11 the mip count.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    unsigned which = r.byte();
    uint32_t lflags = r.u32();
    size_t w = (r.byte() % 64) + 1, h = (r.byte() % 64) + 1;
    auto filter = static_cast<TEX_FILTER_FLAGS>(r.u32() & 0x13FFF377u);
    size_t levels = r.byte() % 8;
    TexMetadata meta{};
    ScratchImage img;
    if (!fuzz::load_plain(which, lflags, r.p, r.n, meta, img)) return -1;

    ScratchImage resized;
    if (meta.dimension != TEX_DIMENSION_TEXTURE3D &&
        SUCCEEDED(Resize(img.GetImages(), img.GetImageCount(), meta, w, h, filter, resized))) {
        img = std::move(resized);
        meta = img.GetMetadata();
    }
    // Mipmap generation works on the top level only.
    ScratchImage base;
    if (meta.mipLevels > 1) {
        TexMetadata m = meta; m.mipLevels = 1;
        if (FAILED(base.Initialize(m))) return 0;
        for (size_t item = 0; item < (meta.dimension == TEX_DIMENSION_TEXTURE3D ? meta.depth : meta.arraySize); ++item) {
            const Image* s = meta.dimension == TEX_DIMENSION_TEXTURE3D ? img.GetImage(0, 0, item) : img.GetImage(0, item, 0);
            const Image* d = meta.dimension == TEX_DIMENSION_TEXTURE3D ? base.GetImage(0, 0, item) : base.GetImage(0, item, 0);
            if (!s || !d) return 0;
            CopyRectangle(*s, Rect(0, 0, s->width, s->height), *d, TEX_FILTER_DEFAULT, 0, 0);
        }
        img = std::move(base);
        meta = img.GetMetadata();
    }
    ScratchImage mips;
    HRESULT hr;
    if (meta.dimension == TEX_DIMENSION_TEXTURE3D)
        hr = GenerateMipMaps3D(img.GetImages(), img.GetImageCount(), meta, filter, levels, mips);
    else
        hr = GenerateMipMaps(img.GetImages(), img.GetImageCount(), meta, filter, levels, mips);
    if (SUCCEEDED(hr) && meta.dimension != TEX_DIMENSION_TEXTURE3D && HasAlpha(meta.format)) {
        ScratchImage cov;
        ScaleMipMapsAlphaForCoverage(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), 0, 0.5f, cov);
    }
    return 0;
}
