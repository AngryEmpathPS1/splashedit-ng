# Movies on a disc, read back the way the drive sees them: psxavenc's three STR
# layouts and a .sbs go in with --movie or through disc.json, and each comes out
# under MOVIES/ with every sector's subheader as it was, Form 2 sectors kept
# Form 2 with their EDC, and the directory giving 2048 bytes a sector. What is
# not a movie, and a name the disc cannot hold, are errors and write no image.
# usage: python3 discmoviecheck.py <splashpack-cli>
import json, os, shutil, struct, subprocess, sys, tempfile

cli = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
scene = os.path.join(here, '..', 'examples', 'courtyard', 'courtyard.scene')
tmp = tempfile.mkdtemp()
fails = checks = 0


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))


def edc(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ (0xD8018001 if c & 1 else 0)
    return c


engine = os.path.join(tmp, 'cd.ps-exe')
open(engine, 'wb').write(b'PS-X EXE' + bytes(2040) + b'code' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0psxsplash disc read speed 2\0' + bytes(100))

SECTORS = 16
VIDEO_SUB = bytes([1, 1, 0x48, 0]) * 2
AUDIO_SUB = bytes([1, 1, 0x64, 0x04]) * 2


def video_payload(i):
    head = struct.pack('<HHHHIIHHHHHH', 0x0160, 0x8001, i, SECTORS, 1, 2016, 320, 240, 0x40, 0x3800, 2, 2)
    return head + bytes(32 - len(head)) + bytes([i & 0xff]) * 2016


def sector_2336(i):
    if i % 8 == 7:
        return AUDIO_SUB + bytes([0x80 + i]) * 2324 + bytes(4)
    return VIDEO_SUB + video_payload(i) + bytes(280)


str_2336 = b''.join(sector_2336(i) for i in range(SECTORS))
sync = bytes([0]) + bytes([0xff]) * 10 + bytes([0])
str_2352 = b''.join(sync + bytes(4) + sector_2336(i) for i in range(SECTORS))
str_2048 = b''.join(video_payload(i) for i in range(SECTORS))
sbs = b''.join(struct.pack('<HHHH', 0x20, 0x3800, 2, 2) + bytes([f]) * 1016 for f in range(4))

for name, data in (('intro.str', str_2336), ('raw.str', str_2352), ('quiet.str', str_2048), ('tv.sbs', sbs)):
    open(os.path.join(tmp, name), 'wb').write(data)

binpath = os.path.join(tmp, 'game.bin')
args = [cli, 'disc', scene, '--engine', engine, '-o', binpath]
for name in ('intro.str', 'raw.str', 'quiet.str', 'tv.sbs'):
    args += ['--movie', os.path.join(tmp, name)]
r = subprocess.run(args, capture_output=True, text=True)
check('disc with movies exits 0', r.returncode == 0, r.stderr)
if r.returncode:
    sys.exit(1)
raw = open(binpath, 'rb').read()


def sector(i):
    return raw[i * 2352:(i + 1) * 2352]


def records(lba, size):
    out, data = {}, b''.join(sector(lba + i)[24:2072] for i in range((size + 2047) // 2048))
    at = 0
    while at < len(data):
        n = data[at]
        if n == 0:
            at = (at // 2048 + 1) * 2048
            continue
        name = data[at + 33:at + 33 + data[at + 32]].decode('latin1')
        out[name] = (struct.unpack_from('<I', data, at + 2)[0], struct.unpack_from('<I', data, at + 10)[0], data[at + 25])
        at += n
    return out


pvd = sector(16)[24:]
root = records(struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0])
check('MOVIES folder on the disc', 'MOVIES' in root and root['MOVIES'][2] & 2, sorted(root))
movies = records(root['MOVIES'][0], root['MOVIES'][1])
check('every movie under its upper-case name', all(n in movies for n in ('INTRO.STR;1', 'RAW.STR;1', 'QUIET.STR;1', 'TV.SBS;1')),
      sorted(movies))

lba, size, _ = movies['INTRO.STR;1']
check('a movie counts 2048 bytes a sector', size == SECTORS * 2048, size)
ok_sub = all(sector(lba + i)[16:24] == (AUDIO_SUB if i % 8 == 7 else VIDEO_SUB) for i in range(SECTORS))
check('every sector keeps its subheader', ok_sub)
check('video sectors hold their chunks', all(sector(lba + i)[24:26] == b'\x60\x01' for i in range(SECTORS) if i % 8 != 7))
form2 = sector(lba + 7)
check('audio stays Form 2, all 2324 bytes', form2[24:24 + 2324] == bytes([0x87]) * 2324)
check('Form 2 carries its EDC', struct.unpack_from('<I', form2, 24 + 2324)[0] == edc(form2[16:24 + 2324]))
form1 = sector(lba)
check('Form 1 carries its EDC', struct.unpack_from('<I', form1, 24 + 2048)[0] == edc(form1[16:24 + 2048]))
address = [form1[12], form1[13], form1[14]]
t = lba + 150
bcd = lambda v: ((v // 10) << 4) | (v % 10)
check('sectors carry their own address', address == [bcd(t // 4500), bcd(t // 75 % 60), bcd(t % 75)], address)

lba2, size2, _ = movies['RAW.STR;1']
same = all(sector(lba + i)[16:2072] == sector(lba2 + i)[16:2072] for i in range(SECTORS))
check('a 2352-byte movie lands as the 2336-byte one', size2 == size and same)
lba3, size3, _ = movies['QUIET.STR;1']
check('a video-only movie gets Form 1 subheaders', size3 == size and sector(lba3)[18] == 0x48 and sector(lba3)[24:26] == b'\x60\x01')
lba4, size4, _ = movies['TV.SBS;1']
check('a .sbs is plain data at its own size', size4 == len(sbs) and sector(lba4)[24:24 + 1024] == sbs[:1024], size4)

# Subtitles: an .srt in the list goes as plain data, MOVIES/<NAME>.SRT, read the way the player reads it. What it
# leaves out, what runs off the screen, and subtitles with no movie of their name are warnings, not errors.
srt = ('1\r\n00:00:00,500 --> 00:00:02,000\r\nA line\r\n\r\n'
       '2\r\n00:00:02,500 -> 00:00:03,000\r\nNo arrow\r\n\r\n'
       '3\r\n00:00:03,500 --> 00:00:05,000\r\nOne\r\nTwo\r\nThree rows\r\n\r\n'
       'a stray line at the end\r\nand its text\r\n').encode()   # block 4: the player skips it, as here
open(os.path.join(tmp, 'intro.srt'), 'wb').write(srt)
open(os.path.join(tmp, 'lone.srt'), 'wb').write(srt[:44])
subbin = os.path.join(tmp, 'sub.bin')
r = subprocess.run([cli, 'disc', scene, '--engine', engine, '-o', subbin, '--movie', os.path.join(tmp, 'intro.str'),
                    '--movie', os.path.join(tmp, 'intro.srt'), '--movie', os.path.join(tmp, 'lone.srt')], capture_output=True, text=True)
check('a disc with subtitles exits 0', r.returncode == 0, r.stderr)
if r.returncode == 0:
    raw = open(subbin, 'rb').read()
    root = records(struct.unpack_from('<I', sector(16)[24:], 158)[0], struct.unpack_from('<I', sector(16)[24:], 166)[0])
    movies = records(root['MOVIES'][0], root['MOVIES'][1])
    lba5, size5, _ = movies.get('INTRO.SRT;1', (0, 0, 0))
    check('an .srt is plain data at its own size, beside its movie', size5 == len(srt) and sector(lba5)[24:24 + len(srt)] == srt,
          sorted(movies))
    check('blocks the player leaves out are named, a trailing one too', 'intro.srt: subtitles 2, 4 have no time line' in r.stderr, r.stderr)
    check('more than two rows is named', 'intro.srt: subtitle 3 has more than two rows' in r.stderr, r.stderr)
    check('subtitles with no movie of their name are named', 'lone.srt: no movie of its name' in r.stderr, r.stderr)
    check('subtitles over their movie are not', 'intro.srt: no movie' not in r.stderr, r.stderr)
    raw = open(binpath, 'rb').read()

project = os.path.join(tmp, 'proj')
shutil.copytree(os.path.dirname(scene), project)
open(os.path.join(project, 'intro.str'), 'wb').write(str_2336)
json.dump({'volume': 'MOVIES', 'licence': 'none', 'movies': ['intro.str']}, open(os.path.join(project, 'disc.json'), 'w'))
fromjson = os.path.join(tmp, 'json.bin')
r = subprocess.run([cli, 'disc', os.path.join(project, 'courtyard.scene'), '--engine', engine, '--project', project, '-o', fromjson],
                   capture_output=True, text=True)
check('disc.json movies go on the disc', r.returncode == 0 and b'INTRO.STR' in open(fromjson, 'rb').read(), r.stderr)

bad = os.path.join(tmp, 'bad.str')
open(bad, 'wb').write(b'hello' * 1000)
out = os.path.join(tmp, 'bad.bin')
r = subprocess.run([cli, 'disc', scene, '--engine', engine, '--movie', bad, '-o', out], capture_output=True, text=True)
check('what is no movie is an error', r.returncode != 0 and 'movie bad.str' in r.stderr and not os.path.exists(out), r.stderr)
long_name = os.path.join(tmp, 'awfullylongname.str')
open(long_name, 'wb').write(str_2336)
r = subprocess.run([cli, 'disc', scene, '--engine', engine, '--movie', long_name, '-o', out], capture_output=True, text=True)
check('a name the disc cannot hold is an error', r.returncode != 0 and 'disc file name' in r.stderr and not os.path.exists(out), r.stderr)
r = subprocess.run([cli, 'disc', scene, '--engine', engine, '--movie', os.path.join(tmp, 'nothere.str'), '-o', out],
                   capture_output=True, text=True)
check('a missing movie is an error', r.returncode != 0 and 'no such file' in r.stderr, r.stderr)

check('a disc reads at double speed unless told', b'psxsplash disc read speed 2' in raw and b'psxsplash disc read speed 1' not in raw)
slow = os.path.join(tmp, 'slow.bin')
r = subprocess.run([cli, 'disc', scene, '--engine', engine, '--single-speed', '-o', slow], capture_output=True, text=True)
data = open(slow, 'rb').read() if r.returncode == 0 else b''
check('--single-speed sets the engine on the disc', b'psxsplash disc read speed 1' in data and b'psxsplash disc read speed 2' not in data, r.stderr)
json.dump({'volume': 'MOVIES', 'licence': 'none', 'singleSpeed': True}, open(os.path.join(project, 'disc.json'), 'w'))
r = subprocess.run([cli, 'disc', os.path.join(project, 'courtyard.scene'), '--engine', engine, '--project', project, '-o', slow],
                   capture_output=True, text=True)
check('disc.json singleSpeed does the same', r.returncode == 0 and b'psxsplash disc read speed 1' in open(slow, 'rb').read(), r.stderr)
old = os.path.join(tmp, 'old.ps-exe')
open(old, 'wb').write(b'PS-X EXE' + bytes(2040) + b'code' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0' + bytes(100))
r = subprocess.run([cli, 'disc', scene, '--engine', old, '--single-speed', '-o', out], capture_output=True, text=True)
check('an engine with no read speed is an error', r.returncode != 0 and 'no read speed' in r.stderr and not os.path.exists(out), r.stderr)

print(f'{checks - fails} of {checks} checks passed')
sys.exit(1 if fails else 0)
