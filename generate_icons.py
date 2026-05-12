#!/usr/bin/env python3
"""Generate simple PNG icons for the task manager using pure Python (no dependencies).
Creates minimal 32x32 PNG files with basic shapes.
Run: python3 generate_icons.py
"""

import struct, zlib, os

def write_png(filename, width, height, pixels):
    """Write an RGBA PNG file. pixels is a list of (r,g,b,a) tuples, row-major."""
    def chunk(chunk_type, data):
        c = chunk_type + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

    raw = b''
    for y in range(height):
        raw += b'\x00'  # filter byte
        for x in range(width):
            r, g, b, a = pixels[y * width + x]
            raw += struct.pack('BBBB', r, g, b, a)

    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    idat = zlib.compress(raw)

    with open(filename, 'wb') as f:
        f.write(sig)
        f.write(chunk(b'IHDR', ihdr))
        f.write(chunk(b'IDAT', idat))
        f.write(chunk(b'IEND', b''))

def make_process_icon():
    """Green gear-like icon representing a running process."""
    w, h = 32, 32
    pixels = [(0,0,0,0)] * (w * h)
    cx, cy = 15, 15
    for y in range(h):
        for x in range(w):
            dx, dy = x - cx, y - cy
            dist = (dx*dx + dy*dy) ** 0.5
            if 4 < dist < 12:
                pixels[y * w + x] = (76, 175, 80, 255)  # green
            elif dist <= 4:
                pixels[y * w + x] = (56, 142, 60, 255)  # dark green center
    return w, h, pixels

def make_cpu_icon():
    """Blue chip icon for CPU."""
    w, h = 32, 32
    pixels = [(0,0,0,0)] * (w * h)
    for y in range(h):
        for x in range(w):
            # Main chip body
            if 6 <= x <= 25 and 6 <= y <= 25:
                pixels[y * w + x] = (33, 150, 243, 255)  # blue
            # Pins
            elif (8 <= x <= 23) and (y < 6 or y > 25):
                if (x % 4) < 2:
                    pixels[y * w + x] = (100, 100, 100, 255)
            elif (8 <= y <= 23) and (x < 6 or x > 25):
                if (y % 4) < 2:
                    pixels[y * w + x] = (100, 100, 100, 255)
            # Inner die
            if 10 <= x <= 21 and 10 <= y <= 21:
                pixels[y * w + x] = (25, 118, 210, 255)  # darker blue
    return w, h, pixels

def make_memory_icon():
    """Purple RAM stick icon."""
    w, h = 32, 32
    pixels = [(0,0,0,0)] * (w * h)
    for y in range(h):
        for x in range(w):
            if 2 <= x <= 29 and 8 <= y <= 23:
                pixels[y * w + x] = (156, 39, 176, 255)  # purple
            if 2 <= x <= 29 and 8 <= y <= 10:
                pixels[y * w + x] = (123, 31, 162, 255)  # dark purple top edge
            # Memory chips on the stick
            if 12 <= y <= 20:
                if (x >= 5 and x <= 8) or (x >= 11 and x <= 14) or \
                   (x >= 17 and x <= 20) or (x >= 23 and x <= 26):
                    pixels[y * w + x] = (206, 147, 216, 255)  # light purple
    return w, h, pixels

def make_kill_icon():
    """Red X icon for kill button."""
    w, h = 32, 32
    pixels = [(0,0,0,0)] * (w * h)
    for y in range(h):
        for x in range(w):
            dx, dy = x - 15, y - 15
            dist = (dx*dx + dy*dy) ** 0.5
            # Circle background
            if dist < 14:
                pixels[y * w + x] = (244, 67, 54, 255)  # red
            # X mark
            if dist < 10:
                if abs(dx - dy) < 2 or abs(dx + dy) < 2:
                    pixels[y * w + x] = (255, 255, 255, 255)  # white X
    return w, h, pixels

def make_sort_icon():
    """Arrow icon for sort indicator."""
    w, h = 16, 16
    pixels = [(0,0,0,0)] * (w * h)
    # Down arrow
    for y in range(h):
        for x in range(w):
            # Shaft
            if 6 <= x <= 9 and 2 <= y <= 10:
                pixels[y * w + x] = (255, 255, 255, 255)
            # Arrow head
            if 10 <= y <= 13:
                half = (y - 10) + 5
                if (15 - half) <= x <= half:
                    pixels[y * w + x] = (255, 255, 255, 255)
    return w, h, pixels

os.makedirs('resrc/images', exist_ok=True)
os.makedirs('resrc/fonts', exist_ok=True)

icons = {
    'resrc/images/process_icon.png': make_process_icon,
    'resrc/images/cpu_icon.png': make_cpu_icon,
    'resrc/images/memory_icon.png': make_memory_icon,
    'resrc/images/kill_icon.png': make_kill_icon,
    'resrc/images/sort_icon.png': make_sort_icon,
}

for path, func in icons.items():
    w, h, px = func()
    write_png(path, w, h, px)
    print(f"Created {path} ({w}x{h})")

print("\nDone! Now copy a TTF font to resrc/fonts/OpenSans-Regular.ttf")
print("You can get it with: wget -O resrc/fonts/OpenSans-Regular.ttf 'https://github.com/google/fonts/raw/main/ofl/opensans/OpenSans%5Bwdth%2Cwght%5D.ttf'")
