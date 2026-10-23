"""Builds assets/icon/psyight.ico (the exe, window and taskbar icon) from assets/icon/psyight_logo.png.

Large sizes show the whole logo; small sizes crop to the "P" so the waveforms don't turn to noise.
Run from the repository root: python tools/python/make_icon.py (needs Pillow).
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets" / "icon" / "psyight_logo.png"
TARGET = ROOT / "assets" / "icon" / "psyight.ico"

LARGE_SIZES = [256, 128, 64, 48]
SMALL_SIZES = [32, 24, 16]


def square_crop(image, centre_x, centre_y, side):
    half = side // 2
    return image.crop((centre_x - half, centre_y - half, centre_x + half, centre_y + half))


def main():
    logo = Image.open(SOURCE).convert("RGBA")
    width, height = logo.size
    whole = square_crop(logo, width // 2, height // 2, 800)  # The waveforms' full width.
    letter = square_crop(logo, 525, 500, 600)  # Just the "P".
    frames = [whole.resize((s, s), Image.LANCZOS) for s in LARGE_SIZES]
    frames += [letter.resize((s, s), Image.LANCZOS) for s in SMALL_SIZES]
    frames[0].save(TARGET, format="ICO", append_images=frames[1:],
                   sizes=[(f.width, f.height) for f in frames])
    print(f"wrote {TARGET}")


if __name__ == "__main__":
    main()
