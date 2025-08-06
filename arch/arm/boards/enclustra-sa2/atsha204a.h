#pragma once

#include <linux/types.h>

/**
 * @brief Read the board MAC address from EEPROM
 * @param[out] buffer A 6-byte buffer set to the MAC address on success
 *
 * If the MAC address is 20:B0:F7:0A:6C:08, `buffer[0]` equals 0x20.
 *
 * Read from the one-time programmable zone (OTP) of the chip:
 * - 4 bytes at address 0x10 (32-bit word address 0x04)
 * - 2 bytes at address 0x14 (32-bit word address 0x04)
 *
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
int atsha204_get_mac(uint8_t *buffer);
