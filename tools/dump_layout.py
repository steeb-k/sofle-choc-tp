#!/usr/bin/env python3
"""READ-ONLY dump of the live Sofle Choc keymap over Vial/VIA raw HID.

Only *_get_* / query commands are ever sent; send() enforces a whitelist and
refuses anything else, so the keyboard's EEPROM cannot be modified.

Usage: .venv/bin/python tools/dump_layout.py [--vil ~/Documents/sofle-mine.vil]
Outputs (in --out, use layout/<board>): current_layout.json, current_layout_keymap.c.txt,
         diff_vs_backup.txt (if --vil given / file exists), png/layer*.png via tools/render_layout.py
"""
import argparse, json, os, re, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENDOR = os.path.join(ROOT, 'vendor', 'vial-qmk')
sys.path.insert(0, os.path.join(VENDOR, 'lib', 'python'))

VID, PID = 0x424C, 0x5343
USAGE_PAGE, USAGE = 0xFF60, 0x61
LAYERS, ROWS, COLS, ENCODERS = 7, 10, 6, 2

# ---- read-only command whitelist (verified against quantum/via.h, vial.h) ----
VIA_GET_PROTOCOL = 0x01
VIA_KEYMAP_GET_KEYCODE = 0x04
VIA_MACRO_GET_COUNT, VIA_MACRO_GET_SIZE, VIA_MACRO_GET_BUFFER = 0x0C, 0x0D, 0x0E
VIA_GET_LAYER_COUNT = 0x11
VIA_KEYMAP_GET_BUFFER = 0x12
VIAL_PREFIX = 0xFE
VIAL_GET_KEYBOARD_ID, VIAL_GET_SIZE, VIAL_GET_DEF = 0x00, 0x01, 0x02
VIAL_GET_ENCODER = 0x03
VIAL_GET_UNLOCK_STATUS = 0x05
VIAL_QS_QUERY, VIAL_QS_GET = 0x09, 0x0A
VIAL_DYNAMIC = 0x0D
DYN_COUNT, DYN_TD_GET, DYN_COMBO_GET, DYN_KO_GET, DYN_AREP_GET = 0x00, 0x01, 0x03, 0x05, 0x07
ALLOWED_VIA = {VIA_GET_PROTOCOL, VIA_KEYMAP_GET_KEYCODE, VIA_MACRO_GET_COUNT, VIA_MACRO_GET_SIZE,
               VIA_MACRO_GET_BUFFER, VIA_GET_LAYER_COUNT, VIA_KEYMAP_GET_BUFFER}
ALLOWED_VIAL = {VIAL_GET_KEYBOARD_ID, VIAL_GET_SIZE, VIAL_GET_DEF, VIAL_GET_ENCODER,
                VIAL_GET_UNLOCK_STATUS, VIAL_QS_QUERY, VIAL_QS_GET}
ALLOWED_DYN = {DYN_COUNT, DYN_TD_GET, DYN_COMBO_GET, DYN_KO_GET, DYN_AREP_GET}


def open_dev():
    import hid
    cands = [d for d in hid.enumerate(VID, PID)
             if d['usage_page'] == USAGE_PAGE and d['usage'] == USAGE]
    if not cands:
        raise SystemExit('Raw HID interface (0xFF60/0x61) for %04x:%04x not found. Found: %s' % (
            VID, PID, [(d['usage_page'], d['usage'], d.get('product_string')) for d in hid.enumerate(VID, PID)]))
    try:
        if hasattr(hid, 'Device'):      # 'hid' >= 1.0 (ctypes wrapper)
            dev = hid.Device(path=cands[0]['path'])
        else:                            # 'hidapi' (cython)
            dev = hid.device()
            dev.open_path(cands[0]['path'])
    except Exception as e:
        raise SystemExit('FAILED to open HID device: %r' % (e,))
    return dev, cands[0]


