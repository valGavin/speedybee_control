#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/uart.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "ibus.h"
#include "control_link.h"

// Configuration
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define UDP_PORT 8888
#define CONTROL_PORT 9877
#define LED_PIN CYW43_WL_GPIO_LED_PIN

// iBUS Configuration
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define IBUS_PERIOD_MS 7  // ~143Hz

// Set to 1 to print every iBUS frame over stdio. Slows the iBUS loop.
#define IBUS_DEBUG_PRINT 0

// Channel values used whenever there is no live control link:
// sticks centred, throttle low, AUX1 disarmed.
static const uint16_t failsafe_channels[IBUS_CHANNEL_COUNT] = {
    1500, 1500, 855, 1500, 1200, 1500, 1500,
    1500, 1500, 1500, 1500, 1500, 1500, 1500
};

// Global variables
struct udp_pcb *broadcast_pcb, *control_pcb;
struct netif *netif;
static ip_addr_t phone_addr;
static volatile bool phone_acknowledged = false;

// Shared between the lwIP callback (core 0) and the iBUS loop (core 1).
// Access only while holding channels_lock.
static critical_section_t channels_lock;
static uint16_t current_channels[IBUS_CHANNEL_COUNT];
static uint32_t last_packet_ms = 0;
static bool have_packet = false;

// Function declaration
void send_broadcast(char *ip_address);
void receive_phone_acknowledgment(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void receive_aetr_values(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void send_ibus_packet(const uint16_t *channels);
void core1_main();

int main() {
    stdio_init_all();
    critical_section_init(&channels_lock);
    memcpy(current_channels, failsafe_channels, sizeof(current_channels));

    if (cyw43_arch_init()) {
        printf("WiFi initialization failed\n");
        return -1;
    }

    // Initialize UART
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    // Blink LED at 250ms interval while connecting
    printf("Connecting to WiFi...\n");
    cyw43_arch_enable_sta_mode();
    int blink_state = 0;
    while (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK)) {
        cyw43_arch_gpio_put(LED_PIN, blink_state);
        blink_state = !blink_state;
        sleep_ms(500);
    }

    // WiFi connected. Blink LED at one second interval
    printf("Connected to WiFi!\n");
    netif = netif_list;
    char pico_ip[16];
    snprintf(pico_ip, sizeof(pico_ip), "%s", ipaddr_ntoa(netif_ip4_addr(netif)));
    printf("Pico W IP Address: %s\n", pico_ip);

    cyw43_arch_lwip_begin();
    broadcast_pcb = udp_new();
    control_pcb = udp_new();
    if (broadcast_pcb && control_pcb) {
        udp_bind(broadcast_pcb, IP_ADDR_ANY, UDP_PORT);
        udp_bind(control_pcb, IP_ADDR_ANY, CONTROL_PORT);
        udp_recv(control_pcb, receive_phone_acknowledgment, NULL);
    }
    cyw43_arch_lwip_end();
    if (!broadcast_pcb || !control_pcb) {
        printf("UDP initialization failed\n");
        return -1;
    }

    int broadcast_counter = 0;
    while (!phone_acknowledged) {
        if (broadcast_counter % 2 == 0)
            send_broadcast(pico_ip);
        cyw43_arch_gpio_put(LED_PIN, broadcast_counter % 2);
        broadcast_counter++;
        sleep_ms(1000);
    }

    // Phone acknowledged. LED OFF
    printf("Phone connected at %s\n", ipaddr_ntoa(&phone_addr));
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Listen for AETR values from phone
    cyw43_arch_lwip_begin();
    udp_recv(control_pcb, receive_aetr_values, NULL);
    cyw43_arch_lwip_end();

    multicore_launch_core1(core1_main);

    while (true)
        tight_loop_contents();
}

/**
 * iBUS loop. Sends a frame every IBUS_PERIOD_MS while the control link is
 * alive: WiFi joined and a valid control packet within CONTROL_TIMEOUT_MS.
 *
 * When the link is lost for either reason the channels are reset to the
 * failsafe values and transmission stops, so the flight controller sees
 * signal loss and applies its own failsafe. Stale values are never resent.
 */
void core1_main() {
    uint16_t channels_copy[IBUS_CHANNEL_COUNT];
    bool link_was_up = false;

    while (true) {
        bool wifi_up = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN;

        critical_section_enter_blocking(&channels_lock);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        bool link_up = wifi_up && control_link_fresh(have_packet, now_ms, last_packet_ms);
        if (!link_up) {
            memcpy(current_channels, failsafe_channels, sizeof(current_channels));
            have_packet = false;
        }
        memcpy(channels_copy, current_channels, sizeof(channels_copy));
        critical_section_exit(&channels_lock);

        if (link_up)
            send_ibus_packet(channels_copy);

        if (link_up != link_was_up) {
            if (link_up)
                printf("Control link up.\n");
            else
                printf("Control link lost (%s). iBUS output stopped.\n", wifi_up ? "no packets" : "WiFi down");
            link_was_up = link_up;
        }

        sleep_ms(IBUS_PERIOD_MS);
    }
}

/**
 * Function to send UDP broadcast with Pico W's IP
 *
 * @param ip_address Pico W's IP Address
 */
void send_broadcast(char *ip_address) {
    char msg[64];
    snprintf(msg, sizeof(msg), "DRONE|%s", ip_address);

    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(msg), PBUF_RAM);
    if (p) {
        memcpy(p->payload, msg, strlen(msg));
        udp_sendto(broadcast_pcb, p, IP_ADDR_BROADCAST, UDP_PORT);
        pbuf_free(p);
    }
    cyw43_arch_lwip_end();

    if (p)
        printf("Broadcast: %s\n", msg);
}

