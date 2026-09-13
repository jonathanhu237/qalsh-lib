"""Verify actual runtime isolation and immutable inputs before benchmark."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import time

ROOT = Path('/home/jonathanhu237/code/qalsh-window-simd/stageb')
EXPECTED = {
    'original-repeat': '3577819b3cc1e29465266c731a6e9b6af43d343d879c2853c0df3c71522b5c6c',
    'current-baseline-repeat': '633282803f7547e79a7870c8cf37d4c56e5b266ca2dc5d0a3f34dd9350cd6517',
    'current-candidate-repeat': 'b423768b8a510c713e1a2c67299a569fdf21a037dc5d5880cdd327851c8ff63b',
    'stage_b_repeat.cc': '1b894357c6fa9530f6c5845d1359dae8b2dde98201c317ecf0e10d54640ed2ef',
    'data/federalist/A.bin': '5bb17396ec0d8b77741f059cb484901a46ae62acee3437afb1d72ae05be07efd',
    'data/federalist/B.bin': '4ab3e20e57692ef81665c6ac97ad32a41d11b4b7bf99fb843c779e9e720b3e95',
    'data/federalist/metadata.json': '67f67afba504a7a3c2d3ec05274cc50a3bedb0b245fd2786ae7c16d211c588a5',
}

def expand(text):
    result = set()
    for token in text.strip().split(','):
        if '-' in token:
            first, last = map(int, token.split('-'))
            result.update(range(first, last+1))
        elif token:
            result.add(int(token))
    return result


def hashes():
    observed = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in EXPECTED}
    assert observed == EXPECTED, 'binary/harness/data provenance changed'
    return observed


cgroup = Path('/proc/self/cgroup').read_text().strip().split('::', 1)[1]
assert cgroup.startswith('/qalshsimdiso20260913a.slice/'), cgroup
own = (Path('/sys/fs/cgroup') / cgroup.lstrip('/') / 'cpuset.cpus.effective').read_text().strip()
assert expand(own) == {6, 18}, own
others = {}
for unit in ['system.slice', 'user.slice']:
    mask = (Path('/sys/fs/cgroup') / unit / 'cpuset.cpus.effective').read_text().strip()
    assert expand(mask) == set(range(24)) - {6, 18}, (unit, mask)
    others[unit] = mask
record = dict(cgroup=cgroup, effective_cpus=own, scheduler_affinity=sorted(os.sched_getaffinity(0)),
              other_slices=others, before_hashes=hashes(), time_unix=time.time(),
              environment={name:os.environ.get(name) for name in ['OMP_NUM_THREADS','OMP_PROC_BIND','OMP_PLACES','STAGEB_CPU','STAGEB_RUN_TAG']})
path = ROOT / 'isolation-context-20260913.json'
assert not path.exists(), 'refusing to overwrite isolation context'
path.write_text(json.dumps(record, indent=2))
print('BENCHMARK ISOLATION CONTEXT', json.dumps(record), flush=True)
try:
    runpy.run_path(str(ROOT/'stage_b_run.py'), run_name='__main__')
finally:
    record['after_hashes'] = hashes()
    record['end_unix'] = time.time()
    path.write_text(json.dumps(record, indent=2))
