# ws2811 nodeMCU edition

This simple program uses the FreeRTOS-derived ESP8266 SDK to drive 3 strips of 100 ws2811's.

This must be compiled with -O3; the GPIO output will be too slow without optimizations enabled.
