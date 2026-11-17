#pragma once
// CRC-32 (IEEE 802.3, as used by zip and BPS patches).
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32_update(uint32_t crc, const void* data, size_t len);  // start with 0
static inline uint32_t crc32_of(const void* data, size_t len) { return crc32_update(0, data, len); }

#ifdef __cplusplus
}
#endif
