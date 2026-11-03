# The scene inspector's Disc section "Movies" sub-list, driven through the GUI
# on copies of the courtyard. Each step adds or removes a movie and checks the
# scripted lines the editor prints ("disc movies: <n> movies", "disc movies:
# <path> <what it is>") and disc.json on disk after the step. A movie that is not
# there, is not a movie, or has a name the disc cannot hold shows that in its row.
# A screenshot of the Disc section is saved (optional 2nd arg sets its path).
# usage: python3 discmovielistcheck.py <splashedit> [shot.png]   (needs a display 1300 tall: xvfb-run -a -s "-screen 0 1600x1300x24")
import json, os, shutil, struct, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
shot_out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash')
FOLDS = ['--click', '@s_sequences', '--wait', '3', '--click', '@s_rendering', '--wait', '3', '--click', '@s_loading', '--wait', '3',
         '--click', '@s_vram', '--wait', '5']
SUB = bytes([1, 1, 0x48, 0]) * 2
AUDIO = bytes([1, 1, 0x64, 0x04]) * 2


def video(i):
    head = struct.pack('<HHHHIIHH', 0x0160, 0x8001, i, 150, 1, 2016, 320, 240)
    return SUB + head + bytes(2328 - len(head))


# Three seconds at double speed, with sound.
MOVIE = b''.join((AUDIO + bytes(2328)) if i % 8 == 7 else video(i) for i in range(450))
SBS = b''.join(struct.pack('<HHHH', 0x20, 0x3800, 2, 2) + bytes(1016) for _ in range(4))


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(args, files=None, keep_shot=False):
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    open(os.path.join(project, 'intro.str'), 'wb').write(MOVIE)
    open(os.path.join(project, 'tv.sbs'), 'wb').write(SBS)
    open(os.path.join(project, 'junk.str'), 'wb').write(b'not a movie' * 400)
    open(os.path.join(project, 'awfullylongname.str'), 'wb').write(MOVIE)
    for name, data in (files or {}).items():
        open(os.path.join(project, name), 'wb').write(data)
    shot = os.path.join(project, 'shot.png')
    cmd = ([editor, '--size', '1600x1300', '--project', project, '--wait', '5'] + FOLDS + args +
           ['--wait', '5', '--screenshot', shot])
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    path = os.path.join(project, 'disc.json')
    data = json.load(open(path)) if os.path.exists(path) else None
    controls = {l[len('control '):].split(' at ')[0] for l in lines if l.startswith('control ') and ' at ' in l}
    movies = [l for l in lines if l.startswith('disc movies:')]
    if keep_shot and shot_out and os.path.exists(shot):
        shutil.copyfile(shot, shot_out)
    return movies, lines, data, controls


def disc_json(movies):
    return {'disc.json': json.dumps({'volume': '', 'licence': '', 'movies': movies}).encode()}


def click(name):
    return ['--click', '@' + name, '--wait', '3']


movies, lines, data, controls = run(click('disc.addmovie') + click('disc.addmovie/intro.str'))
check('the movie controls are drawn', 'disc.addmovie' in controls, sorted(controls))
check('adding a movie writes it to disc.json', (data or {}).get('movies') == ['intro.str'], data)
check('a movie shows its size, length and sound', 'disc movies: 1 movies' in movies and
      'disc movies: intro.str 320x240  0:03  sound' in movies, movies)

movies, lines, data, controls = run(click('disc.addmovie') + click('disc.addmovie/tv.sbs'), disc_json(['intro.str']))
check('adding a second appends it', (data or {}).get('movies') == ['intro.str', 'tv.sbs'], data)
check('a .sbs says it is for a mesh', 'disc movies: tv.sbs for a mesh' in movies, movies)
check('both rows are drawn', 'disc.movie0' in controls and 'disc.movie1' in controls, sorted(controls))

movies, lines, data, controls = run(click('disc.movie0.remove'), disc_json(['intro.str', 'tv.sbs']))
check('the remove button drops the movie', (data or {}).get('movies') == ['tv.sbs'], data)

movies, lines, data, controls = run([], disc_json(['gone.str', 'junk.str', 'awfullylongname.str', 'intro.str', 'tv.sbs']), keep_shot=True)
check('a file that is gone says so', 'disc movies: gone.str missing' in movies, movies)
check('a file that is no movie says so', 'disc movies: junk.str not a movie' in movies, movies)
check('a name the disc cannot hold says so', 'disc movies: awfullylongname.str name too long' in movies, movies)

# Subtitles: an .srt beside its movie says how many it shows and how many the player leaves out; one with no
# movie of its name says so.
SRT = (b'1\r\n00:00:00,500 --> 00:00:02,000\r\nA line\r\n\r\n'
       b'2\r\n00:00:02,500 --> 00:00:03,000\r\nTwo\r\nrows\r\n\r\n'
       b'3\r\n00:00:03,500 -> 00:00:04,000\r\nNo arrow\r\n')
subs = dict(disc_json(['intro.str']), **{'intro.srt': SRT, 'lone.srt': SRT[:44]})
movies, lines, data, controls = run(click('disc.addmovie') + click('disc.addmovie/intro.srt'), subs)
check('adding subtitles writes them to disc.json', (data or {}).get('movies') == ['intro.str', 'intro.srt'], data)
check('subtitles say how many show and how many are left out', 'disc movies: intro.srt 2 subtitles, 1 left out' in movies, movies)
movies, lines, data, controls = run([], dict(disc_json(['intro.str', 'lone.srt']), **{'lone.srt': SRT[:44]}))
check('subtitles with no movie of their name say so', 'disc movies: lone.srt no movie of this name' in movies, movies)
movies, lines, data, controls = run([], dict(disc_json(['intro.str', 'intro.srt']), **{'intro.srt': SRT[:44]}))
check('one good subtitle says so', 'disc movies: intro.srt 1 subtitle' in movies, movies)

movies, lines, data, controls = run(click('disc.singlespeed'), disc_json(['intro.str']))
check('the single speed switch is drawn', 'disc.singlespeed' in controls, sorted(controls))
check('the switch writes it to disc.json', (data or {}).get('singleSpeed') is True, data)

if shot_out:
    check('a screenshot was saved', os.path.exists(shot_out), shot_out)

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
