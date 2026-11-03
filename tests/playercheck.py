# splashpack-cli player on examples/courtyard with a stand-in pcsx-redux laid
# out as this system's bundle has it (Linux: redux/pcsx-redux -> an AppDir's
# AppRun; macOS: a .app; Windows: a folder with pcsx-redux.exe): the disc in
# game/, the install copied into redux/, the seeded config/pcsx.json, the
# marker and the launcher. Off Windows the stand-in is a shell script that
# records its arguments: the folder is moved and its launcher run from
# elsewhere, so it must find its files beside itself. Then a second export
# over it, a folder that is not empty refused, and a missing --redux.
#
# With --boot <pcsx-redux> <psxsplash-cdrom.ps-exe> <out.png> (Linux, needs
# xvfb-run and ImageMagick's import), also a real boot: the player written
# with that pcsx-redux and engine, its launcher run under xvfb-run, a
# screenshot after 25 s, which must not be one colour and must show
# courtyard's sky where pcsx-redux's menus, idle screen or first-run dialog
# would be.
# usage: python3 playercheck.py <splashpack-cli> [--boot <pcsx-redux> <engine> <out.png>]
import json, os, shutil, stat, subprocess, sys, tempfile

cli = os.path.abspath(sys.argv[1])
boot = sys.argv[3:6] if len(sys.argv) >= 6 and sys.argv[2] == '--boot' else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
scene = os.path.join(example, 'courtyard.scene')
tmp = tempfile.mkdtemp()
fails = checks = 0
windows, mac = sys.platform == 'win32', sys.platform == 'darwin'


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))


def write(path, data, mode=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data if isinstance(data, bytes) else data.encode())
    if mode:
        os.chmod(path, mode)


