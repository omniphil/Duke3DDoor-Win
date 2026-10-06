#!/usr/bin/env python3
"""test_windoor.py -- door/test_door.py's TRACE test, run against the Windows door (door/duke3ddoor.exe).

Runs from WSL, which can start Windows programs. tools/winlaunch.exe plays Mystic for Windows: a real loopback TCP
connection, DOOR32.SYS with comm type 2 and the socket handle, telnet commands up front, 0xFF doubled both ways.
Its stdin/stdout are the caller's side, which this relays to test_door.py's fake TERMinator.

    python3 tools/test_windoor.py [--bin]
"""
import os
import shutil
import socket
import subprocess
import sys
import threading

TOOLS = os.path.dirname(os.path.abspath(__file__))
DOOR_DIR = os.path.join(TOOLS, '..', 'door')
sys.path.insert(0, DOOR_DIR)
import test_door  # noqa: E402

LAUNCHER = os.path.join(TOOLS, 'winlaunch.exe')
EXE = os.path.join(DOOR_DIR, 'duke3ddoor.exe')
FOLDER = os.path.join(DOOR_DIR, 'saves', 'player-1')   # handle "player", user number 1 (winlaunch's DOOR32.SYS)


def winpath(p):
    return subprocess.check_output(['wslpath', '-w', os.path.abspath(p)], text=True).strip()


def build_launcher():
    src = os.path.join(TOOLS, 'winlaunch.c')
    if not os.path.exists(LAUNCHER) or os.path.getmtime(LAUNCHER) < os.path.getmtime(src):
        subprocess.check_call(['x86_64-w64-mingw32-gcc', '-O2', '-Wall', src, '-o', LAUNCHER, '-lws2_32', '-static'])


class Launched:
    """What test_door.run() gets back from Popen: the launcher, pumped to and from the fake terminal's fd."""

    def __init__(self, fd):
        self.fd = os.dup(fd)   # run() closes its copy
        self.proc = subprocess.Popen([LAUNCHER, winpath(EXE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     cwd=DOOR_DIR)
        threading.Thread(target=self._up, daemon=True).start()
        threading.Thread(target=self._down, daemon=True).start()

    def _up(self):    # fake terminal -> door
        try:
            while True:
                data = os.read(self.fd, 65536)
                if not data:
                    break
                self.proc.stdin.write(data)
                self.proc.stdin.flush()
        except OSError:
            pass

    def _down(self):  # door -> fake terminal
        while True:
            data = self.proc.stdout.read1(65536)
            if not data:
                break
            os.write(self.fd, data)
        try:
            socket.socket(fileno=os.dup(self.fd)).shutdown(socket.SHUT_WR)
        except OSError:
            pass

    def poll(self):
        return self.proc.poll()

    def wait(self):
        return self.proc.wait()

    def kill(self):
        self.proc.kill()


def main():
    build_launcher()
    # A socket pair stands in for the pty: same fd for reading and writing, as run() expects
    test_door.pty.openpty = lambda: (lambda a, b: (a.detach(), b.detach()))(*socket.socketpair())
    test_door.subprocess = type('subprocess', (), {'Popen': staticmethod(lambda args, stdin=None, **kw: Launched(stdin)),
                                                   'STDOUT': subprocess.STDOUT})

    shutil.rmtree(FOLDER, ignore_errors=True)
    os.makedirs(FOLDER, exist_ok=True)
    with open(os.path.join(FOLDER, 'duke3d.cfg'), 'wb') as f:
        f.write(test_door.EXISTING_CFG)

    code, terminal = test_door.run()
    wasm = test_door.sha256_file(os.path.join(DOOR_DIR, 'duke3d.wasm'))
    pak = test_door.sha256_file(os.path.join(DOOR_DIR, 'duke3d.pak'))
    assert terminal.stored.get(wasm) == open(os.path.join(DOOR_DIR, 'duke3d.wasm'), 'rb').read(), 'game damaged'
    assert terminal.stored.get(pak) == open(os.path.join(DOOR_DIR, 'duke3d.pak'), 'rb').read(), 'data damaged'
    assert terminal.started == pak, 'the module was not told which data to use'
    assert code == 0, f'door exit code {code}'
    print(f'PASS (Windows): game and data ({len(terminal.stored[pak]):,} bytes) arrived intact over the socket')
    assert bytes(terminal.received.get('duke3d.cfg', b'')) == test_door.EXISTING_CFG, 'settings damaged'
    print('PASS (Windows): the player\'s settings (every byte value, incl. 0xFF) arrived intact')
    assert open(os.path.join(FOLDER, 'game0.sav'), 'rb').read() == terminal.saved, 'save on disk differs'
    assert not os.path.exists(os.path.join(FOLDER, 'game0.sav.new')), 'half-written file left'
    print('PASS (Windows): saved game kept for player-1; telnet commands were swallowed')
    shutil.rmtree(FOLDER, ignore_errors=True)


if __name__ == '__main__':
    main()