def send(dev, payload):
    """payload: list of up to 32 bytes. Returns 32-byte response. READ-ONLY guard."""
    p = list(payload)
    cmd = p[0]
    if cmd == VIAL_PREFIX:
        if p[1] not in ALLOWED_VIAL and not (p[1] == VIAL_DYNAMIC and p[2] in ALLOWED_DYN):
            raise RuntimeError('refusing non-read-only vial command %r' % p[:4])
    elif cmd not in ALLOWED_VIA:
        raise RuntimeError('refusing non-read-only command %r' % p[:4])
    buf = bytes(p + [0] * (32 - len(p)))
    for attempt in range(3):
        try:  # drain stale reports
            while dev.read(32, 0):
                pass
        except Exception:
            pass
        dev.write(b'\x00' + buf)
        r = dev.read(32, 1000)
        if r and len(r) == 32:
            return list(r)
        time.sleep(0.1)
    raise RuntimeError('no response to %r' % p[:4])


# ------------------------------ keycode naming ------------------------------
def load_names():
    from qmk.keycodes import load_spec
    cwd = os.getcwd()
    os.chdir(VENDOR)  # qmk.keycodes uses cwd-relative data paths
    try:
        spec = load_spec('latest')
    finally:
        os.chdir(cwd)
    code2name, name2code = {}, {}
    for k, v in spec['keycodes'].items():
        code = int(k, 16)
        names = [v['key']] + list(v.get('aliases', []))
        for n in names:
            name2code.setdefault(n, code)
        # prefer short, readable KC_ names like the vendor keymap (KC_GRV, KC_BSPC)
        best = sorted(names, key=lambda n: (len(n), n != v['key']))[0]
        if best == 'XXXXXXX':
            best = 'KC_NO'
        code2name[code] = best
    return code2name, name2code


MODS = [(0x01, 'CTL'), (0x02, 'SFT'), (0x04, 'ALT'), (0x08, 'GUI')]
MODNAME = {'CTL': 'CTL', 'SFT': 'SFT', 'ALT': 'ALT', 'GUI': 'GUI'}


def mod_expr(bits):
    right = bool(bits & 0x10)
    parts = ['MOD_%s%s' % ('R' if right else 'L', n) for b, n in MODS if bits & b]
    return '|'.join(parts) if parts else '0'


