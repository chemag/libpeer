/* SPDX-License-Identifier: MIT */
/* libsrtp configuration for Zephyr builds of the third_party/libsrtp
 * submodule. */
#ifndef LIBPEER_ZEPHYR_LIBSRTP_CONFIG_H_
#define LIBPEER_ZEPHYR_LIBSRTP_CONFIG_H_

#define PACKAGE_VERSION "2.4.2"
#define PACKAGE_STRING  "libsrtp2 2.4.2"

#if defined(CONFIG_X86_64) || defined(CONFIG_X86)
#define CPU_CISC 1
#define HAVE_X86 1
#else
#define CPU_RISC 1
#endif

#define HAVE_INLINE               1
#define HAVE_INT32_T              1
#define HAVE_NETINET_IN_H         1
#define HAVE_STDINT_H             1
#define HAVE_STDLIB_H             1
#define HAVE_UINT16_T             1
#define HAVE_UINT32_T             1
#define HAVE_UINT64_T             1
#define HAVE_UINT8_T              1
#define SIZEOF_UNSIGNED_LONG      __SIZEOF_LONG__
#define SIZEOF_UNSIGNED_LONG_LONG __SIZEOF_LONG_LONG__

#endif /* LIBPEER_ZEPHYR_LIBSRTP_CONFIG_H_ */
