#!/usr/bin/env python3
"""Draw one pictogram per trackpad gesture for the README (layout/png/gestures/).

Same look as tools/render_layout.py: transparent canvas, dark pad, white marks.
The pad is drawn at the IQS572's 43x40 mm aspect. Fingers are dots; a tap has
rings around the dot, a hold has a three-quarter arc (the wait), movement is an
arrow, a scroll a double-headed arrow. A modifier held during the gesture is a
small keycap above the pad. The set of pictures is fixed in GESTURES below and
does not depend on the layout, so run this by hand after changing it:

    .venv/bin/python tools/render_gestures.py [--out layout/png/gestures]
"""
import argparse, math, os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS_BOLD = ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
              '/usr/share/fonts/liberation/LiberationSans-Bold.ttf',
              '/System/Library/Fonts/Supplemental/Arial Bold.ttf']

S = 2                       # supersampling
W, H = 176, 176             # output size
PAD_W, PAD_H = 129, 120     # 43 x 40 mm at 3 px/mm
PAD_TOP = 44                # room for the modifier keycap above the pad
BG = (0, 0, 0, 0)
PAD = (28, 28, 30)
EDGE = (150, 150, 155)
KEY = (48, 52, 64)          # modifier keycap, like the layout pictures' mod keys
TEXT = (245, 245, 245)
MARK = (245, 245, 245)
HALO = (245, 245, 245, 70)
FINGER_R = 7
FINGER_GAP = 22             # centre to centre for side-by-side fingers


def font(size):
    for p in FONTS_BOLD:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


