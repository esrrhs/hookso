#!/usr/bin/env python3
"""Linux x86-64 owned-process integration tests; all children are reaped.
Usage: python3 test/run_fork_tests.py --hookso /absolute/hookso --repeat 5
Builds fixtures in a private temporary directory; no dependencies are installed.
"""
import argparse
import ctypes
import mmap
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import threading
import time

FIELDS = 'heartbeat forks reaped badchild chld prof traps in_remote_handler remote_active gate release completed simd_errors read_result hold host_sp'.split()
SIZE = 8 * len(FIELDS)

def wait_for(pred, seconds=5, why="deadline exceeded"):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if pred():
            return
        time.sleep(.002)
    raise AssertionError(why)

class Target:
    def __init__(self, binary, directory, mode, hold=False):
        self.path = Path(directory) / 'state'
        self.path.write_bytes(bytes(SIZE))
        self.fd = self.path.open('r+b')
        self.mem = mmap.mmap(self.fd.fileno(), SIZE)
        if hold: self.set('hold', 1)
        # Separate session only for this fixture; cleanup is confined to this
        # exact owned process group, including any outstanding fork children.
        self.p = subprocess.Popen([str(binary), str(self.path), mode], stdin=subprocess.PIPE,
                                  stdout=subprocess.DEVNULL, start_new_session=True)
        self.calls = []
        try:
            wait_for(lambda: self.get('heartbeat') > 0)
        except Exception:
            self.close()
            raise
    def get(self, name):
        return struct.unpack_from('Q', self.mem, FIELDS.index(name)*8)[0]
    def set(self, name, value):
        struct.pack_into('Q', self.mem, FIELDS.index(name)*8, value)
    def call(self, hookso, *args, timeout_ms=3000):
        env = dict(os.environ, HOOKSO_TIMEOUT_MS=str(timeout_ms))
        p = subprocess.Popen([str(hookso), *map(str, args)], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, env=env)
        self.calls.append(p)
        return p
    def result(self, p, ok=True):
        out, err = p.communicate(timeout=8)
        if ok:
            assert p.returncode == 0, (p.returncode, out, err)
        else:
            assert p.returncode != 0, ('unexpected success', out, err)
        return out.strip(), err
    def close(self):
        for p in self.calls:
            if p.poll() is None:
                p.terminate()
                try: p.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    p.kill(); p.communicate(timeout=3)
        if self.p.poll() is None: os.kill(self.p.pid, signal.SIGCONT)
        self.set('hold', 0)
        self.set('release', 1)  # cease new forks before reaping the snapshot
        # Kill only our known child process group. Parent is terminated after
        # children so the SIGCHLD handler has a chance to reap each one.
        try:
            children = Path(f'/proc/{self.p.pid}/task/{self.p.pid}/children').read_text().split()
            for pid in children:
                try: os.kill(int(pid), signal.SIGKILL)
                except ProcessLookupError: pass
            if self.p.poll() is None: os.kill(self.p.pid, signal.SIGCHLD)
            if children:
                wait_for(lambda: not Path(f'/proc/{self.p.pid}/task/{self.p.pid}/children').read_text().split())
        finally:
            try: os.killpg(self.p.pid, signal.SIGKILL)
            except ProcessLookupError: pass
            self.p.wait(timeout=3)
            # This runner is a subreaper: if a failed baseline killed A first,
            # adopt and reap its remaining owned descendants, too.
            end = time.monotonic() + 2
            while True:
                try:
                    child, _ = os.waitpid(-1, os.WNOHANG)
                    if child == 0:
                        if time.monotonic() >= end: raise AssertionError('unreaped descendant')
                        time.sleep(.002)
                except ChildProcessError: break
            self.mem.close(); self.fd.close()
        print(f'CLEANUP target={self.p.pid} calls={len(self.calls)} reaped')

