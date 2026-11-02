# The UI tab's Scene fonts section: the canvas inspector lists the scene's
# fonts with what each builds to (cells, sheet, VRAM) and a sample line, and
# edits them: size, rename (the text elements that use the font follow; a
# name another font has is refused), add (at most three), delete and undo.
# A font's Source menu stays on screen and scrolls when the project has more
# files than fit (#285); Source takes a .ttf or a bitmap .png dragged from the
# Project panel and refuses other files. A bitmap font's Monospace and Gap
# (#310) are saved and change the sample line's spacing.
# Opens a copy of tests/ui/ui.scene with --ui and reads the editor's scripted
# lines ("uicanvas: fonts: ...") and the saved scene. Needs a display (xvfb-run -a).
# usage: python3 uifontscheck.py <splashedit>
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project(many=False):
    """tests/ui/ui.scene as a project of its own, its fonts and texture copied
    in. With `many`: 30 more .ttf and 20 more .png, more than a Source menu
    shows at 960 px, and a file of each kind to drag (dragme.ttf,
    dragsheet.png, dragsound.wav, dragphoto.jpg)."""
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
    if many:
        ttf, png = os.path.join(d, s['fonts'][0]['source']), os.path.join(d, 'textures', 'crate.png')
        for k in range(30): shutil.copy(ttf, os.path.join(d, 'fonts', 'extra_%02d.ttf' % k))
        for k in range(20): shutil.copy(png, os.path.join(d, 'textures', 'sheet_%02d.png' % k))
        os.makedirs(os.path.join(d, 'audio'))
        shutil.copy(ttf, os.path.join(d, 'fonts', 'dragme.ttf'))
        sheetpng(os.path.join(d, 'textures', 'dragsheet.png'))
        shutil.copy(png, os.path.join(d, 'textures', 'dragphoto.jpg'))
        shutil.copy(os.path.join(here, 'audio', 'short11.wav'), os.path.join(d, 'audio', 'dragsound.wav'))
    json.dump(s, open(os.path.join(d, 'ui.scene'), 'w'), indent=1)
    return d


def sheetpng(path):
    """A 256 x 48 bitmap font of 8 x 16 cells (96 glyphs): every glyph a bar
    3 columns wide, so its measured advance is 3 + gap."""
    raw = b''
    for y in range(48):
        ink = 2 <= y % 16 <= 13
        raw += b'\0' + b''.join(b'\xff\xff\xff\xff' if ink and x % 8 < 3 else b'\0\0\0\0' for x in range(256))
    chunk = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 48, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def inkSpan(shot, r):
    """Columns from the first to the last white pixel inside rect r, in sheet
    pixels: a sample that fits is drawn at twice the size (sheetpng's ink is
    12 rows tall)."""
    w, h, bpp, rows = readpng(shot)
    xs = [x for y in range(max(0, r[1]), min(h, r[3])) for x in range(max(0, r[0]), min(w, r[2]))
          if rows[y][x * bpp] > 240 and rows[y][x * bpp + 1] > 240 and rows[y][x * bpp + 2] > 240]
    k = 2 if inkRows(shot, r) > 12 * 1.1 else 1
    return (max(xs) - min(xs) + 1) / k if xs else 0


