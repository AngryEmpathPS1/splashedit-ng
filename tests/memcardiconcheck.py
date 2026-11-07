# The scene inspector's Memory card > Icon takes an image dragged from the
# Project panel, as a Texture field does (#160, #182): dropped on Add icon it
# becomes a new frame, dropped on a frame it replaces that frame, and a file
# that is no image is refused there and changes nothing. Drives the editor
# headless on copies of the courtyard example with two 16x16 PNGs in
# textures/; --scroll-to brings the Icon row into the inspector, a first run
# reads where it is drawn and the next drags the Project panel's first file
# onto it, then Ctrl+S, and reads settings.memCard.icons from the saved scene.
# usage: python3 memcardiconcheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = checks = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
H = 960
SEARCH = '880,731'  # the Project panel's search field at 1600x960 (as assetbrowsercheck.py)
TILE = '530,812'    # the first file the search shows


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png(path, rgb):
    """A 16x16 opaque PNG of one colour."""
    raw = b''.join(b'\0' + bytes(rgb + (255,)) * 16 for _ in range(16))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def project(icons):
    """A courtyard copy with textures/mcred.png and textures/mcblue.png, its
    memory card icon frames set to `icons`."""
    d = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, d)
    png(os.path.join(d, 'textures', 'mcred.png'), (200, 30, 30))
    png(os.path.join(d, 'textures', 'mcblue.png'), (30, 30, 200))
    scene = os.path.join(d, 'courtyard.scene')
    if icons:
        j = json.load(open(scene, encoding='utf-8'))
        j.setdefault('settings', {})['memCard'] = {'enabled': True, 'icons': icons}
        json.dump(j, open(scene, 'w', encoding='utf-8'), indent=2)
    return d, scene


def run(icons, target, search=None, drop=None):
    """Scrolls the inspector to `target` (a named control); with `search` and
    `drop` (x, y), searches the Project panel and drags its first file there,
    then saves. Returns the saved memCard icons, the output and the rects."""
    d, scene = project(icons)
    cmd = [editor, '--size', '1600x%d' % H, '--project', d, '--scene', scene, '--wait', '20', '--wait-export',
           '--scroll-to', '@' + target, '--wait', '3']
    if search:
        cmd += ['--click', SEARCH + ',0', '--text', search, '--wait', '5', '--drag', '%s,%d,%d,0' % (TILE, drop[0], drop[1]),
                '--wait', '10']
    cmd += ['--key', 'ctrl+s', '--wait', '10', '--screenshot', os.path.join(d, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=240)
    lines = (r.stdout + r.stderr).strip().splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    out = '\n'.join(l for l in lines if not l.startswith('control '))
    j = json.load(open(scene, encoding='utf-8'))
    return j.get('settings', {}).get('memCard', {}).get('icons', []), out, rects


def centre(r):
    return ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)


def dropOn(icons, target, search, want, name, refused=False):
    """Locates `target`, then drops the file `search` finds on it."""
    _, out, rects = run(icons, target)
    at = rects.get(target)
    if not at or at[1] < 80 or at[3] > H - 40:
        check(name + ': ' + target + ' is drawn inside the inspector', False, (at, out[-300:]))
        return
    got, out, rects2 = run(icons, target, search, centre(at))
    # The same scroll puts the row in the same place: a new frame takes Add's
    # place (Add moves one tile on), else the frame stays where it was.
    after = 'scene.mcicon.%d' % (len(got) - 1) if target.endswith('.add') and len(got) > len(icons) else target
    check(name + ': ' + after + ' is drawn where ' + target + ' was', rects2.get(after) == at, (rects2.get(after), at))
    check(name, got == want, (got, out[-300:]))
    if refused:
        check(name + ': the status bar says so', 'That field does not take' in out, out[-300:])


dropOn([], 'scene.mcicon.add', 'mcred', ['textures/mcred.png'], 'an image dropped on Add icon becomes the first frame')
dropOn(['textures/mcred.png'], 'scene.mcicon.add', 'mcblue', ['textures/mcred.png', 'textures/mcblue.png'],
       'an image dropped on Add frame becomes frame 2')
dropOn(['textures/mcred.png', 'textures/mcblue.png'], 'scene.mcicon.0', 'mcblue', ['textures/mcblue.png', 'textures/mcblue.png'],
       'an image dropped on frame 1 replaces it')
dropOn(['textures/mcred.png'], 'scene.mcicon.0', 'courtyard.scene', ['textures/mcred.png'],
       'a scene dropped on frame 1 is refused and leaves it', refused=True)

print(f'{checks - fails}/{checks} checks passed')
print(f'{fails} failures')
sys.exit(1 if fails else 0)
