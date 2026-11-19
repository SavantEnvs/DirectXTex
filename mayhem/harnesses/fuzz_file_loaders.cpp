// The *File loaders (std::ifstream paths in DirectXTexDDS/TGA/HDR.cpp, separate from the Memory
// variants): GetMetadataFrom{DDS,TGA,HDR}File + LoadFrom{DDS,TGA,HDR}File(Ex) on the input written
// to a scratch file. Byte 0 picks the loader, bytes 1-4 its flags; the rest is the file.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    unsigned which = r.byte() % 3;
    uint32_t flags = r.u32();
    static const char* const kSuffix[] = {".dds", ".tga", ".hdr"};
    fuzz::TempFile f(r.p, r.n, kSuffix[which]);
    TexMetadata meta{};
    ScratchImage img;
    if (which == 0) {
        auto df = static_cast<DDS_FLAGS>(flags & 0x1FFFF & ~DDS_FLAGS_ALLOW_LARGE_FILES);
        DDSMetaData ddpf{};
        GetMetadataFromDDSFileEx(f.wpath.c_str(), df, meta, &ddpf);
        LoadFromDDSFileEx(f.wpath.c_str(), df, &meta, &ddpf, img);
    } else if (which == 1) {
        auto tf = static_cast<TGA_FLAGS>(flags & 0x3F);
        GetMetadataFromTGAFile(f.wpath.c_str(), tf, meta);
        LoadFromTGAFile(f.wpath.c_str(), tf, &meta, img);
    } else {
        GetMetadataFromHDRFile(f.wpath.c_str(), meta);
        LoadFromHDRFile(f.wpath.c_str(), &meta, img);
    }
    return 0;
}
