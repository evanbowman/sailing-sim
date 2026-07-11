#!/usr/bin/env python3
"""Clamp PNG colors to the GBA's 5-bit-per-channel color space.

Each color byte c is replaced with (c >> 3) << 3, which zeroes the low
three bits. That's what the GBA hardware actually shows when given an
8-bit RGB value, so doing this offline lets you preview on a desktop
exactly how the image will look on hardware.

Handles indexed (mode P) and truecolor (RGB / RGBA) PNGs. Indexed
images stay indexed -- only the palette is rewritten. The alpha
channel is left untouched.

Usage:
    python clamp_gba.py path/to/dir              # overwrite in place
    python clamp_gba.py path/to/dir -o out_dir   # write to out_dir
    python clamp_gba.py path/to/dir -r           # recurse
"""
import argparse
import sys
from pathlib import Path
from PIL import Image

# Lookup table: low 3 bits zeroed in every byte.
LUT = bytes((i >> 3) << 3 for i in range(256))


def quantize(img: Image.Image) -> Image.Image:
    mode = img.mode

    if mode == 'P':
        # Indexed: rewrite the palette in place and stay in mode P.
        palette = img.getpalette()
        if palette is not None:
            img.putpalette(bytes(LUT[c] for c in palette))
        return img

    if mode == 'RGB':
        r, g, b = img.split()
        return Image.merge('RGB', (r.point(LUT), g.point(LUT), b.point(LUT)))

    if mode == 'RGBA':
        r, g, b, a = img.split()
        return Image.merge('RGBA', (r.point(LUT), g.point(LUT), b.point(LUT), a))

    if mode == 'L':
        return img.point(LUT)

    if mode == 'LA':
        l, a = img.split()
        return Image.merge('LA', (l.point(LUT), a))

    # Fallback for anything exotic (CMYK, I, F, ...).
    print(f"  note: converting mode {mode!r} via RGBA", file=sys.stderr)
    return quantize(img.convert('RGBA'))


def process_file(src: Path, dst: Path) -> None:
    with Image.open(src) as img:
        img.load()
        info = img.info.copy()
        out = quantize(img)

    save_kwargs = {}
    # Preserve a single-color or palette-index transparency entry.
    if 'transparency' in info:
        save_kwargs['transparency'] = info['transparency']
    out.save(dst, format='PNG', **save_kwargs)


def find_pngs(root: Path, recursive: bool):
    walker = root.rglob('*') if recursive else root.iterdir()
    return sorted(p for p in walker if p.is_file() and p.suffix.lower() == '.png')


def main() -> int:
    ap = argparse.ArgumentParser(
        description='Clamp PNG colors to GBA 5-bit color space.')
    ap.add_argument('directory', type=Path,
                    help='directory of PNG files to process')
    ap.add_argument('-o', '--output', type=Path, default=None,
                    help='output directory (default: overwrite in place)')
    ap.add_argument('-r', '--recursive', action='store_true',
                    help='recurse into subdirectories')
    args = ap.parse_args()

    src_dir: Path = args.directory
    if not src_dir.is_dir():
        ap.error(f'{src_dir} is not a directory')

    out_dir: Path = args.output or src_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    files = find_pngs(src_dir, args.recursive)
    if not files:
        print('no PNG files found', file=sys.stderr)
        return 1

    for src in files:
        rel = src.relative_to(src_dir)
        dst = out_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        print(f'  {rel}')
        process_file(src, dst)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
