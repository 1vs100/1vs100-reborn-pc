# The avatar format, pack reader and preview renderer. Needs no runtime, so the
# game and the launcher share it. The parent project must enable C (for the
# LZX decompressor) and provide fmt::fmt before adding this directory.
add_library(onevs100_avatar STATIC
    xna_avatar_format.cc
    stfs_file.cc
    third_party/mspack/lzxd.c
    third_party/mspack/system.c
    third_party/mspack/logging.cc)
target_include_directories(onevs100_avatar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mspack)
target_compile_features(onevs100_avatar PUBLIC cxx_std_20)
target_link_libraries(onevs100_avatar PUBLIC fmt::fmt)
if(MSVC)
  # getenv is portable; the CRT's _dupenv_s replacement is not.
  target_compile_definitions(onevs100_avatar PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
