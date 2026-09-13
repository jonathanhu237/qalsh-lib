"""Bounded independent negative fixtures for the Stage-B quality checker."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    query, base = root / 'query.bin', root / 'base.bin'
    old, new = root / 'old.tsv', root / 'new.tsv'
    query.write_bytes(struct.pack('<f', 0.0))
    base.write_bytes(struct.pack('<ff', 0.0, 5e-10))
    old.write_text('0 0 0\n')
    outcomes = []
    cases = [('zero_to_nonzero', '0 1 5e-10\n'),
             ('wrong_zero_distance_encoding', '0 0 9e-5\n')]
    for name, output in cases:
        new.write_text(output)
        command = [sys.argv[1], str(query), str(base), '1', '2', '1', str(old), str(new)]
        completed = subprocess.run(command, text=True, capture_output=True)
        outcomes.append({'case': name, 'expected_quality_rejection': True,
                         'exit_code': completed.returncode,
                         'stdout': completed.stdout, 'stderr': completed.stderr})
    print(json.dumps(outcomes, indent=2))
    sys.exit(0 if all(row['exit_code'] == 5 for row in outcomes) else 1)
