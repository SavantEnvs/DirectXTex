# Mayhem layer: builds upstream's DirectXTex library (the real top-level CMakeLists.txt, added as a
# subdirectory) plus the fuzz harnesses or the functional self-test against it.
#   -DDXTEX_FUZZ=ON   fuzz harnesses (libFuzzer binary + -standalone reproducer each)
#   -DDXTEX_FUZZ=OFF  the self-test runner (normal flags)
cmake_minimum_required(VERSION 3.21)
project(DirectXTexMayhem LANGUAGES C CXX)

option(DXTEX_FUZZ "Build the fuzz harnesses" ON)
set(DXTEX_SAL_INCLUDE "" CACHE PATH "Directory holding sal.h (DirectXMath needs it on Linux)")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(BEFORE SYSTEM ${DXTEX_SAL_INCLUDE})

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. dxtex)

if(NOT DXTEX_FUZZ)
  add_executable(dxtex_selftest selftest/dxtex_selftest.cpp)
  target_link_libraries(dxtex_selftest PRIVATE DirectXTex)
  target_include_directories(dxtex_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Auxiliary)
  return()
endif()

set(LIB_FUZZING_ENGINE "-fsanitize=fuzzer" CACHE STRING "libFuzzer link flag")
set(STANDALONE_FUZZ_MAIN "" CACHE FILEPATH "LLVM's run-once driver (C)")
add_library(standalone_main OBJECT ${STANDALONE_FUZZ_MAIN})
add_library(lsan_off OBJECT lsan_off.cc)

# texconv's PPM/PFM reader, compiled unmodified against the POSIX Windows.h shim.
add_library(texconv_ppm OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/../Texconv/PortablePixMap.cpp)
target_include_directories(texconv_ppm BEFORE PRIVATE harnesses/winshim)
target_link_libraries(texconv_ppm PRIVATE DirectXTex)

set(HARNESSES fuzz_dds_load fuzz_hdr_load fuzz_tga_load fuzz_dds_decompress fuzz_convert
    fuzz_resize_mips fuzz_save_roundtrip fuzz_file_loaders fuzz_ppm_load fuzz_image_ops
    fuzz_png_load fuzz_jpeg_load fuzz_bc_compress)
foreach(h ${HARNESSES})
  add_library(${h}_obj OBJECT harnesses/${h}.cpp)
  target_link_libraries(${h}_obj PRIVATE DirectXTex)
  target_include_directories(${h}_obj PRIVATE harnesses ${CMAKE_CURRENT_SOURCE_DIR}/../Auxiliary)
  set(extra $<TARGET_OBJECTS:lsan_off>)
  if(h STREQUAL "fuzz_ppm_load")
    list(APPEND extra $<TARGET_OBJECTS:texconv_ppm>)
  endif()
  add_executable(${h} $<TARGET_OBJECTS:${h}_obj> ${extra})
  target_link_libraries(${h} PRIVATE DirectXTex)
  target_link_options(${h} PRIVATE ${LIB_FUZZING_ENGINE})
  add_executable(${h}-standalone $<TARGET_OBJECTS:${h}_obj> $<TARGET_OBJECTS:standalone_main> ${extra})
  target_link_libraries(${h}-standalone PRIVATE DirectXTex)
endforeach()
