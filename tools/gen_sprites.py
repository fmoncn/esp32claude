#!/usr/bin/env python3
"""Generate the bundled GENERIC mascot ("Pixel Buddy", an original blocky robot)
as 64x72 RGBA frames into sprites_src/, matching the layout pack_sprites.py expects:
  sprites_src/metadata.json
  sprites_src/frames/<action>/<action>_<i>.png

This is original procedural pixel art (no third-party / trademarked characters).
Want your own character? Replace sprites_src/ with your own frames in this layout
(or regenerate with your own art), then run `python tools/pack_sprites.py`.

Usage (from repo root):  python tools/gen_sprites.py   (needs Pillow)
"""
import json, math, os
from PIL import Image, ImageDraw

FW, FH = 64, 72
OUT = "sprites_src"
SHEETS = "sim/sheets"  # horizontal sprite sheets for the browser sim
CX = 32  # horizontal centre

# palette (RGBA)
BODY   = (74, 200, 222, 255)
SHADE  = (44, 150, 176, 255)
OUTLN  = (18, 54, 70, 255)
SCREEN = (14, 24, 34, 255)
EYE    = (130, 240, 255, 255)
GOLD   = (255, 200, 58, 255)
WHITE  = (240, 250, 255, 255)
PINK   = (255, 90, 150, 255)
RED    = (255, 80, 80, 255)
GREEN  = (120, 235, 150, 255)

# action table: name -> (frames, fps, loop, kind). kind drives the animation.
ACTIONS = [
    ("idle", 6, 6, True, "idle"), ("walking", 6, 10, True, "walk"), ("running", 6, 14, True, "walk"),
    ("jumping", 6, 9, False, "jump"), ("flying", 6, 10, True, "fly"), ("landing", 6, 9, False, "jump"),
    ("blast", 6, 10, False, "beam"), ("chest_blast", 8, 9, False, "beam"), ("missile_launch", 6, 10, False, "beam"),
    ("laser_attack", 6, 10, False, "beam"), ("charging", 8, 10, True, "charge"), ("victory", 6, 8, False, "cheer"),
    ("low_battery", 6, 4, True, "droop"), ("damaged", 6, 12, True, "shake"), ("repairing", 6, 8, False, "tool"),
    ("upgrading", 8, 10, False, "charge"), ("dancing", 8, 10, True, "cheer"), ("eating", 6, 7, False, "eat"),
    ("drinking", 6, 7, False, "eat"), ("sleeping", 6, 4, True, "sleep"), ("exercising", 6, 8, True, "cheer"),
    ("reading", 6, 4, True, "think"), ("playing", 6, 9, True, "cheer"), ("waving", 6, 10, False, "wave"),
    ("hugging", 6, 7, True, "love"), ("giving_heart", 6, 7, False, "love"), ("romantic", 6, 6, True, "love"),
    ("happy", 6, 9, True, "cheer"), ("sad", 6, 5, True, "droop"), ("angry", 6, 14, True, "shake"),
    ("surprised", 6, 9, False, "jump"), ("thinking", 6, 5, True, "think"), ("sleepy", 6, 5, True, "droop"),
    ("in_love", 6, 7, True, "love"),
]


def rrect(d, box, r, fill, outline=None):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=1)


