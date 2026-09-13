"""Root controller: bounded runtime cpuset isolation with independent rollback.

Source prepared locally, copied to a root-owned /run directory before use.
No persistent service configuration, governor or unrelated process is changed.
"""
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

TOKEN = 'qalshsimdiso20260913a'
ROOT = Path('/run') / TOKEN
STATE = ROOT / 'state.json'
SERVICE = TOKEN + '.service'
SLICE = TOKEN + '.slice'
ROLLBACK = TOKEN + 'restore'
RESTRICTED = '0-5,7-17,19-23'
BASE = Path('/home/jonathanhu237/code/qalsh-window-simd/stageb')
TAG = 'stageb-isolated-20260913'
UNITS = ['system.slice', 'user.slice']


def command(argv, *, check=True, timeout=25):
    print('+', ' '.join(map(str, argv)), flush=True)
    p = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout)
    print(p.stdout, end='', flush=True)
    if check and p.returncode:
        raise RuntimeError(f'command exited {p.returncode}: {argv}')
    return p


def props(unit):
    p = subprocess.run(['systemctl', 'show', unit, '-p', 'AllowedCPUs',
                        '-p', 'EffectiveCPUs', '-p', 'ControlGroup'], check=True,
                       text=True, capture_output=True, timeout=20)
    return dict(line.split('=', 1) for line in p.stdout.splitlines() if '=' in line)


def cpu_mask(text):
    if not text.strip():
        return None  # inherited/default AllowedCPUs, not an explicit empty set
    result = set()
    for part in text.replace(',', ' ').split():
        if '-' in part:
            first, last = map(int, part.split('-'))
            result.update(range(first, last + 1))
        else:
            result.add(int(part))
    return frozenset(result)


def save(state):
    temporary = STATE.with_suffix('.tmp')
    temporary.write_text(json.dumps(state, indent=2))
    os.chmod(temporary, 0o600)
    temporary.replace(STATE)


def restore():
    if not STATE.exists():
        print('No state file: no recorded CPU changes.', flush=True)
        return
    # Separate from the lease lock: watchdog must restore even if main hangs.
    with (ROOT / 'restore.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        state = json.loads(STATE.read_text())
        if state.get('restored'):
            print('CPU settings already restored.', flush=True)
            return
        command(['systemctl', 'stop', SERVICE], check=False, timeout=20)
        errors = []
        after = {}
        for unit, before in state['before'].items():
            try:
                current = props(unit)
                if cpu_mask(current['AllowedCPUs']) not in (cpu_mask(before['AllowedCPUs']), cpu_mask(RESTRICTED)):
                    raise RuntimeError(f'concurrent CPU-mask change: {current}')
                if cpu_mask(current['AllowedCPUs']) != cpu_mask(before['AllowedCPUs']):
                    command(['systemctl', 'set-property', '--runtime', unit,
                             'AllowedCPUs=' + before['AllowedCPUs']])
                after[unit] = props(unit)
                if cpu_mask(after[unit]['AllowedCPUs']) != cpu_mask(before['AllowedCPUs']):
                    raise RuntimeError('AllowedCPUs restoration mismatch')
                if cpu_mask(after[unit]['EffectiveCPUs']) != cpu_mask(before['EffectiveCPUs']):
                    raise RuntimeError('EffectiveCPUs restoration mismatch')
            except Exception as error:
                errors.append(f'{unit}: {error}')
        command(['systemctl', 'stop', SLICE], check=False)
        state.update(after=after, restore_errors=errors, restored=not errors,
                     restoration_unix=time.time())
        save(state)
        print(json.dumps({'restored': state['restored'], 'after': after,
                          'errors': errors}, indent=2), flush=True)
        if errors:
            raise RuntimeError('CPU restoration needs attention: ' + '; '.join(errors))


def main():
    assert os.geteuid() == 0, 'root privileges required for runtime masks'
    if len(sys.argv) == 2 and sys.argv[1] == 'restore':
        restore()
        return
    assert len(sys.argv) == 1
    ROOT.mkdir(mode=0o700, exist_ok=True)
    lease = Path('/run/qalsh-performance-isolation.lock').open('w')
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not STATE.exists(), 'refusing to overwrite a prior isolation state'
    assert not (BASE / ('raw-' + TAG)).exists(), 'refusing a repeated result batch'
    siblings = Path('/sys/devices/system/cpu/cpu6/topology/thread_siblings_list').read_text().strip()
    assert siblings == '6,18', siblings
    before = {unit: props(unit) for unit in UNITS}
    assert all(p['EffectiveCPUs'] == '0-23' for p in before.values()), before
    # Unexpected live top-level groups must not silently escape the exclusion.
    escapes = []
    for path in Path('/sys/fs/cgroup').iterdir():
        if not path.is_dir() or path.name in (*UNITS, 'init.scope', SLICE):
            continue
        members = path / 'cgroup.procs'
        if members.exists() and members.read_text().strip():
            escapes.append(path.name)
    assert not escapes, f'unmanaged top-level groups: {escapes}'
    state = dict(token=TOKEN, before=before, restricted=RESTRICTED, reserve='6,18',
                 watchdog=ROLLBACK + '.timer', created_unix=time.time(), restored=False)
    save(state)
    command(['systemd-run', '--unit=' + ROLLBACK, '--on-active=10m',
             '--timer-property=AccuracySec=1s', '/usr/bin/python3',
             str(ROOT / 'isolate_stage_b.py'), 'restore'])
    command(['systemctl', 'is-active', ROLLBACK + '.timer'])

    def interrupt(signum, frame):
        raise InterruptedError(f'controller received signal {signum}')
    for sig in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, interrupt)
    try:
        for unit in UNITS:
            command(['systemctl', 'set-property', '--runtime', unit,
                     'AllowedCPUs=' + RESTRICTED])
        during = {unit: props(unit) for unit in UNITS}
        assert all(cpu_mask(p['EffectiveCPUs']) == cpu_mask(RESTRICTED) for p in during.values()), during
        state = json.loads(STATE.read_text())
        state['during'] = during
        save(state)
        print('ISOLATION VERIFIED', json.dumps(during), flush=True)
        # Independent transient top-level slice avoids the constrained SSH/user slice.
        command(['systemd-run', '--unit=' + TOKEN, '--slice=' + SLICE,
                 '--uid=jonathanhu237', '--gid=jonathanhu237', '--wait', '--pipe', '--collect',
                 '--property=AllowedCPUs=6,18', '--property=RuntimeMaxSec=480',
                 '--property=TimeoutStopSec=5', '--property=KillMode=control-group',
                 '--property=WorkingDirectory=' + str(BASE),
                 '--setenv=STAGEB_CPU=6', '--setenv=STAGEB_RUN_TAG=' + TAG,
                 '--setenv=STAGEB_ADJUSTMENT_REASON=User-authorized runtime cgroup isolation',
                 '--setenv=OMP_NUM_THREADS=1', '--setenv=OMP_PROC_BIND=true',
                 '--setenv=OMP_PLACES={6}', '/usr/bin/python3',
                 '/home/jonathanhu237/code/qalsh-window-continuation/isolated_benchmark_entry.py'],
                timeout=540)
    finally:
        restore()
        if json.loads(STATE.read_text()).get('restored'):
            command(['systemctl', 'stop', ROLLBACK + '.timer'], check=False)
            print('Independent watchdog cancelled after verified restoration.', flush=True)


if __name__ == '__main__':
    main()