class Namer:
    def __init__(self):
        self.c2n, self.n2c = load_names()
        self.unmapped = {}

    def name(self, kc):
        s = self._name(kc)
        return s

    def _name(self, kc):
        if kc in self.c2n and kc < 0x100:
            return self.c2n[kc]
        if 0x100 <= kc <= 0x1FFF:
            bits, base = (kc >> 8) & 0x1F, kc & 0xFF
            inner = self._name(base)
            right = bits & 0x10
            m = bits & 0xF
            if m == 0xF:
                return 'HYPR(%s)' % inner if not right else self._hex(kc, 'right-mod all four')
            if m == 0x7 and not right:
                return 'MEH(%s)' % inner
            for b, n in reversed(MODS):
                if m & b:
                    inner = '%s%s(%s)' % ('R' if right else 'L', n, inner)
            return inner
        if 0x2000 <= kc <= 0x3FFF:
            bits, base = (kc >> 8) & 0x1F, kc & 0xFF
            m = bits & 0xF
            right = bool(bits & 0x10)
            inner = self._name(base)
            if bin(m).count('1') == 1:
                n = [n for b, n in MODS if m & b][0]
                return '%s%s_T(%s)' % ('R' if right else 'L', n, inner)
            if m == 0xF and not right:
                return 'HYPR_T(%s)' % inner
            if m == 0x7 and not right:
                return 'MEH_T(%s)' % inner
            return 'MT(%s, %s)' % (mod_expr(bits), inner)
        if 0x4000 <= kc <= 0x4FFF:
            return 'LT(%d, %s)' % ((kc >> 8) & 0xF, self._name(kc & 0xFF))
        if 0x5000 <= kc <= 0x51FF:
            return 'LM(%d, %s)' % ((kc >> 5) & 0xF, mod_expr(kc & 0x1F))
        for base, mx, fn in ((0x5200, 0x521F, 'TO'), (0x5220, 0x523F, 'MO'), (0x5240, 0x525F, 'DF'),
                             (0x5260, 0x527F, 'TG'), (0x5280, 0x529F, 'OSL'), (0x52C0, 0x52DF, 'TT'),
                             (0x52E0, 0x52FF, 'PDF')):
            if base <= kc <= mx:
                return '%s(%d)' % (fn, kc - base)
        if 0x52A0 <= kc <= 0x52BF:
            return 'OSM(%s)' % mod_expr(kc & 0x1F)
        if 0x5700 <= kc <= 0x57FF:
            return 'TD(%d)' % (kc - 0x5700)
        if kc in self.c2n:
            return self.c2n[kc]
        return self._hex(kc, 'unmapped')

    def _hex(self, kc, why):
        self.unmapped[kc] = why
        return '0x%04X /* %s */' % (kc, why)

    # ---- parse names (for the .vil backup, which uses legacy Vial names) ----
    def parse(self, s):
        if isinstance(s, int):
            return s if s >= 0 else None
        s = s.strip()
        if s in ('', 'KC_NO'):
            return 0
        m = re.fullmatch(r'0x[0-9a-fA-F]+', s)
        if m:
            return int(s, 16)
        if s in self.n2c:
            return self.n2c[s]
        legacy = {'KC_BSLASH': 'KC_BSLS', 'KC_BSPACE': 'KC_BSPC', 'KC_CAPSLOCK': 'KC_CAPS', 'KC_LBRACKET': 'KC_LBRC',
                  'KC_RBRACKET': 'KC_RBRC', 'KC_LCTRL': 'KC_LCTL', 'KC_RCTRL': 'KC_RCTL', 'KC_LSHIFT': 'KC_LSFT',
                  'KC_RSHIFT': 'KC_RSFT', 'KC_PGDOWN': 'KC_PGDN', 'KC_PSCREEN': 'KC_PSCR', 'KC_SCOLON': 'KC_SCLN'}
        if s in legacy:  # legacy Vial/QMK names used in .vil files
            return self.n2c[legacy[s]]
        if s.startswith('KC_') and 'KC_' + s[3:] in self.n2c:
            return self.n2c[s]
        m = re.fullmatch(r'(\w+)\((.*)\)', s)
        if not m:
            raise ValueError(s)
        fn, args = m.group(1), self._split(m.group(2))
        num = lambda a: int(a)
        simple = {'TO': 0x5200, 'MO': 0x5220, 'DF': 0x5240, 'TG': 0x5260, 'OSL': 0x5280, 'TT': 0x52C0,
                  'PDF': 0x52E0, 'TD': 0x5700, 'MACRO': 0x7700, 'M': 0x7700}
        if fn in simple:
            return simple[fn] + num(args[0])
        if fn == 'LT':
            return 0x4000 | (num(args[0]) << 8) | self.parse(args[1])
        if fn == 'LM':
            return 0x5000 | (num(args[0]) << 5) | self._mods(args[1])
        if fn == 'OSM':
            return 0x52A0 | self._mods(args[0])
        if fn == 'MT':
            return 0x2000 | (self._mods(args[0]) << 8) | self.parse(args[1])
        modfn = {'LCTL': 0x01, 'LSFT': 0x02, 'LALT': 0x04, 'LGUI': 0x08, 'RCTL': 0x11, 'RSFT': 0x12,
                 'RALT': 0x14, 'RGUI': 0x18, 'C': 0x01, 'S': 0x02, 'A': 0x04, 'G': 0x08, 'LCMD': 0x08,
                 'LWIN': 0x08, 'RCMD': 0x18, 'RWIN': 0x18, 'RCTRL': 0x11, 'LCTRL': 0x01, 'MEH': 0x07,
                 'HYPR': 0x0F, 'LOPT': 0x04, 'ROPT': 0x14}
        if fn in modfn:
            return 0x0100 * 0 + ((modfn[fn] << 8) | self.parse(args[0]))
        mt = re.fullmatch(r'([LR])?(CTL|SFT|ALT|GUI|MEH|HYPR)_T', fn)
        if mt:
            bits = {'CTL': 1, 'SFT': 2, 'ALT': 4, 'GUI': 8, 'MEH': 7, 'HYPR': 15}[mt.group(2)]
            if mt.group(1) == 'R':
                bits |= 0x10
            return 0x2000 | (bits << 8) | self.parse(args[0])
        raise ValueError(s)

    @staticmethod
    def _split(a):
        out, d, cur = [], 0, ''
        for ch in a:
            if ch == ',' and d == 0:
                out.append(cur.strip()); cur = ''
                continue
            d += (ch == '(') - (ch == ')')
            cur += ch
        out.append(cur.strip())
        return out

    def _mods(self, s):
        v = 0
        for p in re.split(r'\s*\|\s*', s.strip()):
            m = re.fullmatch(r'MOD_([LR])(CTL|SFT|ALT|GUI)', p)
            if not m:
                raise ValueError(s)
            v |= {'CTL': 1, 'SFT': 2, 'ALT': 4, 'GUI': 8}[m.group(2)] | (0x10 if m.group(1) == 'R' else 0)
        return v


