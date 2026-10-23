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
POWER = colors.HexColor("#F6D5D5")
GROUND = colors.HexColor("#3B3F47")
RESERVED = colors.HexColor("#F5E2B8")

# Lane: (STM32 pin, Zio connector pin, ST morpho pin, Zio name).
LANES = [
    ("PD0", "CN9 pin 25", "CN11 pin 57", "D67"),
    ("PD1", "CN9 pin 27", "CN11 pin 55", "D66"),
    ("PD2", "CN8 pin 12", "CN11 pin 4", "D48"),
    ("PD3", "CN9 pin 10", "CN11 pin 40", "D55"),
    ("PD4", "CN9 pin 8", "CN11 pin 39", "D54"),
    ("PD5", "CN9 pin 6", "CN11 pin 41", "D53"),
    ("PD6", "CN9 pin 4", "CN11 pin 43", "D52"),
    ("PD7", "CN9 pin 2", "CN11 pin 45", "D51"),
]

# The Zio headers (UM1974 Rev 7, Table 13): pin number -> (signal, Zio name).
CN8 = {1: ("NC", ""), 3: ("IOREF", ""), 5: ("RESET", ""), 7: ("+3.3V", ""), 9: ("+5V", ""),
       11: ("GND", ""), 13: ("GND", ""), 15: ("VIN", ""),
       2: ("PC8", "D43"), 4: ("PC9", "D44"), 6: ("PC10", "D45"), 8: ("PC11", "D46"),
       10: ("PC12", "D47"), 12: ("PD2", "D48"), 14: ("PG2", "D49"), 16: ("PG3", "D50")}
CN9 = {1: ("PA3", "A0"), 3: ("PC0", "A1"), 5: ("PC3", "A2"), 7: ("PF3", "A3"),
       9: ("PF5", "A4"), 11: ("PF10", "A5"), 13: ("NC", "D72"), 15: ("PA7", "D71"),
       17: ("PF2", "D70"), 19: ("PF1", "D69"), 21: ("PF0", "D68"), 23: ("GND", ""),
       25: ("PD0", "D67"), 27: ("PD1", "D66"), 29: ("PG0", "D65"),
       2: ("PD7", "D51"), 4: ("PD6", "D52"), 6: ("PD5", "D53"), 8: ("PD4", "D54"),
       10: ("PD3", "D55"), 12: ("GND", ""), 14: ("PE2", "D56"), 16: ("PE4", "D57"),
       18: ("PE5", "D58"), 20: ("PE6", "D59"), 22: ("PE3", "D60"), 24: ("PF8", "D61"),
       26: ("PF7", "D62"), 28: ("PF9", "D63"), 30: ("PG1", "D64")}
CN7 = {1: ("PC6", "D16"), 3: ("PB15", "D17"), 5: ("PB13", "D18"), 7: ("PB12", "D19"),
       9: ("PA15", "D20"), 11: ("PC7", "D21"), 13: ("PB5", "D22"), 15: ("PB3", "D23"),
       17: ("PA4", "D24"), 19: ("PB4", "D25"),
       2: ("PB8", "D15"), 4: ("PB9", "D14"), 6: ("AREF", ""), 8: ("GND", ""),
       10: ("PA5", "D13"), 12: ("PA6", "D12"), 14: ("PA7", "D11"), 16: ("PD14", "D10"),
       18: ("PD15", "D9"), 20: ("PF12", "D8")}
CN10 = {1: ("AVDD", ""), 3: ("AGND", ""), 5: ("GND", ""), 7: ("PB1", "A6"), 9: ("PC2", "A7"),
        11: ("PF4", "A8"), 13: ("PB6", "D26"), 15: ("PB2", "D27"), 17: ("GND", ""),
        19: ("PD13", "D28"), 21: ("PD12", "D29"), 23: ("PD11", "D30"), 25: ("PE2", "D31"),
        27: ("GND", ""), 29: ("PA0", "D32"), 31: ("PB0", "D33"), 33: ("PE0", "D34"),
        2: ("PF13", "D7"), 4: ("PE9", "D6"), 6: ("PE11", "D5"), 8: ("PF14", "D4"),
        10: ("PE13", "D3"), 12: ("PF15", "D2"), 14: ("PG14", "D1"), 16: ("PG9", "D0"),
        18: ("PE8", "D42"), 20: ("PE7", "D41"), 22: ("GND", ""), 24: ("PE10", "D40"),
        26: ("PE12", "D39"), 28: ("PE14", "D38"), 30: ("PE15", "D37"), 32: ("PB10", "D36"),
        34: ("PB11", "D35")}

