#!/usr/bin/env python3
"""Render a layout dump (tools/dump_layout.py JSON) as one PNG per layer.

The key geometry comes from the KLE block in keymaps/sofle_choc_tp/vial.json, so
the pictures match the physical board including the rotated thumb keys and the
two encoders. Keycodes are shortened to what is printed on a keycap; tap-hold
keys show the tap on top and the hold underneath; tap dances are expanded from
the dump. Layers that are entirely transparent are skipped.

Usage: .venv/bin/python tools/render_layout.py layout/<board>/current_layout.json
       [--out layout/<board>/png] [--vial keymaps/sofle_choc_tp/vial.json]
"""
import argparse, json, math, os, re, sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = ['/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
         '/usr/share/fonts/liberation/LiberationSans-Regular.ttf',
         '/System/Library/Fonts/Supplemental/Arial.ttf']
FONTS_BOLD = ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
              '/usr/share/fonts/liberation/LiberationSans-Bold.ttf',
              '/System/Library/Fonts/Supplemental/Arial Bold.ttf']

U = 64            # pixels per key unit
PAD = 24          # canvas margin
GAP = 5           # gap between keycaps
SCALE = 2         # supersampling factor

BG = (250, 250, 250)
KEY = (255, 255, 255)
KEY_MOD = (232, 236, 244)   # keys whose tap is a modifier or layer key
KEY_TRNS = (243, 243, 243)
EDGE = (120, 120, 120)
TEXT = (20, 20, 20)
SUB = (90, 90, 90)
TRNS = (170, 170, 170)

LAYER_NAMES = {0: 'base', 1: 'numpad', 2: 'symbols / F-keys', 3: 'nav / edit'}

# ---------------------------------------------------------------- labels ----
SIMPLE = {
    'KC_NO': '', 'KC_TRNS': '▽', '_______': '▽', 'XXXXXXX': '',
    'KC_ESC': 'Esc', 'KC_TAB': 'Tab', 'KC_BSPC': 'Bksp', 'KC_ENT': 'Enter', 'KC_SPC': 'Space',
    'KC_DEL': 'Del', 'KC_INS': 'Ins', 'KC_CAPS': 'Caps', 'KC_APP': 'Menu', 'KC_PSCR': 'PrtSc',
    'KC_SCRL': 'ScrLk', 'KC_PAUS': 'Pause', 'KC_HOME': 'Home', 'KC_END': 'End',
    'KC_PGUP': 'PgUp', 'KC_PGDN': 'PgDn', 'KC_UP': '↑', 'KC_DOWN': '↓', 'KC_LEFT': '←', 'KC_RGHT': '→',
    'KC_GRV': '`', 'KC_MINS': '-', 'KC_EQL': '=', 'KC_LBRC': '[', 'KC_RBRC': ']', 'KC_BSLS': '\\',
    'KC_SCLN': ';', 'KC_QUOT': "'", 'KC_COMM': ',', 'KC_DOT': '.', 'KC_SLSH': '/',
    'KC_LSFT': 'Shift', 'KC_RSFT': 'Shift', 'KC_LCTL': 'Ctrl', 'KC_RCTL': 'Ctrl',
    'KC_LALT': 'Alt', 'KC_RALT': 'Alt', 'KC_LGUI': 'GUI', 'KC_RGUI': 'GUI',
    'KC_MUTE': 'Mute', 'KC_VOLU': 'Vol+', 'KC_VOLD': 'Vol-', 'KC_MPLY': 'Play',
    'KC_MNXT': 'Next', 'KC_MPRV': 'Prev', 'KC_BRIU': 'Bri+', 'KC_BRID': 'Bri-',
    'KC_PSLS': 'KP /', 'KC_PAST': 'KP *', 'KC_PMNS': 'KP -', 'KC_PPLS': 'KP +', 'KC_PENT': 'KP Ent',
    'KC_PDOT': 'KP .', 'KC_PCMM': 'KP ,', 'KC_PEQL': 'KP =', 'KC_NUM': 'NumLk',
    'KC_UNDO': 'Undo', 'KC_CUT': 'Cut', 'KC_COPY': 'Copy', 'KC_PSTE': 'Paste', 'KC_AGIN': 'Again',
    'KC_MISSION_CONTROL': 'Mission\nCtrl', 'KC_MCTL': 'Mission\nCtrl', 'KC_LPAD': 'Launch\npad',
    'RM_TOGG': 'RGB\non/off', 'RM_NEXT': 'RGB\nnext', 'RM_PREV': 'RGB\nprev',
    'RM_HUEU': 'Hue+', 'RM_HUED': 'Hue-', 'RM_SATU': 'Sat+', 'RM_SATD': 'Sat-',
    'RM_VALU': 'Bright+', 'RM_VALD': 'Bright-', 'RM_SPDU': 'Spd+', 'RM_SPDD': 'Spd-',
    'MS_WHLU': 'Wheel↑', 'MS_WHLD': 'Wheel↓', 'MS_WHLL': 'Wheel←', 'MS_WHLR': 'Wheel→',
    'MS_BTN1': 'Btn1', 'MS_BTN2': 'Btn2', 'MS_BTN3': 'Btn3',
    'QK_LLCK': 'Layer\nlock', 'QK_BOOT': 'Boot', 'QK_CLEAR_EEPROM': 'EEPROM\nclear', 'QK_REP': 'Repeat',
    'CW_TOGG': 'Caps\nWord',
}
SHIFTED = {'1': '!', '2': '@', '3': '#', '4': '$', '5': '%', '6': '^', '7': '&', '8': '*', '9': '(', '0': ')',
           '-': '_', '=': '+', '[': '{', ']': '}', '\\': '|', ';': ':', "'": '"', ',': '<', '.': '>', '/': '?', '`': '~'}
