# Psyight pixel node (Nucleo-F767ZI)

Firmware for the custom Art-Net and sACN pixel node of SPEC section 12: up to 8 WS2812-class strips on one GPIO port, driven by timer and DMA, fed over Ethernet with sync, a stored configuration, and a node-side fallback show when the signal is lost.

## Layout

- `src/core/`: hardware-free C99, also compiled into the app's unit tests (`app/tests/node_firmware_test.cpp`), which drive it with the app's own packet builders:
  - `protocol`: Art-Net 4 and sACN parsing, and ArtPollReply.
  - `node_core`: universe store and sync latch, lane mapping, signal-loss policies, the fallback show, and the 8-lane bit transpose.
  - `node_config`: the stored settings and the text config protocol.
- `src/board/`: clocks, LEDs, UART printf, watchdog, flash config, interrupts.
- `src/ws2812/`: the parallel output engine (TIM1 plus three DMA2 streams into GPIOE's BSRR).
- `src/net/`: lwIP 2.2 (no RTOS) on the MAC and LAN8742 PHY, and the UDP services.
- `third_party/` (gitignored): ST's HAL, CMSIS, lwIP port and PHY driver at pinned commits, fetched by `fetch_deps.ps1`.

## Build

Needs arm-none-eabi-gcc 13.3 (the one in STM32CubeIDE 1.19 is found by itself; otherwise unzip the Arm GNU Toolchain 13.3.rel1 and pass its `bin` folder as `-DARM_TOOLCHAIN_DIR=...` or in the `ARM_TOOLCHAIN_DIR` environment variable), CMake 3.25 or later and Ninja. From `firmware/node-f767`:

```
powershell -ExecutionPolicy Bypass -File fetch_deps.ps1
cmake -S . -B build/f1 -G Ninja -DCMAKE_TOOLCHAIN_FILE=%CD%/cmake/arm-none-eabi.cmake -DNODE_STAGE=1
cmake --build build/f1
```

Use `-DNODE_STAGE=2` or `3` for the other stages. The toolchain file must be given as an absolute path. Each build writes `psyight_node_f<stage>.elf`, `.bin` and `.hex`. At commit time the sizes were: F1 12.8 KB flash and 99 KB RAM, F2 49 KB and 70 KB, F3 56 KB and 176 KB, against 1792 KB of flash (the last 256 KB hold the config) and 512 KB of RAM.

## Flash

With STM32CubeProgrammer, which ships inside STM32CubeIDE, over the Nucleo's ST-LINK:

```
STM32_Programmer_CLI -c port=SWD -w build/f3/psyight_node_f3.elf -v -rst
```

Alternatively, copy the `.bin` onto the NODE_F767ZI USB drive the ST-LINK presents. To debug, open the `.elf` in STM32CubeIDE through Run > Debug Configurations > STM32 C/C++ Application, pointing at it with "Disable auto build".

The serial console is the ST-LINK virtual COM port at 115200 8N1, carrying start-up information and status lines.

## Wiring

- Lanes 0 to 7: PE0 to PE7 (Zio and morpho connectors). The default config drives lanes 0 to 3 (the app's 2.5 by 1.2 m test frame, `active 15`).
- Each lane goes through a 74HCT245 (3.3 V to 5 V) with a 33 to 100 Ω series resistor at the output, into the strip's DIN.
- The board's ground must be common with the strips' supply ground. Fuse each output and inject power along long strips (SPEC 12.2, 13).
- Ethernet: the Nucleo's RJ45.
- LEDs:
  - LD1 (green) blinks at 1 Hz while alive.
  - LD2 (blue) is on while data arrives.
  - LD3 (red) is on without an Ethernet link, or after a fault.

## Bring-up (SPEC 12.4)

### F1: strips only

Flash F1 and scope PE0. Each bit must be 1.25 µs long (±150 ns). A 0 bit is high for 0.40 µs and a 1 bit for 0.80 µs, each within ±150 ns. The low gap between frames must be at least 280 µs. Each lane shows a short chase in its own colour at about 10% brightness, and the console prints frames per second and the transpose time.

Check that bit 0 of a frame is not missing. The timer starts at its top value so that the first event is the rising edge; if the first bit were lost, the colours would come out shifted.

### F2: network only

Flash F2 and connect it to the network. The console prints the node's address (from DHCP, or after 10 s without a lease the fallback 2.x.y.z/8 address derived from the MAC), then, once a second, each universe's packets, last sequence number and size.

In the app, use the Preview's Output section: sACN multicast, or unicast or Art-Net to the printed address. Step the top bar to Ambient, React or Show, and watch universes 1 to 3 count at about 60 packets a second with the sequence numbers advancing. The Preflight tab's "Discover Art-Net nodes" should list the node.

### F3: the node

Flash F3. Frames from the app reach the strips, and the console reports output frames per second, packets, latches, sequence gaps and ignored universes every 5 s. Then check:

- **Sync:** sACN sync universe 1000, or ArtSync. Every strip changes on the same frame.
- **Signal loss:** stop sending, and after `loss_ms` (default 2 s) the policy runs. The default is `fallback`, a dim violet plasma.
- **Watchdog:** the console says "after a watchdog reset" if the node ever reset itself.

## Configuration (UDP port 6455)

Send one text command per datagram; the reply is text. From PowerShell:

```
$u = New-Object System.Net.Sockets.UdpClient
$u.Client.ReceiveTimeout = 2000
function node($cmd) { $b = [Text.Encoding]::ASCII.GetBytes($cmd); [void]$u.Send($b, $b.Length, "2.18.52.86", 6455); $ep = $null; [Text.Encoding]::ASCII.GetString($u.Receive([ref]$ep)) }
node "get"
node "set lane4 444 60"
node "set active 31"
node "save"
node "reboot"
```

Replace the address with the node's. The keys are listed in `src/core/node_config.h`.

The easy way: the app's Preflight tab > Node setup works out the universes and data lines the current rig needs, sends them (with the fallback show's colours), applies them and reads them back; Save on the node keeps them.

Protocol 2 (`get` starts with `protocol 2`): `apply` puts the rig settings to use at once, between two frames, with no flash write and no restart: universes, lanes, `active`, the signal-loss settings and the fallback show, including `palette` (one to four `RRGGBB`). Names and `protocols` act at once; the network settings (ip, mask, gateway, dhcp) after save and reboot. `save` erases a 256 KB flash sector, which pauses output and network for 1 to 2 s.

Protocol 3 (`get` starts with `protocol 3`) adds `order0` to `order7`, each lane's colour order (`rgb`, `grb`, `brg`, `rbg`, `gbr`, `bgr`) for the fallback show; live data already arrives in the strip's order from the app. The palette is kept as RGB. A configuration saved by protocol 2 firmware is read and upgraded at start (lanes GRB), so updating the firmware keeps the node's settings. The app sends the orders only to protocol 3 nodes, and sets the signal-loss policy and fallback show from the project.

Wiring, pinout, flashing and set-up as a printable guide: `docs/NODE_WIRING_GUIDE.pdf` (regenerate with `python tools/python/make_node_guide.py`, which needs reportlab).

Lanes are mapped by global pixel index, the way the app patches (170 pixels a universe, strips running across universe boundaries). Each lane is `first_pixel count`.

## To verify on the bench

- WS2812 timing and the first bit, as above, also while the network is busy, since Ethernet DMA and LED DMA share the bus matrix.
- That the ST-LINK MCO really supplies the 8 MHz HSE (Nucleo-144 user manual, UM1974). If the UART baud rate or the LED timing is off by a constant factor, this is why.
- Link up at 100 Mbit full duplex. Unplug and replug the cable: the node must come back (MAC speed and duplex follow the PHY).
- sACN multicast through the switch (IGMP snooping), and that the node joins the sync universe's group.
- ArtPollReply in the app's discovery, and in a third-party tool if available.
- Config save and reload across a power cycle. Corrupt or erased flash must fall back to defaults.

Not yet built: ArtAddress (renaming over Art-Net), DMX512 outputs, RS-485 lanes, a per-PSU current limit on the node, and an ArtPollReply sent unprompted on start-up.