# Pins the node or the board use: never wire them.
ETHERNET = {"PA7", "PB13"}  # RMII on the Zio headers (JP6, JP7 fitted for Ethernet).
NOTES = {"PB0": "LD1"}      # The green LED.

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


def box(d, x, y, w, h, title, lines=(), fill=colors.white, stroke=colors.black, size=8.5):
    d.add(Rect(x, y, w, h, strokeColor=stroke, fillColor=fill, strokeWidth=0.8))
    d.add(String(x + w / 2, y + h - 12, title, fontName="Helvetica-Bold", fontSize=size,
                 textAnchor="middle"))
    for i, line in enumerate(lines):
        d.add(String(x + w / 2, y + h - 24 - 10 * i, line, fontName="Helvetica", fontSize=7.2,
                     textAnchor="middle", fillColor=DIM))


def wire(d, points, colour=colors.black, width=1.0):
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        d.add(Line(x0, y0, x1, y1, strokeColor=colour, strokeWidth=width))


def label(d, x, y, text, size=7, colour=DIM, anchor="start", bold=False):
    d.add(String(x, y, text, fontName="Helvetica-Bold" if bold else "Helvetica", fontSize=size,
                 fillColor=colour, textAnchor=anchor))


def lane_diagram():
    """One data line, Nucleo to strip, with power and ground."""
    d = Drawing(170 * mm, 70 * mm)
    box(d, 5, 95, 95, 85, "NUCLEO-F767ZI", ["PD0..PD7: data (3.3 V)", "GND", "RJ45: Ethernet",
                                          "USB: ST-LINK, power"], fill=SOFT)
    box(d, 150, 105, 95, 70, "74HCT245", ["A1..A8 in, B1..B8 out", "DIR (1) to 5 V",
                                         "OE (19) to GND", "VCC (20) 5 V, GND (10)"])
    d.add(Rect(285, 133, 34, 12, strokeColor=colors.black, fillColor=colors.white))
    label(d, 302, 122, "33 to 100 ohm", anchor="middle")
    box(d, 360, 105, 105, 70, "WS2812 strip", ["DIN (first pixel)", "+5 V, GND",
                                              "inject power along it"])
    box(d, 360, 10, 105, 55, "5 V supply", ["sized for the rig", "(app: power budget)"])
    wire(d, [(100, 140), (150, 140)], ACCENT, 1.4)
    label(d, 104, 144, "PDx", colour=ACCENT)
    wire(d, [(245, 139), (285, 139)], ACCENT, 1.4)
    wire(d, [(319, 139), (360, 139)], ACCENT, 1.4)
    label(d, 324, 144, "DIN", colour=ACCENT)
    wire(d, [(440, 65), (440, 105)], WARN, 1.2)
    label(d, 444, 82, "+5 V", colour=WARN)
    wire(d, [(420, 65), (420, 88), (198, 88), (198, 105)], WARN, 1.2)
    label(d, 202, 92, "+5 V to VCC", colour=WARN)
    wire(d, [(385, 65), (385, 105)], colors.black, 1.2)
    wire(d, [(385, 78), (52, 78), (52, 95)], colors.black, 1.2)
    wire(d, [(175, 78), (175, 105)], colors.black, 1.2)
    label(d, 58, 70, "common ground: Nucleo GND, 245 GND, strip GND, supply 0 V")
    label(d, 230, 40, "fuse each strip feed", size=7, colour=WARN)
    return d


