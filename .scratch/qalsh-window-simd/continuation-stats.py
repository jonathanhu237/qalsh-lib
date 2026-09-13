"""All-sample frozen Stage-B continuation statistics; no sample exclusion."""
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import sys

root = Path(sys.argv[1])
records = json.loads((root / 'records.json').read_text())
by = {(r['triplet'], r['code']): r for r in records}
triplets = sorted({r['triplet'] for r in records})
assert len(triplets) == 12 and len(by) == len(records) == 36
orders = ['ABC', 'ACB', 'BAC', 'BCA', 'CAB', 'CBA'] * 2
flags = []
for t, order in zip(triplets, orders):
    group = [by[t, code] for code in 'ABC']
    assert all(r['order'] == order and r['returncode'] == 0 for r in group)
    occupancy = {}
    frequency = {}
    for r in group:
        occupancy[r['code']] = {}
        for cpu in r['siblings']:
            if cpu == r['cpu']:
                continue
            a = r['before_cpu_stat'][str(cpu)]['fields'][:8]
            b = r['after_cpu_stat'][str(cpu)]['fields'][:8]
            d = [y-x for x, y in zip(a, b)]
            assert sum(d) > 0
            occupancy[r['code']][str(cpu)] = 1.0 - (d[3]+d[4])/sum(d)
        frequency[r['code']] = r['perf']['effective_frequency_hz']
        assert frequency[r['code']] > 0
        result = root / Path(r['result_file']).name
        assert hashlib.sha256(result.read_bytes()).hexdigest() == r['result_sha256']
        assert r['app']['warm_hash'] == r['app']['final_hash']
    contaminated = (any(value > .05 for cpus in occupancy.values() for value in cpus.values())
                    or max(frequency.values()) / min(frequency.values()) > 1.05)
    flags.append(dict(triplet=t, order=order, contaminated=contaminated,
                      sibling_occupancy=occupancy, effective_frequency_hz=frequency))


def pair(base):
    times = [(by[t, base]['app']['query_ms_per_population'],
              by[t, 'C']['app']['query_ms_per_population']) for t in triplets]
    ratios = [new / old for old, new in times]
    logs = [math.log(r) for r in ratios]
    rng = random.Random(20260912)
    boot = sorted(statistics.fmean(rng.choices(logs, k=len(logs))) for _ in range(20000))
    def quantile(p):
        x = (len(boot)-1)*p
        i = int(x)
        return math.exp(boot[i]*(1-(x-i)) + boot[min(i+1, len(boot)-1)]*(x-i))
    ci = [quantile(.025), quantile(.975)]
    return dict(baseline=base, candidate='C', pairs=len(times),
                baseline_median_ms=statistics.median(x[0] for x in times),
                candidate_median_ms=statistics.median(x[1] for x in times),
                ratios=ratios, geometric_mean_ratio=math.exp(statistics.fmean(logs)),
                interval95=ci, evidence='improvement' if ci[1]<1 else 'slowdown' if ci[0]>1 else 'inconclusive')

pairs = [pair('B'), pair('A')]
contaminated = [r['triplet'] for r in flags if r['contaminated']]
result_hashes = {code: sorted({by[t, code]['result_sha256'] for t in triplets}) for code in 'ABC'}
assert all(len(hashes) == 1 for hashes in result_hashes.values())
assert result_hashes['B'] == result_hashes['C']
assert result_hashes['A'] == ['dfeb3c2c217c9974f0624fede9f762624cefb6822b28280a3d750adc939302a4']
assert result_hashes['B'] == ['48695f1a09f283e9ec8991cc4d04e94f0e8735a14cccc6b0df90ef8c780c370e']
accepted = len(contaminated)/len(triplets) <= .20 and all(p['evidence']=='improvement' for p in pairs)
summary = dict(triplets=flags, pairs=pairs, contaminated_triplets=contaminated,
               contamination_fraction=len(contaminated)/len(triplets),
               result_hashes=result_hashes, candidate_equals_722ecfb=True,
               result_and_data_identity_permits_reusing_existing_independent_Federalist_quality=True,
               stage_b_pass=accepted,
               full_project_acceptance=False, all_samples_included=True,
               bootstrap_resamples=20000, bootstrap_seed=20260912)
print(json.dumps(summary, indent=2))