def run(args, shot=None, many=False):
    """Runs the editor on a fresh project with the UI tab open (the canvas
    inspector, as nothing is selected), then Ctrl+S. Returns the saved scene,
    the scripted font lines in order, the named controls and the screenshot."""
    d = project(many)
    shot = shot or os.path.join(d, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', d, '--ui', '--wait', '5'] + args + [
        '--key', 'ctrl+s', '--wait', '5', '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=120)
    out = (r.stdout + r.stderr).strip()
    run.out = out
    scene = json.load(open(os.path.join(d, 'ui.scene'), encoding='utf-8'))
    lines = [m.group(1) for m in re.finditer(r'^uicanvas: (fonts: .*)$', out, re.M)]
    controls = {m.group(1): tuple(map(int, m.groups()[1:])) for m in re.finditer(r'^control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', out, re.M)}
    return scene, lines, controls, shot


def font_of(scene, element):
    for e in scene['canvases'][0]['elements']:
        if e['name'] == element:
            return e.get('font') or ''  # the system font is saved as null
    return None


def readpng(path):
    """8-bit RGB or RGBA PNG: width, height, bytes per pixel, rows."""
    d = open(path, 'rb').read()
    p, idat = 8, b''
    while p < len(d):
        n, = struct.unpack_from('>I', d, p)
        t = d[p + 4:p + 8]
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', d, p + 8)
            assert depth == 8 and ctype in (2, 6), (depth, ctype)
            bpp = 3 if ctype == 2 else 4
        if t == b'IDAT':
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride): line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride): line[i] = (line[i] + ((line[i - bpp] if i >= bpp else 0) + prev[i]) // 2) & 255
        elif f == 4:
            for i in range(stride):
                a, b, c = line[i - bpp] if i >= bpp else 0, prev[i], prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def white(shot, r):
    """White pixels (the sample's ink) inside rect r of the screenshot."""
    w, h, bpp, rows = readpng(shot)
    n = 0
    for y in range(max(0, r[1]), min(h, r[3])):
        row = rows[y]
        for x in range(max(0, r[0]), min(w, r[2])):
            if row[x * bpp] > 240 and row[x * bpp + 1] > 240 and row[x * bpp + 2] > 240: n += 1
    return n


def inkRows(shot, r):
    """Rows of rect r holding any white pixel: the sample's drawn height."""
    w, h, bpp, rows = readpng(shot)
    return sum(1 for y in range(max(0, r[1]), min(h, r[3]))
               if any(rows[y][x * bpp] > 240 and rows[y][x * bpp + 1] > 240 and rows[y][x * bpp + 2] > 240
                      for x in range(max(0, r[0]), min(w, r[2]))))


LINE = re.compile(r'fonts: (\d+) (.+?) (ttf|bitmap) (\S+) (\d+px|\d+x\d+) vram (\d+)$')


def parsed(lines, i):
    """The last line reported for font i, parsed: name, kind, file, size, vram."""
    for l in reversed(lines):
        m = LINE.match(l)
        if m and int(m.group(1)) == i:
            return m.group(2), m.group(3), m.group(4), m.group(5), int(m.group(6))
    return None


# What the section shows at first: both fonts, their VRAM, a sample of each.
scene0, lines, ctl, shot = run([])
ctl0 = ctl
t, b = parsed(lines, 0), parsed(lines, 1)
check('font 0 reported with its sheet', t is not None and t[:4] == ('title', 'ttf', 'fonts/Inter-SemiBold.ttf', '16px') and t[4] > 0, lines)
check('font 1 reported with its sheet', b is not None and b[:4] == ('body', 'ttf', 'fonts/Inter-Regular.ttf', '12px') and b[4] > 0, lines)
check('two fonts, Add font enabled', 'fonts: count 2 add enabled' in lines, lines)
check('no error lines for buildable fonts', not any(' error ' in l for l in lines), lines)
need = ['cvfont.%d.%s' % (i, k) for i in (0, 1) for k in ('name', 'source', 'size', 'info', 'sample', 'delete')] + ['cvfonts.add']
check('every font control is drawn', all(n in ctl for n in need), [n for n in need if n not in ctl])
check('a TTF font shows no glyph cell fields', 'cvfont.0.gw' not in ctl and 'cvfont.0.size' in ctl, sorted(ctl)[:20])
if 'cvfont.0.sample' in ctl and 'cvfont.1.sample' in ctl:
    s0, s1 = white(shot, ctl['cvfont.0.sample']), white(shot, ctl['cvfont.1.sample'])
    check('each sample line draws the font', s0 > 30 and s1 > 30, (s0, s1))
    # A sample that fits is drawn at twice the size (four times the pixels), and
    # nearest sampling keeps every upscaled texel white: compare per texel.
    k0 = 4 if inkRows(shot, ctl['cvfont.0.sample']) > 16 * 1.1 else 1
    k1 = 4 if inkRows(shot, ctl['cvfont.1.sample']) > 12 * 1.1 else 1
    check('the bigger font draws more ink', s0 / k0 > s1 / k1, (s0, k0, s1, k1))
check('the scene keeps its fonts', [f['name'] for f in scene0['fonts']] == ['title', 'body'], scene0['fonts'])

# Size: font 1 from 12 to 14 px; its line and its VRAM follow.
scene, lines, ctl, _ = run(['--dblclick', '@cvfont.1.size', '--wait', '3', '--text', '14', '--key', 'enter', '--wait', '5'])
b2 = parsed(lines, 1)
check('a size edit changes the font line', b2 is not None and b2[3] == '14px' and b2[4] > b[4], (b, b2))
check('the size is saved', scene['fonts'][1]['size'] == 14 and scene['fonts'][0]['size'] == 16, scene['fonts'])

# Rename: body -> main; the text element 'body' now uses 'main'. Then a name
# another font has is refused and changes nothing.
scene, lines, ctl, _ = run(['--click', '@cvfont.1.name', '--wait', '3', '--text', 'main', '--key', 'enter', '--wait', '5',
                            '--click', '@cvfont.1.name', '--wait', '3', '--text', 'title', '--key', 'enter', '--wait', '5'])
m2 = parsed(lines, 1)
check('a rename changes the font line', m2 is not None and m2[0] == 'main', lines)
check('the renamed font is saved', [f['name'] for f in scene['fonts']] == ['title', 'main'], scene['fonts'])
check('the text element that used it follows', font_of(scene, 'body') == 'main', font_of(scene, 'body'))
check('the other text element keeps its font', font_of(scene, 'title') == 'title', font_of(scene, 'title'))
check('a duplicate name is refused', any(l.startswith('fonts: rename refused: ') and "'title'" in l for l in lines), lines)
check('the refusal changes no font line', parsed(lines, 1)[0] == 'main' and parsed(lines, 0)[0] == 'title', lines)

# Add: a third font from the project's first TTF/OTF; then Add font is
# disabled (a click on it adds nothing); Delete takes font 0 and its
# element's font; Ctrl+Z brings both back.
INSP = '1420,600'
scene, lines, ctl, _ = run(['--click', '@cvfonts.add', '--wait', '5', '--wheel', INSP + ',-20', '--wait', '5'])
f2 = parsed(lines, 2)
check('Add font adds a third font', f2 is not None and f2[0] == 'font' and f2[1] == 'ttf' and f2[2].startswith('fonts/Inter-') and f2[4] > 0, lines)
check('at three fonts Add font is disabled', 'fonts: count 3 add disabled' in lines, lines)
check('the new font is saved', len(scene['fonts']) == 3 and scene['fonts'][2]['name'] == 'font', scene['fonts'])
add = ctl.get('cvfonts.add')
check('scrolled, Add font is on screen', add is not None and 0 <= add[1] and add[3] <= 960, add)
check('the third font has its controls', 'cvfont.2.size' in ctl and 'cvfont.2.sample' in ctl, sorted(ctl))

scene, lines, ctl, _ = run(['--click', '@cvfonts.add', '--wait', '5', '--wheel', INSP + ',-20', '--wait', '5',
                            '--click', '@cvfonts.add', '--wait', '5'])
check('a click on the disabled Add font adds nothing', parsed(lines, 2) is not None and parsed(lines, 3) is None and
      len(scene['fonts']) == 3 and 'fonts: count 4' not in ' '.join(lines), lines)

scene, lines, ctl, _ = run(['--click', '@cvfont.0.delete', '--wait', '5'])
check('Delete removes the font', [f['name'] for f in scene['fonts']] == ['body'] and 'fonts: count 1 add enabled' in lines, lines)
check('its text element goes back to the system font', font_of(scene, 'title') == '' and font_of(scene, 'body') == 'body',
      (font_of(scene, 'title'), font_of(scene, 'body')))

scene, lines, ctl, _ = run(['--click', '@cvfont.0.delete', '--wait', '5', '--key', 'ctrl+z', '--wait', '5'])
check('the delete reported before the undo', 'fonts: count 1 add enabled' in lines, lines)
after = lines[lines.index('fonts: count 1 add enabled') + 1:] if 'fonts: count 1 add enabled' in lines else []
check('Ctrl+Z brings the font back', [f['name'] for f in scene['fonts']] == ['title', 'body'] and
      'fonts: count 2 add enabled' in after and parsed(after, 0) is not None and parsed(after, 0)[0] == 'title', lines)
check('and its text element', font_of(scene, 'title') == 'title', font_of(scene, 'title'))

# ---- #285: with more files than fit, the Source menu stays on screen and
# scrolls; its last file can be scrolled to and picked.
MENU = re.compile(r'fonts: source menu 0 at (-?\d+),(-?\d+) (-?\d+),(-?\d+) scroll (\d+) of (\d+) files (\d+)$')
LAST = 'cvfont.0.source/textures/sheet_19.png'


def menu(lines):
    """The last report of font 0's open Source menu: (x0, y0, x1, y1, scroll, max, files)."""
    ms = [MENU.match(l) for l in lines]
    ms = [tuple(map(int, m.groups())) for m in ms if m]
    return ms[-1] if ms else None


scene, lines, ctl, _ = run(['--click', '@cvfont.0.source', '--wait', '5'], many=True)
m = menu(lines)
check('the Source menu lists every font file and image', m is not None and m[6] == 55, m)
check('the long Source menu stays inside the display', m is not None and 0 <= m[1] and m[3] <= 960 and m[1] < m[3], m)
check('it scrolls, opened on the current file', m is not None and m[5] > 0 and m[4] > 0 and
      'cvfont.0.source/fonts/Inter-SemiBold.ttf' in ctl and m[1] <= ctl['cvfont.0.source/fonts/Inter-SemiBold.ttf'][1] and
      ctl['cvfont.0.source/fonts/Inter-SemiBold.ttf'][3] <= m[3], (m, ctl.get('cvfont.0.source/fonts/Inter-SemiBold.ttf')))
# The wheel over the menu (where this build has it; else where it opens at 1600x960).
WHEEL = ['--wheel', '%d,%d,-40' % (((m[0] + m[2]) // 2, (m[1] + m[3]) // 2) if m else (1466, 700)), '--wait', '5']
scene, lines, ctl, _ = run(['--click', '@cvfont.0.source', '--wait', '5'] + WHEEL, many=True)
m2, last = menu(lines), ctl.get(LAST)
check('the wheel scrolls the menu to its end, the last file inside it', m2 is not None and m2[5] > 0 and m2[4] == m2[5] and
      last is not None and m2[1] <= last[1] and last[3] <= m2[3], (m2, last))
scene, lines, ctl, _ = run(['--click', '@cvfont.0.source', '--wait', '5'] + WHEEL + ['--click', '@' + LAST, '--wait', '5'], many=True)
check('the last file, scrolled to, can be picked', 'textures/sheet_19.png' in scene['fonts'][0].values() and
      scene['fonts'][0].get('source') != 'fonts/Inter-SemiBold.ttf', scene['fonts'][0])

# ---- #285: Source takes a .ttf or a bitmap font .png dragged from the Project
# panel (as a UI image's Texture takes an image, #160); a .wav or .jpg is refused.
SEARCH = '880,731'  # the Project panel's search field, under the UI tab at 1600x960
TILE = '530,812'    # the first file the search shows
src = ctl.get('cvfont.0.source') or (1356, 388, 1576, 412)  # where it is at 1600x960
AT = '%d,%d' % ((src[0] + src[2]) // 2, (src[1] + src[3]) // 2)


def dragged(name):
    return run(['--click', SEARCH + ',0', '--text', name, '--wait', '5', '--drag', TILE + ',' + AT + ',0', '--wait', '10'], many=True)


scene, lines, ctl, _ = dragged('dragme')
f0 = scene['fonts'][0]
check('a .ttf dragged onto Source becomes the font\'s source', f0.get('source') == 'fonts/dragme.ttf' and not f0.get('bitmap') and
      parsed(lines, 0) is not None and parsed(lines, 0)[2] == 'fonts/dragme.ttf', (f0, lines[-3:], run.out[-300:]))
scene, lines, ctl, _ = dragged('dragsheet')
f0 = scene['fonts'][0]
check('a .png dragged onto Source makes it a bitmap font', 'textures/dragsheet.png' in f0.values() and not f0.get('source'),
      (f0, run.out[-300:]))
check('a bitmap font shows Monospace and Gap', 'cvfont.0.mono' in ctl and 'cvfont.0.gap' in ctl, sorted(ctl)[:30])
check('a TTF font shows neither', 'cvfont.0.mono' not in ctl0 and 'cvfont.0.gap' not in ctl0, sorted(ctl0)[:30])

# #310: Monospace and Gap on a bitmap font, saved to the scene.
DRAG = ['--click', SEARCH + ',0', '--text', 'dragsheet', '--wait', '5', '--drag', TILE + ',' + AT + ',0', '--wait', '10']
scene, lines, ctl, dshot = run(DRAG, many=True)
check('the dragged sheet builds as a bitmap font', not any(' error ' in l for l in lines), lines)
span1 = inkSpan(dshot, ctl['cvfont.0.sample']) if 'cvfont.0.sample' in ctl else 0
scene, lines, ctl, mshot = run(DRAG + ['--click', '@cvfont.0.mono', '--wait', '5'], many=True)
spanm = inkSpan(mshot, ctl['cvfont.0.sample']) if 'cvfont.0.sample' in ctl else 0
f0 = scene['fonts'][0]
check('Monospace is saved, and hides Gap', f0.get('monospace') is True and 'gap' not in f0 and 'cvfont.0.gap' not in ctl,
      (f0, sorted(ctl)[:30], run.out[-300:]))
check('the monospace font still builds', not any(' error ' in l for l in lines), lines)
scene, lines, ctl, gshot = run(DRAG + ['--dblclick', '@cvfont.0.gap', '--wait', '3', '--text', '0', '--key', 'enter', '--wait', '5'],
                               many=True)
f0 = scene['fonts'][0]
check('Gap 0 is saved', f0.get('gap') == 0 and 'monospace' not in f0, (f0, run.out[-300:]))
check('the gap 0 font still builds', not any(' error ' in l for l in lines), lines)
span0 = inkSpan(gshot, ctl['cvfont.0.sample']) if 'cvfont.0.sample' in ctl else 0
check('the sample line narrows with gap 0 and widens with Monospace', 0 < span0 < span1 < spanm, (span0, span1, spanm))
print('screenshots:', dshot, mshot, gshot)

for name, kind in (('dragsound', '.wav'), ('dragphoto', '.jpg')):
    scene, lines, ctl, _ = dragged(name)
    f0 = scene['fonts'][0]
    check('a %s (%s) dropped on Source is refused and leaves it' % (kind, name), f0.get('source') == 'fonts/Inter-SemiBold.ttf' and
          not f0.get('bitmap') and 'That field does not take' in run.out, (f0, run.out[-300:]))

print(f'{fails} failures')
sys.exit(1 if fails else 0)
