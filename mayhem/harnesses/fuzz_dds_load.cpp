// DDS reader: GetMetadataFromDDSMemoryEx + LoadFromDDSMemoryEx (DirectXTexDDS.cpp, ComputePitch).
// The first 4 bytes pick the DDS_FLAGS; the rest is the .dds file.
#include "fuzz_common.h"
using namespace DirectX;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz::Reader r{data, size};
    // ALLOW_LARGE_FILES lifts upstream's own size limits; keep them so one input can't ask for GBs.
    auto flags = static_cast<DDS_FLAGS>(r.u32() & 0x1FFFF & ~DDS_FLAGS_ALLOW_LARGE_FILES);
    TexMetadata meta{};
    DDSMetaData ddpf{};
    GetMetadataFromDDSMemoryEx(r.p, r.n, flags, meta, &ddpf);
    ScratchImage img;
    if (SUCCEEDED(LoadFromDDSMemoryEx(r.p, r.n, flags, &meta, &ddpf, img))) {
        img.IsAlphaAllOpaque();
    }
    return 0;
}
