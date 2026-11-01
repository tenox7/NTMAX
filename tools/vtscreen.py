#!/usr/bin/env python3
#
#  vtscreen.py -- show the console output of the firmware (a drive.py log)
#  the way a 24x80 VT100 terminal shows it.
#
#    python3 tools/vtscreen.py [-a] [LOG]
#
#  Prints the screen at the end of LOG (default obj/drive.log); -a prints
#  the screen as it was before each clear of the screen as well.  The
#  firmware writes the IBM PC box characters, shown here as + - |.

import re, sys

ROWS, COLS = 24, 80
BOX = {0xc9: '+', 0xbb: '+', 0xc8: '+', 0xbc: '+', 0xcd: '-', 0xba: '|',
       0xda: '+', 0xbf: '+', 0xc0: '+', 0xd9: '+', 0xc4: '-', 0xb3: '|'}

args = sys.argv[1:]
every = '-a' in args
args = [a for a in args if a != '-a']
data = open(args[0] if args else 'obj/drive.log', 'rb').read()

screen = [[' '] * COLS for _ in range(ROWS)]
row = col = 0

def show():
    lines = [''.join(r).rstrip() for r in screen]
    while lines and not lines[-1]:
        lines.pop()
    print('+' + '-' * COLS + '+')
    for l in lines:
        print('|' + l.ljust(COLS) + '|')
    print('+' + '-' * COLS + '+')

def clear(r0, r1):
    for r in range(r0, r1):
        screen[r] = [' '] * COLS

def scroll():
    del screen[0]
    screen.append([' '] * COLS)

i = 0
while i < len(data):
    c = data[i]
    if c == 0x1b and i + 1 < len(data) and data[i + 1] == ord('['):
        i += 2
        csi = True
    elif c == 0x9b:
        i += 1
        csi = True
    else:
        csi = False
    if csi:
        m = re.match(rb'([0-9;]*)([A-Za-z])', data[i:])
        if not m:
            continue
        i += m.end()
        params = [int(p) if p else 0 for p in m.group(1).split(b';')] if m.group(1) else []
        p0 = params[0] if params else 0
        f = m.group(2)
        if f == b'H' or f == b'f':
            row = max(1, p0 or 1) - 1
            col = max(1, (params[1] if len(params) > 1 else 1) or 1) - 1
        elif f == b'J':
            if p0 == 2:
                if every:
                    show()
                clear(0, ROWS)
            elif p0 == 0:
                screen[row][col:] = [' '] * (COLS - col)
                clear(row + 1, ROWS)
        elif f == b'K':
            screen[row][col:] = [' '] * (COLS - col)
        elif f == b'A':
            row = max(0, row - (p0 or 1))
        elif f == b'B':
            row = min(ROWS - 1, row + (p0 or 1))
        elif f == b'C':
            col = min(COLS - 1, col + (p0 or 1))
        elif f == b'D':
            col = max(0, col - (p0 or 1))
        row = min(row, ROWS - 1)
        col = min(col, COLS - 1)
        continue
    i += 1
    if c == 0x0d:
        col = 0
    elif c == 0x0a:
        if row == ROWS - 1:
            scroll()
        else:
            row += 1
    elif c == 0x08:
        col = max(0, col - 1)
    elif c == 0x09:
        col = min(COLS - 1, (col // 8 + 1) * 8)
    elif c >= 0x20 and c != 0x7f:
        ch = BOX.get(c, chr(c) if c < 0x7f else '?')
        screen[row][col] = ch
        if col < COLS - 1:
            col += 1
show()
