set(ZLIB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../zlib")

idf_component_register(
    SRCS
        "${ZLIB_SOURCE_DIR}/adler32.c"
        "${ZLIB_SOURCE_DIR}/compress.c"
        "${ZLIB_SOURCE_DIR}/crc32.c"
        "${ZLIB_SOURCE_DIR}/deflate.c"
        "${ZLIB_SOURCE_DIR}/gzclose.c"
        "${ZLIB_SOURCE_DIR}/gzlib.c"
        "${ZLIB_SOURCE_DIR}/gzread.c"
        "${ZLIB_SOURCE_DIR}/gzwrite.c"
        "${ZLIB_SOURCE_DIR}/infback.c"
        "${ZLIB_SOURCE_DIR}/inffast.c"
        "${ZLIB_SOURCE_DIR}/inflate.c"
        "${ZLIB_SOURCE_DIR}/inftrees.c"
        "${ZLIB_SOURCE_DIR}/trees.c"
        "${ZLIB_SOURCE_DIR}/uncompr.c"
        "${ZLIB_SOURCE_DIR}/zutil.c"
    INCLUDE_DIRS "${ZLIB_SOURCE_DIR}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-implicit-int
    -Wno-return-type
    -Wno-format-extra-args
    -Wno-unused-parameter
    -Wno-unused-const-variable
    -Wno-implicit-fallthrough
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PPM_SUPPORTED
    HAVE_MATH_H
    HAVE_CTYPE_H
    HAVE_UNISTD_H
    HAVE_ERRNO_H
)
