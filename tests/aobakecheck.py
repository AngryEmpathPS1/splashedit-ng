# Ambient occlusion baked into vertex colours (#305), driven through the GUI
# on a copy of the courtyard. With the Floor selected, the Paint tool's bar
# (tool.paint, then paint.aodist, paint.aostrength, paint.aobake) bakes
# occlusion from every mesh in the scene into the Floor's painted layer: the
# saved Floor is Painted with one colour per vertex, its vertices at the foot
# of the Back Wall (z 6) are darker on average than those in the open middle,
# and, against the colours it showed before (Lighting > Painted in the
# inspector), the bake darkened the wall's foot and left the middle. One
# Ctrl+Z before saving puts the Floor back as it was, with no layer.
# usage: python3 aobakecheck.py <splashedit> [keep-dir]   (needs a display: xvfb-run -a)
# keep-dir, when given, receives baked.png (the Scene view after the bake).
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
keep = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


W = lambda n: ['--wait', str(n)]


def find(objs, name):
    for o in objs:
        if o['name'] == name: return o
        c = find(o.get('children', []), name)
        if c: return c
    return None


def mesh_of(scene, name):
    o = find(scene['objects'], name) if scene else None
    return next((c for c in o.get('components', []) if c.get('type') == 'mesh'), None) if o else None


def layer(scene, name):
    """The object's painted layer as a list of (r, g, b), or None without one."""
    m = mesh_of(scene, name)
    s = m.get('paintColors') if m else None
    return [tuple(int(s[i + k:i + k + 2], 16) for k in (0, 2, 4)) for i in range(0, len(s), 6)] if s is not None else None