# Stand-in CD-ROM engine, as disccheck.py makes it.
engine = os.path.join(tmp, 'cd.ps-exe')
write(engine, b'PS-X EXE' + bytes(2040) + b'code' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0' + bytes(100))

# The stand-in records its arguments where SPLASHEDIT_FAKE_REDUX_LOG says.
fake_sh = '#!/bin/sh\nfor a in "$@"; do printf "%s\\n" "$a"; done > "$SPLASHEDIT_FAKE_REDUX_LOG"\n'
fake = os.path.join(tmp, 'fake')
if windows:
    redux = os.path.join(fake, 'redux', 'pcsx-redux.exe')
    write(redux, b'MZ stand-in')
    write(os.path.join(fake, 'redux', 'SDL3.dll'), b'dll')
    copied = {'pcsx-redux.exe': b'MZ stand-in', 'SDL3.dll': b'dll'}
    launcher_ext, run_rel = '.bat', None
elif mac:
    app = os.path.join(fake, 'PCSX-Redux.app', 'Contents')
    redux = os.path.join(app, 'MacOS', 'PCSX-Redux')
    write(redux, fake_sh, 0o755)
    write(os.path.join(app, 'Info.plist'), b'plist')
    copied = {'PCSX-Redux.app/Contents/MacOS/PCSX-Redux': fake_sh.encode(), 'PCSX-Redux.app/Contents/Info.plist': b'plist'}
    launcher_ext, run_rel = '.command', 'PCSX-Redux.app/Contents/MacOS/PCSX-Redux'
else:
    appdir = os.path.join(fake, 'redux', 'pcsx-redux.AppDir')
    write(os.path.join(appdir, 'usr', 'bin', 'pcsx-redux'), fake_sh, 0o755)
    write(os.path.join(appdir, 'usr', 'lib', 'libfoo.so'), b'so')
    os.symlink('usr/bin/pcsx-redux', os.path.join(appdir, 'AppRun'))
    redux = os.path.join(fake, 'redux', 'pcsx-redux')
    os.symlink('pcsx-redux.AppDir/AppRun', redux)
    copied = {'pcsx-redux.AppDir/usr/bin/pcsx-redux': fake_sh.encode(), 'pcsx-redux.AppDir/usr/lib/libfoo.so': b'so',
              'pcsx-redux.AppDir/AppRun': fake_sh.encode()}
    launcher_ext, run_rel = '.sh', 'pcsx-redux.AppDir/AppRun'

out = os.path.join(tmp, 'player')
r = subprocess.run([cli, 'player', scene, '--engine', engine, '--redux', redux, '-o', out, '--title', 'Courtyard: Demo',
                    '--project', example], capture_output=True, text=True)
launcher = os.path.join(out, 'Courtyard Demo' + launcher_ext)
check('player exits 0 and prints the launcher', r.returncode == 0 and r.stdout.startswith(launcher + ' sectors='),
      (r.returncode, r.stdout, r.stderr))
if r.returncode:
    sys.exit(1)

bin_path, cue_path = os.path.join(out, 'game', 'Courtyard Demo.bin'), os.path.join(out, 'game', 'Courtyard Demo.cue')
raw = open(bin_path, 'rb').read() if os.path.isfile(bin_path) else b''
check('game/ holds the disc, whole raw sectors with SYSTEM.CNF', len(raw) > 0 and len(raw) % 2352 == 0 and b'SYSTEM.CNF' in raw,
      len(raw))
cue = open(cue_path).read() if os.path.isfile(cue_path) else ''
check('its cue names the bin beside it', 'FILE "Courtyard Demo.bin" BINARY' in cue, cue)
for rel, data in copied.items():
    p = os.path.join(out, 'redux', *rel.split('/'))
    check('redux/' + rel + ' copied', os.path.isfile(p) and open(p, 'rb').read() == data)
if not windows and not mac:
    check('the AppDir is copied, not the bundle symlink', os.path.islink(os.path.join(out, 'redux', 'pcsx-redux.AppDir', 'AppRun'))
          and not os.path.lexists(os.path.join(out, 'redux', 'pcsx-redux')))
try:
    cfg = json.load(open(os.path.join(out, 'config', 'pcsx.json')))
except (OSError, ValueError) as e:
    cfg = str(e)
check('config/pcsx.json is seeded', isinstance(cfg, dict) and cfg.get('emulator') == {'ShownAutoUpdateConfig': True,
                                                                                    'AutoUpdate': False}, cfg)
check('the marker lists what it wrote', open(os.path.join(out, '.splashedit-player')).read().split('\n')[:4] ==
      ['game', 'redux', 'config', 'Courtyard Demo' + launcher_ext])
text = open(launcher, newline='').read() if os.path.isfile(launcher) else ''
if windows:
    check('the .bat starts the copied exe in kiosk mode, paths from its own folder',
          'start "" "%~dp0redux\\pcsx-redux.exe" -kiosk -run -iso "%~dp0game\\Courtyard Demo.cue" -portable "%~dp0config"\r\n'
          in text, text)
else:
    check('the launcher is executable', os.stat(launcher).st_mode & stat.S_IXUSR)
    check('the launcher runs the copied pcsx-redux in kiosk mode, paths from its own folder',
          'exec "$here"/"redux/' + run_rel + '" -kiosk -run -iso "$here"/"game/Courtyard Demo.cue" -portable "$here"/"config"\n'
          in text, text)
    # Unpacked somewhere else and started from a third folder.
    moved = os.path.join(tmp, 'unpacked here', 'Courtyard Demo')
    os.makedirs(os.path.dirname(moved))
    shutil.move(out, moved)
    log = os.path.join(tmp, 'args.txt')
    cwd = os.path.join(tmp, 'elsewhere')
    os.makedirs(cwd)
    rr = subprocess.run([os.path.join(moved, 'Courtyard Demo' + launcher_ext)], cwd=cwd, capture_output=True, text=True,
                        env=dict(os.environ, SPLASHEDIT_FAKE_REDUX_LOG=log), timeout=30)
    got = open(log).read().split('\n')[:-1] if os.path.isfile(log) else None
    want = ['-kiosk', '-run', '-iso', os.path.join(moved, 'game', 'Courtyard Demo.cue'), '-portable',
            os.path.join(moved, 'config')]
    check('the moved launcher runs the copy beside it with the cue and config beside it', rr.returncode == 0 and got == want,
          (rr.returncode, rr.stderr, got))
    out = moved

# Written again over the export: a previous player, so allowed; the old launcher goes.
r = subprocess.run([cli, 'player', scene, '--engine', engine, '--redux', redux, '-o', out, '--title', 'Again'],
                   capture_output=True, text=True)
check('a second export over a player folder', r.returncode == 0 and os.path.isfile(os.path.join(out, 'Again' + launcher_ext)) and
      not os.path.exists(os.path.join(out, 'Courtyard Demo' + launcher_ext)) and
      os.path.isfile(os.path.join(out, 'game', 'Again.bin')), (r.returncode, r.stderr))

# A folder with anything else in it is refused before anything is built.
foreign = os.path.join(tmp, 'foreign')
write(os.path.join(foreign, 'notes.txt'), b'mine')
r = subprocess.run([cli, 'player', scene, '--engine', engine, '--redux', redux, '-o', foreign], capture_output=True, text=True)
check('a folder that is not empty is refused', r.returncode == 1 and 'not empty' in r.stderr and
      os.listdir(foreign) == ['notes.txt'], (r.returncode, r.stderr, os.listdir(foreign)))
r = subprocess.run([cli, 'player', scene, '--engine', engine, '-o', os.path.join(tmp, 'noredux')], capture_output=True, text=True)
check('no --redux is a usage error', r.returncode == 2 and not os.path.exists(os.path.join(tmp, 'noredux')), r.returncode)
r = subprocess.run([cli, 'player', scene, '--engine', engine, '--redux', os.path.join(tmp, 'gone'), '-o',
                    os.path.join(tmp, 'gone-out')], capture_output=True, text=True)
check('a missing pcsx-redux is an error', r.returncode == 1 and 'pcsx-redux not found' in r.stderr, (r.returncode, r.stderr))

if boot:
    real_redux, real_engine, png = (os.path.abspath(a) for a in boot)
    bout = os.path.join(tmp, 'boot')
    r = subprocess.run([cli, 'player', scene, '--engine', real_engine, '--redux', real_redux, '-o', bout, '--title', 'Courtyard',
                        '--project', example], capture_output=True, text=True)
    check('boot: player with the real pcsx-redux', r.returncode == 0, r.stderr)
    shot = os.path.join(tmp, 'boot.png')
    # Started from another folder, as a double click would; the frame grabbed after 25 s.
    script = ('"$1" >"$2" 2>&1 & pid=$!; sleep 25; import -window root "$3"; kill $pid; sleep 2; kill -9 $pid 2>/dev/null; '
              'pkill -9 -f "$4" 2>/dev/null; true')
    rr = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1280x800x24', 'sh', '-c', script, 'boot',
                         os.path.join(bout, 'Courtyard.sh'), os.path.join(tmp, 'boot.log'), shot, bout],
                        cwd=tmp, capture_output=True, text=True, timeout=120)
    ok = os.path.isfile(shot)
    if ok:
        shutil.copy(shot, png)
        # Distinct colours over the whole frame: a game drawn has many, a blank screen one.
        colours = subprocess.run(['identify', '-format', '%k', shot], capture_output=True, text=True).stdout.strip()
        check('boot: the frame is not one colour (%s colours)' % colours, colours.isdigit() and int(colours) > 16, colours)
        # Many colours is also pcsx-redux's own idle screen and menus, or its
        # first-run dialog over the game: courtyard's sky must show where
        # those draw (the menu bar, the dialog, the idle logo and text).
        ppm = subprocess.run(['convert', shot, '-depth', '8', 'ppm:-'], capture_output=True).stdout.split(maxsplit=4)
        w, data = int(ppm[1]), ppm[4]
        probes = [(300, 8), (200, 60), (380, 250), (640, 100), (1000, 150)]
        px = [tuple(data[(y * w + x) * 3:(y * w + x) * 3 + 3]) for x, y in probes]
        sky = [min(p) > 90 and p[2] >= max(p) and max(p) - min(p) < 80 for p in px]
        check('boot: the game fills the window, no pcsx-redux menus or dialog over it', all(sky), list(zip(probes, px)))
    else:
        check('boot: screenshot taken', False, rr.stderr[-500:])
    first_run = os.path.join(bout, 'config', 'pcsx.json')
    try:
        emu = json.load(open(first_run)).get('emulator', {})
    except (OSError, ValueError):
        emu = {}
    # pcsx-redux writes its whole settings file back: more than the two seeded keys.
    check('boot: pcsx-redux kept its settings in the player\'s config/', emu.get('ShownAutoUpdateConfig') is True and
          emu.get('AutoUpdate') is False and len(emu) > 2, sorted(emu)[:5])

print(f'{checks - fails} of {checks} checks passed')
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
