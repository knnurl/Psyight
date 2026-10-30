# The ESP32 node: its own board, WiFi, network and LED code, and the STM32 node's hardware-free
# core (packets, latching, fallback show, config protocol), shared unchanged and tested by the
# app's unit tests.
set(CORE ${CMAKE_CURRENT_LIST_DIR}/../../node-f767/src/core)
idf_component_register(
    SRCS "main.c" "net.c" "wifi.c" "led.c" "config_store.c"
         "${CORE}/node_core.c" "${CORE}/node_config.c" "${CORE}/protocol.c"
    INCLUDE_DIRS "." "${CORE}")