def board_diagram():
    """The NUCLEO-F767ZI from above, schematic: where the connectors are."""
    d = Drawing(170 * mm, 118 * mm)
    bx, by, bw, bh = 120, 24, 240, 300  # The board.
    d.add(Rect(bx, by, bw, bh, strokeColor=colors.black, fillColor=colors.HexColor("#FBFBFC"),
               strokeWidth=1.0, rx=6, ry=6))
    # ST-LINK part at the top, its USB connector at the top edge.
    d.add(Line(bx, by + bh - 62, bx + bw, by + bh - 62, strokeColor=DIM, strokeWidth=0.6,
               strokeDashArray=[3, 2]))
    label(d, bx + bw / 2, by + bh - 50, "ST-LINK part", anchor="middle")
    box(d, bx + bw / 2 - 40, by + bh - 28, 80, 22, "CN1 USB", fill=SOFT, size=7.5)
    label(d, bx + bw / 2, by + bh + 6, "to the PC: power, flashing, console", anchor="middle")
    # Morpho headers along the long edges.
    box(d, bx + 6, by + 40, 18, bh - 115, "", fill=colors.HexColor("#EDEFF2"))
    label(d, bx + 15, by + 30, "CN11", anchor="middle", bold=True)
    box(d, bx + bw - 24, by + 40, 18, bh - 115, "", fill=colors.HexColor("#EDEFF2"))
    label(d, bx + bw - 15, by + 30, "CN12", anchor="middle", bold=True)
    label(d, bx - 6, by + 120, "ST morpho", anchor="end")
    label(d, bx - 6, by + 110, "(outer rows)", anchor="end")
    # Zio headers: CN8 and CN9 on the left, CN7 and CN10 on the right.
    zio = colors.HexColor("#E3E6EB")
    box(d, bx + 34, by + 186, 24, 40, "", fill=zio)
    label(d, bx + 46, by + 229, "CN8", anchor="middle", bold=True)
    box(d, bx + 34, by + 64, 24, 116, "", fill=SOFT, stroke=ACCENT)
    label(d, bx + 46, by + 54, "CN9", anchor="middle", bold=True, colour=ACCENT)
    box(d, bx + bw - 58, by + 180, 24, 46, "", fill=zio)
    label(d, bx + bw - 46, by + 229, "CN7", anchor="middle", bold=True)
    box(d, bx + bw - 58, by + 64, 24, 110, "", fill=zio)
    label(d, bx + bw - 46, by + 54, "CN10", anchor="middle", bold=True)
    # The data pins, called out.
    wire(d, [(bx + 58, by + 205), (bx + 80, by + 205)], ACCENT, 1.2)
    label(d, bx + 83, by + 202, "CN8 pin 12: PD2 (line 3)", colour=ACCENT, size=6.8)
    wire(d, [(bx + 58, by + 168), (bx + 80, by + 168)], ACCENT, 1.2)
    label(d, bx + 83, by + 170, "CN9 pins 2 to 10: PD7..PD3", colour=ACCENT, size=6.8)
    label(d, bx + 83, by + 161, "(lines 8 to 4), GND at pin 12", colour=ACCENT, size=6.8)
    wire(d, [(bx + 58, by + 112), (bx + 80, by + 112)], ACCENT, 1.2)
    label(d, bx + 83, by + 114, "CN9 pins 25, 27: PD0, PD1", colour=ACCENT, size=6.8)
    label(d, bx + 83, by + 105, "(lines 1, 2), GND at pin 23", colour=ACCENT, size=6.8)
    # Bottom edge: RJ45 and the user USB; the LEDs.
    box(d, bx + bw / 2 + 10, by + 6, 70, 26, "CN14 RJ45", fill=SOFT, size=7.5)
    label(d, bx + bw / 2 + 45, by - 8, "Ethernet to the PC or switch", anchor="middle")
    box(d, bx + 30, by + 6, 60, 22, "CN13 USB", size=7.5)
    label(d, bx + 60, by - 8, "(user USB: unused)", anchor="middle")
    label(d, bx + bw / 2, by + 42, "LD1 green, LD2 blue, LD3 red", anchor="middle")
    # Notes on the right.
    tx = bx + bw + 14
    label(d, tx, by + 270, "Top view, schematic:", size=7.5, colour=colors.black, bold=True)
    label(d, tx, by + 259, "not to scale. The silkscreen", size=7.2)
    label(d, tx, by + 249, "names each connector and", size=7.2)
    label(d, tx, by + 239, "marks pin 1: check it", size=7.2)
    label(d, tx, by + 229, "before wiring.", size=7.2)
    label(d, tx, by + 205, "Jumpers JP6 and JP7 fitted", size=7.2)
    label(d, tx, by + 195, "(Ethernet needs them).", size=7.2)
    label(d, tx, by + 171, "Zio: odd pins one row,", size=7.2)
    label(d, tx, by + 161, "even pins the other.", size=7.2)
    return d