/**
 * Function to receive phone acknowledgement message.
 *
 * The phone is identified by the source address of the datagram, not by the
 * address written in the message.
 *
 * @param arg
 * @param pcb
 * @param p
 * @param addr
 * @param port
 */
void receive_phone_acknowledgment(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p) return;

    char received_msg[32];
    u16_t copied = pbuf_copy_partial(p, received_msg, sizeof(received_msg) - 1, 0);
    received_msg[copied] = '\0';
    if (!phone_acknowledged && strncmp(received_msg, "PHONE|", 6) == 0) {
        ip_addr_copy(phone_addr, *addr);
        phone_acknowledged = true;
        printf("Received acknowledgement from phone: %s\n", ipaddr_ntoa(addr));
    }

    pbuf_free(p);
}

/**
 * Function to receive AETR values from phone.
 *
 * Packets are accepted only from the acknowledged phone, and only if they
 * decode to plausible channel values.
 *
 * @param arg
 * @param pcb
 * @param p
 * @param addr
 * @param port
 */
void receive_aetr_values(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p) return;

    uint8_t payload[CONTROL_PACKET_LENGTH];
    uint16_t channels[CONTROL_CHANNEL_COUNT];
    bool valid = ip_addr_cmp(addr, &phone_addr)
        && p->tot_len == CONTROL_PACKET_LENGTH
        && pbuf_copy_partial(p, payload, sizeof(payload), 0) == sizeof(payload)
        && control_packet_decode(payload, sizeof(payload), channels);
    pbuf_free(p);
    if (!valid) return;

    critical_section_enter_blocking(&channels_lock);
    memcpy(current_channels, channels, sizeof(channels));
    last_packet_ms = to_ms_since_boot(get_absolute_time());
    have_packet = true;
    critical_section_exit(&channels_lock);
}

/**
 * Function to send iBUS packet over UART
 *
 * @param channels An array of 14 channels of iBUS packets
 */
void send_ibus_packet(const uint16_t *channels) {
    uint8_t packet[IBUS_FRAME_LENGTH];
    ibus_pack(channels, packet);

    uart_tx_wait_blocking(UART_ID);
    uart_write_blocking(UART_ID, packet, IBUS_FRAME_LENGTH);

#if IBUS_DEBUG_PRINT
    printf("Packet sent: ");
    for (int i = 0; i < IBUS_FRAME_LENGTH; i++)
        printf("%02X ", packet[i]);
    printf("\n");
#endif
}
