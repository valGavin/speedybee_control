# speedybee_control
A C code for RPi Pico W to work as drone receiver using WiFi connectivity.

Part of [vGP-Drone](https://github.com/valGavin/vGP-Drone), which holds the
project documentation.

## How It Works?
Upon starting, the device will look for and try to connect to the WiFi with the given SSID and password. After connected, the device will perform a UDP broadcast, sending out message: DRONE|<IP address>. It'll blink the LED with one second interval while waiting for the acknowldege respond. After receiving the acknowledgement, it'll wait for the AETR values, converts them into iBUS packets, and send them through UART1 (TX on GP4).

## Control Link
- Control packets are accepted only from the phone that sent the acknowledgement, and only if they are exactly 10 bytes with every value between 800 and 2200.
- iBUS frames are sent only while the link is alive: WiFi joined **and** a valid control packet received within the last 500 ms (`CONTROL_TIMEOUT_MS`).
- When the link is lost, iBUS output stops and the channels are reset to disarmed defaults. The flight controller sees signal loss and applies its own failsafe, so **Betaflight's failsafe must be configured**.
- The phone therefore has to send packets continuously, not only when a stick moves. Use a `vgp_drone_controller` build that includes the fixed-rate sender.

## Tests
The iBUS framing, control packet decoding and link timeout have host unit tests that need only a C compiler:
```
gcc -Wall -Wextra -I. test/test_host.c -o test_host && ./test_host
```

## Debugging
Set `IBUS_DEBUG_PRINT` to 1 to print every iBUS frame over stdio. This slows the iBUS loop, so leave it off for normal use.
