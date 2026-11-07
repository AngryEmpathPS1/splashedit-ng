# The file fields' right-click menu (#323): Browse..., Show in Project, Show
# in file explorer and Clear. Clear sets the field back to None as one undo
# step; Show in Project selects the file in the Project panel. Drives the
# editor headless on copies of examples/courtyard with Crate selected (a Mesh
# with a model and a texture) and reads the saved scene and what the fields
# report (assetfield: <name> shows <file>).
# usage: python3 filepickercheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = checks = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = ['--wait', '3']
HEAD = ['--select', 'Crate', '--wait', '10']
ITEMS = ['Browse...', 'Show in Project', 'Show in file explorer', 'Clear']


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(args):
    """Runs the editor on a fresh courtyard copy with Crate selected, then
    `args`; returns the saved scene, the output (without its controls) and the
    named controls of the last frame."""
    d = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, d)
    scene = os.path.join(d, 'courtyard.scene')
    cmd = [editor, '--size', '1600x960', '--project', d, '--scene', scene] + HEAD + args + [
        '--wait', '5', '--screenshot', os.path.join(d, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=150)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    rects = {}
    for l in lines:
        m = re.match(r'control (.+?) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    out = '\n'.join(l for l in lines if not l.startswith('control '))
    return json.load(open(scene, encoding='utf-8')), out, rects


def crate_mesh(s):
    def walk(objs):
        for o in objs:
            if o['name'] == 'Crate':
                return next(c for c in o['components'] if c['type'] == 'mesh')
            m = walk(o.get('children', []))
            if m: return m
    return walk(s['objects'])


def shows(out, name):
    """What the field `name` last said it shows."""
    said = re.findall(r'^assetfield: ' + re.escape(name) + r' shows (.*)$', out, re.M)
    return said[-1] if said else None


def last_line(out, prefix):
    said = [l for l in out.splitlines() if l.startswith(prefix)]
    return said[-1][len(prefix):] if said else None


def count(s):
    def walk(objs):
        return sum(1 + walk(o.get('children', [])) for o in objs)
    return walk(s['objects'])


ORIG = json.load(open(os.path.join(example, 'courtyard.scene'), encoding='utf-8'))
orig = crate_mesh(ORIG)
TEX, MODEL = orig['materials'][0]['texture'], orig['mesh']
assert TEX and MODEL and len(orig['materials']) == 1

_, out, rects = run([])
check("Crate's Texture field shows its image", shows(out, 'obj.texture') == os.path.basename(TEX), out[-400:])
check("Crate's Model field shows its model", shows(out, 'obj.model') == os.path.basename(MODEL), out[-400:])
if 'obj.texture' not in rects or 'obj.model' not in rects:
    check('the fields are named obj.texture and obj.model', False, sorted(rects)[:40])
    print(f'{fails} failures')
    sys.exit(1)

# A right click opens the menu with its four rows.
RT = ['--click', '@obj.texture,0,0,1'] + W
_, out, rects = run(RT)
check('a right click on the Texture field opens its menu', 'popups open: yes' in out, out[-200:])
check('... with Browse..., Show in Project, Show in file explorer and Clear',
      all('obj.texture.menu/' + i in rects for i in ITEMS), sorted(k for k in rects if '.menu/' in k))

# Clear: the field reads None, the saved scene has no texture on the material.
CLEAR = RT + ['--click', '@obj.texture.menu/Clear'] + W
s, out, _ = run(CLEAR + ['--key', 'ctrl+s'] + W)
m = crate_mesh(s)
check('Clear sets the Texture field to None', shows(out, 'obj.texture') == 'None', out[-400:])
check('... reports what it cleared', 'assetfield: obj.texture clear ' + TEX in out, out[-400:])
check('... and closes the menu', 'popups open: no' in out, out[-200:])
check('the saved Crate has no texture', not m['materials'][0].get('texture'), m['materials'])
check('... and keeps its material and model', len(m['materials']) == 1 and m['mesh'] == MODEL, m)

# One Ctrl+Z brings it back (then a Duplicate, so the save is certain to write).
s, out, _ = run(CLEAR + ['--key', 'ctrl+z'] + W + ['--key', 'ctrl+d'] + W + ['--key', 'ctrl+s'] + W)
check('one Ctrl+Z after Clear shows the texture again', shows(out, 'obj.texture') == os.path.basename(TEX), out[-400:])
check('... and the saved Crate has it', crate_mesh(s)['materials'][0].get('texture') == TEX, crate_mesh(s)['materials'])
check('... the Duplicate was saved (the save wrote)', count(s) == count(ORIG) + 1, (count(s), out[-200:]))
# Ctrl+Y clears it again.
s, out, _ = run(CLEAR + ['--key', 'ctrl+z'] + W + ['--key', 'ctrl+y'] + W + ['--key', 'ctrl+s'] + W)
check('Ctrl+Y after that clears it again', shows(out, 'obj.texture') == 'None' and not crate_mesh(s)['materials'][0].get('texture'),
      (crate_mesh(s)['materials'], out[-300:]))

# With the field None, Clear is disabled: a click on it leaves the menu open.
_, out, rects = run(CLEAR + RT + ['--click', '@obj.texture.menu/Clear'] + W)
check('on an empty field Clear is disabled (the click leaves the menu open)',
      'popups open: yes' in out and 'obj.texture.menu/Clear' in rects, out[-200:])

# The Model field has the menu, but no Clear: a mesh with no model stops the export.
RM = ['--click', '@obj.model,0,0,1'] + W
s, out, rects = run(RM + ['--click', '@obj.model.menu/Clear'] + W + ['--key', 'escape'] + W + ['--key', 'ctrl+s'] + W)
check("the Model field's Clear is disabled: the model stays", shows(out, 'obj.model') == os.path.basename(MODEL) and
      crate_mesh(s)['mesh'] == MODEL and 'assetfield: obj.model clear' not in out, (crate_mesh(s), out[-300:]))
_, out, rects = run(RM + ['--click', '@obj.model.menu/Clear'] + W)
check('the Model field has the menu', all('obj.model.menu/' + i in rects for i in ITEMS), sorted(k for k in rects if '.menu/' in k))
check("... and a click on its Clear leaves it open", 'popups open: yes' in out, out[-200:])

# Show in Project: the Project panel opens on the file, selected.
_, out, _ = run(RT + ['--click', '@obj.texture.menu/Show in Project'] + W)
check('Show in Project reports the file', 'assetfield: obj.texture project ' + TEX in out, out[-400:])
check('... the Project panel is open', last_line(out, 'project panel open: ') == 'yes', out[-300:])
check('... with the file selected', last_line(out, 'project panel selected: ') == TEX, out[-300:])
check('... in its folder', os.path.basename(TEX) in (last_line(out, 'project panel: ') or '').split('|'), out[-300:])
check('... and changes nothing', 'unsaved changes: no' in out, out[-200:])

print(f'{checks - fails}/{checks} checks passed')
print(f'{fails} failures')
sys.exit(1 if fails else 0)