MODS = {'LSFT': 'Shift', 'RSFT': 'Shift', 'LCTL': 'Ctrl', 'RCTL': 'Ctrl', 'LALT': 'Alt', 'RALT': 'Alt',
        'LGUI': 'GUI', 'RGUI': 'GUI', 'HYPR': 'Hyper', 'MEH': 'Meh', 'LCAG': 'Ctrl+Alt+GUI', 'LCA': 'Ctrl+Alt',
        'LSA': 'Shift+Alt', 'LSG': 'Shift+GUI', 'LAG': 'Alt+GUI', 'LCG': 'Ctrl+GUI', 'RCS': 'Ctrl+Shift',
        'C': 'Ctrl', 'S': 'Shift', 'A': 'Alt', 'G': 'GUI'}


def base_label(kc):
    """Label for a plain keycode (no tap-hold)."""
    if kc in SIMPLE:
        return SIMPLE[kc]
    m = re.fullmatch(r'KC_([A-Z])', kc)
    if m:
        return m.group(1)
    m = re.fullmatch(r'KC_([0-9])', kc)
    if m:
        return m.group(1)
    m = re.fullmatch(r'KC_F([0-9]+)', kc)
    if m:
        return 'F' + m.group(1)
    m = re.fullmatch(r'KC_P([0-9])', kc)
    if m:
        return 'KP ' + m.group(1)
    m = re.fullmatch(r'(MO|TG|TO|TT|OSL|DF|PDF)\((\d+)\)', kc)
    if m:
        verb = {'MO': 'L{} hold', 'TG': 'L{} toggle', 'TO': 'go L{}', 'TT': 'L{} tap/hold', 'OSL': 'L{} once',
                'DF': 'default L{}', 'PDF': 'default L{}'}[m.group(1)]
        return verb.format(m.group(2))
    m = re.fullmatch(r'([A-Z]+)\((.+)\)', kc)
    if m and m.group(1) in MODS:
        inner = base_label(m.group(2))
        if m.group(1) in ('LSFT', 'RSFT', 'S') and inner in SHIFTED:
            return SHIFTED[inner]
        return MODS[m.group(1)] + '+' + inner
    return kc.replace('KC_', '').replace('QK_', '')


