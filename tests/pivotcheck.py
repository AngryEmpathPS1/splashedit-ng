# Import settings' Pivot row: drops a glTF cube whose node is moved, turned and
# scaled on the viewport of a copy of the courtyard example (it comes in drawing
# the node's own mesh), checks the row is on, picks Bottom centre and checks the
# mesh file now sits on its origin (lowest y 0, x and z centred) with the pivot
# kept in the sidecar. A second run picks Custom and keeps the screenshot of the
# row with its x/y/z fields open.
# usage: python3 pivotcheck.py <splashedit> [screenshot.png]   (needs a display: xvfb-run -a)
import json, os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
keep = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


root = tempfile.mkdtemp()
src = os.path.join(root, 'pivotcube.gltf')
# The triangle (0,0,0) (1,0,0) (0,1,0) on a node moved to (5,1,0), turned 90
# degrees about Y and scaled 2 along X.
open(src, 'w').write('''{"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"Cube","mesh":0,"translation":[5,1,0],"rotation":[0,0.70710678,0,0.70710678],"scale":[2,1,1]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
"bufferViews":[{"buffer":0,"byteLength":36}],
"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}]}''')
W = lambda n: ['--wait', str(n)]
MESH = 'models/pivotcube/Cube.mesh'


def run(name, args):
    """The drop on a fresh copy of the courtyard, then `args`: the editor's
    output, the node mesh's positions and the sidecar."""
    project = os.path.join(root, name)
    shutil.copytree(example, project)
    shot = os.path.join(root, name + '.png')
    r = subprocess.run([editor, '--size', '1600x960', '--project', project] + W(5) + ['--drop-now', src + '@800,400'] + W(10) +
                       ['--wait-import'] + W(10) + ['--scroll-to', '@ipivot'] + W(10) + args + ['--screenshot', shot],
                       capture_output=True, text=True, env=ENV, timeout=300)
    if keep and name == 'custom' and os.path.exists(shot): shutil.copyfile(shot, keep)
    out = (r.stdout + r.stderr).strip()
    for l in out.splitlines():
        if l.startswith('@'): print('editor: ' + l)  # a scripted control it could not find
    mesh = os.path.join(project, MESH)
    pos = None
    if os.path.exists(mesh):
        p = json.load(open(mesh))['positions']
        pos = list(zip(p[0::3], p[1::3], p[2::3]))
    side = os.path.join(project, 'models', 'pivotcube.gltf.import.json')
    return out, pos, json.load(open(side)) if os.path.exists(side) else {}


out, before, side = run('ready', [])
check('the dropped cube draws its node mesh', before is not None and len(before) == 3, out[-600:])
check('the Pivot row is on', 'import pivot: ready' in out, out[-600:])
check('no pivot in the sidecar yet', 'pivots' not in side, side)

out, after, side = run('bottom', ['--click', '@ipivot'] + W(10) + ['--click', '@ipivot/Bottom centre'] + W(20) + ['--wait-import'] + W(20))
check('Bottom centre sets the pivot', 'pivot: set ' + MESH in out, out[-600:])
if after:
    lo = [min(v[i] for v in after) for i in range(3)]
    hi = [max(v[i] for v in after) for i in range(3)]
    check('the mesh sits on its origin', abs(lo[1]) < 1e-4 and abs(lo[0] + hi[0]) < 1e-4 and abs(lo[2] + hi[2]) < 1e-4, (lo, hi))
else:
    check('the mesh is still there', False, out[-600:])
check('the sidecar keeps the pivot', MESH in side.get('pivots', {}), side)

out, _, side = run('custom', ['--click', '@ipivot'] + W(10) + ['--click', '@ipivot/Custom'] + W(20))
check('Custom opens the x/y/z row', 'import pivot: ready custom' in out, out[-600:])
check('Custom alone changes nothing', 'pivots' not in side, side)

print(f'{fails} failures')
sys.exit(1 if fails else 0)
