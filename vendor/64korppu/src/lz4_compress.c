/*
 * Minimal LZ4 block compressor for 64korppu IEC transfer.
 *
 * Hash-taulun koko määritetään config.h:ssa (COMPRESS_HASH_SIZE).
 * Oletuksena 128 entryä × 2 tavua = 256 tavua pinossa.
 *
 * - Standard LZ4 block format (raw, no frame header)
 * - Min match length: 4
 * - Last 5 bytes always literals (LZ4 spec)
 * - Max offset: 65535
 */

#include "lz4_compress.h"
#include "config.h"
#include <string.h>

#define HASH_SIZE     COMPRESS_HASH_SIZE
#define MIN_MATCH     4
#define LAST_LITERALS 5

/* Hash function: multiply + shift, masked to HASH_SIZE */
static inline uint8_t lz4_hash(uint32_t v)
{
    return (uint8_t)(((v * 2654435761U) >> 24) & (HASH_SIZE - 1));
}

static inline uint32_t read32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

int lz4_compress_block(const uint8_t *src, int src_len,
                       uint8_t *dst, int dst_cap)
{
    if (src_len == 0)
        return 0;

    uint16_t htable[HASH_SIZE];
    memset(htable, 0, sizeof(htable));

    const uint8_t *ip = src;           /* input pointer */
    const uint8_t *iend = src + src_len;
    const uint8_t *ilimit = iend - LAST_LITERALS;  /* stop matching here */
    const uint8_t *mflimit = iend - MIN_MATCH;     /* can't start match after this */
    const uint8_t *anchor = src;       /* start of current literal run */

    uint8_t *op = dst;                 /* output pointer */
    uint8_t *oend = dst + dst_cap;

    /* First byte is always a literal, put it in the hash table */
    if (src_len >= MIN_MATCH) {
        htable[lz4_hash(read32(ip))] = (uint16_t)(ip - src);
    }
    ip++;

    for (;;) {
        /* Find a match */
        const uint8_t *match = NULL;
        uint16_t offset = 0;

        while (ip <= mflimit) {
            uint32_t h = lz4_hash(read32(ip));
            uint16_t ref_idx = htable[h];
            const uint8_t *ref = src + ref_idx;
            htable[h] = (uint16_t)(ip - src);

            offset = (uint16_t)(ip - ref);
            if (offset > 0 && offset <= 65535 &&
                ip - offset >= src &&
                read32(ref) == read32(ip)) {
                match = ref;
                break;
            }
            ip++;
        }

        if (match == NULL) {
            /* No more matches: emit remaining as literals */
            break;
        }

        /* Count match length (already know 4 match) */
        int match_len = MIN_MATCH;
        {
            const uint8_t *mp = ip + MIN_MATCH;
            const uint8_t *mr = match + MIN_MATCH;
            while (mp < ilimit && *mp == *mr) {
                mp++;
                mr++;
            }
            match_len = (int)(mp - ip);
        }

        /* Literal count */
        int lit_len = (int)(ip - anchor);

        /* Token byte */
        int token_lit = lit_len < 15 ? lit_len : 15;
        int token_match = (match_len - MIN_MATCH) < 15 ?
                          (match_len - MIN_MATCH) : 15;

        /* Check output space: token + lit_extra + literals + 2(offset) + match_extra */
        int extra_lit = lit_len >= 15 ? 1 + (lit_len - 15) / 255 : 0;
        int extra_match = (match_len - MIN_MATCH) >= 15 ?
                          1 + (match_len - MIN_MATCH - 15) / 255 : 0;
        int needed = 1 + extra_lit + lit_len + 2 + extra_match;
        if (op + needed > oend)
            return -1;

        /* Write token */
        *op++ = (uint8_t)((token_lit << 4) | token_match);

        /* Write literal length extension */
        if (lit_len >= 15) {
            int remaining = lit_len - 15;
            while (remaining >= 255) {
                *op++ = 255;
                remaining -= 255;
            }
            *op++ = (uint8_t)remaining;
        }

        /* Write literals */
        memcpy(op, anchor, lit_len);
        op += lit_len;

        /* Write offset (little-endian) */
        *op++ = (uint8_t)(offset & 0xFF);
        *op++ = (uint8_t)(offset >> 8);

        /* Write match length extension */
        if (match_len - MIN_MATCH >= 15) {
            int remaining = match_len - MIN_MATCH - 15;
            while (remaining >= 255) {
                *op++ = 255;
                remaining -= 255;
            }
            *op++ = (uint8_t)remaining;
        }

        /* Advance past match */
        ip += match_len;
        anchor = ip;

        /* If we've gone past the match limit, done */
        if (ip > mflimit)
            break;

        /* Insert current position into hash */
        htable[lz4_hash(read32(ip))] = (uint16_t)(ip - src);
    }

    /* Emit final literals */
    {
        int lit_len = (int)(iend - anchor);
        if (lit_len > 0) {
            int extra = lit_len >= 15 ? 1 + (lit_len - 15) / 255 : 0;
            int needed = 1 + extra + lit_len;
            if (op + needed > oend)
                return -1;

            int token_lit = lit_len < 15 ? lit_len : 15;
            *op++ = (uint8_t)(token_lit << 4);

            if (lit_len >= 15) {
                int remaining = lit_len - 15;
                while (remaining >= 255) {
                    *op++ = 255;
                    remaining -= 255;
                }
                *op++ = (uint8_t)remaining;
            }

            memcpy(op, anchor, lit_len);
            op += lit_len;
        }
    }

    return (int)(op - dst);
}
