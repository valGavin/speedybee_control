#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

// Configuration
#define WIFI_SSID "val Gavin"
#define WIFI_PASSWORD "db4ntwb4rry"
#define UDP_PORT 8888
#define CONTROL_PORT 9877
#define LED_PIN CYW43_WL_GPIO_LED_PIN

// iBUS Configuration
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

// Global variables
char phone_ip[16] = "";
struct udp_pcb *broadcast_pcb, *control_pcb;
struct netif *netif;

// Function declaration
void send_broadcast(char *ip_address);
void receive_phone_acknowledgment(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void receive_aetr_values(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void send_ibus_packet(uint16_t *channels);

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("WiFi initialization failed\n");
        return -1;
    }

    // Blink LED at 250ms interval while connecting
    printf("Connecting to WiFi...\n");
    cyw43_arch_enable_sta_mode();
    int blink_state = 0;
    while (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK)) {
        cyw43_arch_gpio_put(LED_PIN, blink_state);
        blink_state = !blink_state;
        sleep_ms(250);
    }

    // WiFi connected. Blink LED at one second interval
    printf("Connected to WiFi!\n");
    netif = netif_list;
    char pico_ip[16];
    snprintf(pico_ip, sizeof(pico_ip), "%s", ipaddr_ntoa(netif_ip4_addr(netif)));
    printf("Pico W IP Address: %s\n", pico_ip);

    broadcast_pcb = udp_new();
    udp_bind(broadcast_pcb, IP_ADDR_ANY, UDP_PORT);

    control_pcb = udp_new();
    udp_bind(control_pcb, IP_ADDR_ANY, CONTROL_PORT);
    udp_recv(control_pcb, receive_phone_acknowledgment, NULL);

    int broadcast_counter = 0;
    while (strlen(phone_ip) == 0) {
        if (broadcast_counter % 2 == 0)
            send_broadcast(pico_ip);
        cyw43_arch_gpio_put(LED_PIN, broadcast_counter % 2);
        broadcast_counter++;
        sleep_ms(1000);
    }

    // Phone acknowledged. LED OFF
    printf("Phone connected at %s\n", phone_ip);
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Listen for AETR values from phone
    udp_recv(control_pcb, receive_aetr_values, NULL);

    while (1)
        sleep_ms(100);
}

/**
 * Function to send UDP broadcast with Pico W's IP
 *
 * @param ip_address Pico W's IP Address
 */
void send_broadcast(char *ip_address) {
    char msg[64];
    snprintf(msg, sizeof(msg), "DRONE|%s", ip_address);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(msg), PBUF_RAM);
    if (p) {
        memcpy(p->payload, msg, strlen(msg));
        udp_sendto(broadcast_pcb, p, IP_ADDR_BROADCAST, UDP_PORT);
        pbuf_free(p);
        printf("Broadcast: %s\n", msg);
    }
}

/**
 * Function to receive phone acknowledgement message.
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
    strncpy(received_msg, (char *)p->payload, p->len);
    received_msg[p->len] = '\0';
    if (strncmp(received_msg, "PHONE|", 6) == 0) {
        strncpy(phone_ip, received_msg + 6, 15);
        phone_ip[15] = '\0';
        printf("Received acknowledgement from phone: %s\n", phone_ip);
    }

    pbuf_free(p);
}

/**
 * Function to receive AETR values from phone and send as iBUS.
 *
 * @param arg
 * @param pcb
 * @param p
 * @param addr
 * @param port
 */
void receive_aetr_values(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p || p->len != 8) return;

    uint16_t channels[14] = {1500};  // Default iBUS values
    memcpy(channels, p->payload, 8);

    send_ibus_packet(channels);
    pbuf_free(p);
}

/**
 * Function to send iBUS packet over UART
 *
 * @param channels An array of 14 channels of iBUS packets
 */
void send_ibus_packet(uint16_t *channels) {
    uint8_t packet[32];
    packet[0] = 0x20;  // iBUS start byte
    packet[1] = 0x2C;  // Packet length (32 bytes total)
    for (int i = 0; i < 14; i++) {  //Pack 14x 16-bit channel values in little-endian order
        packet[2 + (i * 2)] = channels[i] & 0xFF;  // Low byte
        packet[3 + (i * 2)] = (channels[i] >> 8) & 0xFF;  // High byte
    }

    // Compute the correct CRC checksum
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < 30; i++)
        crc -= packet[i];
    packet[30] = crc & 0xFF;
    packet[31] = (crc >> 8) & 0xFF;

    uart_tx_wait_blocking(UART_ID);
    uart_write_blocking(UART_ID, packet, 32);
}