def draw_bot(t, bob=0, eye="open", mouth="-", armL=0, armR=0, lean=0, tint=None, feet=0):
    """Draw one mascot frame. t in [0,1) for subtle idle motion. Returns RGBA image + draw."""
    im = Image.new("RGBA", (FW, FH), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = CX + lean
    by = 20 + bob              # body top
    body = tint or BODY
    # legs / feet
    d.rectangle([cx - 9, 54, cx - 4, 60 + feet], fill=SHADE)
    d.rectangle([cx + 4, 54, cx + 9, 60 - feet], fill=SHADE)
    rrect(d, [cx - 12, 59 + feet, cx - 2, 64 + feet], 2, GOLD, OUTLN)
    rrect(d, [cx + 2, 59 - feet, cx + 12, 64 - feet], 2, GOLD, OUTLN)
    # arms (angle in degrees from horizontal-down)
    for sgn, ang in ((-1, armL), (1, armR)):
        ax, ay = cx + sgn * 15, by + 16
        ex = ax + sgn * int(2 + 7 * math.cos(math.radians(ang)))
        ey = ay - int(10 * math.sin(math.radians(ang)))
        d.line([ax, ay, ex, ey], fill=SHADE, width=4)
        d.ellipse([ex - 3, ey - 3, ex + 3, ey + 3], fill=body, outline=OUTLN)
    # body
    rrect(d, [cx - 14, by, cx + 14, by + 30], 8, body, OUTLN)
    rrect(d, [cx - 14, by + 20, cx + 14, by + 30], 6, SHADE)  # belly shade
    # antenna
    d.line([cx, by - 6, cx, by], fill=OUTLN, width=2)
    d.ellipse([cx - 3, by - 10, cx + 3, by - 4], fill=GOLD, outline=OUTLN)
    # face screen
    rrect(d, [cx - 11, by + 3, cx + 11, by + 17], 4, SCREEN, OUTLN)
    # eyes
    ex1, ex2, ey = cx - 6, cx + 6, by + 9
    if eye == "closed" or eye == "sleep":
        for x in (ex1, ex2): d.line([x - 3, ey, x + 3, ey], fill=EYE, width=2)
    elif eye == "happy":
        for x in (ex1, ex2): d.arc([x - 4, ey - 4, x + 4, ey + 3], 200, 340, fill=EYE, width=2)
    elif eye == "angry":
        for x, dx in ((ex1, 1), (ex2, -1)):
            d.line([x - 4, ey - 3, x + 4, ey - 1], fill=RED, width=2)
            d.ellipse([x - 2, ey, x + 2, ey + 4], fill=RED)
    elif eye == "wide":
        for x in (ex1, ex2): d.ellipse([x - 4, ey - 4, x + 4, ey + 4], fill=WHITE, outline=EYE)
    elif eye == "love":
        for x in (ex1, ex2): _heart(d, x, ey, 4, PINK)
    else:  # open
        for x in (ex1, ex2): d.ellipse([x - 3, ey - 3, x + 3, ey + 3], fill=EYE)
    # mouth
    my = by + 14
    if mouth == "smile": d.arc([cx - 4, my - 3, cx + 4, my + 2], 200, 340, fill=EYE, width=1)
    elif mouth == "open": d.ellipse([cx - 2, my - 1, cx + 2, my + 3], fill=EYE)
    elif mouth == "flat": d.line([cx - 3, my, cx + 3, my], fill=EYE, width=1)
    return im, d, cx, by


def _heart(d, x, y, s, col):
    d.ellipse([x - s, y - s, x, y], fill=col)
    d.ellipse([x, y - s, x + s, y], fill=col)
    d.polygon([(x - s, y - s // 2), (x + s, y - s // 2), (x, y + s)], fill=col)


def frame(kind, i, n):
    t = i / n
    s = math.sin(t * 2 * math.pi)
    if kind == "idle":
        im, d, cx, by = draw_bot(t, bob=int(1 * s), eye="closed" if i == n - 1 else "open", mouth="smile", armL=10, armR=10)
    elif kind == "walk":
        sw = [0, 1, 0, -1, 0, 1][i % 6]
        im, d, cx, by = draw_bot(t, bob=abs(sw), eye="open", mouth="-", armL=20 * sw, armR=-20 * sw, feet=sw * 2)
    elif kind == "jump":
        up = [-1, -6, -10, -8, -3, 0][i % 6]
        im, d, cx, by = draw_bot(t, bob=up, eye="wide", mouth="open", armL=70, armR=70)
    elif kind == "fly":
        im, d, cx, by = draw_bot(t, bob=int(2 * s) - 2, eye="open", mouth="smile", armL=60, armR=60)
        d.polygon([(cx - 4, 64), (cx + 4, 64), (cx, 70 + (i % 3) * 2)], fill=GOLD)  # thruster
    elif kind == "beam":
        ar = 90 if i >= 2 else 30
        im, d, cx, by = draw_bot(t, eye="angry", mouth="flat", armL=ar, armR=ar)
        if i >= 2:
            col = PINK if i % 2 else EYE
            d.line([cx, by + 6, cx, by - 14 - i * 2], fill=col, width=3)  # upward beam
            d.ellipse([cx - 4, by - 18 - i * 2, cx + 4, by - 10 - i * 2], fill=col)
    elif kind == "charge":
        glow = GOLD if i % 2 else (255, 230, 140, 255)
        im, d, cx, by = draw_bot(t, eye="open", mouth="-", armL=10, armR=10, tint=glow)
        for k in range(4):
            a = t * 6 + k * 1.57
            d.ellipse([cx + math.cos(a) * 18 - 2, by + 14 + math.sin(a) * 18 - 2,
                       cx + math.cos(a) * 18 + 2, by + 14 + math.sin(a) * 18 + 2], fill=GOLD)
    elif kind == "cheer":
        up = [0, -3, -5, -3][i % 4]
        im, d, cx, by = draw_bot(t, bob=up, eye="happy", mouth="smile", armL=80, armR=80)
        if i % 2: _heart(d, cx + 18, by, 3, GOLD)
    elif kind == "droop":
        im, d, cx, by = draw_bot(t, bob=2 + (i % 2), eye="sleep", mouth="flat", armL=-10, armR=-10, lean=int(s))
    elif kind == "shake":
        im, d, cx, by = draw_bot(t, lean=[-2, 2, -2, 2, -1, 1][i % 6], eye="angry", mouth="open",
                                 tint=RED if i % 2 else None, armL=40, armR=40)
    elif kind == "tool":
        im, d, cx, by = draw_bot(t, eye="open", mouth="-", armL=50, armR=10)
        d.rectangle([cx - 20, by + 6, cx - 14, by + 9], fill=GOLD, outline=OUTLN)  # wrench
        if i % 2: d.ellipse([cx - 22, by + 2, cx - 16, by + 8], outline=GOLD)
    elif kind == "eat":
        im, d, cx, by = draw_bot(t, eye="happy", mouth="open" if i % 2 else "smile", armL=55, armR=10)
        d.ellipse([cx - 16, by + 8, cx - 10, by + 14], fill=GREEN, outline=OUTLN)  # snack
    elif kind == "sleep":
        im, d, cx, by = draw_bot(t, bob=int(s), eye="sleep", mouth="flat", armL=-5, armR=-5)
        for k in range(3):
            zx, zy = cx + 12 + k * 4, by - 2 - k * 6 - (i % 2)
            d.text((zx, zy), "z", fill=WHITE)
    elif kind == "think":
        im, d, cx, by = draw_bot(t, eye="open", mouth="-", armL=45, armR=10, lean=-1)
        d.text((cx + 14, by - 6 - (i % 2)), "?", fill=GOLD)
    elif kind == "wave":
        ar = [90, 70, 90, 60, 90, 70][i % 6]
        im, d, cx, by = draw_bot(t, eye="happy", mouth="smile", armL=10, armR=ar)
    elif kind == "love":
        im, d, cx, by = draw_bot(t, bob=int(s), eye="love", mouth="smile", armL=30, armR=30)
        _heart(d, cx, by - 8 - (i % 3) * 2, 5, PINK)
        if i % 2: _heart(d, cx + 14, by - 2, 3, PINK)
    else:
        im, d, cx, by = draw_bot(t, eye="open", mouth="smile")
    return im


def main():
    order = [a[0] for a in ACTIONS]
    meta = {"frame_w": FW, "frame_h": FH, "batches": {"all": order}, "actions": {}}
    os.makedirs(SHEETS, exist_ok=True)
    for name, frames, fps, loop, kind in ACTIONS:
        meta["actions"][name] = {"frames": frames, "fps": fps, "loop": loop}
        fdir = os.path.join(OUT, "frames", name)
        os.makedirs(fdir, exist_ok=True)
        sheet = Image.new("RGBA", (FW * frames, FH), (0, 0, 0, 0))  # for the browser sim
        for i in range(frames):
            im = frame(kind, i, frames)
            im.save(os.path.join(fdir, f"{name}_{i}.png"))   # individual frame (for pack_sprites.py)
            sheet.alpha_composite(im, (i * FW, 0))
        sheet.save(os.path.join(SHEETS, f"{name}.png"))       # horizontal strip (for sim/)
    os.makedirs(OUT, exist_ok=True)
    json.dump(meta, open(os.path.join(OUT, "metadata.json"), "w"), ensure_ascii=False, indent=2)
    print(f"generated {len(order)} actions -> {OUT}/ (device frames) + {SHEETS}/ (sim sheets)")
    print("next: python tools/pack_sprites.py")


if __name__ == "__main__":
    main()
