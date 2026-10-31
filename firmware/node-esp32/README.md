# Psyight pixel node on ESP32 (WiFi)

The STM32 node (`../node-f767`) ported to an **ESP32 DevKit (WROOM-32)** for WiFi: Art-Net and sACN in, one WS2812-class strip out on **GPIO2 (D2)**. It shares the STM32 node's hardware-free core unchanged (`../node-f767/src/core`: packet parsing, sync latching, signal-loss policies and the fallback show, the text configuration protocol), which the app's unit tests cover, so the app sees the same node: Preflight > Node setup and Art-Net discovery work as they do over Ethernet.

## Layout

- `src/main.c`: start-up, the LED loop (core 1), the command handler (the shared protocol plus the WiFi settings), the USB serial console.
- `src/led.c`: WS2812 on the RMT peripheral (10 MHz: 0 bit 0.4 µs high, 1 bit 0.8 µs high, 300 µs latch).
- `src/net.c`: UDP sockets on 6454 (Art-Net, ArtPollReply to the subnet's broadcast address), 5568 (sACN, a multicast join per universe and the sync universe), 4048 (DDP, always on) and 6455 (configuration), one task on core 0.
- `src/wifi.c`: joins the stored network; opens its own access point when it cannot.
- `src/config_store.c`: the settings and WiFi credentials in NVS.

## Build and flash

PlatformIO (the VS Code extension, or `pio` from `%USERPROFILE%\.platformio\penv\Scripts`), from `firmware/node-esp32`:

```
pio run                 # build (the first build downloads ESP-IDF)
pio run -t upload       # flash over USB
pio device monitor      # console, 115200 8N1
```

## WiFi

- **Joining:** the node joins the stored network (DHCP, or the config's fixed address with `dhcp 0`). Power saving is off, so sACN at 60 fps arrives on time.
- **Fallback access point:** with no network stored, or not joined within 20 s, it also opens its own access point `Psyight-node-XXXX` (the MAC's last two bytes), password `psyight-node`, the node at 192.168.4.1, and keeps trying to join. Once joined, the access point closes.
- **Setting the network**, over the USB console or UDP 6455:
  ```
  set wifi_ssid <network>
  set wifi_password <password>
  save
  reboot
  ```
  `get` lists the network (never the password) and whether the node is joined.
- **A compiled-in default:** copy `src/wifi_secrets.example.h` to `src/wifi_secrets.h` (gitignored: never commit it) to give the node a network to use when none is stored.

## Wiring

- GPIO2 (labelled D2) through a 74HCT245 (3.3 V to 5 V) and a 33 to 100 Ω series resistor into the strip's DIN; grounds common. On many DevKits GPIO2 also drives the blue on-board LED, which then flickers with the data: harmless.
- One output: lane 0 of the shared configuration (`lane0 <first pixel> <count>`, up to 680 pixels); `active` and lanes 1 to 7 are ignored.

## In the app

The node answers ArtPoll, so Preflight's discovery lists it. Send sACN unicast to its address (WiFi multicast is less reliable than on a cable), then Preflight > Node setup > Set up the node and Save on the node, as for the STM32 node.

## Console

Every 5 s: address, WiFi state and signal, output frames per second, packets, latches, sequence gaps, losses (times the signal was taken as lost), ignored universes, ArtPolls.
