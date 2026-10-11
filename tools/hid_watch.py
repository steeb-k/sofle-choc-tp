#!/usr/bin/env python3
"""Print what the Sofle sends over USB, decoded, so a swipe or chord can be
checked on a host without seeing the OLED. Linux only: reads the board's
hidraw nodes directly (the Vial udev rule makes them world-readable, so no
root or input group is needed; nothing is grabbed, the desktop keeps working).

    python3 tools/hid_watch.py            # keys, consumer usages, mouse buttons/wheel
    python3 tools/hid_watch.py --motion   # also cursor motion (noisy)
    python3 tools/hid_watch.py -t 30      # stop after 30 s

A three-finger swipe should print one key press and release, e.g. on Linux
"kbd  press  LGui+LAlt+Right"; swipe up prints "consumer 0x029F (AC Desktop
Show All Windows = KC_MISSION_CONTROL, XF86LaunchA on Linux)".
"""
import argparse
import glob
import os
import select
import sys
import time

MODS = ["LCtl", "LSft", "LAlt", "LGui", "RCtl", "RSft", "RAlt", "RGui"]
KEYS = {
    0x2B: "Tab", 0x4F: "Right", 0x50: "Left", 0x51: "Down", 0x52: "Up",
    0x28: "Enter", 0x29: "Esc", 0x2C: "Space", 0x2A: "Backspace",
}
CONSUMER = {0x29F: "AC Desktop Show All Windows = KC_MISSION_CONTROL, XF86LaunchA on Linux",
            0x2A0: "AC Soft Keyboard", 0xB5: "Next Track", 0xB6: "Prev Track", 0xCD: "Play/Pause",
            0xE2: "Mute", 0xE9: "Vol+", 0xEA: "Vol-"}
REPORT_IDS = {1: "keyboard", 2: "mouse", 3: "system", 4: "consumer", 5: "prog button", 6: "nkro", 7: "joystick", 8: "digitizer"}


def key_name(code):
    if code in KEYS:
        return KEYS[code]
    if 0x04 <= code <= 0x1D:
        return chr(ord("a") + code - 0x04)
    if 0x1E <= code <= 0x27:
        return "1234567890"[code - 0x1E]
    if 0x3A <= code <= 0x45:
        return "F%d" % (code - 0x39)
    return "0x%02X" % code


def fmt_kbd(mods, keys):
    parts = [MODS[i] for i in range(8) if mods & (1 << i)]
    parts += [key_name(k) for k in keys if k]
    return "+".join(parts) if parts else "(none)"


def find_devices():
    devs = []
    for uevent in glob.glob("/sys/class/hidraw/hidraw*/device/uevent"):
        info = dict(line.split("=", 1) for line in open(uevent).read().split("\n") if "=" in line)
        if "Sofle" not in info.get("HID_NAME", ""):
            continue
        node = "/dev/" + uevent.split("/")[4]
        phys = info.get("HID_PHYS", "")
        iface = phys.rsplit("/input", 1)[-1] if "/input" in phys else "?"
        devs.append((node, iface))
    return sorted(devs, key=lambda d: d[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--motion", action="store_true", help="also print mouse motion and plain scroll")
    ap.add_argument("-t", "--timeout", type=float, default=0, help="stop after this many seconds (0 = run until Ctrl-C)")
    args = ap.parse_args()

    start = time.monotonic()

    def open_devices():
        # Wait for the board (it may be replugged or still enumerating), then open its nodes.
        while True:
            devs = find_devices()
            if devs:
                break
            if args.timeout and time.monotonic() - start > args.timeout:
                sys.exit("no Sofle hidraw device found (is the board plugged in?)")
            time.sleep(0.5)
        fds = {}
        for node, iface in devs:
            if iface == "1":
                continue  # Vial raw HID channel; nothing to decode and Vial needs it
            try:
                fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
            except PermissionError:
                sys.exit("cannot open %s; install the Vial udev rule or run as root" % node)
            except OSError:
                continue
            fds[fd] = (node, "kbd" if iface == "0" else "shared")
            print("%7.3f watching %s (interface %s)" % (time.monotonic() - start, node, iface))
        print("ready: tap, swipe or press a chord on the board (Ctrl-C to stop)")
        return fds

    fds = open_devices()
    last_kbd = None
    last_buttons = 0
    while True:
        remaining = None
        if args.timeout:
            remaining = args.timeout - (time.monotonic() - start)
            if remaining <= 0:
                break
        ready, _, _ = select.select(list(fds), [], [], remaining)
        for fd in ready:
            node, kind = fds[fd]
            try:
                data = os.read(fd, 64)
            except BlockingIOError:
                continue
            except OSError:
                # Board unplugged: drop the nodes and wait for it to come back.
                print("%7.3f board disconnected, waiting for it" % (time.monotonic() - start))
                for f in fds:
                    os.close(f)
                time.sleep(1.0)
                fds = open_devices()
                last_kbd, last_buttons = None, 0
                break
            if not data:
                continue
            ts = "%7.3f" % (time.monotonic() - start)
            if kind == "kbd":
                mods, keys = data[0], list(data[2:8])
                state = (mods, tuple(k for k in keys if k))
                if state == last_kbd:
                    continue
                last_kbd = state
                print("%s kbd      %s" % (ts, "release" if state == (0, ()) else "press   " + fmt_kbd(mods, keys)))
                continue
            rid, body = data[0], data[1:]
            name = REPORT_IDS.get(rid, "id %d" % rid)
            if name == "consumer":
                usage = body[0] | (body[1] << 8) if len(body) >= 2 else 0
                print("%s consumer %s" % (ts, "release" if usage == 0 else "0x%04X (%s)" % (usage, CONSUMER.get(usage, "?"))))
            elif name == "mouse":
                # QMK shared-EP mouse report: buttons, x, y, v, h (x/y 8 or 16 bit)
                if len(body) >= 7:
                    buttons = body[0]
                    x = int.from_bytes(body[1:3], "little", signed=True)
                    y = int.from_bytes(body[3:5], "little", signed=True)
                    v, h = body[5] - 256 * (body[5] > 127), body[6] - 256 * (body[6] > 127)
                else:
                    buttons, x, y = body[0], body[1] - 256 * (body[1] > 127), body[2] - 256 * (body[2] > 127)
                    v = body[3] - 256 * (body[3] > 127) if len(body) > 3 else 0
                    h = body[4] - 256 * (body[4] > 127) if len(body) > 4 else 0
                if buttons != last_buttons:
                    print("%s mouse    buttons %s" % (ts, " ".join("B%d" % (i + 1) for i in range(8) if buttons & (1 << i)) or "none"))
                    last_buttons = buttons
                if (v or h) and (args.motion or buttons == 0):
                    print("%s mouse    wheel v=%+d h=%+d" % (ts, v, h))
                if (x or y) and args.motion:
                    print("%s mouse    move x=%+d y=%+d" % (ts, x, y))
            elif name == "nkro":
                mods = body[0]
                keys = [i * 8 + b for i, byte in enumerate(body[1:]) for b in range(8) if byte & (1 << b)]
                state = (mods, tuple(keys))
                if state == last_kbd:
                    continue
                last_kbd = state
                print("%s nkro     %s" % (ts, "release" if state == (0, ()) else "press   " + fmt_kbd(mods, keys)))
            else:
                print("%s %-8s %s" % (ts, name, data.hex(" ")))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