def zio_header(name, pins, used):
    """A Zio header drawn pin by pin: odd pins left, even right; coloured by use."""
    rows = (max(pins) + 1) // 2
    cell = 10.5
    width = 84
    d = Drawing(2 * width + 12, rows * cell + 18)
    label(d, 2, rows * cell + 6, name, size=8.5, colour=colors.black, bold=True)
    for r in range(rows):
        y = rows * cell - (r + 1) * cell
        for col, num in enumerate((2 * r + 1, 2 * r + 2)):
            if num not in pins:
                continue
            sig, zname = pins[num]
            x = 2 + col * (width + 6)
            text_colour = colors.black
            extra = ""
            if sig in used:
                fill, text_colour, extra = ACCENT, colors.white, f"  line {used[sig] + 1}"
            elif sig in ("GND", "AGND"):
                fill, text_colour = GROUND, colors.white
            elif sig in ("+5V", "+3.3V", "VIN", "IOREF", "AVDD", "AREF"):
                fill = POWER
            elif sig in ETHERNET:
                fill, extra = RESERVED, "  Ethernet"
            elif sig in NOTES:
                fill, extra = colors.HexColor("#E8F3EA"), "  " + NOTES[sig]
            else:
                fill = colors.white
            d.add(Rect(x, y, width, cell - 1, strokeColor=RULE, strokeWidth=0.4, fillColor=fill))
            d.add(String(x + 2.5, y + 2.4, f"{num}", fontName="Helvetica-Bold", fontSize=6.3,
                         fillColor=text_colour if fill in (ACCENT, GROUND) else DIM))
            d.add(String(x + 15, y + 2.4, sig + (f" {zname}" if zname else "") + extra,
                         fontName="Helvetica", fontSize=6.3, fillColor=text_colour))
    return d


def legend():
    d = Drawing(170 * mm, 14)
    items = [(ACCENT, "data line (PD0..PD7)"), (GROUND, "ground"), (POWER, "power, reference"),
             (RESERVED, "Ethernet: do not use"), (colors.HexColor("#E8F3EA"), "on-board LED")]
    x = 2
    for fill, text in items:
        d.add(Rect(x, 3, 9, 8, strokeColor=RULE, strokeWidth=0.4, fillColor=fill))
        label(d, x + 12, 4, text, size=7)
        x += 12 + len(text) * 3.6 + 14
    return d


