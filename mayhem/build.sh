#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Header-only deps installed by mayhem/Dockerfile under /opt/toolchains/deps (DirectXMath,
# DirectX-Headers, sal.h). Defaults mirror the Dockerfile ENV so a re-run without it still works.
: "${DXTEX_DEPS_PREFIX:=$(ls -d /opt/toolchains/deps/directxmath-* | head -1);$(ls -d /opt/toolchains/deps/directx-headers-* | head -1)}"
: "${DXTEX_SAL_INCLUDE:=$(ls -d /opt/toolchains/deps/sal-corert-*/include | head -1)}"

# Upstream's Linux build: library only (tools/sample/DX11/DX12 are Windows-only), no OpenMP, with
# the optional libpng/libjpeg loaders. Precompiled headers off so the build stays ccache-friendly.
COMMON_CMAKE=(
  -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  "-DCMAKE_PREFIX_PATH=$DXTEX_DEPS_PREFIX" "-DDXTEX_SAL_INCLUDE=$DXTEX_SAL_INCLUDE"
  -DBUILD_TOOLS=OFF -DBUILD_SAMPLE=OFF -DBUILD_DX11=OFF -DBUILD_DX12=OFF -DBC_USE_OPENMP=OFF
  -DENABLE_LIBPNG_SUPPORT=ON -DENABLE_LIBJPEG_SUPPORT=ON -DBUILD_TESTING=OFF
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
)

# 1+2) Sanitized library + harnesses (fuzzer + -standalone each), all instrumented for coverage.
#      mayhem/CMakeLists.txt links mayhem/lsan_off.cc (LeakSanitizer off) into every one of them.
#      Build trees live under out/ (gitignored upstream: /out).
#      UBSan's `alignment` check is relaxed (only it): DirectXTex reads and writes pixel/header data
#      through uint32_t/uint16_t pointers at arbitrary byte offsets (DDS header after the magic, TGA
#      pixels after a variable-length ID field, PFM data after a text header, odd row pitches), which
#      x86/ARM64 handle natively and which fired on nearly every input. ASan and the rest of UBSan halt.
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
case "$SANITIZER_FLAGS" in *undefined*) FUZZ_FLAGS="$FUZZ_FLAGS -fno-sanitize=alignment" ;; esac
cmake -S mayhem -B out/fuzz "${COMMON_CMAKE[@]}" -DDXTEX_FUZZ=ON \
  "-DCMAKE_C_FLAGS=$FUZZ_FLAGS" "-DCMAKE_CXX_FLAGS=$FUZZ_FLAGS" \
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1" "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O1" \
  "-DLIB_FUZZING_ENGINE=$LIB_FUZZING_ENGINE" "-DSTANDALONE_FUZZ_MAIN=$STANDALONE_FUZZ_MAIN" \
  "-DCMAKE_EXE_LINKER_FLAGS=$SANITIZER_FLAGS -fsanitize=fuzzer-no-link"
cmake --build out/fuzz -j"$MAYHEM_JOBS"

# 3) Functional self-test (mayhem/selftest) against an independent, NORMAL-flags library build.
cmake -S mayhem -B out/test "${COMMON_CMAKE[@]}" -DCMAKE_BUILD_TYPE=Release -DDXTEX_FUZZ=OFF \
  "-DCMAKE_C_FLAGS=$COVERAGE_FLAGS" "-DCMAKE_CXX_FLAGS=$COVERAGE_FLAGS" \
  "-DCMAKE_EXE_LINKER_FLAGS=$COVERAGE_FLAGS"
cmake --build out/test -j"$MAYHEM_JOBS"
