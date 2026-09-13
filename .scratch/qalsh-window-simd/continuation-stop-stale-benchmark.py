"""Stop only the verified orphaned/deleted QALSH benchmark, not services."""
import json
import os
from pathlib import Path
import signal
import time

pid = 416377
proc = Path('/proc') / str(pid)
expected = ['/tmp/old_algo', '/tmp/qalsh-acceptance/base/index/qalsh/l2/b',
            '/tmp/qalsh-acceptance/base/B.bin', '/tmp/qalsh-acceptance/base/A.bin']
# pidfd prevents the signal being delivered to a newly reused PID.
fd = os.pidfd_open(pid)
try:
    argv = proc.joinpath('cmdline').read_bytes().rstrip(b'\0').decode().split('\0')
    status = proc.joinpath('status').read_text()
    fields = dict(line.split(':', 1) for line in status.splitlines())
    ppid = int(fields['PPid'].strip())
    parent = Path('/proc') / str(ppid)
    parent_status = dict(line.split(':', 1) for line in parent.joinpath('status').read_text().splitlines())
    record = dict(pid=pid, ppid=ppid, argv=argv, uid=proc.stat().st_uid,
                  executable=os.readlink(proc / 'exe'),
                  stdout=os.readlink(proc / 'fd/1'),
                  parent_ppid=int(parent_status['PPid'].strip()),
                  parent_argv=parent.joinpath('cmdline').read_bytes().rstrip(b'\0').decode().split('\0'),
                  cpu_allowed=fields['Cpus_allowed_list'].strip(),
                  observed_unix=time.time(), signal='SIGTERM')
    assert argv == expected, 'not the verified old benchmark'
    assert record['uid'] == os.getuid(), 'not owned by this user'
    assert record['parent_ppid'] == 1, 'parent is no longer orphaned'
    assert record['executable'] == '/tmp/old_algo (deleted)', 'executable identity changed'
    assert record['stdout'] == '/tmp/oldalgo.out (deleted)', 'stdout identity changed'
    assert not Path('/tmp/qalsh-acceptance').exists(), 'old inputs still exist; reassess before stopping'
    print(json.dumps(record, indent=2), flush=True)
    signal.pidfd_send_signal(fd, signal.SIGTERM)
    print('SIGTERM sent to verified stale benchmark via pidfd; no other process signaled.', flush=True)
finally:
    os.close(fd)
