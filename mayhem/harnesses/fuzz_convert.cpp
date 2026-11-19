// Load (DDS/TGA/HDR) -> Convert: LoadScanline/StoreScanline for every DXGI format, dithering and
// filters (DirectXTexConvert.cpp), plus Decompress/ConvertToSinglePlane for BC and planar inputs.
// Byte 0 picks the loader, bytes 1-4 its flags, byte 5 the target DXGI format, bytes 6-9 the
// TEX_FILTER flags; the rest is the image file.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    unsigned which = r.byte();
    uint32_t lflags = r.u32();
    auto target = static_cast<DXGI_FORMAT>(r.byte() % 192);
    // Every TEX_FILTER bit except FORCE_WIC (WIC does not exist off Windows).
    auto filter = static_cast<TEX_FILTER_FLAGS>(r.u32() & 0x13FFF377u);
    TexMetadata meta{};
    ScratchImage img;
    if (!fuzz::load_plain(which, lflags, r.p, r.n, meta, img)) return -1;
    ScratchImage out;
    Convert(img.GetImages(), img.GetImageCount(), meta, target, filter, TEX_THRESHOLD_DEFAULT, out);
    return 0;
}
