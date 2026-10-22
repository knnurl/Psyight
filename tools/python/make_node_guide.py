"""Writes docs/NODE_WIRING_GUIDE.pdf: wiring, pinout, flashing and set-up of the Psyight pixel node
(firmware/node-f767 on a NUCLEO-F767ZI).

Usage, from the repository root (needs reportlab):
    python tools/python/make_node_guide.py

Pin positions are from ST's UM1974 Rev 7 (Table 13: Zio connectors of the NUCLEO-F746ZG, F756ZG and
F767ZI; Table 21: ST morpho connectors; Table 11: Ethernet pins) and the firmware's own sources
(src/ws2812, src/board, src/net). Keep them in step when the firmware's pins change.
"""

import pathlib

from reportlab.graphics.shapes import Drawing, Line, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (KeepTogether, Paragraph, SimpleDocTemplate, Spacer,
                                Table, TableStyle)

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "NODE_WIRING_GUIDE.pdf"

ACCENT = colors.HexColor("#5B2BD6")
DIM = colors.HexColor("#5A5F6A")
RULE = colors.HexColor("#D5D8DE")
SOFT = colors.HexColor("#F1EEFB")
WARN = colors.HexColor("#B4232C")

# Lane: (STM32 pin, Zio connector pin, ST morpho pin, Zio name).
LANES = [
    ("PE0", "CN10 pin 33", "CN12 pin 64", "D34"),
    ("PE1", "(not on Zio)", "CN11 pin 61", "-"),
    ("PE2", "CN9 pin 14", "CN11 pin 46", "D56"),
    ("PE3", "CN9 pin 22", "CN11 pin 47", "D60"),
    ("PE4", "CN9 pin 16", "CN11 pin 48", "D57"),
    ("PE5", "CN9 pin 18", "CN11 pin 50", "D58"),
    ("PE6", "CN9 pin 20", "CN11 pin 62", "D59"),
    ("PE7", "CN10 pin 20", "CN12 pin 44", "D41"),
]

styles = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=styles["BodyText"], fontName="Helvetica", fontSize=9.5,
                      leading=13, spaceAfter=5)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=8, leading=10.5, textColor=DIM)
CELL = ParagraphStyle("cell", parent=BODY, fontSize=8.5, leading=11, spaceAfter=0)
CELL_B = ParagraphStyle("cellb", parent=CELL, fontName="Helvetica-Bold")
H1 = ParagraphStyle("h1", parent=styles["Heading1"], fontName="Helvetica-Bold", fontSize=20,
                    leading=24, textColor=colors.black, spaceAfter=4)
H2 = ParagraphStyle("h2", parent=styles["Heading2"], fontName="Helvetica-Bold", fontSize=13,
                    leading=16, textColor=ACCENT, spaceBefore=10, spaceAfter=5)
H3 = ParagraphStyle("h3", parent=styles["Heading3"], fontName="Helvetica-Bold", fontSize=10.5,
                    leading=13, spaceBefore=6, spaceAfter=3)
CODE = ParagraphStyle("code", parent=BODY, fontName="Courier", fontSize=8.3, leading=10.5,
                      backColor=colors.HexColor("#F4F5F7"), borderPadding=(4, 5, 4, 5),
                      spaceBefore=3, spaceAfter=7, alignment=TA_LEFT)


def p(text, style=BODY):
    return Paragraph(text, style)


def bullets(items):
    return [Paragraph("&bull;&nbsp;&nbsp;" + item, ParagraphStyle(
        "bullet", parent=BODY, leftIndent=10, firstLineIndent=-8, spaceAfter=2)) for item in items]


