# Build the libsrtp sources libpeer uses from the third_party/libsrtp
# submodule, with the configuration in libsrtp/config.h.

set(LIBSRTP_DIR ${LIBPEER_DIR}/third_party/libsrtp)
set(LIBSRTP_GENERATED_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/libsrtp/include)

# libpeer includes <srtp2/srtp.h>, the installed location of the header.
configure_file(
  ${LIBSRTP_DIR}/include/srtp.h
  ${LIBSRTP_GENERATED_INCLUDE}/srtp2/srtp.h
  COPYONLY
)
zephyr_include_directories(${LIBSRTP_GENERATED_INCLUDE})

zephyr_library_named(libpeer_srtp)
zephyr_library_sources(
  ${LIBSRTP_DIR}/srtp/srtp.c
  ${LIBSRTP_DIR}/crypto/cipher/aes.c
  ${LIBSRTP_DIR}/crypto/cipher/aes_icm.c
  ${LIBSRTP_DIR}/crypto/cipher/cipher.c
  ${LIBSRTP_DIR}/crypto/cipher/cipher_test_cases.c
  ${LIBSRTP_DIR}/crypto/cipher/null_cipher.c
  ${LIBSRTP_DIR}/crypto/hash/auth.c
  ${LIBSRTP_DIR}/crypto/hash/auth_test_cases.c
  ${LIBSRTP_DIR}/crypto/hash/hmac.c
  ${LIBSRTP_DIR}/crypto/hash/null_auth.c
  ${LIBSRTP_DIR}/crypto/hash/sha1.c
  ${LIBSRTP_DIR}/crypto/kernel/alloc.c
  ${LIBSRTP_DIR}/crypto/kernel/crypto_kernel.c
  ${LIBSRTP_DIR}/crypto/kernel/err.c
  ${LIBSRTP_DIR}/crypto/kernel/key.c
  ${LIBSRTP_DIR}/crypto/math/datatypes.c
  ${LIBSRTP_DIR}/crypto/replay/rdb.c
  ${LIBSRTP_DIR}/crypto/replay/rdbx.c
)
zephyr_library_compile_definitions(HAVE_CONFIG_H)
zephyr_library_compile_options(-std=gnu17 -Wno-error)
# Private directories come first, so <config.h> resolves to libsrtp's
# configuration, not to libpeer's src/config.h.
zephyr_library_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}/libsrtp
  ${LIBSRTP_DIR}/include
  ${LIBSRTP_DIR}/crypto/include
)
