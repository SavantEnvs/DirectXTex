// Known-answer functional tests for DirectXTex's portable core (the commit image's test.sh oracle).
// Each test prints "PASS <name>" or "FAIL <name>: why"; mayhem/test.sh counts them.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DirectXTex.h"
#include "DirectXTexPNG.h"
#include "DirectXTexJPEG.h"

using namespace DirectX;

static int g_fail = 0;
#define CHECK(name, cond, why)                                    \
    do {                                                          \
        if (cond) printf("PASS %s\n", name);                      \
        else { printf("FAIL %s: %s\n", name, why); ++g_fail; }    \
    } while (0)

static bool make_rgba(ScratchImage& img, size_t w, size_t h) {
    if (FAILED(img.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1))) return false;
    const Image* im = img.GetImage(0, 0, 0);
    for (size_t y = 0; y < h; ++y)
        for (size_t x = 0; x < w; ++x) {
            uint8_t* p = im->pixels + y * im->rowPitch + x * 4;
            p[0] = uint8_t(x * 40); p[1] = uint8_t(y * 50); p[2] = uint8_t(x * 10 + y); p[3] = uint8_t(255 - x);
        }
    return true;
}

static bool same_pixels(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.format != b.format) return false;
    size_t rb = a.width * BitsPerPixel(a.format) / 8;
    for (size_t y = 0; y < a.height; ++y)
        if (memcmp(a.pixels + y * a.rowPitch, b.pixels + y * b.rowPitch, rb)) return false;
    return true;
}

