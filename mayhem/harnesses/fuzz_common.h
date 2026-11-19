// Shared helpers for the DirectXTex fuzz harnesses (mayhem/harnesses).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "DirectXTex.h"

namespace fuzz {

// Small cursor that peels selector bytes off the front of the input.
struct Reader {
    const uint8_t* p;
    size_t n;
    uint8_t byte() { if (!n) return 0; uint8_t b = *p++; --n; return b; }
    uint32_t u32() { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(byte()) << (8 * i); return v; }
};

// Pipeline harnesses (convert/resize/compress/...) only process images up to this many pixels per
// surface and this many surfaces in total: the loaders accept textures far larger than one fuzz
// iteration can process in reasonable time. The loaders themselves are fuzzed without a cap.
constexpr size_t kMaxPixels = 64 * 64 * 4;
constexpr size_t kMaxImages = 64;

inline bool small_enough(const DirectX::TexMetadata& m) {
    if (m.width == 0 || m.height == 0 || m.depth == 0) return false;
    if (m.width * m.height > kMaxPixels || m.depth > 64) return false;
    if (m.arraySize * m.mipLevels * m.depth > kMaxImages) return false;
    return true;
}

// Load DDS/TGA/HDR from memory; `which` picks the codec (0=DDS, 1=TGA, 2=HDR).
inline HRESULT load_any(unsigned which, uint32_t flagbits, const uint8_t* d, size_t n,
                        DirectX::TexMetadata& meta, DirectX::ScratchImage& img) {
    using namespace DirectX;
    switch (which % 3) {
    case 0:
        return LoadFromDDSMemory(d, n, static_cast<DDS_FLAGS>(flagbits & 0x1FFFF & ~DDS_FLAGS_ALLOW_LARGE_FILES), &meta, img);
    case 1:
        return LoadFromTGAMemory(d, n, static_cast<TGA_FLAGS>(flagbits & 0x3F), &meta, img);
    default:
        return LoadFromHDRMemory(d, n, &meta, img);
    }
}

// Load, then decompress BC formats and flatten planar formats, so the result is a plain
// uncompressed image the processing functions accept.
inline bool load_plain(unsigned which, uint32_t flagbits, const uint8_t* d, size_t n,
                       DirectX::TexMetadata& meta, DirectX::ScratchImage& out) {
    using namespace DirectX;
    ScratchImage img;
    if (FAILED(load_any(which, flagbits, d, n, meta, img))) return false;
    meta = img.GetMetadata();
    if (!small_enough(meta)) return false;
    if (IsCompressed(meta.format)) {
        ScratchImage dec;
        if (FAILED(Decompress(img.GetImages(), img.GetImageCount(), meta, DXGI_FORMAT_UNKNOWN, dec))) return false;
        img = std::move(dec);
    } else if (IsPlanar(meta.format)) {
        ScratchImage sp;
        if (FAILED(ConvertToSinglePlane(img.GetImages(), img.GetImageCount(), meta, sp))) return false;
        img = std::move(sp);
    }
    if (IsTypeless(img.GetMetadata().format) || IsPalettized(img.GetMetadata().format)) return false;
    meta = img.GetMetadata();
    out = std::move(img);
    return true;
}

// Writes the input to a private scratch file under $TMPDIR (default /tmp) for the *File APIs.
struct TempFile {
    std::string path;
    std::wstring wpath;
    TempFile(const uint8_t* d, size_t n, const char* suffix) {
        const char* t = getenv("TMPDIR");
        std::string tmpl = std::string(t && *t ? t : "/tmp") + "/dxtexfuzzXXXXXX" + suffix;
        std::string buf = tmpl;
        int fd = mkstemps(&buf[0], (int)strlen(suffix));
        if (fd < 0) abort();
        size_t off = 0;
        while (off < n) { ssize_t w = write(fd, d + off, n - off); if (w <= 0) abort(); off += (size_t)w; }
        close(fd);
        path = buf;
        wpath.assign(path.begin(), path.end());
    }
    ~TempFile() { unlink(path.c_str()); }
};

} // namespace fuzz
