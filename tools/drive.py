#!/usr/bin/env python3
#
#  drive.py -- run gxemul as a DECstation 5000/200 on a pseudo terminal and
#  type at its serial console from a script, for tests.
#
#    python3 tools/drive.py [-t SECONDS] [-l LOG] DISK.IMG|MACHINE.json STEP...
#
#  DISK.IMG boots with gxemul's PROM emulation, a MACHINE.json (gxemul's
#  machine file, e.g. obj/prom/prom.json of run.sh prom) as it says.
#  Each STEP is "PATTERN=>KEYS": wait until the console shows PATTERN (a
#  regular expression, matched on the output after the last match with the
#  terminal escape sequences taken out), then type KEYS.  KEYS are typed as
#  they are (a backslash is a backslash: \os\nt), except the words {up}
#  {down} {right} {left} (the arrow keys), {enter}, {esc}, {bs}
#  (backspace) and {ctrlc} (gxemul's debugger takes it).  A PATTERN of
#  "@SECONDS" waits until that many seconds after the start instead.  A
#  STEP without "=>" only waits.  After the last step the
#  emulator runs on for -w seconds (default 2) and is stopped; -t is the
#  limit for the whole run (default 120).  The raw console output goes to
#  LOG (default obj/drive.log) and the text without escapes to stdout.
#  GXEMUL_OPTS adds gxemul options.

import os, pty, re, select, shlex, signal, sys, time

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
gxemul = os.environ.get('GXEMUL', os.path.join(here, '..', 'gxemul', 'gxemul'))

args = sys.argv[1:]
limit, after, log = 120.0, 2.0, os.path.join(here, 'obj', 'drive.log')
while args and args[0].startswith('-'):
    opt = args.pop(0)
    if opt == '-t': limit = float(args.pop(0))
    elif opt == '-w': after = float(args.pop(0))
    elif opt == '-l': log = args.pop(0)
    else: sys.exit('drive.py: unknown option ' + opt)
if not args:
    sys.exit(__doc__ or 'usage: drive.py [-t SECONDS] [-w SECONDS] [-l LOG] DISK.IMG STEP...')
disk, steps = args[0], args[1:]

KEYS = {'{up}': '\x1b[A', '{down}': '\x1b[B', '{right}': '\x1b[C', '{left}': '\x1b[D',
        '{enter}': '\r', '{esc}': '\x1b', '{bs}': '\x08', '{ctrlc}': '\x03'}

def keys(s):
    for k, v in KEYS.items():
        s = s.replace(k, v)
    return s.encode('latin-1')

ESC = re.compile(rb'(\x1b\[|\x9b)[0-9;]*[A-Za-z]|\x1b.')

def text(raw):
    return ESC.sub(b' ', raw).decode('latin-1')

opts = shlex.split(os.environ.get('GXEMUL_OPTS', ''))
if disk.endswith('.json'):
    cmd = [gxemul] + opts + [disk]
else:
    cmd = [gxemul, '-e', '3max'] + opts + ['-d', disk]
pid, fd = pty.fork()
if pid == 0:
    os.execv(cmd[0], cmd)

raw = b''
seen = 0			# the output before this offset has been matched
start = time.time()
out = open(log, 'wb')

def pump(timeout):
    global raw
    r, _, _ = select.select([fd], [], [], timeout)
    if r:
        try:
            data = os.read(fd, 4096)
        except OSError:
            return False
        if not data:
            return False
        raw += data
        out.write(data)
        out.flush()
    return True

def stop(status):
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    sys.stdout.write(text(raw))
    sys.stdout.write('\n')
    sys.exit(status)

for step in steps:
    pattern, _, send = step.partition('=>')
    if re.fullmatch(r'@[0-9.]+', pattern):
        when = start + float(pattern[1:])
        while time.time() < when:
            if not pump(min(0.2, max(0.0, when - time.time()))):
                sys.stdout.write('drive.py: the emulator ended before %s\n' % pattern)
                stop(1)
        if send:
            os.write(fd, keys(send))
        seen = len(raw)
        continue
    rx = re.compile(pattern)
    while True:
        m = rx.search(text(raw[seen:]))
        if m:
            break
        if time.time() - start > limit or not pump(0.2):
            sys.stdout.write('drive.py: timed out waiting for %r\n' % pattern)
            stop(1)
    # Find the end of the match in the raw output: the shortest prefix of
    # the unmatched output whose text holds the match.
    end = seen
    while not rx.search(text(raw[seen:end])):
        end += 1
    seen = end
    if send:
        time.sleep(0.3)
        os.write(fd, keys(send))

deadline = time.time() + after
while time.time() < deadline and pump(0.2):
    pass
stop(0)
