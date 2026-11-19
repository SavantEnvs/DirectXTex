// DDS -> Decompress: BC1-BC7 block decoders (BC.cpp, BC4BC5.cpp, BC6HBC7.cpp, DirectXTexCompress.cpp).
// The first 4 bytes pick DDS_FLAGS and a decompress target; the rest is the .dds file.
#include "fuzz_common.h"
using namespace DirectX;

static const DXGI_FORMAT kTargets[] = {
    DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8_UNORM,
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    uint32_t sel = r.u32();
    TexMetadata meta{};
    ScratchImage img;
    if (FAILED(LoadFromDDSMemory(r.p, r.n, static_cast<DDS_FLAGS>(sel & 0x1FFFF & ~DDS_FLAGS_ALLOW_LARGE_FILES), &meta, img)))
        return 0;
    meta = img.GetMetadata();
    if (!IsCompressed(meta.format) || !fuzz::small_enough(meta)) return 0;
    ScratchImage out;
    Decompress(img.GetImages(), img.GetImageCount(), meta, kTargets[(sel >> 24) % 6], out);
    return 0;
}
