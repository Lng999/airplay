"""Draw app/res/app.ico from scratch - no imaging library available in this environment.

The mark: a rounded dark-navy tile, a white screen with a blue face, and a triangle rising
out of its lower edge - "something is being thrown onto this display". Deliberately not
Apple's AirPlay glyph: different proportions, an outlined screen and a two-tone palette.

Every size is a 32-bit BGRA DIB (the widely supported ICO form), rendered at 4x and boxed
down for anti-aliasing.
"""
import struct
import sys

SS = 4  # supersampling factor

def bgr(rgb):
    """DIB pixels are stored blue-first; the constants below stay readable as #rrggbb."""
    return ((rgb >> 0) & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF)


NAVY = bgr(0x14223C)   # tile
BLUE = bgr(0x1C6CC8)   # screen face
WHITE = bgr(0xFFFFFF)  # screen frame + triangle


def rounded_rect(x, y, w, h, r):
    """Predicate for a rounded rectangle in supersampled coordinates."""
    def inside(px, py):
        if px < x or py < y or px >= x + w or py >= y + h:
            return False
        cx = min(max(px, x + r), x + w - r)
        cy = min(max(py, y + r), y + h - r)
        dx, dy = px - cx, py - cy
        return dx * dx + dy * dy <= r * r or (x + r <= px < x + w - r) or (y + r <= py < y + h - r)
    return inside


def triangle(ax, ay, bx, by, cx, cy):
    def sign(px, py, qx, qy, rx, ry):
        return (px - rx) * (qy - ry) - (qx - rx) * (py - ry)

    def inside(px, py):
        d1 = sign(px, py, ax, ay, bx, by)
        d2 = sign(px, py, bx, by, cx, cy)
        d3 = sign(px, py, cx, cy, ax, ay)
        neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
        pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
        return not (neg and pos)
    return inside


def render(size):
    """Return size*size list of (b, g, r, a), top row first."""
    n = size * SS
    tile = rounded_rect(0, 0, n, n, n * 0.22)

    # screen: outer white frame, inner blue face
    sx, sy = n * 0.16, n * 0.20
    sw, sh = n * 0.68, n * 0.42
    frame = rounded_rect(sx, sy, sw, sh, n * 0.06)
    inset = n * 0.055
    face = rounded_rect(sx + inset, sy + inset, sw - 2 * inset, sh - 2 * inset, n * 0.03)

    # triangle below the screen, pointing up into it
    tri = triangle(n * 0.50, n * 0.60, n * 0.28, n * 0.86, n * 0.72, n * 0.86)

    hi = []
    for py in range(n):
        row = []
        for px in range(n):
            p = (px + 0.5, py + 0.5)
            if not tile(*p):
                row.append((0, 0, 0, 0))
            elif tri(*p):
                row.append(WHITE + (255,))
            elif face(*p):
                row.append(BLUE + (255,))
            elif frame(*p):
                row.append(WHITE + (255,))
            else:
                row.append(NAVY + (255,))
        hi.append(row)

    # box filter down to the target size
    out = []
    for y in range(size):
        for x in range(size):
            r = g = b = a = 0
            for dy in range(SS):
                for dx in range(SS):
                    pr, pg, pb, pa = hi[y * SS + dy][x * SS + dx]
                    # premultiply so transparent pixels do not darken the edges
                    r += pr * pa
                    g += pg * pa
                    b += pb * pa
                    a += pa
            count = SS * SS
            if a == 0:
                out.append((0, 0, 0, 0))
            else:
                out.append((r // a, g // a, b // a, a // count))
    return out


def dib(size, pixels):
    """32-bit BITMAPINFOHEADER image: bottom-up BGRA rows plus an (empty) AND mask."""
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, size * size * 4,
                         0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            b, g, r, a = pixels[y * size + x]
            body += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    body += bytes(mask_row * size)
    return header + bytes(body)


def main(path, sizes=(16, 24, 32, 48, 64, 128, 256)):
    images = [(s, dib(s, render(s))) for s in sizes]
    out = bytearray(struct.pack('<HHH', 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for s, data in images:
        out += struct.pack('<BBBBHHII', s if s < 256 else 0, s if s < 256 else 0, 0, 0,
                           1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    with open(path, 'wb') as f:
        f.write(bytes(out))
    print('wrote %s (%d bytes, %s)' % (path, len(out), ', '.join(str(s) for s, _ in images)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'app.ico')
