# Picked-primitive names in the Frame tab (splashedit-ng#212 phase 2) driven
# through the GUI on tests/fixtures/frame/tagged.gpd: one courtyard frame
# captured with psxsplash's capture tags on (make_tagfixture.py says how it
# was made), whose mesh triangles carry 2 GP0(00h) words each naming the game
# object, its triangle and the ordering-table slot. The courtyard example is
# exported with splashpack-cli as scene_0.splashpack beside the capture, the
# way Play leaves it, so the names come from what the exporter writes. Clicks
# on the image at 1x pick a back wall, side wall, crate and floor triangle and
# one the engine left untagged (no names: nothing new shown). Without a pack
# beside it the same capture shows the raw object indices.
# usage: python3 primnamecheck.py <splashedit> [splashpack-cli] [keep.png]   (needs a display: xvfb-run -a)
import os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
cli = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
keep = sys.argv[3] if len(sys.argv) > 3 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fixture = os.path.join(here, 'fixtures', 'frame', 'tagged.gpd')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png_rgb(path):
    """(width, height, rows of RGB bytes) of an 8-bit RGBA or RGB PNG."""
    data = open(path, 'rb').read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 3
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            bpp = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[i] for i in range(stride) if i % bpp < 3))
        prev = line
    return w, h, rows


def pick(x, y):
    """--click on VRAM pixel (x, y) of the 320x240 image at 0,256, shown at 1x: offsets from its centre (160,376)."""
    return ['--click', '@frame.image,%d,%d' % (x - 160, y - 376), '--wait', '4']


PICKS = [(107, 392), (21, 385), (82, 445), (97, 487), (160, 376)]


def run(work, with_pack, shot):
    os.makedirs(work)
    gpd = os.path.join(work, 'tagged.gpd')
    shutil.copy(fixture, gpd)
    project = os.path.join(work, 'courtyard')
    shutil.copytree(example, project)
    if with_pack:
        r = subprocess.run([cli, 'export', os.path.join(project, 'courtyard.scene'), '-o', os.path.join(work, 'scene_0.splashpack'),
                            '--project', project], capture_output=True, text=True, timeout=300)
        check('splashpack-cli exports the courtyard beside the capture', r.returncode == 0, (r.stdout + r.stderr)[-400:])
    args = ['--frame', gpd, '--wait', '10', '--click', '@frame.zoom/1x', '--wait', '4']
    for x, y in PICKS: args += pick(x, y)
    args += pick(97, 487)  # the floor again, selected for the screenshot
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene')] + args + [
        '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    rects = {}
    for l in lines:
        mm = re.match(r'control (.+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if mm: rects[mm.group(1)] = tuple(int(v) for v in mm.groups()[1:])
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    return [l for l in lines if l.startswith('frame:')], rects


work = tempfile.mkdtemp()
shot = os.path.join(work, 'names.png')
frame, rects = run(os.path.join(work, 'named'), True, shot)
check('the capture loads: 571 primitives (the tag words are no commands), a 320x240 image at 0,256',
      'frame: loaded tagged.gpd prims 571 commands 598 frames 3 image 320x240 at 0,256' in frame, frame[:2])
check('249 primitives tagged, named from the pack beside the capture',
      'frame: tagged 249 names from scene_0.splashpack' in frame, [l for l in frame if 'tagged' in l])
picks = [l for l in frame if l.startswith('frame: pick')]
want = ['frame: pick 107,392 -> #144 tri-gouraud-tex obj Back Wall tri 181 slot 336',
        'frame: pick 21,385 -> #212 tri-gouraud-tex obj Side Wall tri 30 slot 335',
        'frame: pick 82,445 -> #540 tri-gouraud-tex obj Crate tri 2 slot 116',
        'frame: pick 97,487 -> #560 tri-gouraud-tex obj Floor tri 384 slot 90',
        'frame: pick 160,376 -> #61 tri-gouraud-tex',
        'frame: pick 97,487 -> #560 tri-gouraud-tex obj Floor tri 384 slot 90']
check('each pick names the object, triangle and OT slot that drew it (none for an untagged one)', picks == want, picks)

# The details column shows the Object row (its name in light blue, 7fc8ff) for the selected floor triangle.
if os.path.exists(shot) and 'frame.image' in rects:
    w, h, rows = png_rgb(shot)
    x1, y0, y1 = rects['frame.image'][2], rects['frame.image'][1] - 40, rects['frame.image'][3]

    def blue(p): return p[2] > 200 and 0.65 * p[2] <= p[1] <= 0.9 * p[2] and p[0] < 0.7 * p[2]
    n = sum(1 for yy in range(max(0, y0), min(h, y1)) for xx in range(x1, min(w, x1 + 300)) if blue(rows[yy][xx * 3:xx * 3 + 3]))
    check('the selected primitive\'s details show its object name', n > 20, n)
else:
    check('screenshot written', False, shot)

frame2, _ = run(os.path.join(work, 'bare'), False, os.path.join(work, 'bare.png'))
check('without a pack beside it: indices only, said so',
      'frame: tagged 249 no scene_0.splashpack beside the capture: object indices only' in frame2,
      [l for l in frame2 if 'tagged' in l])
picks2 = [l for l in frame2 if l.startswith('frame: pick')]
check('and a pick shows the raw object index', picks2[:1] == ['frame: pick 107,392 -> #144 tri-gouraud-tex obj #1 tri 181 slot 336'],
      picks2[:1])

if keep and os.path.exists(shot):
    shutil.copy(shot, keep)
shutil.rmtree(work, ignore_errors=True)
print('primnamecheck: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
