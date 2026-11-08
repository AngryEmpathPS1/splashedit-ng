# The tileset picker a tilemap brush's Tile thumbnail opens stays on screen,
# as the colour pickers do (#324): with the Tile row near the window's bottom
# it opens above the row instead of under it (where it ran off the window),
# still lined up with the row's right edge; with room under the row it still
# opens there. Drives the editor headless on copies of the courtyard example
# with tests/tilemap/tiles.png as its one sprite sheet (as tilemapcheck.py
# does): Add tilemap, then a click on the thumbnail (scene.tilebrushcell), and
# reads the named rects of the cell number field (scene.tilebrushcellnum, the
# row's right end) and of the picker window (scene.tilebrushcellpicker).
# usage: python3 tilepopcheck.py <splashedit> [shot.png]   (needs a display: xvfb-run -a)
# With shot.png it keeps the screenshot of the picker flipped above the row.
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
shot_to = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = checks = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project():
    """A courtyard copy whose one sprite sheet is the tileset."""
    d = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, d)
    shutil.copy(os.path.join(here, 'tilemap', 'tiles.png'), os.path.join(d, 'textures', 'tiles.png'))
    scene = os.path.join(d, 'courtyard.scene')
    j = json.load(open(scene, encoding='utf-8'))
    j['spriteSheets'] = [{'name': 'tiles', 'texture': 'textures/tiles.png', 'bitDepth': 4, 'cellWidth': 16, 'cellHeight': 16,
                          'anims': []}]
    json.dump(j, open(scene, 'w', encoding='utf-8'), indent=2)
    return d, scene


def run(size, to_row, shot=None):
    """Runs the editor at `size` (w, h): the inspector scrolled to its bottom
    (the Tilemap section), Add tilemap (which grows the section), then with
    `to_row` the inspector scrolled just far enough to show the Tile row (which
    leaves it at the window's bottom), else back to its end, then a click on the Tile thumbnail. Returns its
    output (without its controls) and the named controls of the last frame."""
    d, scene = project()
    shot_path = os.path.join(d, 'shot.png')
    wheel = lambda n: ['--wheel', '1420,%d,%d' % (size[1] // 2, n), '--wait', '3']
    cmd = [editor, '--size', '%dx%d' % size, '--project', d, '--scene', scene, '--wait', '20', '--wait-export'] + wheel(-200) + \
        ['--click', '@scene.tileadd', '--wait', '3'] + (['--scroll-to', '@scene.tilebrushcell', '--wait', '3'] if to_row else wheel(-200)) + \
        ['--click', '@scene.tilebrushcell', '--wait', '5', '--screenshot', shot_path]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=240)
    lines = (r.stdout + r.stderr).strip().splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    if shot and os.path.exists(shot_path): shutil.copy(shot_path, shot)
    return '\n'.join(l for l in lines if not l.startswith('control ')), rects


def inside(r, size):
    return r[0] >= 0 and r[1] >= 0 and r[2] <= size[0] and r[3] <= size[1]


def picker(size, to_row, where, shot=None):
    """Opens the picker and checks it is wholly inside the window, `where`
    ('above' or 'below') the Tile row and right-aligned with it."""
    out, rects = run(size, to_row, shot)
    thumb, num, pick = 'scene.tilebrushcell', 'scene.tilebrushcellnum', 'scene.tilebrushcellpicker'
    if thumb not in rects or num not in rects or pick not in rects:
        check('%dx%d: the Tile row and its picker are drawn' % size, False, (sorted(rects)[:30], out[-300:]))
        return
    t, n, p = rects[thumb], rects[num], rects[pick]
    row = (t[0], min(t[1], n[1]), n[2], max(t[3], n[3]))
    print('     %dx%d: Tile row %s, picker %s' % (size[0], size[1], row, p))
    check('the picker is wholly inside the %dx%d window' % size, inside(p, size), (p, size))
    check('the Tile row sits inside the window too', inside(row, size), (row, size))
    if where == 'above':
        check('... above the Tile row (no room under it)', p[3] <= row[1], (p, row))
    else:
        check('... under the Tile row (room enough)', p[1] >= row[3], (p, row))
    check('... its right edge on the row\'s', abs(p[2] - row[2]) <= 1, (p, row))


# Scrolled to the bottom, a 960 px window leaves room under the Tile row; in a
# 600 px one Add pushes the row under the window, and scrolling it back into
# view leaves it at the bottom.
picker((1600, 960), False, 'below')
picker((1600, 600), True, 'above', shot_to)

print(f'{checks - fails}/{checks} checks passed')
print(f'{fails} failures')
sys.exit(1 if fails else 0)
