#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CRC-16 CCITT-FALSE: poly = 0x1021, init = 0xFFFF, no reflection, xorout = 0.
 * Used by DAB+ per ETSI TS 102 563 §5.3.2 to protect each access unit.
 * Reference check: crc16_ccitt("123456789", 9) == 0x29B1.
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/*
 * DAB Fire code: g(x) = x^16 + x^14 + x^13 + x^11 + x^10 + x^9 + x^8
 *                       + x^6 + x^5 + x + 1    (polynomial 0x782F when taken
 * as the 16 lower coefficients, i.e. the value you feed a shift-register).
 *
 * Per ETSI TS 102 563 §5.2 the Fire code of the superframe header is
 * computed over the 9 bytes that follow it (bytes 2..10) with initial
 * register value 0xFFFF and output is XORed with 0xFFFF.
 */
uint16_t crc16_firecode(const uint8_t *data, size_t len);

/*
 * FIB CRC (ETSI EN 300 401 §5.3.1): same register as CRC-16 CCITT-FALSE
 * but the 16-bit result is complemented (XORed with 0xFFFF) before being
 * appended to the FIB body. Reference check: crc16_fib("123456789", 9)
 * == 0xD64E (== 0x29B1 XOR 0xFFFF).
 */
uint16_t crc16_fib(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
