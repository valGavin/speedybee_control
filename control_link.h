#ifndef CONTROL_LINK_H
#define CONTROL_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Control packet from the phone: roll, pitch, throttle, yaw, AUX1 as
// little-endian uint16.
#define CONTROL_CHANNEL_COUNT 5
#define CONTROL_PACKET_LENGTH (CONTROL_CHANNEL_COUNT * 2)

// Anything outside this window is not a plausible RC value.
#define CONTROL_VALUE_MIN 800
#define CONTROL_VALUE_MAX 2200

// Silence longer than this means the phone is gone.
#define CONTROL_TIMEOUT_MS 500

/**
 * Decode and validate a control packet.
 *
 * Has no hardware dependency so it can be unit tested on the host
 * (see test/test_control_link.c).
 *
 * @param payload Raw datagram payload
 * @param length Payload length in bytes
 * @param channels Output array of CONTROL_CHANNEL_COUNT values, written only
 *                 when the packet is valid
 * @return true if the packet has the right length and every value is plausible
 */
static inline bool control_packet_decode(const uint8_t *payload, uint16_t length, uint16_t *channels) {
    if (length != CONTROL_PACKET_LENGTH)
        return false;

    uint16_t decoded[CONTROL_CHANNEL_COUNT];
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        decoded[i] = (uint16_t)(payload[i * 2] | (payload[(i * 2) + 1] << 8));
        if (decoded[i] < CONTROL_VALUE_MIN || decoded[i] > CONTROL_VALUE_MAX)
            return false;
    }

    memcpy(channels, decoded, sizeof(decoded));
    return true;
}

/**
 * Decide whether the control link is alive.
 *
 * @param have_packet Whether a valid packet has arrived since the last loss
 * @param now_ms Current time
 * @param last_packet_ms Arrival time of the most recent valid packet
 * @return true if a packet arrived within CONTROL_TIMEOUT_MS
 */
static inline bool control_link_fresh(bool have_packet, uint32_t now_ms, uint32_t last_packet_ms) {
    return have_packet && (uint32_t)(now_ms - last_packet_ms) < CONTROL_TIMEOUT_MS;
}

#endif