def main():
    used = {pin: i for i, (pin, *_rest) in enumerate(LANES)}
    story = []
    story.append(p("Psyight pixel node", H1))
    story.append(p("Wiring, pinout, flashing and set-up for the NUCLEO-F767ZI running "
                   "<font name='Courier'>firmware/node-f767</font> (stage F3, config protocol 3, "
                   "data on port D).",
                   ParagraphStyle("sub", parent=BODY, fontSize=10.5, textColor=DIM)))
    story.append(Spacer(1, 4))
    story.append(table([
        ["What", "Where"],
        ["Data lines (8)", "PD0 to PD7, all on the Zio headers (seven on CN9, one on CN8), one "
                           "WS2812-class strip chain each, through a 74HCT245"],
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
    story.append(p("All eight lines sit on the Zio headers. The ST morpho column is the same pin "
                   "on the long outer header, if that suits the harness better: use one or the "
                   "other for a pin, not both. The app's Preflight &gt; Node setup numbers lines "
                   "1 to 8; line n is lane n-1 and pin PD(n-1)."))
    rows = [["Line (app)", "Lane", "STM32 pin", "Zio connector", "ST morpho", "Zio name"]]
    for i, (pin, zio, morpho, dname) in enumerate(LANES):
        rows.append([str(i + 1), str(i), pin, zio, morpho, dname])
    story.append(table(rows, [20 * mm, 14 * mm, 20 * mm, 32 * mm, 30 * mm, 20 * mm]))
    story.append(Spacer(1, 3))
    story.extend(bullets([
        "Lines 4 to 8 (PD3 to PD7) are side by side on CN9 pins 10, 8, 6, 4 and 2, with a ground "
        "at CN9 pin 12 next to them: one 6-way connector carries five lines and their ground.",
        "Lines 1 and 2 (PD0, PD1) are on CN9 pins 25 and 27, a ground at CN9 pin 23; line 3 "
        "(PD2) is on CN8 pin 12.",
        "No board function uses PD0 to PD7 (ST's board file). PD8 and PD9 on the same port carry "
        "the console; the output only ever drives bits 0 to 7.",
        "The default configuration drives lines 1 to 4 (the app's 2.5 by 1.2 m test frame, "
        "<font name='Courier'>active 15</font>); Node setup sets the lines the current rig needs.",
        "Firmware before 2026-10-23 used PE0 to PE7: flash the current F3 before wiring port D.",
    ]))

    board = [p("3. Board pinout", H2), board_diagram(), legend()]
    story.append(KeepTogether(board))
    zio = Table([
        [zio_header("CN8 (Zio, left, upper)", CN8, used),
         zio_header("CN7 (Zio, right, upper)", CN7, used)],
        [zio_header("CN9 (Zio, left, lower)", CN9, used),
         zio_header("CN10 (Zio, right, lower)", CN10, used)],
    ], colWidths=[87 * mm, 87 * mm])
    zio.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
                             ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    story.append(p("The Zio headers, pin by pin (UM1974 Table 13; the Zio names follow each "
                   "pin). Odd pins in the left column, even pins in the right. PA7 and PB13 carry "
                   "Ethernet (with JP6 and JP7 fitted): leave them free. PB0 is the green LED.",
                   SMALL))
    story.append(zio)

    story.append(p("4. Power, ground and the other pins the node uses", H2))
    story.append(table([
        ["Signal", "Zio", "ST morpho", "Notes"],
        ["+5 V", "CN8 pin 9", "CN11 pin 18", "Board 5 V. Do not feed strips from it."],
        ["+3.3 V", "CN8 pin 7", "CN11 pin 16", "Not needed by the 245 (it runs at 5 V)."],
        ["GND", "CN8 pins 11, 13; CN9 pins 12, 23; CN10 pins 5, 17, 22, 27; CN7 pin 8",
         "CN11 pins 8, 19, 20, 22, 49, 60; CN12 pins 9, 20, 39, 54, 63",
         "Tie to the 245's GND and the strip supply's 0 V."],
        ["LD1 green", "PB0 (CN10 pin 31)", "", "Blinks at 1 Hz while the firmware runs."],
        ["LD2 blue", "PB7", "", "On while show data arrives."],
        ["LD3 red", "PB14", "", "On without an Ethernet link, or after a fault."],
        ["Console", "USART3: PD8 TX, PD9 RX", "", "Wired to the ST-LINK: the USB virtual COM "
                                                 "port, 115200 8N1."],
        ["Ethernet (RMII)", "PA1, PA2, PA7, PB13, PC1, PC4, PC5, PG11, PG13", "",
         "Onboard LAN8742A PHY. Jumpers JP6 and JP7 must be fitted (closed) for Ethernet."],
        ["Timer and DMA", "TIM1, DMA2 (three streams into GPIOD BSRR)", "",
         "Internal: no pins. TIM1's own pins (PE9, PE11, PE13) stay free."],
    ], [25 * mm, 44 * mm, 44 * mm, 57 * mm]))

    story.append(p("5. 74HCT245 connections (DIP-20 or SO-20)", H2))
    story.append(table([
        ["245 pin", "Name", "Connect to"],
        ["1", "DIR", "+5 V (A to B)"],
        ["2 to 9", "A1 to A8", "PD0 to PD7 (line 1 on A1 ... line 8 on A8)"],
        ["10", "GND", "Common ground"],
        ["18 down to 11", "B1 to B8", "Series resistor, then strip DIN (B1 for line 1 ... B8 for "
                                       "line 8)"],
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
        "Connect the node to the same network as the PC, or straight to the PC's Ethernet port. It "
        "asks for an address by DHCP; without a lease (a cable straight to the PC) it takes a "
        "link-local 169.254.x.y/16 address from its MAC after 10 s, the range Windows gives the "
        "PC on the same cable, so they find each other with no settings. The console prints it. A "
        "fixed address: set dhcp 0 with ip and mask (by default the Art-Net style 2.x.y.z/8).",
        "In Psyight: Preflight &gt; Discover Art-Net nodes, then Node setup. It shows the data lines "
        "the current rig needs (pixels and colour order per line), keeping each line at or under "
        "545 pixels (60 fps) when the eight lines allow it.",
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
                   "run in parallel, so the longest line sets the rate. Example: the Lotus preset "
                   "(1,427 pixels) needs three lines: seed and inner petals 1 to 5 (537 pixels), "
                   "inner 6 to 8 and outer 1 to 3 (510), outer 4 to 8 (380).", SMALL))

    story.append(p("10. Bench checklist", H2))
    story.extend(bullets([
        "Scope PD0 (CN9 pin 25) with F1: bits 1.25 us (plus or minus 150 ns), 0 high 0.40 us, 1 "
        "high 0.80 us, frame gap at least 280 us; repeat with heavy network traffic.",
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
    story.append(p("Sources: ST UM1974 Rev 7, Tables 11, 13 and 21 (NUCLEO-F767ZI); the STM32CubeMX "
                   "board file for the NUCLEO-F767ZI; Psyight firmware sources (src/ws2812, "
                   "src/board, src/net, src/core/node_config.h); SPEC sections 12 and 13. "
                   "Generated by tools/python/make_node_guide.py.", SMALL))

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
