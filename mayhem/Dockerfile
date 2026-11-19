# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

# The org base, pinned to a fixed DATE-tagged base (YYYYMMDD; shared by all integrations
# minted against it). Use the current dated tag; pin by digest in production: ...base@sha256:<digest>.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV — an
# empty value breaks clang ("must be a non-negative decimal integer") both here and for
# any in-image rebuild. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# To change it for a build (e.g. ASan-only, or add MSan): --build-arg SANITIZER_FLAGS="...".
# Declare it WITHOUT a default (no '='): an undefaulted ARG lets the base ENV flow through to
# build.sh when not passed, and an explicit --build-arg overrides it. (An empty default — `=` —
# would instead shadow the base ENV and silently wipe the sanitizers.)
ARG SANITIZER_FLAGS
# Coverage (optional, off by default): instrument the TEST build to measure how much of the project
# the suite exercises — --build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping".
# Empty by default (no effect on the normal oracle build); build.sh appends it to the test build only.
ARG COVERAGE_FLAGS=
# Build parallelism (optional). build.sh falls back to nproc when unset: --build-arg MAYHEM_JOBS=8
ARG MAYHEM_JOBS=

# Extra apt packages: libpng/libjpeg dev headers for upstream's optional ENABLE_LIBPNG_SUPPORT /
# ENABLE_LIBJPEG_SUPPORT loaders (Auxiliary/DirectXTexPNG.cpp, DirectXTexJPEG.cpp).
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends libpng-dev libjpeg-dev \
 && rm -rf /var/lib/apt/lists/*

# Upstream's non-Windows CMake build requires find_package(directxmath) and
# find_package(directx-headers) (CMakeLists.txt "if(MINGW OR (NOT WIN32))"), and DirectXMath needs a
# <sal.h> on Linux (DirectXMath README). None are in Debian at the version upstream needs
# (DirectXMath 3.22, "Update for DirectXMath 3.22 changes"), so they are installed once from pinned
# commits into /opt/toolchains/deps (SPEC 6.2 item 19). All three are header-only.
ARG DXMATH_SHA=6ac6a3c51d4d99237656f22486ae5177c14c565d
ARG DXMATH_SHA256=0e122170d259c73a101c807fce56a1c6b2f91c9a073430b27c15aaffc043f8d4
ARG DXHEADERS_SHA=a97becc4e80fd1d9c3ae95d4db4a5703d5c227c6
ARG DXHEADERS_SHA256=f2f9c6aaee4b1a82d37376139321c48ba527c852f49b7ea7fc84d4f976b26651
ARG SALH_SHA=c6af4cfc8b625851b91823d9be746c4f7abdc667
ARG SALH_SHA256=bb9ea7c1d05cdfc10a17fe80d87a7cd160c1063c7e5fa089cd6405c31c610a1b
RUN set -e; \
    dep() { name=$1 url=$2 sum=$3; d=/opt/toolchains/deps/$name; mkdir -p "$d/src" /tmp/dep-build-$name; \
      curl -fsSL -o /tmp/$name.tgz "$url"; echo "$sum  /tmp/$name.tgz" | sha256sum -c -; \
      tar -xzf /tmp/$name.tgz --strip-components=1 -C "$d/src"; \
      cmake -S "$d/src" -B /tmp/dep-build-$name -DCMAKE_INSTALL_PREFIX="$d" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF -DDXHEADERS_BUILD_TEST=OFF -DDXHEADERS_BUILD_GOOGLE_TEST=OFF; \
      cmake --build /tmp/dep-build-$name; cmake --install /tmp/dep-build-$name; \
      rm -rf /tmp/dep-build-$name /tmp/$name.tgz; }; \
    dep directxmath-${DXMATH_SHA} https://github.com/microsoft/DirectXMath/archive/${DXMATH_SHA}.tar.gz ${DXMATH_SHA256}; \
    dep directx-headers-${DXHEADERS_SHA} https://github.com/microsoft/DirectX-Headers/archive/${DXHEADERS_SHA}.tar.gz ${DXHEADERS_SHA256}; \
    d=/opt/toolchains/deps/sal-corert-${SALH_SHA}; mkdir -p "$d/src" "$d/include"; \
    curl -fsSL -o "$d/src/sal.h" https://raw.githubusercontent.com/dotnet/corert/${SALH_SHA}/src/Native/inc/unix/sal.h; \
    echo "${SALH_SHA256}  $d/src/sal.h" | sha256sum -c -; cp "$d/src/sal.h" "$d/include/sal.h"; \
    chmod -R a+rX /opt/toolchains/deps
ENV DXTEX_DEPS_PREFIX="/opt/toolchains/deps/directxmath-${DXMATH_SHA};/opt/toolchains/deps/directx-headers-${DXHEADERS_SHA}" \
    DXTEX_SAL_INCLUDE=/opt/toolchains/deps/sal-corert-${SALH_SHA}/include
USER mayhem

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
