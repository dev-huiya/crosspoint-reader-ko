#include <stdint.h>

uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t previous) {
  const uint8_t* bytes = (const uint8_t*)data;
  uint32_t a = previous & 0xffffu;
  uint32_t b = previous >> 16;
  for (unsigned int i = 0; i < length; ++i) {
    a = (a + bytes[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t previous) {
  const uint8_t* bytes = (const uint8_t*)data;
  uint32_t crc = previous ^ 0xffffffffu;
  for (unsigned int i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
  }
  return crc ^ 0xffffffffu;
}
