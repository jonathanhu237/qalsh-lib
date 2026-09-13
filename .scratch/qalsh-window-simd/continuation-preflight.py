import json
from pathlib import Path
import time


def expand(text):
    result = []
    for part in text.strip().split(','):
        if '-' in part:
            a, b = map(int, part.split('-'))
            result.extend(range(a, b + 1))
        else:
            result.append(int(part))
    return sorted(result)


def snapshot():
    result = {}
    for line in Path('/proc/stat').read_text().splitlines():
        fields = line.split()
        if fields[0].startswith('cpu') and fields[0][3:].isdigit():
            # guest/guest_nice already included in user/nice; count only first8.
            result[int(fields[0][3:])] = list(map(int, fields[1:9]))
    return result


pairs = {}
for path in Path('/sys/devices/system/cpu').glob('cpu[0-9]*/topology/thread_siblings_list'):
    siblings = expand(path.read_text())
    pairs[siblings[0]] = siblings
samples = []
for _ in range(3):
    before = snapshot()
    time.sleep(1.0)
    after = snapshot()
    occupancy = {}
    for cpu in before:
        delta = [b-a for a, b in zip(before[cpu], after[cpu])]
        occupancy[cpu] = 1.0 - (delta[3] + delta[4]) / sum(delta)
    samples.append(dict(before=before, after=after, occupancy=occupancy))
ranks = sorted((max(sample['occupancy'][cpu] for sample in samples for cpu in siblings), first)
               for first, siblings in pairs.items())
busy, chosen = ranks[0]
print(json.dumps(dict(timestamp_unix=time.time(), siblings=pairs, samples=samples,
                      ranking=ranks, selected_cpu=chosen, selected_siblings=pairs[chosen],
                      selected_max_preflight_busy=busy), indent=2))