def run(name, args, select='Floor'):
    """A fresh copy with `select` selected, then `args` and Ctrl+S; returns
    the saved scene, the output lines and the screenshot."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    scene = os.path.join(project, 'courtyard.scene')
    shot = os.path.join(project, name + '.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--select', select] + W(20) + args + \
        ['--key', 'ctrl+s'] + W(10) + ['--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=240)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l or 'no object named' in l]
    check(name + ': every control the script names was drawn', not missing, missing)
    return json.load(open(scene, encoding='utf-8')), lines, shot


def typed(name, text):
    return ['--dblclick', '@' + name] + W(3) + ['--key', 'ctrl+a', '--text', text, '--key', 'enter'] + W(3)


def last(lines, prefix):
    v = None
    for l in lines:
        if l.startswith(prefix): v = l
    return v


ORIG = json.load(open(os.path.join(example, 'courtyard.scene'), encoding='utf-8'))
ORIG_FLOOR = mesh_of(ORIG, 'Floor')

# The Floor's vertices in world space: its mesh plus its position (no rotation, scale 1, parent at the origin).
fm = json.load(open(os.path.join(example, 'meshes', 'floor.mesh'), encoding='utf-8'))
fo = find(ORIG['objects'], 'Floor')
pos = fo['transform']['position']
check('the Floor sits unrotated and unscaled (the world positions below rely on it)',
      fo['transform']['rotation'] == [0, 0, 0, 1] and fo['transform']['scale'] == [1, 1, 1], fo['transform'])
P = fm['positions']
FLOOR = [(P[i] + pos[0], P[i + 1] + pos[1], P[i + 2] + pos[2]) for i in range(0, len(P), 3)]
TOP = max(p[1] for p in FLOOR)
# On the top face: at the foot of the Back Wall (its face at z 6, x -7..7), and in the open
# middle (around x 0, z -1.5 to -4, more than 2 m from every wall, pillar and crate).
NEAR = [i for i, p in enumerate(FLOOR) if abs(p[1] - TOP) < 0.01 and abs(p[2] - 6.0) < 0.5 and abs(p[0]) < 7]
MIDDLE = [i for i, p in enumerate(FLOOR) if abs(p[1] - TOP) < 0.01 and abs(p[0]) < 1.5 and -4.5 < p[2] < -1]
check('the courtyard has Floor vertices at the wall and in the middle', len(NEAR) >= 3 and len(MIDDLE) >= 3, (len(NEAR), len(MIDDLE)))


def bright(lay, idx):
    return sum(sum(lay[i]) for i in idx) / (3.0 * len(idx)) if lay and idx else None


# 0. The colours the Floor shows before any bake: Lighting > Painted in the inspector starts the
# layer from them (not through the bake), the base the bake multiplies into.
s, lines, shot = run('base', ['--click', '@obj.lighting'] + W(4) + ['--click', '@lighting/Painted'] + W(6))
BASE = layer(s, 'Floor')
check('the base layer has one colour per Floor vertex', BASE is not None and len(BASE) == len(FLOOR), BASE and len(BASE))

# 1. Bake occlusion with distance 2 m and strength 0.6 (typed, as the defaults are), then save.
BAKE = ['--click', '@tool.paint'] + W(4) + typed('paint.aodist', '2') + typed('paint.aostrength', '0.6') + \
    ['--click', '@paint.aobake'] + W(6)
s, lines, shot = run('bake', BAKE)
print('bake:', last(lines, 'vpaint: aobake'))
m = re.match(r'vpaint: aobake (\d+) distance (\S+) strength (\S+) rays (\d+) ms (\S+)$', last(lines, 'vpaint: aobake') or '')
check('Bake occlusion ran once, on the Floor, with the typed settings',
      m is not None and m.group(1) == '1' and m.group(2) == '2.000' and m.group(3) == '0.600', last(lines, 'vpaint: aobake'))
L = layer(s, 'Floor')
check('the bake made the Floor Painted', (mesh_of(s, 'Floor') or {}).get('vertexColors') == 'painted', mesh_of(s, 'Floor') and
      mesh_of(s, 'Floor').get('vertexColors'))
check('with one colour per Floor vertex', L is not None and len(L) == len(FLOOR), L and len(L))
near, middle = bright(L, NEAR), bright(L, MIDDLE)
print('mean byte at the Back Wall foot %s, in the middle %s' % (near, middle))
check('the vertices at the foot of the Back Wall are darker on average than the middle',
      near is not None and middle is not None and near < middle, (near, middle))
# The baked lighting is already darker at the wall, so against the base: the bake itself darkens there.
if BASE and L and len(BASE) == len(L) == len(FLOOR):
    ratio = lambda idx: sum(sum(L[i]) for i in idx) / max(1, sum(sum(BASE[i]) for i in idx))
    rn, rm = ratio(NEAR), ratio(MIDDLE)
    print('baked / base at the Back Wall foot %.3f, in the middle %.3f' % (rn, rm))
    check('no vertex got brighter than its base', all(L[i][k] <= BASE[i][k] for i in range(len(L)) for k in range(3)),
          [(i, BASE[i], L[i]) for i in range(len(L)) if any(L[i][k] > BASE[i][k] for k in range(3))][:5])
    check('the bake darkened the foot of the Back Wall (below 0.9 of its base)', rn < 0.9, rn)
    check('and left the open middle about as it was (above 0.97)', rm > 0.97, rm)
else:
    check('baked layer comparable with the base', False, (BASE and len(BASE), L and len(L)))
check('only the Floor was baked', all(layer(s, n) is None for n in ('Back Wall', 'Side Wall', 'Platform', 'Crate')))
if keep and os.path.exists(shot): shutil.copy(shot, os.path.join(keep, 'baked.png'))

# 2. The same bake, then one Ctrl+Z before saving: the Floor is as it was.
s, lines, shot = run('undo', BAKE + ['--key', 'ctrl+z'] + W(4))
check('the bake ran', (last(lines, 'vpaint: aobake') or '').startswith('vpaint: aobake 1 '), last(lines, 'vpaint: aobake'))
check('one Ctrl+Z puts the Floor back to its mode with no layer',
      mesh_of(s, 'Floor') is not None and layer(s, 'Floor') is None and
      mesh_of(s, 'Floor').get('vertexColors') == ORIG_FLOOR['vertexColors'], mesh_of(s, 'Floor') and
      {k: v for k, v in mesh_of(s, 'Floor').items() if k != 'paintColors'})
check('and nothing in the scene keeps a painted layer', 'paintColors' not in json.dumps(s))

print('FAILS=%d' % fails)
sys.exit(1 if fails else 0)
