# speedybee_control
A C code for RPi Pico W to work as drone receiver using WiFi connectivity.

## How It Works?
Upon starting, the device will look for and try to connect to the WiFi with the given SSID and password. After connected, the device will perform a UDP broadcast, sending out message: DRONE|<IP address>. It'll blink the LED with one second interval while waiting for the acknowldege respond. After receiving the acknowledgement, it'll wait for the AETR values, converts them into iBUS packets, and send them through the UART0.