def exercise(hookso, binary, directory, mode, case):
    t = Target(binary, directory, mode, hold=case in ('signals', 'dlopen', 'chld'))
    try:
        pid = t.p.pid
        if case in ('signals', 'dlopen', 'chld'):
            wait_for(lambda: t.get('forks') >= 8)
            before_prof = t.get('prof')
            before_chld = t.get('chld')
            p = (t.call(hookso, 'call', pid, binary, 'remote_probe', 'i=160') if case != 'dlopen'
                 else t.call(hookso, 'dlopen', pid, Path(directory)/'fork_payload.so'))
            wait_for(lambda: t.get('remote_active') == 1)
            assert t.get('forks') > t.get('reaped'), 'no outstanding children'
            t.set('release', 1)  # no later fork can mask a swallowed SIGCHLD
            t.set('hold', 0)  # all held children exit during remote execution
            if case != 'chld': os.kill(pid, signal.SIGPROF)  # exactly one tick
            out, err = t.result(p)
            assert (out == '31337' if case != 'dlopen' else int(out) != 0), (out, err)
            if case != 'chld':
                wait_for(lambda: t.get('prof') == before_prof + 1, seconds=1, why='SIGPROF lost after detach')
            wait_for(lambda: t.get('reaped') == t.get('forks'), seconds=1, why='SIGCHLD lost: unreaped children without later fork')
            wait_for(lambda: t.get('chld') > before_chld)
            assert t.get('in_remote_handler') == 0, 'handler ran on temporary call context'
            assert t.get('simd_errors') == 0, 'XMM15 corrupted'
            t.set('release', 0)
            start = t.get('heartbeat'); forks = t.get('forks')
            wait_for(lambda: t.get('heartbeat') > start + 10 and t.get('forks') > forks + 5)
            assert t.get('badchild') == 0
        elif case == 'arg':
            p = t.call(hookso, 'arg', pid, binary, 'gate_point', '1')
            # Synchronize using hardware breakpoint state in /proc? Instead
            # wait for tracing, then allow setup plenty of time; no parent hit
            # occurs before we set gate (only fork children call gate_point).
            wait_for(lambda: f'TracerPid:\t{p.pid}\n' in Path(f'/proc/{pid}/status').read_text())
            time.sleep(.08)
            prof = t.get('prof'); chld = t.get('chld'); traps = t.get('traps')
            os.kill(pid, signal.SIGPROF)
            time.sleep(.05)
            assert p.poll() is None, 'child hit or unrelated signal completed arg'
            t.set('gate', 1)
            out, err = t.result(p)
            assert out == '4242', (out, err)
            wait_for(lambda: t.get('traps') == traps + 1)
            assert t.get('prof') == prof + 1
            assert t.get('chld') > chld
            assert t.get('badchild') == 0, 'fork child inherited int3/hardware breakpoint'
        elif case == 'storm':
            stop = threading.Event()
            def spam():
                while not stop.wait(.001):
                    os.kill(pid, signal.SIGPROF)
                    os.kill(pid, signal.SIGCHLD)
            sender = threading.Thread(target=spam)
            sender.start()
            try:
                for _ in range(10):
                    p = t.call(hookso, 'call', pid, binary, 'remote_probe', 'i=20')
                    out, err = t.result(p)
                    assert out == '31337', (out, err)
                assert t.get('in_remote_handler') == 0, 'handler executed on temporary registers/stack'
                assert t.get('simd_errors') == 0
                assert t.get('prof') > 0 and t.get('chld') > 0
                assert t.get('completed') == 10 and t.get('badchild') == 0
            finally:
                stop.set(); sender.join(timeout=2)
                assert not sender.is_alive()
        elif case in ('stop', 'remote-stop', 'pre-stopped'):
            def stopped():
                return '\nState:\tT ' in ('\n' + Path(f'/proc/{pid}/status').read_text())
            if case == 'pre-stopped':
                os.kill(pid, signal.SIGSTOP); wait_for(stopped)
                p = t.call(hookso, 'call', pid, binary, 'remote_probe', 'i=20')
            elif case == 'stop':
                p = t.call(hookso, 'arg', pid, binary, 'gate_point', '1')
                wait_for(lambda: f'TracerPid:\t{p.pid}\n' in Path(f'/proc/{pid}/status').read_text())
                time.sleep(.04); os.kill(pid, signal.SIGSTOP)
            else:
                p = t.call(hookso, 'call', pid, binary, 'remote_probe', 'i=300')
                wait_for(lambda: t.get('remote_active') == 1); os.kill(pid, signal.SIGSTOP)
            out, err = t.result(p, ok=False)
            wait_for(stopped, why='business SIGSTOP was lost')
            os.kill(pid, signal.SIGCONT)
            start = t.get('heartbeat'); wait_for(lambda: t.get('heartbeat') > start + 10)
        elif case == 'external-trap':
            p = t.call(hookso, 'call', pid, binary, 'remote_probe', 'i=300')
            wait_for(lambda: t.get('remote_active') == 1)
            traps = t.get('traps'); os.kill(pid, signal.SIGTRAP)
            out, err = t.result(p, ok=False)
            assert 'completion breakpoint' in err, err
            wait_for(lambda: t.get('traps') == traps + 1)
            assert t.get('in_remote_handler') == 0, 'trap handler ran on injection stack'
            assert t.get('completed') == 0
            start = t.get('heartbeat'); wait_for(lambda: t.get('heartbeat') > start + 10)
        elif case == 'fault':
            p = t.call(hookso, 'call', pid, binary, 'illegal_instruction')
            out, err = t.result(p, ok=False)
            assert 'completion breakpoint' in err, err
            traps = t.get('traps'); os.kill(pid, signal.SIGILL)
            wait_for(lambda: t.get('traps') == traps + 1, why='SIGILL disposition was not preserved')
            assert t.get('in_remote_handler') == 0
        elif case == 'trap':
            p = t.call(hookso, 'call', pid, binary, 'unexpected_trap')
            out, err = t.result(p, ok=False)
            assert 'completion breakpoint' in err, err
            assert t.get('completed') == 0
            start = t.get('heartbeat'); wait_for(lambda: t.get('heartbeat') > start + 10)
        elif case == 'remote-timeout':
            p = t.call(hookso, 'call', pid, binary, 'forever', timeout_ms=120)
            out, err = t.result(p, ok=False)
            assert 'timeout' in err and 'mappings retained' in err, err
            prof = t.get('prof'); os.kill(pid, signal.SIGPROF)
            wait_for(lambda: t.get('prof') == prof + 1)
            start = t.get('heartbeat'); wait_for(lambda: t.get('heartbeat') > start + 10)
            assert 'TracerPid:\t0\n' in Path(f'/proc/{pid}/status').read_text()
        elif case in ('timeout', 'cancel'):
            p = t.call(hookso, 'arg', pid, binary, 'gate_point', '1', timeout_ms=120 if case == 'timeout' else 3000)
            if case == 'cancel':
                wait_for(lambda: f'TracerPid:\t{p.pid}\n' in Path(f'/proc/{pid}/status').read_text())
                time.sleep(.04); p.send_signal(signal.SIGINT)
            out, err = t.result(p, ok=False)
            assert ('timeout' if case == 'timeout' else 'cancelled') in err, err
            assert 'TracerPid:\t0\n' in Path(f'/proc/{pid}/status').read_text()
            t.set('gate', 1)
            start = t.get('heartbeat'); wait_for(lambda: t.get('heartbeat') > start + 10)
            assert t.get('badchild') == 0
        elif case == 'threads':
            p = t.call(hookso, 'call', pid, binary, 'remote_probe', 'i=10')
            out, err = t.result(p, ok=False)
            assert 'unsupported multithreaded' in err, err
            assert t.get('completed') == 0
        elif case == 'read':
            p = t.call(hookso, 'syscall', pid, '39')  # getpid while read is blocked
            out, err = t.result(p)
            assert out == str(pid), (out, err)
            t.p.stdin.write(b'Z'); t.p.stdin.flush()
            wait_for(lambda: t.get('read_result') != 0)
            assert t.get('read_result') == ord('Z'), 'interrupted read not restarted correctly'
        assert t.p.poll() is None, 'parent A died'
    finally:
        t.close()

