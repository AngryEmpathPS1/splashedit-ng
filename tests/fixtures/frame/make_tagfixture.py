# Writes tagged.gpd beside this script, the fixture of primnamecheck.py: one
# frame of examples/courtyard captured through the editor's Play + Capture
# frame with psxsplash's capture tags on (psxsplash src/capturetag.hh; 571
# primitives, 249 of them tagged: the engine leaves the rest untagged when its
# bump allocator runs low), trimmed to about 23 KB. The capture's full VRAM
# restore (a 1 MB GP0(A0h) upload) is replaced by fills of the CLUT lines the
# courtyard textures use (y 240-255 from x 0), so every texel indexes a non-zero,
# opaque colour: each textured triangle draws (and owns) all of its pixels in
# a flat tint per texture. Everything else is kept word for word.
# usage: python3 -P make_tagfixture.py <capture.gpd>
import os, struct, sys

MAGIC = b'PSXGPUDUMPv1r1\x00\x00'


def fill(rgb, x, y, w, h):
    return [0x02000000 | rgb, (y << 16) | x, (h << 16) | w]


src = open(sys.argv[1], 'rb').read()
assert src[:16] == MAGIC, 'not a GPU dump'
words = struct.unpack_from('<%dI' % ((len(src) - 16) // 4), src, 16)
out, i, live, replaced = [], 0, False, 0
while i < len(words):
    kind, n = words[i] >> 24, words[i] & 0xffffff
    body = list(words[i + 1:i + 1 + n])
    if kind == 0x05:
        live = True
    if kind == 0x00 and not live and body and body[0] >> 24 == 0xa0:
        # Colours are 0xBBGGRR: grey for every CLUT line, then the floor (240),
        # wall (241) and crate (242) lines.
        body = fill(0x808080, 0, 240, 256, 16) + fill(0x8898a0, 0, 240, 256, 1) + fill(0x3048b0, 0, 241, 256, 1) + \
            fill(0x2068a0, 0, 242, 256, 1)
        replaced += 1
    out += [(kind << 24) | len(body)] + body
    i += 1 + n
assert replaced == 1, 'expected one VRAM upload before TraceBegin, found %d' % replaced
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tagged.gpd')
open(path, 'wb').write(MAGIC + struct.pack('<%dI' % len(out), *out))
print('wrote %s (%d bytes)' % (path, 16 + 4 * len(out)))