# ------------------------------ macro decoding ------------------------------
def decode_ext(kc):
    return (kc & 0xFF) << 8 if kc > 0xFF00 else kc


def decode_macros(buf, count, namer):
    chunks = buf.split(b'\x00')
    macros = []
    for i in range(count):
        data = chunks[i] if i < len(chunks) else b''
        acts, j, text = [], 0, ''
        def flush():
            nonlocal text
            if text:
                acts.append({'type': 'text', 'text': text}); text = ''
        while j < len(data):
            b = data[j]; j += 1
            if b == 1 and j < len(data):
                t = data[j]; j += 1
                if t in (1, 2, 3):
                    flush()
                    kc = data[j]; j += 1
                    acts.append({'type': {1: 'tap', 2: 'down', 3: 'up'}[t], 'keycode': kc, 'name': namer.name(kc)})
                elif t in (5, 6, 7):
                    flush()
                    kc = decode_ext(data[j] | (data[j + 1] << 8)); j += 2
                    acts.append({'type': {5: 'tap', 6: 'down', 7: 'up'}[t], 'keycode': kc, 'name': namer.name(kc), 'ext': True})
                elif t == 4:
                    flush()
                    ms = (data[j] - 1) + (data[j + 1] - 1) * 255; j += 2
                    acts.append({'type': 'delay', 'ms': ms})
                else:
                    acts.append({'type': 'unknown', 'byte': t})
            else:
                text += chr(b)
        flush()
        macros.append(acts)
    return macros


def macro_str(acts):
    out = []
    for a in acts:
        if a['type'] == 'text':
            out.append('"%s"' % a['text'])
        elif a['type'] == 'delay':
            out.append('delay(%dms)' % a['ms'])
        elif a['type'] in ('tap', 'down', 'up'):
            out.append('%s(%s)' % (a['type'], a['name']))
        else:
            out.append(str(a))
    return ' '.join(out)


# ----------------------------------- main -----------------------------------
QS_WIDTH = {1: 1, 2: 2, 3: 1, 4: 2, 5: 1, 6: 2, 7: 2, 18: 1, 19: 1, 20: 1, 21: 1, 22: 1, 23: 1, 24: 1,
            25: 2, 26: 1, 27: 2}