def key_label(kc, tds):
    """Return (main, sub, kind). sub is the hold action, kind picks the fill."""
    m = re.fullmatch(r'([A-Z]+)_T\((.+)\)', kc)
    if m:
        return base_label(m.group(2)), MODS.get(m.group(1), m.group(1)), 'tap'
    m = re.fullmatch(r'LT\((\d+), *(.+)\)', kc)
    if m:
        return base_label(m.group(2)), 'L%s hold' % m.group(1), 'tap'
    m = re.fullmatch(r'TD\((\d+)\)', kc)
    if m:
        td = tds[int(m.group(1))] if int(m.group(1)) < len(tds) else None
        if td:
            n = td['names']
            parts = []
            if n['on_tap'] != 'KC_NO':
                # a layer key in the tap slot acts while held, so say so
                parts.append(base_label(n['on_tap']) if n['on_tap'].startswith('MO(') else 'tap ' + base_label(n['on_tap']))
            if n['on_hold'] != 'KC_NO': parts.append('hold ' + base_label(n['on_hold']))
            if n['on_double_tap'] != 'KC_NO': parts.append('2×tap ' + base_label(n['on_double_tap']))
            if n['on_tap_hold'] != 'KC_NO': parts.append('tap+hold ' + base_label(n['on_tap_hold']))
            if not parts:
                return 'TD%s' % m.group(1), '(empty)', 'tap'
            return parts[0], '\n'.join(parts[1:] + ['(TD%s)' % m.group(1)]), 'tap'
        return 'TD%s' % m.group(1), '(not in EEPROM)', 'tap'
    lab = base_label(kc)
    if kc in ('_______', 'KC_TRNS'):
        return lab, '', 'trns'
    if re.fullmatch(r'(MO|TG|TO|TT|OSL|DF)\(\d+\)', kc) or kc in ('KC_LSFT', 'KC_RSFT', 'KC_LCTL', 'KC_RCTL',
                                                                 'KC_LALT', 'KC_RALT', 'KC_LGUI', 'KC_RGUI'):
        return lab, '', 'mod'
    return lab, '', 'plain'


# ------------------------------------------------------------- geometry ----
def parse_kle(rows):
    """Minimal KLE deserializer. Returns a list of dicts with x, y, w, h, r, rx, ry, label, decal."""
    keys = []
    x = y = 0.0
    r = rx = ry = 0.0
    w = h = 1.0
    decal = False
    for row in rows:
        if isinstance(row, dict):
            continue
        for item in row:
            if isinstance(item, dict):
                if 'r' in item: r = item['r']
                if 'rx' in item:
                    rx = item['rx']; x = rx; y = ry
                if 'ry' in item:
                    ry = item['ry']; y = ry; x = rx
                x += item.get('x', 0); y += item.get('y', 0)
                w = item.get('w', 1); h = item.get('h', 1)
                decal = item.get('d', False)
            else:
                keys.append(dict(x=x, y=y, w=w, h=h, r=r, rx=rx, ry=ry, label=item, decal=decal))
                x += w; w = h = 1.0; decal = False
        y += 1; x = rx
    return keys


def rot(px, py, cx, cy, deg):
    a = math.radians(deg)
    dx, dy = px - cx, py - cy
    return cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a)


def key_polygon(k):
    pts = [(k['x'], k['y']), (k['x'] + k['w'], k['y']), (k['x'] + k['w'], k['y'] + k['h']), (k['x'], k['y'] + k['h'])]
    return [rot(px, py, k['rx'], k['ry'], k['r']) for px, py in pts]


# -------------------------------------------------------------- drawing ----
def font(size, bold=False):
    for p in (FONTS_BOLD if bold else FONTS):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def fit_font(draw, text, max_w, start, bold=False, floor=9):
    size = start
    while size > floor:
        f = font(size, bold)
        if max(draw.textlength(line, font=f) for line in text.split('\n')) <= max_w:
            return f
        size -= 1
    return font(floor, bold)