int main() {
    ScratchImage src;
    bool ok = make_rgba(src, 4, 4);
    CHECK("make_image", ok, "Initialize2D failed");
    const Image& s = *src.GetImage(0, 0, 0);

    { // ComputePitch known answers
        size_t row = 0, slice = 0;
        HRESULT hr = ComputePitch(DXGI_FORMAT_R8G8B8A8_UNORM, 7, 3, row, slice);
        size_t brow = 0, bslice = 0;
        HRESULT hr2 = ComputePitch(DXGI_FORMAT_BC1_UNORM, 7, 3, brow, bslice);
        CHECK("compute_pitch", SUCCEEDED(hr) && row == 28 && slice == 84 && SUCCEEDED(hr2) && brow == 16 && bslice == 16,
              "unexpected pitch");
    }
    { // DDS writer: header layout + round trip
        Blob b;
        HRESULT hr = SaveToDDSMemory(s, DDS_FLAGS_NONE, b);
        const uint8_t* p = b.GetConstBufferPointer();
        bool hdr = SUCCEEDED(hr) && b.GetBufferSize() == 4 + 124 + 64 && memcmp(p, "DDS ", 4) == 0 &&
                   p[4] == 124 && p[12] == 4 && p[16] == 4;
        CHECK("dds_save_header", hdr, "bad DDS header/size");
        TexMetadata m{};
        ScratchImage back;
        hr = SUCCEEDED(hr) ? LoadFromDDSMemory(p, b.GetBufferSize(), DDS_FLAGS_NONE, &m, back) : hr;
        CHECK("dds_roundtrip", SUCCEEDED(hr) && m.width == 4 && m.height == 4 && m.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
              same_pixels(s, *back.GetImage(0, 0, 0)), "pixels differ after DDS round trip");
        HRESULT bad = LoadFromDDSMemory(p, 100, DDS_FLAGS_NONE, &m, back);
        CHECK("dds_truncated_rejected", FAILED(bad), "truncated DDS accepted");
    }
    { // TGA writer + reader
        Blob b;
        HRESULT hr = SaveToTGAMemory(s, TGA_FLAGS_NONE, b);
        const uint8_t* p = b.GetConstBufferPointer();
        CHECK("tga_save_header", SUCCEEDED(hr) && p[2] == 2 && p[12] == 4 && p[14] == 4 && p[16] == 32,
              "bad TGA header");
        TexMetadata m{};
        ScratchImage back;
        hr = SUCCEEDED(hr) ? LoadFromTGAMemory(p, b.GetBufferSize(), TGA_FLAGS_NONE, &m, back) : hr;
        CHECK("tga_roundtrip", SUCCEEDED(hr) && m.width == 4 && m.height == 4 && back.GetImage(0, 0, 0) &&
              same_pixels(s, *back.GetImage(0, 0, 0)), "pixels differ after TGA round trip");
    }
    { // HDR writer + reader (RGBE, lossy: compare with tolerance)
        ScratchImage f;
        HRESULT hr = Convert(s, DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, f);
        Blob b;
        hr = SUCCEEDED(hr) ? SaveToHDRMemory(*f.GetImage(0, 0, 0), b) : hr;
        bool magic = SUCCEEDED(hr) && b.GetBufferSize() > 10 && memcmp(b.GetConstBufferPointer(), "#?RADIANCE", 10) == 0;
        CHECK("hdr_save_header", magic, "missing #?RADIANCE");
        TexMetadata m{};
        ScratchImage back;
        hr = magic ? LoadFromHDRMemory(b.GetConstBufferPointer(), b.GetBufferSize(), &m, back) : E_FAIL;
        bool close = SUCCEEDED(hr) && m.width == 4 && m.height == 4;
        if (close) {
            const float* a = reinterpret_cast<const float*>(f.GetPixels());
            const float* c = reinterpret_cast<const float*>(back.GetPixels());
            for (size_t i = 0; i < 16; ++i)
                for (int k = 0; k < 3; ++k)
                    if (std::fabs(a[i * 4 + k] - c[i * 4 + k]) > 0.02f) close = false;
        }
        CHECK("hdr_roundtrip", close, "HDR pixels differ");
    }
    { // BC1 decode of a known block: color0 = pure red (0xF800), color1 = pure blue, all indices 0
        uint8_t block[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
        Image bi{};
        bi.width = 4; bi.height = 4; bi.format = DXGI_FORMAT_BC1_UNORM; bi.rowPitch = 8; bi.slicePitch = 8; bi.pixels = block;
        ScratchImage out;
        HRESULT hr = Decompress(bi, DXGI_FORMAT_R8G8B8A8_UNORM, out);
        bool red = SUCCEEDED(hr);
        if (red) {
            const uint8_t* p = out.GetPixels();
            for (int i = 0; i < 16; ++i)
                if (p[i * 4] != 255 || p[i * 4 + 1] != 0 || p[i * 4 + 2] != 0 || p[i * 4 + 3] != 255) red = false;
        }
        CHECK("bc1_decode_known_block", red, "BC1 block did not decode to opaque red");
    }
    { // BC1/BC3 encode of a flat colour survives an encode/decode round trip
        ScratchImage flat;
        flat.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, 1);
        for (size_t i = 0; i < 64; ++i) { uint8_t* p = flat.GetPixels() + i * 4; p[0] = 0; p[1] = 255; p[2] = 0; p[3] = 255; }
        bool good = true;
        for (DXGI_FORMAT fmt : {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM}) {
            ScratchImage c, d;
            if (FAILED(Compress(*flat.GetImage(0, 0, 0), fmt, TEX_COMPRESS_DEFAULT, TEX_THRESHOLD_DEFAULT, c)) ||
                FAILED(Decompress(*c.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, d)) ||
                c.GetPixelsSize() != (fmt == DXGI_FORMAT_BC1_UNORM ? 32u : 64u)) { good = false; continue; }
            for (size_t i = 0; i < 64; ++i) { const uint8_t* p = d.GetPixels() + i * 4; if (p[0] > 4 || p[1] < 250 || p[2] > 4) good = false; }
        }
        CHECK("bc_encode_roundtrip", good, "BC1/BC3 flat colour round trip failed");
    }
    { // Convert R8G8B8A8 -> float: 255 -> 1.0, 40 -> 40/255
        ScratchImage f;
        HRESULT hr = Convert(s, DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, f);
        const float* p = SUCCEEDED(hr) ? reinterpret_cast<const float*>(f.GetPixels()) : nullptr;
        CHECK("convert_to_float", p && p[3] == 1.0f && std::fabs(p[4] - 40.f / 255.f) < 1e-6f && p[0] == 0.0f,
              "wrong float values");
        ScratchImage g;
        hr = Convert(s, DXGI_FORMAT_B5G6R5_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, g);
        CHECK("convert_to_565", SUCCEEDED(hr) && g.GetMetadata().format == DXGI_FORMAT_B5G6R5_UNORM && g.GetPixelsSize() == 32,
              "B5G6R5 conversion failed");
    }
    { // Resize + mipmaps
        ScratchImage r;
        HRESULT hr = Resize(s, 2, 2, TEX_FILTER_POINT, r);
        CHECK("resize_point", SUCCEEDED(hr) && r.GetMetadata().width == 2 && r.GetMetadata().height == 2, "resize failed");
        ScratchImage mips;
        hr = GenerateMipMaps(s, TEX_FILTER_BOX, 0, mips);
        bool mok = SUCCEEDED(hr) && mips.GetMetadata().mipLevels == 3 && mips.GetImage(2, 0, 0) &&
                   mips.GetImage(2, 0, 0)->width == 1;
        if (mok) { // box filter of the whole 4x4 image: the alpha channel averages 255-x over x=0..3 -> 253.5
            uint8_t a = mips.GetImage(2, 0, 0)->pixels[3];
            mok = a == 253 || a == 254;
        }
        CHECK("generate_mipmaps", mok, "wrong mip chain");
    }
    { // Metrics + premultiplied alpha
        float mse = -1, v[4];
        HRESULT hr = ComputeMSE(s, s, mse, v);
        CHECK("compute_mse_self", SUCCEEDED(hr) && mse == 0.0f, "MSE of image with itself is not 0");
        ScratchImage pm;
        hr = PremultiplyAlpha(s, TEX_PMALPHA_IGNORE_SRGB, pm);
        // pixel (1,0): r=40, a=254 -> 40*254/255 ~= 39.84
        const uint8_t* p = SUCCEEDED(hr) ? pm.GetPixels() + 4 : nullptr;
        CHECK("premultiply_alpha", p && (p[0] == 39 || p[0] == 40) && p[3] == 254, "wrong premultiplied value");
        CHECK("alpha_not_opaque", !src.IsAlphaAllOpaque(), "IsAlphaAllOpaque wrong");
    }
    { // PNG + JPEG auxiliary loaders: round trip through a file
        std::wstring png = L"/tmp/dxtex_selftest.png", jpg = L"/tmp/dxtex_selftest.jpg";
        ScratchImage back;
        TexMetadata m{};
        HRESULT hr = SaveToPNGFile(s, PNG_FLAGS_NONE, png.c_str());
        hr = SUCCEEDED(hr) ? LoadFromPNGFile(png.c_str(), PNG_FLAGS_NONE, &m, back) : hr;
        CHECK("png_roundtrip", SUCCEEDED(hr) && m.width == 4 && back.GetImage(0, 0, 0) &&
              memcmp(back.GetImage(0, 0, 0)->pixels, s.pixels, 4) == 0, "PNG round trip failed");
        ScratchImage jb;
        hr = SaveToJPEGFile(s, JPEG_FLAGS_NONE, jpg.c_str());
        hr = SUCCEEDED(hr) ? LoadFromJPEGFile(jpg.c_str(), JPEG_FLAGS_NONE, &m, jb) : hr;
        CHECK("jpeg_roundtrip", SUCCEEDED(hr) && m.width == 4 && m.height == 4, "JPEG round trip failed");
        remove("/tmp/dxtex_selftest.png");
        remove("/tmp/dxtex_selftest.jpg");
    }
    printf("DONE %d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