for _i in range(9, 18):
    QS_WIDTH[_i] = 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vil', default=os.path.expanduser('~/Documents/sofle-mine.vil'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'layout'),
                    help='directory for the dump; use layout/<board> (debian, cachyos)')
    ap.add_argument('--no-png', action='store_true', help='skip rendering layout/<board>/png/')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    namer = Namer()
    dev, info = open_dev()
    print('Opened', info.get('product_string'), info['path'])
    S = lambda *p: send(dev, list(p))

    r = S(VIA_GET_PROTOCOL)
    via_proto = (r[1] << 8) | r[2]
    r = S(VIAL_PREFIX, VIAL_GET_KEYBOARD_ID)
    vial_proto = r[0] | r[1] << 8 | r[2] << 16 | r[3] << 24
    vial_uid = bytes(r[4:12]).hex()
    r = S(VIAL_PREFIX, VIAL_GET_SIZE)
    def_size = r[0] | r[1] << 8 | r[2] << 16 | r[3] << 24
    r = S(VIAL_PREFIX, VIAL_GET_UNLOCK_STATUS)
    unlocked = r[0]
    r = S(VIA_GET_LAYER_COUNT)
    layers = r[1]
    print('via proto', via_proto, 'vial proto', vial_proto, 'uid', vial_uid, 'layers', layers, 'unlocked', unlocked)
    assert layers == LAYERS, layers

    # keymap via get_buffer
    total = layers * ROWS * COLS * 2
    raw = bytearray()
    while len(raw) < total:
        n = min(28, total - len(raw))
        r = S(VIA_KEYMAP_GET_BUFFER, len(raw) >> 8, len(raw) & 0xFF, n)
        raw += bytes(r[4:4 + n])
    keymap = [[[(raw[((l * ROWS + rw) * COLS + c) * 2] << 8) | raw[((l * ROWS + rw) * COLS + c) * 2 + 1]
                for c in range(COLS)] for rw in range(ROWS)] for l in range(layers)]
    # cross-check a few single-key reads
    for (l, rw, c) in ((0, 0, 0), (2, 5, 3), (6, 9, 5)):
        r = S(VIA_KEYMAP_GET_KEYCODE, l, rw, c)
        assert ((r[4] << 8) | r[5]) == keymap[l][rw][c], ('mismatch', l, rw, c)

    # encoders (Vial 0x03): response bytes 0-1 = ccw, 2-3 = cw
    enc = []
    for l in range(layers):
        row = []
        for e in range(ENCODERS):
            r = S(VIAL_PREFIX, VIAL_GET_ENCODER, l, e)
            row.append({'ccw': (r[0] << 8) | r[1], 'cw': (r[2] << 8) | r[3]})
        enc.append(row)

    # macros
    mcount = S(VIA_MACRO_GET_COUNT)[1]
    r = S(VIA_MACRO_GET_SIZE)
    msize = (r[1] << 8) | r[2]
    mbuf = bytearray()
    while len(mbuf) < msize:
        n = min(28, msize - len(mbuf))
        r = S(VIA_MACRO_GET_BUFFER, len(mbuf) >> 8, len(mbuf) & 0xFF, n)
        mbuf += bytes(r[4:4 + n])
    macros = decode_macros(bytes(mbuf), mcount, namer)

    # dynamic entries
    r = S(VIAL_PREFIX, VIAL_DYNAMIC, DYN_COUNT)
    n_td, n_combo, n_ko, n_arep = r[0], r[1], r[2], r[3]
    feats = r[31]
    u16 = lambda b, o: b[o] | (b[o + 1] << 8)
    tds, combos, kos, areps = [], [], [], []
    for i in range(n_td):
        r = S(VIAL_PREFIX, VIAL_DYNAMIC, DYN_TD_GET, i)
        tds.append({'status': r[0], 'on_tap': u16(r, 1), 'on_hold': u16(r, 3), 'on_double_tap': u16(r, 5),
                    'on_tap_hold': u16(r, 7), 'tapping_term': u16(r, 9)})
    for i in range(n_combo):
        r = S(VIAL_PREFIX, VIAL_DYNAMIC, DYN_COMBO_GET, i)
        combos.append({'status': r[0], 'input': [u16(r, 1), u16(r, 3), u16(r, 5), u16(r, 7)], 'output': u16(r, 9)})
    for i in range(n_ko):
        r = S(VIAL_PREFIX, VIAL_DYNAMIC, DYN_KO_GET, i)
        kos.append({'status': r[0], 'trigger': u16(r, 1), 'replacement': u16(r, 3), 'layers': u16(r, 5),
                    'trigger_mods': r[7], 'negative_mod_mask': r[8], 'suppressed_mods': r[9], 'options': r[10]})
    for i in range(n_arep):
        r = S(VIAL_PREFIX, VIAL_DYNAMIC, DYN_AREP_GET, i)
        areps.append({'status': r[0], 'keycode': u16(r, 1), 'alt_keycode': u16(r, 3),
                      'allowed_mods': r[5], 'options': r[6]})

    # QMK settings
    qs, ids, gt = {}, [], 0
    while True:
        r = S(VIAL_PREFIX, VIAL_QS_QUERY, gt & 0xFF, gt >> 8)
        got = [u16(r, o) for o in range(0, 32, 2) if u16(r, o) != 0xFFFF]
        if not got:
            break
        ids += got
        gt = got[-1]
    for q in ids:
        r = S(VIAL_PREFIX, VIAL_QS_GET, q & 0xFF, q >> 8)
        w = QS_WIDTH.get(q, 1)
        val = int.from_bytes(bytes(r[1:1 + w]), 'little')
        qs[str(q)] = {'status': r[0], 'raw_hex': bytes(r[1:5]).hex(), 'width_assumed': w, 'value': val}

    dev.close()

    # ---- JSON ----
    N = namer.name
    out = {
        'via_protocol': via_proto, 'vial_protocol': vial_proto, 'vial_keyboard_uid': vial_uid,
        'vial_definition_size': def_size, 'vial_unlocked': unlocked, 'layer_count': layers,
        'matrix': {'rows': ROWS, 'cols': COLS}, 'encoder_count': ENCODERS,
        'counts': {'macros': mcount, 'tap_dance': n_td, 'combo': n_combo, 'key_override': n_ko,
                   'alt_repeat_key': n_arep, 'macro_buffer_bytes': msize, 'feature_flags': feats},
        'keymap': keymap,
        'keymap_names': [[[N(k) for k in row] for row in layer] for layer in keymap],
        'encoders': enc,
        'encoders_names': [[{'ccw': N(e['ccw']), 'cw': N(e['cw'])} for e in row] for row in enc],
        'macro_buffer_hex': bytes(mbuf).hex(),
        'macros': [{'index': i, 'actions': m, 'readable': macro_str(m)} for i, m in enumerate(macros)],
        'tap_dance': [dict(t, names={k: N(t[k]) for k in ('on_tap', 'on_hold', 'on_double_tap', 'on_tap_hold')}) for t in tds],
        'combos': [dict(c, names={'input': [N(k) for k in c['input']], 'output': N(c['output'])}) for c in combos],
        'key_overrides': [dict(k, names={'trigger': N(k['trigger']), 'replacement': N(k['replacement'])}) for k in kos],
        'alt_repeat_keys': [dict(a, names={'keycode': N(a['keycode']), 'alt_keycode': N(a['alt_keycode'])}) for a in areps],
        'qmk_settings': qs,
        'unmapped_keycodes': {'0x%04X' % k: v for k, v in sorted(namer.unmapped.items())},
    }
    with open(os.path.join(args.out, 'current_layout.json'), 'w') as f:
        json.dump(out, f, indent=1)

    # ---- C file ----
    kb = json.load(open(os.path.join(VENDOR, 'keyboards', 'sofle_choc', 'keyboard.json')))
    order = [tuple(k['matrix']) for k in kb['layouts']['LAYOUT']['layout']]
    lines = ['// Generated by tools/dump_layout.py from the live keyboard (read-only).',
             'const uint16_t PROGMEM keymaps[DYNAMIC_KEYMAP_LAYER_COUNT][MATRIX_ROWS][MATRIX_COLS] = {']
    # same shape as vendor: LAYOUT(...) wrapped, per layer
    lines = ['// Generated by tools/dump_layout.py from the live keyboard (read-only).',
             'const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {']
    for l in range(layers):
        lines.append('[%d] = LAYOUT(' % l)
        names = [N(keymap[l][r][c]) for (r, c) in order]
        # json order is row-by-row: 12, 12, 12, 14 (incl. 2 encoder-click keys), 10 (thumbs)
        i = 0
        for ri, cnt in enumerate((12, 12, 12, 14, 10)):
            seg = names[i:i + cnt]; i += cnt
            half = cnt // 2
            text = ', '.join(seg[:half]) + ',   ' + ', '.join(seg[half:])
            lines.append('  ' + text + (',' if ri < 4 else ''))
        lines.append('),')
    lines.append('};')
    lines.append('')
    lines.append('#if defined(ENCODER_MAP_ENABLE)')
    lines.append('const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {')
    for l in range(layers):
        lines.append('    [%d] = { %s },' % (l, ', '.join('ENCODER_CCW_CW(%s, %s)' % (N(e['ccw']), N(e['cw'])) for e in enc[l])))
    lines.append('};')
    lines.append('#endif')
    lines.append('')
    lines.append('/* Macros (Vial macro buffer, %d bytes, %d slots) */' % (msize, mcount))
    for i, m in enumerate(macros):
        if m:
            lines.append('/* macro %d: %s */' % (i, macro_str(m)))
    lines.append('/* Tap dance / combos / key overrides: see current_layout.json */')
    for i, t in enumerate(tds):
        if any(t[k] for k in ('on_tap', 'on_hold', 'on_double_tap', 'on_tap_hold')):
            lines.append('/* TD(%d): tap=%s hold=%s dtap=%s taphold=%s term=%d */' % (
                i, N(t['on_tap']), N(t['on_hold']), N(t['on_double_tap']), N(t['on_tap_hold']), t['tapping_term']))
    for i, c in enumerate(combos):
        if c['output']:
            lines.append('/* combo %d: %s -> %s */' % (i, ' + '.join(N(k) for k in c['input'] if k), N(c['output'])))
    for i, k in enumerate(kos):
        if k['trigger'] or k['replacement']:
            lines.append('/* key_override %d: %s -> %s layers=0x%04X trig_mods=0x%02X neg=0x%02X supp=0x%02X opts=0x%02X */' % (
                i, N(k['trigger']), N(k['replacement']), k['layers'], k['trigger_mods'], k['negative_mod_mask'], k['suppressed_mods'], k['options']))
    open(os.path.join(args.out, 'current_layout_keymap.c.txt'), 'w').write('\n'.join(lines) + '\n')

    # ---- diff vs .vil ----
    rep = []
    if os.path.exists(args.vil):
        vil = json.load(open(args.vil))
        bad = set()
        def pv(s):
            try:
                return namer.parse(s)
            except Exception:
                bad.add(str(s)); return None
        ndiff = 0
        for l in range(layers):
            for r_ in range(ROWS):
                for c in range(COLS):
                    old = vil['layout'][l][r_][c]
                    oc = pv(old)
                    nc = keymap[l][r_][c]
                    same = (oc == nc) if oc is not None else False
                    if not same:
                        ndiff += 1
                        rep.append('L%d r%d c%d: backup=%s now=%s' % (l, r_, c, old, N(nc)))
        for l in range(layers):
            for e in range(ENCODERS):
                o = vil['encoder_layout'][l][e]
                for j, k in enumerate(('ccw', 'cw')):
                    if pv(o[j]) != enc[l][e][k]:
                        rep.append('ENC L%d e%d %s: backup=%s now=%s' % (l, e, k, o[j], N(enc[l][e][k])))
        for i in range(min(len(vil['macro']), mcount)):
            if vil['macro'][i] != [] or macros[i]:
                rep.append('MACRO %d: backup=%r now=%s' % (i, vil['macro'][i], macro_str(macros[i])))
        for i, t in enumerate(vil['tap_dance']):
            now = [tds[i][k] for k in ('on_tap', 'on_hold', 'on_double_tap', 'on_tap_hold', 'tapping_term')] if i < n_td else None
            old = [pv(x) for x in t[:4]] + [t[4]]
            if now != old:
                rep.append('TD %d: backup=%r now=%r' % (i, t, now))
        for i, t in enumerate(vil['combo']):
            now = (combos[i]['input'] + [combos[i]['output']]) if i < n_combo else None
            if now != [pv(x) for x in t]:
                rep.append('COMBO %d: backup=%r now=%r' % (i, t, now))
        for i, k in enumerate(vil['key_override']):
            if i >= n_ko: break
            now = kos[i]
            old = (pv(k['trigger']), pv(k['replacement']), k['layers'], k['trigger_mods'], k['negative_mod_mask'], k['suppressed_mods'], k['options'])
            nw = (now['trigger'], now['replacement'], now['layers'], now['trigger_mods'], now['negative_mod_mask'], now['suppressed_mods'], now['options'])
            if old != nw:
                rep.append('KEY_OVERRIDE %d: backup=%r now=%r' % (i, k, nw))
        for i, a in enumerate(vil.get('alt_repeat_key', [])):
            if i >= n_arep: break
            now = areps[i]
            if [pv(a['keycode']), pv(a['alt_keycode']), a['allowed_mods'], a['options']] != [now['keycode'], now['alt_keycode'], now['allowed_mods'], now['options']]:
                rep.append('ALT_REPEAT %d: backup=%r now=%r' % (i, a, now))
        for q, v in vil.get('settings', {}).items():
            if q in qs and qs[q]['value'] != v:
                rep.append('QMK_SETTING %s: backup=%r now=%r' % (q, v, qs[q]['value']))
        if bad:
            rep.append('UNPARSEABLE backup names (counted as differing): %s' % sorted(bad))
        rep.insert(0, 'key diffs: %d of %d' % (ndiff, layers * ROWS * COLS))
        open(os.path.join(args.out, 'diff_vs_backup.txt'), 'w').write('\n'.join(rep) + '\n')

    print(json.dumps(out['counts']))
    print('unmapped:', out['unmapped_keycodes'])
    print('\n'.join(rep))

    if not args.no_png:
        # Redraw the per-layer pictures next to the dump (tools/render_layout.py).
        import subprocess
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'render_layout.py'),
                        os.path.join(args.out, 'current_layout.json')], check=False)


if __name__ == '__main__':
    main()