def table(rows, widths, header=True):
    data = [[Paragraph(str(c), CELL_B if header and r == 0 else CELL) for c in row]
            for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [
        ("GRID", (0, 0), (-1, -1), 0.4, RULE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]
    if header:
        style.append(("BACKGROUND", (0, 0), (-1, 0), SOFT))
    t.setStyle(TableStyle(style))
    return t


def box(d, x, y, w, h, title, lines=(), fill=colors.white):
    d.add(Rect(x, y, w, h, strokeColor=colors.black, fillColor=fill, strokeWidth=0.8))
    d.add(String(x + w / 2, y + h - 12, title, fontName="Helvetica-Bold", fontSize=8.5,
                 textAnchor="middle"))
    for i, line in enumerate(lines):
        d.add(String(x + w / 2, y + h - 24 - 10 * i, line, fontName="Helvetica", fontSize=7.2,
                     textAnchor="middle", fillColor=DIM))


def wire(d, points, colour=colors.black, width=1.0):
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        d.add(Line(x0, y0, x1, y1, strokeColor=colour, strokeWidth=width))


def label(d, x, y, text, size=7, colour=DIM, anchor="start"):
    d.add(String(x, y, text, fontName="Helvetica", fontSize=size, fillColor=colour,
                 textAnchor=anchor))


def lane_diagram():
    """One data line, Nucleo to strip, with power and ground."""
    d = Drawing(170 * mm, 70 * mm)
    box(d, 5, 95, 95, 85, "NUCLEO-F767ZI", ["PE0..PE7: data (3.3 V)", "GND", "RJ45: Ethernet",
                                          "USB: ST-LINK, power"], fill=SOFT)
    box(d, 150, 105, 95, 70, "74HCT245", ["A1..A8 in, B1..B8 out", "DIR (1) to 5 V",
                                         "OE (19) to GND", "VCC (20) 5 V, GND (10)"])
    d.add(Rect(285, 133, 34, 12, strokeColor=colors.black, fillColor=colors.white))
    label(d, 302, 122, "33 to 100 ohm", anchor="middle")
    box(d, 360, 105, 105, 70, "WS2812 strip", ["DIN (first pixel)", "+5 V, GND",
                                              "inject power along it"])
    box(d, 360, 10, 105, 55, "5 V supply", ["sized for the rig", "(app: power budget)"])
    # Data.
    wire(d, [(100, 140), (150, 140)], ACCENT, 1.4)
    label(d, 104, 144, "PEx", colour=ACCENT)
    wire(d, [(245, 139), (285, 139)], ACCENT, 1.4)
    wire(d, [(319, 139), (360, 139)], ACCENT, 1.4)
    label(d, 324, 144, "DIN", colour=ACCENT)
    # 5 V.
    wire(d, [(440, 65), (440, 105)], WARN, 1.2)
    label(d, 444, 82, "+5 V", colour=WARN)
    wire(d, [(420, 65), (420, 88), (198, 88), (198, 105)], WARN, 1.2)
    label(d, 202, 92, "+5 V to VCC", colour=WARN)
    # Ground: everything common.
    wire(d, [(385, 65), (385, 100), (385, 105)], colors.black, 1.2)
    wire(d, [(385, 78), (52, 78), (52, 95)], colors.black, 1.2)
    wire(d, [(175, 78), (175, 105)], colors.black, 1.2)
    label(d, 58, 70, "common ground: Nucleo GND, 245 GND, strip GND, supply 0 V")
    label(d, 230, 40, "fuse each strip feed", size=7, colour=WARN)
    return d


def header_diagram(name, pins_odd, pins_even, used):
    """Two columns of a header: odd pins left, even right; lane pins highlighted."""
    rows = len(pins_odd)
    cell = 11
    d = Drawing(64 * mm, (rows * cell + 22))
    label(d, 4, rows * cell + 10, name, size=8.5, colour=colors.black)
    for i in range(rows):
        y = rows * cell - (i + 1) * cell
        for col, (num, sig) in enumerate((pins_odd[i], pins_even[i])):
            x = 4 + col * 88
            # CN10 pin 25 is PE2 again (CN9 pin 14): an alternative, not a second lane pin.
            alias = name.startswith("CN10") and num == 25
            hit = sig in used and not alias
            fill = ACCENT if hit else SOFT if alias else colors.white
            d.add(Rect(x, y, 84, cell - 1, strokeColor=RULE, strokeWidth=0.4, fillColor=fill))
            d.add(String(x + 3, y + 2.5, f"{num}", fontName="Helvetica-Bold", fontSize=6.5,
                         fillColor=colors.white if hit else DIM))
            text = sig + (f"  lane {used[sig]}" if hit else "  (= CN9 14)" if alias else "")
            d.add(String(x + 18, y + 2.5, text, fontName="Helvetica", fontSize=6.5,
                         fillColor=colors.white if hit else colors.black))
    return d


def main():
    used = {pin: i for i, (pin, *_rest) in enumerate(LANES)}
    story = []
    story.append(p("Psyight pixel node", H1))
    story.append(p("Wiring, pinout, flashing and set-up for the NUCLEO-F767ZI running "
                   "<font name='Courier'>firmware/node-f767</font> (stage F3, config protocol 3).",
                   ParagraphStyle("sub", parent=BODY, fontSize=10.5, textColor=DIM)))
    story.append(Spacer(1, 4))
    story.append(table([
        ["What", "Where"],
        ["Data lines (8)", "PE0 to PE7, one WS2812-class strip chain each, through a 74HCT245"],
        ["Network", "RJ45 (CN14), 100 Mbit; Art-Net (UDP 6454) and sACN; config on UDP 6455"],
        ["Programming and console", "USB on the ST-LINK side (CN1): flashing, and a virtual COM "
                                    "port at 115200 8N1"],
        ["Power for the board", "USB (ST-LINK) for the bench; the strips never draw from the "
                                "Nucleo"],
    ], [42 * mm, 128 * mm]))

    story.append(p("1. One data line, end to end", H2))
    story.append(lane_diagram())
    story.append(p("The MCU's outputs are 3.3 V; WS2812 inputs want at least 0.7 x VDD (3.5 V at "
                   "5 V), so each line goes through a 74HCT245 powered at 5 V (HCT inputs accept "
                   "3.3 V as high). A 33 to 100 ohm series resistor at the 245's output damps "
                   "ringing on the cable. Keep the data cable short (a few metres) or mount the "
                   "node near the strip; for long runs use RS-485 per line (not built yet)."))

    story.append(p("2. Data line pins", H2))
    story.append(p("Each line can be reached on a Zio connector (the Arduino-style headers) or on "
                   "the ST morpho headers (the long double rows at the board's edges). Use one or "
                   "the other for a pin, not both. The app's Preflight > Node setup numbers lines "
                   "1 to 8; line n is lane n-1 here."))
    rows = [["Line (app)", "Lane", "STM32 pin", "Zio connector", "ST morpho", "Zio name"]]
    for i, (pin, zio, morpho, dname) in enumerate(LANES):
        rows.append([str(i + 1), str(i), pin, zio, morpho, dname])
    story.append(table(rows, [20 * mm, 14 * mm, 20 * mm, 32 * mm, 30 * mm, 20 * mm]))
    story.append(Spacer(1, 3))
    story.extend(bullets([
        "PE1 is only on the morpho header (CN11 pin 61).",
        "PE2 also appears on CN10 pin 25 (QSPI_BK1_IO2): CN9 pin 14 and CN10 pin 25 are the same "
        "pin. Wire only one of them.",
        "PE2 to PE6 double as the trace port (TRACECLK, TRACED0 to 3); nothing uses tracing here, so "
        "they are free.",
        "The default configuration drives lines 1 to 4 (the app's 2.5 by 1.2 m test frame, "
        "<font name='Courier'>active 15</font>); Node setup sets the lines the current rig needs.",
    ]))

    story.append(p("3. Power, ground and the other pins the node uses", H2))
    story.append(table([
        ["Signal", "Zio", "ST morpho", "Notes"],
        ["+5 V", "CN8 pin 9", "CN11 pin 18", "Board 5 V. Do not feed strips from it."],
        ["+3.3 V", "CN8 pin 7", "CN11 pin 16", "Not needed by the 245 (it runs at 5 V)."],
        ["GND", "CN8 pins 11, 13; CN9 pin 23; CN10 pins 5, 17, 22, 27",
         "CN11 pins 8, 19, 20, 22, 49, 60; CN12 pins 9, 20, 39, 54, 63",
         "Tie to the 245's GND and the strip supply's 0 V."],
        ["LD1 green", "PB0", "", "Blinks at 1 Hz while the firmware runs."],
        ["LD2 blue", "PB7", "", "On while show data arrives."],
        ["LD3 red", "PB14", "", "On without an Ethernet link, or after a fault."],
        ["Console", "USART3: PD8 TX, PD9 RX", "", "Wired to the ST-LINK: the USB virtual COM "
                                                 "port, 115200 8N1."],
        ["Ethernet (RMII)", "PA1, PA2, PA7, PB13, PC1, PC4, PC5, PG11, PG13", "",
         "Onboard LAN8742A PHY. Jumpers JP6 and JP7 must be fitted (closed) for Ethernet."],
        ["Timer and DMA", "TIM1, DMA2 (three streams into GPIOE BSRR)", "",
         "Internal: no pins. TIM1's own pins (PE9, PE11, PE13) stay free."],
    ], [25 * mm, 44 * mm, 44 * mm, 57 * mm]))

    header_section = [p("4. Header maps (lane pins highlighted)", H2)]
    header_section.append(p("Pin 1 of each header is marked on the board's silkscreen; odd pins form one "
                   "row and even pins the other. Only the stretch holding lane pins is drawn.",
                   SMALL))
    story_header = header_section
    cn9_odd = [(n, s) for n, s in [(13, "NC"), (15, "PA7"), (17, "PF2"), (19, "PF1"),
                                   (21, "PF0"), (23, "GND"), (25, "PD0"), (27, "PD1"),
                                   (29, "PG0")]]
    cn9_even = [(n, s) for n, s in [(14, "PE2"), (16, "PE4"), (18, "PE5"), (20, "PE6"),
                                    (22, "PE3"), (24, "PF8"), (26, "PF7"), (28, "PF9"),
                                    (30, "PG1")]]
    cn10_odd = [(17, "GND"), (19, "PD13"), (21, "PD12"), (23, "PD11"), (25, "PE2"),
                (27, "GND"), (29, "PA0"), (31, "PB0"), (33, "PE0")]
    cn10_even = [(16, "PG9"), (18, "PE8"), (20, "PE7"), (22, "GND"), (24, "PE10"),
                 (26, "PE12"), (28, "PE14"), (30, "PE15"), (32, "PB10")]
    cn11_odd = [(43, "PD6"), (45, "PD7"), (47, "PE3"), (49, "GND"), (51, "PF1"), (53, "PF0"),
                (55, "PD1"), (57, "PD0"), (59, "PG0"), (61, "PE1")]
    cn11_even = [(44, "PG3"), (46, "PE2"), (48, "PE4"), (50, "PE5"), (52, "PF2"), (54, "PF8"),
                 (56, "PF9"), (58, "PG1"), (60, "GND"), (62, "PE6")]
    cn12_odd = [(39, "GND"), (41, "PD13"), (43, "PD12"), (45, "PD11"), (47, "PE10"),
                (49, "PE12"), (51, "PE14"), (53, "PE15"), (55, "PE13"), (57, "PF13"),
                (59, "PF12"), (61, "PG14"), (63, "GND")]
    cn12_even = [(40, "PE8"), (42, "PF10"), (44, "PE7"), (46, "PD14"), (48, "PD15"),
                 (50, "PF14"), (52, "PE9"), (54, "GND"), (56, "PE11"), (58, "PF3"),
                 (60, "PF15"), (62, "PF11"), (64, "PE0")]
    grid = Table([
        [header_diagram("CN9 (Zio), pins 13 to 30", cn9_odd, cn9_even, used),
         header_diagram("CN10 (Zio), pins 16 to 33", cn10_odd, cn10_even, used)],
        [header_diagram("CN11 (morpho), pins 43 to 62", cn11_odd, cn11_even, used),
         header_diagram("CN12 (morpho), pins 39 to 64", cn12_odd, cn12_even, used)],
    ], colWidths=[85 * mm, 85 * mm])
    grid.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
                              ("BOTTOMPADDING", (0, 0), (-1, -1), 8)]))
    story_header.append(grid)
    story.append(KeepTogether(story_header))
    story.append(p("For the full sets the morpho headers are the easiest: lanes 1 to 6 sit together "
                   "on CN11 (pins 46 to 62), lanes 0 and 7 on CN12 (pins 64 and 44), with grounds "
                   "at CN11 pin 49 and CN12 pin 63 next to them.", SMALL))

    story.append(p("5. 74HCT245 connections (DIP-20 or SO-20)", H2))
    story.append(table([
        ["245 pin", "Name", "Connect to"],
        ["1", "DIR", "+5 V (A to B)"],
        ["2 to 9", "A1 to A8", "PE0 to PE7 (lane 0 on A1 ... lane 7 on A8)"],
        ["10", "GND", "Common ground"],
        ["18 down to 11", "B1 to B8", "Series resistor, then strip DIN (B1 for lane 0 ... B8 for "
                                       "lane 7)"],
        ["19", "OE (active low)", "GND (always enabled)"],
        ["20", "VCC", "+5 V, with a 100 nF capacitor to GND at the pin"],
    ], [28 * mm, 30 * mm, 112 * mm]))

    story.append(p("6. Power and safety", H2))
    story.extend(bullets([
        "One ground for everything: the Nucleo, the 245 and every strip supply's 0 V.",
        "Fuse each output (each strip feed) for its cable and the strip's rating (SPEC 12.2, 13).",
        "Inject 5 V along long strips: a WS2812 draws up to about 60 mA at full white, so 150 "
        "pixels can draw 9 A. The app's power budget and brightness cap keep the show within the "
        "supply.",
        "Power the strips before (or with) the node, never data into an unpowered strip.",
        "The flash limiter in the app is on by default; it is a safeguard, not a certification.",
    ]))

    story.append(p("7. Building and flashing the firmware", H2))
    story.append(p("Needs STM32CubeIDE 1.19 (its arm-none-eabi-gcc 13.3 is found automatically), "
                   "CMake 3.25 or later and Ninja. From <font name='Courier'>firmware/node-f767</font>:"))
    story.append(p("powershell -ExecutionPolicy Bypass -File fetch_deps.ps1<br/>"
                   "cmake -S . -B build/f3 -G Ninja -DCMAKE_TOOLCHAIN_FILE=%CD%/cmake/arm-none-eabi.cmake "
                   "-DNODE_STAGE=3<br/>cmake --build build/f3", CODE))
    story.append(p("Plug the Nucleo's ST-LINK USB (CN1) into the PC, then flash with "
                   "STM32CubeProgrammer's command line (installed with STM32CubeIDE or on its own):"))
    story.append(p("STM32_Programmer_CLI -l st-link<br/>"
                   "STM32_Programmer_CLI -c port=SWD -w build/f3/psyight_node_f3.elf -v -rst", CODE))
    story.extend(bullets([
        "The first command lists the probe (it should say NUCLEO-F767ZI).",
        "The second erases only the sectors the program needs, writes, verifies and resets. The "
        "node's saved settings (the last flash sector) are kept; settings saved by protocol 2 "
        "firmware are read and upgraded at start.",
        "Without the command line: copy <font name='Courier'>psyight_node_f3.bin</font> onto the "
        "NODE_F767ZI USB drive the ST-LINK shows.",
        "Console: open the ST-LINK virtual COM port at 115200 8N1. Every 5 s it prints the address, "
        "output frames per second, packets, latches, sequence gaps, ignored universes, and "
        "\"signal lost\" while no show data arrives.",
    ]))

    story.append(p("8. Setting the node up from the app", H2))
    story.extend(bullets([
        "Connect the node to the same network as the PC. It asks for an address by DHCP; without a "
        "lease it takes 2.x.y.z/8 (from its MAC) after 10 s. The console prints it.",
        "In Psyight: Preflight &gt; Discover Art-Net nodes, then Node setup. It shows the data lines "
        "the current rig needs (pixels and colour order per line) and any notes, for example a line "
        "too long for 60 fps (more than 545 pixels).",
        "Choose what the node does when the signal stops (hold, fade, fallback show or blackout), "
        "after how long, and the fallback show's colours, brightness and speed. These are saved "
        "with the project.",
        "Set up the node: the app sends everything and applies it at once (no restart), then reads "
        "it back and lists any difference. Save on the node keeps it across restarts (output "
        "pauses 1 to 2 s while the flash sector erases).",
    ]))
    story.append(p("Text protocol (UDP 6455), for scripts or a terminal: get, set &lt;key&gt; "
                   "&lt;value&gt;, apply, save, defaults, reboot. Keys: universe, universes, "
                   "pixels_per_universe, lane0 to lane7 (first pixel and count), order0 to order7 "
                   "(rgb, grb, brg, rbg, gbr, bgr), active (line bit mask), loss_ms, policy, "
                   "fade_ms, fallback_brightness and fallback_speed (thousandths), palette (one to "
                   "four RRGGBB), name, long_name, protocols, ip, mask, gateway, dhcp.", SMALL))

    story.append(p("9. Frame rate per line", H2))
    story.append(table([
        ["Pixels per line", "Universes", "Frame time", "Max frame rate"],
        ["170", "1", "5.4 ms", "about 185 fps"],
        ["340", "2", "10.5 ms", "about 95 fps"],
        ["510", "3", "15.6 ms", "about 64 fps"],
        ["545", "3.2", "16.7 ms", "60 fps (the show's rate)"],
        ["680 (the most a line takes)", "4", "20.7 ms", "about 48 fps"],
    ], [52 * mm, 28 * mm, 30 * mm, 60 * mm]))
    story.append(p("WS2812: 30 microseconds a pixel plus a 300 microsecond reset; all eight lines "
                   "run in parallel, so the longest line sets the rate.", SMALL))

    story.append(p("10. Bench checklist", H2))
    story.extend(bullets([
        "Scope PE0 with F1: bits 1.25 us (plus or minus 150 ns), 0 high 0.40 us, 1 high 0.80 us, "
        "frame gap at least 280 us; repeat with heavy network traffic.",
        "Link at 100 Mbit full duplex; unplug and replug: the node must come back.",
        "sACN multicast through the switch (IGMP snooping); every line changes on the same frame "
        "(sync universe 1000 or ArtSync).",
        "Stop sending: after loss_ms the chosen policy runs; check the fallback show's colours on "
        "each line (colour order per line, protocol 3).",
        "Set up from the app, apply without a restart (no glitch mid-frame), save, power cycle, "
        "read back.",
        "If the red LED stays on or the address stays 0.0.0.0: check the cable, then that JP6 and "
        "JP7 are fitted.",
    ]))
    story.append(Spacer(1, 6))
    story.append(p("Sources: ST UM1974 Rev 7, Tables 11, 13 and 21 (NUCLEO-F767ZI); Psyight "
                   "firmware sources (src/ws2812, src/board, src/net, src/core/node_config.h); "
                   "SPEC sections 12 and 13. Generated by tools/python/make_node_guide.py.", SMALL))

    def footer(canvas, doc):
        canvas.saveState()
        canvas.setFont("Helvetica", 7.5)
        canvas.setFillColor(DIM)
        canvas.drawString(18 * mm, 10 * mm, "Psyight pixel node: wiring and pinout")
        canvas.drawRightString(A4[0] - 18 * mm, 10 * mm, f"Page {doc.page}")
        canvas.restoreState()

    doc = SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm,
                            topMargin=16 * mm, bottomMargin=18 * mm,
                            title="Psyight pixel node: wiring and pinout", author="Psyight")
    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(OUT)


if __name__ == "__main__":
    main()