class Pic:
    def __init__(self):
        self.img = Image.new('RGBA', (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.img)
        self.px = (W - PAD_W) / 2
        self.py = PAD_TOP
        self.d.rounded_rectangle([self.px * S, self.py * S, (self.px + PAD_W) * S, (self.py + PAD_H) * S],
                                 radius=8 * S, fill=PAD, outline=EDGE, width=S)

    def P(self, fx, fy):
        """Pad-relative fraction (0..1, 0..1) to canvas pixels (supersampled)."""
        return (self.px + fx * PAD_W) * S, (self.py + fy * PAD_H) * S

    def finger(self, fx, fy, halo=True):
        x, y = self.P(fx, fy)
        r = FINGER_R * S
        if halo:
            self.d.ellipse([x - r * 1.9, y - r * 1.9, x + r * 1.9, y + r * 1.9], fill=HALO)
        self.d.ellipse([x - r, y - r, x + r, y + r], fill=MARK)

    def rings(self, fx, fy, n=2):
        x, y = self.P(fx, fy)
        for i in range(1, n + 1):
            r = FINGER_R * S * (1.9 + 0.9 * i)
            self.d.ellipse([x - r, y - r, x + r, y + r], outline=MARK, width=S)

    def wait_arc(self, fx, fy):
        x, y = self.P(fx, fy)
        r = FINGER_R * S * 2.6
        self.d.arc([x - r, y - r, x + r, y + r], start=-90, end=180, fill=MARK, width=2 * S)

    def arrow(self, fx0, fy0, fx1, fy1, both=False, width=3):
        (x0, y0), (x1, y1) = self.P(fx0, fy0), self.P(fx1, fy1)
        self.d.line([x0, y0, x1, y1], fill=MARK, width=width * S)
        self.head(x0, y0, x1, y1)
        if both:
            self.head(x1, y1, x0, y0)

    def head(self, x0, y0, x1, y1, size=9):
        a = math.atan2(y1 - y0, x1 - x0)
        s = size * S
        pts = [(x1, y1),
               (x1 - s * math.cos(a - 0.5), y1 - s * math.sin(a - 0.5)),
               (x1 - s * math.cos(a + 0.5), y1 - s * math.sin(a + 0.5))]
        self.d.polygon(pts, fill=MARK)

    def modifier(self, label):
        f = font(12 * S)
        tw = self.d.textlength(label, font=f)
        w = tw + 16 * S
        x0 = (W * S - w) / 2
        y0, y1 = 8 * S, 34 * S
        self.d.rounded_rectangle([x0, y0, x0 + w, y1], radius=4 * S, fill=KEY, outline=EDGE, width=S)
        self.d.text((W * S / 2, (y0 + y1) / 2), label, font=f, fill=TEXT, anchor='mm')

    def save(self, path):
        self.img.resize((W, H), Image.LANCZOS).save(path, optimize=True)


# ------------------------------------------------------------ gestures ----
def fingers_row(p, n, fy, fx=0.5):
    """n fingers side by side centred on fx; returns their x fractions."""
    step = FINGER_GAP / PAD_W
    xs = [fx + (i - (n - 1) / 2) * step for i in range(n)]
    for x in xs:
        p.finger(x, fy)
    return xs


def tap(n):
    p = Pic()
    xs = fingers_row(p, n, 0.5)
    if n == 1:
        p.rings(xs[0], 0.5, n=2)
    else:
        # one ring around the whole group: the fingers land and lift together
        (x0, y), (x1, _) = p.P(xs[0], 0.5), p.P(xs[-1], 0.5)
        for i in (1, 2):
            r = FINGER_R * S * (1.9 + 0.9 * i)
            p.d.rounded_rectangle([x0 - r, y - r, x1 + r, y + r], radius=r, outline=MARK, width=S)
    return p


def drag(mod=None):
    p = Pic()
    if mod:
        p.modifier(mod)
    p.finger(0.3, 0.6)
    p.wait_arc(0.3, 0.6)
    p.arrow(0.44, 0.52, 0.8, 0.3)
    return p


def scroll(axis, mod=None):
    p = Pic()
    if mod:
        p.modifier(mod)
    if axis == 'v':
        fingers_row(p, 2, 0.5, fx=0.38)
        p.arrow(0.74, 0.8, 0.74, 0.2, both=True)
    else:
        fingers_row(p, 2, 0.4)
        p.arrow(0.2, 0.74, 0.8, 0.74, both=True)
    return p


def swipe3(direction):
    p = Pic()
    if direction in ('left', 'right'):
        fingers_row(p, 3, 0.4)
        if direction == 'left':
            p.arrow(0.78, 0.76, 0.22, 0.76)
        else:
            p.arrow(0.22, 0.76, 0.78, 0.76)
    else:
        fingers_row(p, 3, 0.5, fx=0.42)
        if direction == 'up':
            p.arrow(0.84, 0.82, 0.84, 0.18)
        else:
            p.arrow(0.84, 0.18, 0.84, 0.82)
    return p


def mod_swipe(mod):
    """Modifier + two-finger sideways swipe: one discrete tick per swipe."""
    p = Pic()
    p.modifier(mod)
    fingers_row(p, 2, 0.4)
    p.arrow(0.2, 0.74, 0.8, 0.74, both=True)
    return p


GESTURES = {
    'tap1': lambda: tap(1),
    'tap2': lambda: tap(2),
    'tap3': lambda: tap(3),
    'drag': lambda: drag(),
    'drag_gui': lambda: drag('Cmd / Super'),
    'drag_ctrl': lambda: drag('Ctrl'),
    'scroll_v': lambda: scroll('v'),
    'scroll_h': lambda: scroll('h'),
    'scroll_gui_v': lambda: scroll('v', 'Cmd'),
    'swipe3_left': lambda: swipe3('left'),
    'swipe3_right': lambda: swipe3('right'),
    'swipe3_up': lambda: swipe3('up'),
    'swipe3_down': lambda: swipe3('down'),
    'swipe_gui': lambda: mod_swipe('Cmd / Super'),
    'swipe_ctrl': lambda: mod_swipe('Ctrl'),
    'swipe_alt': lambda: mod_swipe('Alt'),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'layout', 'png', 'gestures'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name, make in GESTURES.items():
        path = os.path.join(args.out, name + '.png')
        make().save(path)
        print(path)


if __name__ == '__main__':
    main()
