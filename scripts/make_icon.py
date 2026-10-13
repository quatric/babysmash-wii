#!/usr/bin/env python3
"""Draws icon.png (128x48, the Homebrew Channel size): a row of BabySmash shapes with faces."""
import math, sys
from PIL import Image, ImageDraw

S = 8                                   # supersampling factor
W, H = 128, 48
img = Image.new("RGB", (W * S, H * S), (245, 245, 245))
d = ImageDraw.Draw(img)

def shade(c, k):
    return tuple(max(0, min(255, v + k)) for v in c)

def glossy(poly, color, cx, cy):
    """black outline, dark rim -> mid -> lit centre, like the in-game shapes"""
    pts = [(x * S, y * S) for x, y in poly]
    out = [(cx * S + (x - cx * S) * 1.07, cy * S + (y - cy * S) * 1.07) for x, y in pts]
    d.polygon(out, fill=(0, 0, 0))
    for scale, k in ((1.0, -50), (0.78, -20), (0.56, 0), (0.34, 25), (0.16, 50)):
        d.polygon([(cx * S + (x - cx * S) * scale, cy * S + (y - cy * S) * scale) for x, y in pts],
                  fill=shade(color, k))

def face(cx, cy, r):
    for ex in (-0.28, 0.28):
        d.ellipse([(cx + ex * r - 0.08 * r) * S, (cy - 0.2 * r - 0.12 * r) * S,
                   (cx + ex * r + 0.08 * r) * S, (cy - 0.2 * r + 0.12 * r) * S], fill=(0, 0, 0))
    d.arc([(cx - 0.32 * r) * S, (cy - 0.15 * r) * S, (cx + 0.32 * r) * S, (cy + 0.35 * r) * S],
          20, 160, fill=(0, 0, 0), width=int(1.4 * S))

def ngon(cx, cy, r, n, rot=-90, inner=None):
    pts = []
    for i in range(n * (2 if inner else 1)):
        a = math.radians(rot + i * 360 / (n * (2 if inner else 1)))
        rr = inner if inner and i % 2 else r
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts

cy = 24
# circle (red), star (yellow), triangle (blue), heart (orange)
glossy(ngon(20, cy, 15, 40), (255, 0, 0), 20, cy); face(20, cy, 15)
glossy(ngon(52, cy, 17, 5, inner=7.2), (255, 255, 0), 52, cy); face(52, cy + 1.5, 10)
tri = [(84, cy - 16), (99, cy + 13), (69, cy + 13)]
glossy(tri, (0, 0, 255), 84, cy + 3); face(84, cy + 3, 11)
heart = []
for i in range(60):
    t = i * 2 * math.pi / 60
    heart.append((111 + 16 * math.sin(t) ** 3 * 0.8, cy + 1 - (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) * 0.8))
glossy(heart, (255, 165, 0), 111, cy + 1); face(111, cy + 1, 11)

img.resize((W, H), Image.LANCZOS).save(sys.argv[1] if len(sys.argv) > 1 else "icon.png")