def draw_text_block(img, cx, cy, angle, main, sub, box_w, box_h, scale):
    """Render main (bold) and sub (small) text centred at cx, cy, rotated by angle."""
    tile_w, tile_h = int(box_w * 1.6), int(box_h * 1.6)
    tile = Image.new('RGBA', (tile_w, tile_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(tile)
    inner_w = box_w - 10 * scale
    fm = fit_font(d, main, inner_w, 17 * scale, bold=True, floor=8 * scale) if main else None
    fs = fit_font(d, sub, inner_w, 10 * scale, floor=7 * scale) if sub else None
    hm = (fm.getbbox('Ag')[3] - fm.getbbox('Ag')[1]) * (main.count('\n') + 1) if main else 0
    hs = (fs.getbbox('Ag')[3] - fs.getbbox('Ag')[1]) * (sub.count('\n') + 1) + 1 * scale if sub else 0
    gap = 3 * scale if (main and sub) else 0
    total = hm + gap + hs
    ty = (tile_h - total) / 2
    if main:
        d.multiline_text((tile_w / 2, ty), main, font=fm, fill=TEXT if main != '▽' else TRNS, anchor='ma', align='center')
        ty += hm + gap
    if sub:
        d.multiline_text((tile_w / 2, ty), sub, font=fs, fill=SUB, anchor='ma', align='center')
    if angle:
        tile = tile.rotate(-angle, resample=Image.BICUBIC, expand=False)
    img.alpha_composite(tile, (int(cx - tile_w / 2), int(cy - tile_h / 2)))


def render_layer(keys, layer, names, enc_names, tds, title, bounds):
    s = SCALE
    minx, miny, maxx, maxy = bounds
    W = int((maxx - minx) * U + 2 * PAD) * s
    H = int((maxy - miny) * U + 2 * PAD + 34) * s
    img = Image.new('RGBA', (W, H), BG + (255,))
    d = ImageDraw.Draw(img)
    d.text((PAD * s, 10 * s), title, font=font(16 * s, bold=True), fill=TEXT)
    oy = 34 * s

    def P(px, py):
        return ((px - minx) * U + PAD) * s, ((py - miny) * U + PAD) * s + oy

    for k in keys:
        if k['decal']:
            continue
        lab = k['label'].split('\n')
        is_enc = len(lab) > 9 and lab[9] == 'e'
        rc = lab[0]
        rr, cc = (int(v) for v in rc.split(','))
        poly = [P(*pt) for pt in key_polygon(k)]
        # shrink the keycap by GAP on every side (in the key's own frame)
        g = GAP / U
        kk = dict(k, x=k['x'] + g, y=k['y'] + g, w=k['w'] - 2 * g, h=k['h'] - 2 * g)
        poly = [P(*pt) for pt in key_polygon(kk)]
        cx = sum(p[0] for p in poly) / 4
        cy = sum(p[1] for p in poly) / 4
        if is_enc:
            kc = enc_names[layer][rr]['cw' if cc == 1 else 'ccw']
            main, sub = ('⟳ ' if cc == 1 else '⟲ ') + base_label(kc), 'enc %d' % rr
            fill, kind = KEY_MOD, 'enc'
            rad = (kk['w'] * U / 2) * s
            d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=fill, outline=EDGE, width=s)
        else:
            kc = names[layer][rr][cc]
            main, sub, kind = key_label(kc, tds)
            fill = {'trns': KEY_TRNS, 'mod': KEY_MOD, 'tap': KEY_MOD}.get(kind, KEY)
            d.polygon(poly, fill=fill, outline=EDGE)
            d.line(poly + [poly[0]], fill=EDGE, width=s)
        draw_text_block(img, cx, cy, k['r'], main, sub, kk['w'] * U * s, kk['h'] * U * s, s)
    return img.resize((W // s, H // s), Image.LANCZOS).convert('RGB')


def layer_is_empty(names, enc, layer):
    cells = [c for r in names[layer] for c in r] + [e[k] for e in enc[layer] for k in ('ccw', 'cw')]
    return all(c in ('_______', 'KC_TRNS', 'KC_NO') for c in cells)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('--out')
    ap.add_argument('--vial', default=os.path.join(ROOT, 'keymaps', 'sofle_choc_tp', 'vial.json'))
    ap.add_argument('--title', default=None, help='prefix for the image titles (default: the dump directory name)')
    args = ap.parse_args()
    out = args.out or os.path.join(os.path.dirname(os.path.abspath(args.dump)), 'png')
    os.makedirs(out, exist_ok=True)
    d = json.load(open(args.dump))
    vial = json.load(open(args.vial))
    keys = [k for k in parse_kle(vial['layouts']['keymap']) if not k['decal']]
    pts = [pt for k in keys for pt in key_polygon(k)]
    bounds = (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))
    board = args.title or os.path.basename(os.path.dirname(os.path.abspath(args.dump)))
    written = []
    for layer in range(d['layer_count']):
        if layer and layer_is_empty(d['keymap_names'], d['encoders_names'], layer):
            continue
        title = '%s: layer %d%s' % (board, layer, ' (%s)' % LAYER_NAMES[layer] if layer in LAYER_NAMES else '')
        img = render_layer(keys, layer, d['keymap_names'], d['encoders_names'], d['tap_dance'], title, bounds)
        p = os.path.join(out, 'layer%d.png' % layer)
        img.save(p, optimize=True)
        written.append(p)
    # drop images of layers that no longer exist
    for f in os.listdir(out):
        if re.fullmatch(r'layer\d+\.png', f) and os.path.join(out, f) not in written:
            os.remove(os.path.join(out, f))
    print('\n'.join(written))


if __name__ == '__main__':
    main()