def main():
    assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0  # PR_SET_CHILD_SUBREAPER
    parser = argparse.ArgumentParser()
    parser.add_argument('--hookso', required=True, type=Path)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--case', choices=['signals','dlopen','chld','arg','trap','external-trap','timeout','cancel','remote-timeout','threads','read','stop','remote-stop','pre-stopped','storm','fault'])
    args = parser.parse_args()
    if args.repeat < 1: parser.error('--repeat must be positive')
    hookso = args.hookso.resolve()
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix='hookso-fork-') as directory:
        binary = Path(directory) / 'fork_target'
        source = Path(__file__).with_name('fork_target.cpp').resolve()
        subprocess.run(['/usr/bin/g++', '-g', '-O0', '-ffixed-xmm15', '-rdynamic', '-no-pie',
                        str(source), '-pthread', '-o', str(binary)], check=True)
        payload = Path(directory) / 'fork_payload.so'
        subprocess.run(['/usr/bin/g++', '-shared', '-fPIC', str(source.with_name('fork_payload.cpp')),
                        '-o', str(payload)], check=True)
        cases = [(mode, case) for mode in ('fork', 'exec') for case in ('signals','dlopen','chld','arg','trap','external-trap','timeout','cancel','remote-timeout','stop','remote-stop','pre-stopped','storm','fault')]
        cases += [('threads','threads'), ('read','read')]
        if args.case: cases = [(m,c) for m,c in cases if c == args.case]
        for repeat in range(args.repeat):
            for mode, case in cases:
                try:
                    exercise(hookso, binary, directory, mode, case)
                    passed += 1; print(f'PASS {repeat+1} {mode}/{case}', flush=True)
                except Exception as e:
                    failed += 1; print(f'FAIL {repeat+1} {mode}/{case}: {e!r}', flush=True)
    print(f'PASS={passed} FAIL={failed} SKIP=0')
    return bool(failed)
if __name__ == '__main__': raise SystemExit(main())
