# A colour picker opened from a colour well stays on screen (#324): a well
# near the window's bottom opens its picker above the field instead of under
# it (where it ran off the window, with no way to scroll it back), still lined
# up with the field's right edge; mid-window it still opens under the field.
# Drives the editor headless at a short window size on copies of
# tests/ui/ui.scene (a UI element's Colour) and tests/light/light.scene (a
# light's Colour and the scene's Background) and reads the named rects of the
# well (ui.col, obj.lcol, scene.fogcol) and of its picker window (...pick).
# usage: python3 colourpopcheck.py <splashedit> [shot.png]   (needs a display: xvfb-run -a)
# With shot.png it keeps the screenshot of the UI element's picker flipped above its well.
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
shot_to = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
fails = checks = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = ['--wait', '3']


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def ui_project():
    """tests/ui/ui.scene as a project of its own, fonts and textures copied in."""
    d = os.path.join(tempfile.mkdtemp(), 'ui')
    os.makedirs(os.path.join(d, 'fonts'))
    os.makedirs(os.path.join(d, 'textures'))
    s = json.load(open(os.path.join(here, 'ui', 'ui.scene'), encoding='utf-8'))
    for f in s['fonts']:
        shutil.copy(os.path.join(here, 'ui', f['source']), os.path.join(d, 'fonts'))
        f['source'] = 'fonts/' + os.path.basename(f['source'])
    for e in s['canvases'][0]['elements']:
        if 'texture' in e:
            shutil.copy(os.path.join(here, 'ui', e['texture']), os.path.join(d, 'textures'))
            e['texture'] = 'textures/' + os.path.basename(e['texture'])
    json.dump(s, open(os.path.join(d, 'ui.scene'), 'w'), indent=1)
    return d, os.path.join(d, 'ui.scene')


def light_project():
    d = os.path.join(tempfile.mkdtemp(), 'light')
    shutil.copytree(os.path.join(here, 'light'), d)
    return d, os.path.join(d, 'light.scene')


def run(make, size, args, shot=None):
    """Runs the editor at `size` (w, h) on a fresh project from `make` with
    `args`; returns its output (without its controls) and the named controls
    of the last frame."""
    d, scene = make()
    shot_path = os.path.join(d, 'shot.png')
    cmd = [editor, '--size', '%dx%d' % size, '--project', d, '--scene', scene] + args + ['--wait', '5', '--screenshot', shot_path]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=150)
    lines = (r.stdout + r.stderr).strip().splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    if shot and os.path.exists(shot_path): shutil.copy(shot_path, shot)
    return '\n'.join(l for l in lines if not l.startswith('control ')), rects


def inside(r, size):
    return r[0] >= 0 and r[1] >= 0 and r[2] <= size[0] and r[3] <= size[1]


def picker(what, make, size, head, well, pick, where, shot=None):
    """Opens the picker of the well named `well` and checks its window `pick`
    is wholly inside the window, right-aligned with the well, and `where`
    ('above' or 'below') the well."""
    out, rects = run(make, size, head + ['--click', '@' + well] + W, shot)
    if well not in rects or pick not in rects:
        check('%s: the well %s and its picker %s are drawn' % (what, well, pick), False, (sorted(rects)[:30], out[-300:]))
        return
    f, p = rects[well], rects[pick]
    print('     %s at %dx%d: well %s, picker %s' % (what, size[0], size[1], f, p))
    check('%s: the picker is wholly inside the %dx%d window' % (what, size[0], size[1]), inside(p, size), (p, size))
    if where == 'above':
        check('%s: ... above its well (no room under it)' % what, p[3] <= f[1], (p, f))
    else:
        check('%s: ... under its well (room enough)' % what, p[1] >= f[3], (p, f))
    check('%s: ... its right edge on the well\'s' % what, abs(p[2] - f[2]) <= 1, (p, f))


# ---------------- The UI canvas: an element's Colour ----------------
TALL, SHORT = (1600, 960), (1600, 480)
UI = ['--ui', '--wait', '5']
out, _ = run(ui_project, TALL, UI)
m = re.search(r'uicanvas: screen [\d.]+,[\d.]+ scale [\d.]+ list ([\d.]+),([\d.]+) row ([\d.]+)', out)
check('--ui reports where the element list is', m is not None, out[-300:])
if not m:
    print(f'{fails} failures')
    sys.exit(1)
LX, LY, LH = map(float, m.groups())
CORNER = '%d,%d' % (round(LX + 40), round(LY + 1.5 * LH))  # the list's second row: 'corner'
HEAD = UI + ['--click', CORNER] + W
picker('UI element Colour', ui_project, TALL, HEAD, 'ui.col', 'ui.colpick', 'below')
picker('UI element Colour', ui_project, SHORT, HEAD, 'ui.col', 'ui.colpick', 'above', shot_to)

# ---------------- A light's Colour and the scene's Background ----------------
picker("a light's Colour", light_project, TALL, ['--select', 'Red', '--wait', '20'], 'obj.lcol', 'obj.lcolpick', 'below')
picker("a light's Colour", light_project, (1600, 640), ['--select', 'Red', '--wait', '20'], 'obj.lcol', 'obj.lcolpick', 'above')
picker('the scene Background', light_project, TALL, ['--wait', '20'], 'scene.fogcol', 'scene.fogcolpick', 'below')
picker('the scene Background', light_project, (1600, 640), ['--wait', '20'], 'scene.fogcol', 'scene.fogcolpick', 'above')

print(f'{checks - fails}/{checks} checks passed')
print(f'{fails} failures')
sys.exit(1 if fails else 0)
