# Up and Down in a number field being typed into step its value and keep the
# field (#158): before, ImGui's keyboard navigation took them and moved the
# focus to another item. Tab still moves from field to field, and typing into a
# field Tab reached still replaces its value (#158's first fix).
# Opens a copy of tests/ui/ui.scene with --ui (as uiinspectorcheck.py does),
# selects the 'title' text (at 8, 8, 300 wide) by its list row, double-clicks
# its Position X, Tabs on into Position Y, presses Up and Down there, Tabs on
# into Size W and types, then Ctrl+S, and reads the saved element's rect.
# usage: python3 fieldarrowcheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
fails = checks = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project():
    """tests/ui/ui.scene as a project of its own, its fonts and textures copied in."""
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
    return d


def run(args, save=True):
    """Runs the editor on a fresh project with the UI tab open, then (with
    `save`) Ctrl+S; returns the saved canvas and the output."""
    d = project()
    cmd = [editor, '--size', '1600x960', '--project', d, '--ui', '--wait', '5'] + args + \
        (['--key', 'ctrl+s'] if save else []) + ['--wait', '5', '--screenshot', os.path.join(d, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=120)
    out = (r.stdout + r.stderr).strip()
    missing = [l for l in out.splitlines() if 'no such control drawn' in l]
    if missing: check('every control the script names was drawn', False, missing)
    scene = json.load(open(os.path.join(d, 'ui.scene'), encoding='utf-8'))
    return scene['canvases'][0], out


def rect(cv, name='title'):
    return {e['name']: e for e in cv['elements']}[name]['rect']


# Where the element list is: the editor prints it with --ui.
cv0, out = run([], save=False)
m = re.search(r'uicanvas: screen [\d.]+,[\d.]+ scale [\d.]+ list ([\d.]+),([\d.]+) row ([\d.]+)', out)
check('--ui reports the element list', m is not None, out[-300:])
if not m:
    print(f'{fails} failures')
    sys.exit(1)
LX, LY, LH = map(float, m.groups())
ORDER = [e['name'] for e in cv0['elements']]
TITLE = ['--click', '%d,%d' % (round(LX + 40), round(LY + (ORDER.index('title') + 0.5) * LH)), '--wait', '3']
X0, Y0, W0 = rect(cv0)[:3]
check("'title' starts at 8, 8", (X0, Y0) == (8, 8), rect(cv0))
POS_X_THEN_TAB = TITLE + ['--dblclick', '@ui.posa', '--wait', '3', '--key', 'tab', '--wait', '3']
K = lambda k: ['--key', k, '--wait', '2']

# Up in Position Y (reached by Tab) adds one; Tab commits it and moves on to
# Size W, where what is typed replaces the value.
cv, out = run(POS_X_THEN_TAB + K('up') + ['--key', 'tab', '--wait', '3', '--text', '200', '--key', 'enter', '--wait', '3'])
check('Up in a tabbed-into Position Y adds one; Tab keeps it and moves on to Size W, where typing replaces it',
      rect(cv)[:3] == [X0, Y0 + 1, 200], rect(cv))

# Up three times, Down once: two up, the field typed into all along (Enter commits Y itself).
cv, out = run(POS_X_THEN_TAB + K('up') + K('up') + K('up') + K('down') + ['--key', 'enter', '--wait', '3'])
check('Up, Up, Up, Down in Position Y adds two, X and W untouched', rect(cv)[:3] == [X0, Y0 + 2, W0], rect(cv))

# Down in Position X, double-clicked into (not Tab): the same.
cv, out = run(TITLE + ['--dblclick', '@ui.posa', '--wait', '3'] + K('down') + K('down') + ['--key', 'enter', '--wait', '3'])
check('Down, Down in Position X takes off two', rect(cv)[:2] == [X0 - 2, Y0], rect(cv))

# Escape after Up leaves the value as it was.
cv, out = run(POS_X_THEN_TAB + K('up') + ['--key', 'escape', '--wait', '3'])
check('Escape after Up leaves Position Y as it was', rect(cv)[:3] == [X0, Y0, W0], rect(cv))

# #158 as it was: typing after Tab sets the next field, Tab runs on through Size W.
cv, out = run(TITLE + ['--dblclick', '@ui.posa', '--wait', '3', '--text', '12', '--key', 'tab', '--wait', '3', '--text', '7',
                       '--key', 'tab', '--wait', '3', '--text', '150', '--key', 'enter', '--wait', '3'])
check('typing after Tab still sets each field (#158)', rect(cv)[:3] == [12, 7, 150], rect(cv))

print(f'{checks - fails}/{checks} checks passed')
print(f'{fails} failures')
sys.exit(1 if fails else 0)
