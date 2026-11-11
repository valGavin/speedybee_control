#ifndef IBUS_H
#define IBUS_H

#include <stdint.h>

// FlySky iBUS servo frame: length byte, command byte, 14 channels, checksum.
#define IBUS_CHANNEL_COUNT 14
#define IBUS_FRAME_LENGTH 32
#define IBUS_COMMAND_SERVO 0x40

/**
 * Pack channel values into an iBUS servo frame.
 *
 * Has no hardware dependency so it can be unit tested on the host
 * (see test/test_ibus.c).
 *
 * @param channels An array of IBUS_CHANNEL_COUNT channel values
 * @param frame Output buffer of IBUS_FRAME_LENGTH bytes
 */
static inline void ibus_pack(const uint16_t *channels, uint8_t *frame) {
    frame[0] = IBUS_FRAME_LENGTH;
    frame[1] = IBUS_COMMAND_SERVO;
    for (int i = 0; i < IBUS_CHANNEL_COUNT; i++) {  // 16-bit little-endian
        frame[2 + (i * 2)] = channels[i] & 0xFF;
        frame[3 + (i * 2)] = (channels[i] >> 8) & 0xFF;
    }

    // Checksum: 0xFFFF minus the sum of every preceding byte
    uint16_t checksum = 0xFFFF;
    for (int i = 0; i < IBUS_FRAME_LENGTH - 2; i++)
        checksum -= frame[i];
    frame[IBUS_FRAME_LENGTH - 2] = checksum & 0xFF;
    frame[IBUS_FRAME_LENGTH - 1] = (checksum >> 8) & 0xFF;
}

#endif
