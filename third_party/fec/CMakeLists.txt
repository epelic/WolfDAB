# Minimal Reed-Solomon slice of Phil Karn's libfec (quiet/libfec mirror).
# We only need the char-variant RS codec for DAB+ RS(120,110).
# Full libfec in ../libfec/ is NOT built; its CMakeLists sets global flags
# we don't want. Instead we compile the needed .c files into a static lib.

set(LIBFEC_SRC_DIR "${CMAKE_SOURCE_DIR}/third_party/libfec")

add_library(fec_rs STATIC
    ${LIBFEC_SRC_DIR}/init_rs_char.c
    ${LIBFEC_SRC_DIR}/encode_rs_char.c
    ${LIBFEC_SRC_DIR}/decode_rs_char.c
)
target_include_directories(fec_rs PUBLIC ${LIBFEC_SRC_DIR})
target_compile_definitions(fec_rs PRIVATE HAVE_MEMSET=1 HAVE_MEMMOVE=1)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(fec_rs PRIVATE -w)   # third-party: silence warnings
endif()
