// texconv's PPM (P6) and PFM (Pf/PF) readers: LoadFromPortablePixMap + LoadFromPortablePixMapHDR
// from Texconv/PortablePixMap.cpp, compiled unmodified against mayhem/harnesses/winshim/Windows.h.
// Byte 0 picks the reader; the rest is the file.
#include "fuzz_common.h"
using namespace DirectX;

HRESULT __cdecl LoadFromPortablePixMap(const wchar_t* szFile, TexMetadata* metadata, ScratchImage& image) noexcept;
HRESULT __cdecl LoadFromPortablePixMapHDR(const wchar_t* szFile, TexMetadata* metadata, ScratchImage& image) noexcept;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    bool hdr = r.byte() & 1;
    fuzz::TempFile f(r.p, r.n, hdr ? ".pfm" : ".ppm");
    TexMetadata meta{};
    ScratchImage img;
    if (hdr)
        LoadFromPortablePixMapHDR(f.wpath.c_str(), &meta, img);
    else
        LoadFromPortablePixMap(f.wpath.c_str(), &meta, img);
    return 0;
}
