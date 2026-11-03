# Build > Export Standalone Player... in the editor: off with no pcsx-redux
# (its row's click does nothing), and with a stand-in pcsx-redux it writes
# the player where --export-player-to says, the status-bar notice's Show in
# folder link revealing the launcher. The folder's contents are
# playercheck.py's business; here only that the editor writes one.
# usage: python3 playerguicheck.py <splashedit>   (needs a display: xvfb-run -a)
import os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
tmp = tempfile.mkdtemp()
fails = 0
windows, mac = sys.platform == 'win32', sys.platform == 'darwin'


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


engine = os.path.join(tmp, 'cd.ps-exe')
open(engine, 'wb').write(b'PS-X EXE' + bytes(2040) + b'code' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0' + bytes(100))
fake = os.path.join(tmp, 'fake', 'pcsx-redux.exe' if windows else 'pcsx-redux')
os.makedirs(os.path.dirname(fake))
open(fake, 'wb').write(b'stand-in')
ext = '.bat' if windows else '.sh'  # a lone file: the launcher of the system this runs on
if mac:
    ext = '.command'

project = os.path.join(tmp, 'courtyard')
shutil.copytree(example, project)
item = '@menu.build/Export Standalone Player...'


def run(redux, out):
    env = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe',
               SPLASHEDIT_DISC_ENGINE=engine)
    cmd = [editor, '--size', '1600x960', '--project', project, '--export-player-to', out, '--wait', '30',
           '--click', '@menu.build', '--wait', '3', '--click', item, '--wait-play', '--wait', '5',
           '--click', '@notice.reveal', '--wait', '3', '--screenshot', os.path.join(tmp, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=120)
    return r, (r.stdout + r.stderr).strip()


off = os.path.join(tmp, 'off')
r, out = run(os.path.join(tmp, 'nothing', 'pcsx-redux'), off)
check('no pcsx-redux: the item is off, nothing built or written', r.returncode == 0 and not os.path.exists(off) and
      'player: ' not in r.stdout and 'build report: Export Standalone Player' not in r.stdout, out[-300:])

player = os.path.join(tmp, 'player')
r, out = run(fake, player)
launcher = os.path.join(player, 'courtyard' + ext)
check('with pcsx-redux it writes the player, named after the project', r.returncode == 0 and os.path.isfile(launcher) and
      os.path.isfile(os.path.join(player, 'game', 'courtyard.bin')) and
      os.path.isfile(os.path.join(player, 'redux', os.path.basename(fake))), out[-300:])
check('it says where', 'player: ' + launcher in r.stdout.splitlines(), out[-300:])
check('Show in folder reveals the launcher', 'reveal: ' + launcher in r.stdout.splitlines(),
      [l for l in r.stdout.splitlines() if l.startswith('reveal')])
lines = [l for l in r.stdout.splitlines() if l.startswith('build report: Export Standalone Player')]
check('the build report has the lone-copy warning', len(lines) == 1 and lines[0].endswith('errors 0 warnings 1'), lines)

print(f'{fails} failures')
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
