// Load -> PremultiplyAlpha / ComputeNormalMap / EvaluateImage / TransformImage / ComputeMSE /
// IsAlphaAllOpaque (DirectXTexPMAlpha.cpp, DirectXTexNormalMaps.cpp, DirectXTexMisc.cpp).
// Byte 0 picks the loader, bytes 1-4 its flags, byte 5 the operation, bytes 6-9 its flags.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    unsigned which = r.byte();
    uint32_t lflags = r.u32();
    unsigned op = r.byte() % 4;
    uint32_t oflags = r.u32();
    TexMetadata meta{};
    ScratchImage img;
    if (!fuzz::load_plain(which, lflags, r.p, r.n, meta, img)) return -1;
    img.IsAlphaAllOpaque();
    ScratchImage out;
    switch (op) {
    case 0:
        PremultiplyAlpha(img.GetImages(), img.GetImageCount(), meta,
                         static_cast<TEX_PMALPHA_FLAGS>(oflags & 0x3000003u), out);
        break;
    case 1:
        ComputeNormalMap(img.GetImages(), img.GetImageCount(), meta,
                         static_cast<CNMAP_FLAGS>(oflags & 0xF007u), 2.0f,
                         (oflags & 0x80000000u) ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R32G32B32A32_FLOAT, out);
        break;
    case 2: {
        float acc = 0;
        EvaluateImage(img.GetImages(), img.GetImageCount(), meta,
                      [&](const XMVECTOR* px, size_t w, size_t) { for (size_t i = 0; i < w; ++i) acc += XMVectorGetX(px[i]); });
        TransformImage(img.GetImages(), img.GetImageCount(), meta,
                       [](XMVECTOR* o, const XMVECTOR* in, size_t w, size_t) { for (size_t i = 0; i < w; ++i) o[i] = XMVectorSwizzle<3, 2, 1, 0>(in[i]); },
                       out);
        break;
    }
    default: {
        const Image* a = img.GetImage(0, 0, 0);
        if (!a) break;
        float mse = 0, v[4];
        ComputeMSE(*a, *a, mse, v, static_cast<CMSE_FLAGS>(oflags & 0x3F3u));
        break;
    }
    }
    return 0;
}
