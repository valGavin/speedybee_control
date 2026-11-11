// Host unit tests for the hardware-independent parts of the firmware.
//
// Build and run from the repository root:
//   gcc -Wall -Wextra -I. test/test_host.c -o test_host && ./test_host

#include <stdio.h>
#include "ibus.h"
#include "control_link.h"

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

static void test_ibus_all_centred(void) {
    uint16_t channels[IBUS_CHANNEL_COUNT];
    for (int i = 0; i < IBUS_CHANNEL_COUNT; i++)
        channels[i] = 1500;

    uint8_t frame[IBUS_FRAME_LENGTH];
    ibus_pack(channels, frame);

    CHECK(frame[0] == 0x20);
    CHECK(frame[1] == 0x40);
    for (int i = 0; i < IBUS_CHANNEL_COUNT; i++) {
        CHECK(frame[2 + (i * 2)] == 0xDC);
        CHECK(frame[3 + (i * 2)] == 0x05);
    }
    // 0xFFFF - (0x20 + 0x40 + 14 * (0xDC + 0x05)) = 0xF351
    CHECK(frame[30] == 0x51);
    CHECK(frame[31] == 0xF3);
}

// A frame captured from a FlySky receiver, widely used as a reference vector.
static void test_ibus_reference_frame(void) {
    const uint16_t channels[IBUS_CHANNEL_COUNT] = {
        0x05DB, 0x05DC, 0x0554, 0x05DC, 0x03E8, 0x07D0, 0x05D2,
        0x03E8, 0x05DC, 0x05DC, 0x05DC, 0x05DC, 0x05DC, 0x05DC
    };
    const uint8_t expected[IBUS_FRAME_LENGTH] = {
        0x20, 0x40, 0xDB, 0x05, 0xDC, 0x05, 0x54, 0x05, 0xDC, 0x05, 0xE8,
        0x03, 0xD0, 0x07, 0xD2, 0x05, 0xE8, 0x03, 0xDC, 0x05, 0xDC, 0x05,
        0xDC, 0x05, 0xDC, 0x05, 0xDC, 0x05, 0xDC, 0x05, 0xDA, 0xF3
    };

    uint8_t frame[IBUS_FRAME_LENGTH];
    ibus_pack(channels, frame);
    CHECK(memcmp(frame, expected, IBUS_FRAME_LENGTH) == 0);
}

static void test_control_packet_valid(void) {
    // roll 1500, pitch 1500, throttle 855, yaw 1500, AUX1 1200
    const uint8_t payload[CONTROL_PACKET_LENGTH] = {
        0xDC, 0x05, 0xDC, 0x05, 0x57, 0x03, 0xDC, 0x05, 0xB0, 0x04
    };
    uint16_t channels[CONTROL_CHANNEL_COUNT] = {0};

    CHECK(control_packet_decode(payload, sizeof(payload), channels));
    CHECK(channels[0] == 1500);
    CHECK(channels[1] == 1500);
    CHECK(channels[2] == 855);
    CHECK(channels[3] == 1500);
    CHECK(channels[4] == 1200);
}

static void test_control_packet_rejects_bad_input(void) {
    const uint8_t valid[CONTROL_PACKET_LENGTH] = {
        0xDC, 0x05, 0xDC, 0x05, 0x57, 0x03, 0xDC, 0x05, 0xB0, 0x04
    };
    uint16_t channels[CONTROL_CHANNEL_COUNT] = {1, 2, 3, 4, 5};

    CHECK(!control_packet_decode(valid, 9, channels));
    CHECK(!control_packet_decode(valid, 0, channels));

    uint8_t too_low[CONTROL_PACKET_LENGTH];
    memcpy(too_low, valid, sizeof(valid));
    too_low[4] = 0x00;  // throttle = 0x0300 = 768
    CHECK(!control_packet_decode(too_low, sizeof(too_low), channels));

    uint8_t too_high[CONTROL_PACKET_LENGTH];
    memcpy(too_high, valid, sizeof(valid));
    too_high[9] = 0xFF;  // AUX1 = 0xFFB0
    CHECK(!control_packet_decode(too_high, sizeof(too_high), channels));

    // A rejected packet must not touch the output
    CHECK(channels[0] == 1 && channels[2] == 3 && channels[4] == 5);
}

static void test_control_link_fresh(void) {
    CHECK(!control_link_fresh(false, 1000, 1000));
    CHECK(control_link_fresh(true, 1000, 1000));
    CHECK(control_link_fresh(true, 1000 + CONTROL_TIMEOUT_MS - 1, 1000));
    CHECK(!control_link_fresh(true, 1000 + CONTROL_TIMEOUT_MS, 1000));
    // Millisecond counter wrapping around
    CHECK(control_link_fresh(true, 10, UINT32_MAX - 10));
    CHECK(!control_link_fresh(true, CONTROL_TIMEOUT_MS, UINT32_MAX));
}

int main(void) {
    test_ibus_all_centred();
    test_ibus_reference_frame();
    test_control_packet_valid();
    test_control_packet_rejects_bad_input();
    test_control_link_fresh();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
