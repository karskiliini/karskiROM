#ifndef LZ4_COMPRESS_H
#define LZ4_COMPRESS_H

#include <stdint.h>

/*
 * LZ4 block compressor for IEC transfer acceleration.
 *
 * Produces standard LZ4 block format (no frame header).
 * Compatible with any LZ4 decoder (lz4, python-lz4, etc.)
 *
 * Returns compressed size in bytes, -1 if dst_cap too small,
 * 0 if src_len is 0.
 */
int lz4_compress_block(const uint8_t *src, int src_len,
                       uint8_t *dst, int dst_cap);

#endif /* LZ4_COMPRESS_H */
