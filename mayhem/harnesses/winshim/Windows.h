// Minimal <Windows.h> stand-in so texconv's Texconv/PortablePixMap.cpp (upstream, unmodified) compiles
// on Linux for the fuzz_ppm_load harness. It maps the few Win32 file APIs that file uses onto POSIX.
// Only the harness build puts this directory on the include path.
#pragma once
// Standard headers first: sal.h (pulled in through DirectXTex.h) defines macros that clash with
// libstdc++ internals if <algorithm> & co. are parsed after it.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <tuple>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "DirectXTex.h"

#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | (7 << 16) | 0x80000000)))
#endif
#define ERROR_FILE_TOO_LARGE 223L
#define ERROR_HANDLE_EOF 38L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_ARITHMETIC_OVERFLOW 534L
#define ERROR_GEN_FAILURE 31L
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// Win32's LARGE_INTEGER has LowPart/HighPart; the DirectX-Headers stub does not, so use our own.
union WINSHIM_LARGE_INTEGER {
    struct { DWORD LowPart; int32_t HighPart; };
    int64_t QuadPart;
};
typedef struct _FILE_STANDARD_INFO {
    WINSHIM_LARGE_INTEGER AllocationSize;
    WINSHIM_LARGE_INTEGER EndOfFile;
    DWORD NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFO;
typedef struct _FILE_DISPOSITION_INFO { BOOLEAN DeleteFile; } FILE_DISPOSITION_INFO;
enum FILE_INFO_BY_HANDLE_CLASS { FileStandardInfo = 1, FileDispositionInfo = 4 };

namespace winshim {
inline thread_local DWORD last_error = 0;
struct File { int fd; std::string path; };
inline BOOL fail() { last_error = ERROR_GEN_FAILURE; return 0; }
}

inline DWORD GetLastError() { return winshim::last_error; }

inline HANDLE CreateFile2(const wchar_t* name, DWORD access, DWORD, DWORD disposition, void*) {
    std::string path = std::filesystem::path(name).string();
    int flags = (access & GENERIC_WRITE) ? (O_WRONLY | O_CREAT | O_TRUNC) : O_RDONLY;
    (void)disposition;
    int fd = open(path.c_str(), flags | O_CLOEXEC, 0600);
    if (fd < 0) { winshim::last_error = 2 /* ERROR_FILE_NOT_FOUND */; return INVALID_HANDLE_VALUE; }
    return new winshim::File{fd, path};
}

inline BOOL CloseHandle(HANDLE h) {
    auto* f = static_cast<winshim::File*>(h);
    close(f->fd);
    delete f;
    return 1;
}

inline BOOL GetFileInformationByHandleEx(HANDLE h, FILE_INFO_BY_HANDLE_CLASS cls, void* out, DWORD size) {
    if (cls != FileStandardInfo || size < sizeof(FILE_STANDARD_INFO)) return winshim::fail();
    struct stat st;
    if (fstat(static_cast<winshim::File*>(h)->fd, &st) != 0) return winshim::fail();
    auto* info = static_cast<FILE_STANDARD_INFO*>(out);
    memset(info, 0, sizeof(*info));
    info->EndOfFile.QuadPart = st.st_size;
    info->AllocationSize.QuadPart = st.st_size;
    return 1;
}

inline BOOL SetFileInformationByHandle(HANDLE h, FILE_INFO_BY_HANDLE_CLASS cls, void* in, DWORD) {
    if (cls == FileDispositionInfo && static_cast<FILE_DISPOSITION_INFO*>(in)->DeleteFile)
        return unlink(static_cast<winshim::File*>(h)->path.c_str()) == 0 ? 1 : winshim::fail();
    return winshim::fail();
}

inline BOOL ReadFile(HANDLE h, void* buf, DWORD n, DWORD* got, void*) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(static_cast<winshim::File*>(h)->fd, static_cast<char*>(buf) + off, n - off);
        if (r < 0) return winshim::fail();
        if (r == 0) break;
        off += (size_t)r;
    }
    if (got) *got = (DWORD)off;
    return 1;
}

inline BOOL WriteFile(HANDLE h, const void* buf, DWORD n, DWORD* put, void*) {
    ssize_t w = write(static_cast<winshim::File*>(h)->fd, buf, n);
    if (w < 0) return winshim::fail();
    if (put) *put = (DWORD)w;
    return 1;
}

// MSVC secure-CRT functions used by PortablePixMap.cpp. Where MSVC would call the invalid-parameter
// handler (which terminates the process by default), these abort().
template <size_t N>
inline int sprintf_s(char (&buf)[N], const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, N, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= N) abort();
    return r;
}

template <size_t N>
inline int strncpy_s(char (&dst)[N], const char* src, size_t count) {
    if (!src) abort();
    size_t n = strnlen(src, count);
    if (n >= N) abort();
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

// sscanf_s: every %s/%c/%[ conversion takes an extra buffer-size argument. Rewrite those into
// width-limited sscanf conversions and forward up to 8 pointers (%[ is not supported).
inline int sscanf_s(const char* buf, const char* fmt, ...) {
    std::string out;
    void* ptrs[8] = {};
    int np = 0;
    va_list ap;
    va_start(ap, fmt);
    for (const char* f = fmt; *f; ++f) {
        out += *f;
        if (*f != '%') continue;
        ++f;
        if (*f == '%') { out += '%'; continue; }
        bool suppress = false;
        if (*f == '*') { suppress = true; out += *f++; }
        std::string spec;
        while (*f && !strchr("diouxXeEfgGaAscpn[", *f)) spec += *f++;
        char conv = *f;
        if (!conv) break;
        if (conv == '[') abort(); // not used by PortablePixMap.cpp
        if (!suppress && (conv == 's' || conv == 'c')) {
            void* p = va_arg(ap, void*);
            unsigned sz = va_arg(ap, unsigned);
            if (sz == 0) abort();
            out += std::to_string(conv == 'c' ? 1 : sz - 1);
            if (np >= 8) abort();
            ptrs[np++] = p;
        } else if (!suppress) {
            if (np >= 8) abort();
            ptrs[np++] = va_arg(ap, void*);
        }
        out += spec;
        out += conv;
    }
    va_end(ap);
    return sscanf(buf, out.c_str(), ptrs[0], ptrs[1], ptrs[2], ptrs[3], ptrs[4], ptrs[5], ptrs[6], ptrs[7]);
}

inline uint16_t _byteswap_ushort(uint16_t v) { return __builtin_bswap16(v); }
inline uint32_t _byteswap_ulong(uint32_t v) { return __builtin_bswap32(v); }

// FlipRotate is part of DirectXTex's WIC (Windows-only) code. PortablePixMap.cpp only calls it from
// SaveToPortablePixMapHDR, which the harness never runs; a vertical flip is enough to link.
namespace DirectX {
inline HRESULT FlipRotate(const Image& src, TEX_FR_FLAGS flags, ScratchImage& out) noexcept {
    if (flags != TEX_FR_FLIP_VERTICAL) return E_NOTIMPL;
    HRESULT hr = out.Initialize2D(src.format, src.width, src.height, 1, 1);
    if (FAILED(hr)) return hr;
    const Image* dst = out.GetImage(0, 0, 0);
    for (size_t y = 0; y < src.height; ++y)
        memcpy(dst->pixels + y * dst->rowPitch, src.pixels + (src.height - 1 - y) * src.rowPitch,
               dst->rowPitch < src.rowPitch ? dst->rowPitch : src.rowPitch);
    return S_OK;
}
}
