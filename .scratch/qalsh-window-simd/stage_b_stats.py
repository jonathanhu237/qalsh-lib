#!/usr/bin/env python3
import hashlib, json, math, pathlib, random, statistics, struct

ROOT=pathlib.Path('.scratch/qalsh-window-simd')
raw=json.loads((ROOT/'raw-stageb-20260913/records.json').read_text())
by={(r['triplet'],r['code']):r for r in raw}

def app(t,c): return by[t,c]['app']
def occ(r):
  vals=[]
  for cpu in r['siblings']:
    key=str(cpu); a=r['before_cpu_stat'][key]; b=r['after_cpu_stat'][key]
    vals.append((b['busy']-a['busy'])/(b['total']-a['total']))
  return vals

def stat(t):
  rs=[by[t,c] for c in 'ABC']
  freqs=[r['perf']['effective_frequency_hz'] for r in rs]
  contaminated=any(occ(r)[1] > .05 for r in rs) or max(freqs)/min(freqs)>1.05
  return {'triplet':t,'order':rs[0]['order'],'contaminated':contaminated,
          'sibling_occupancies':{c:occ(by[t,c])[1] for c in 'ABC'},
          'effective_frequencies_hz':dict(zip('ABC',freqs)),
          'query_ms_per_population':{c:app(t,c)['query_ms_per_population'] for c in 'ABC'},
          'open_ms':{c:app(t,c)['open_ms'] for c in 'ABC'},
          'warm_ms':{c:app(t,c)['warm_ms'] for c in 'ABC'},
          'result_sha256':{c:by[t,c]['result_sha256'] for c in 'ABC'},
          'result_hash':{c:app(t,c)['final_hash'] for c in 'ABC'}}

def boot(logs):
  rng=random.Random(20260912)
  means=sorted(statistics.fmean(rng.choices(logs,k=len(logs))) for _ in range(20000))
  def q(p):
    x=(len(means)-1)*p; i=int(x); f=x-i
    return math.exp(means[i]*(1-f)+means[min(i+1,len(means)-1)]*f)
  return [q(.025),q(.975)]

def pair(current,base):
  times=[(app(t,base)['query_ms_per_population'],app(t,current)['query_ms_per_population']) for t in range(1,13)]
  ratios=[n/o for o,n in times]; logs=[math.log(x) for x in ratios]
  return {'baseline':base,'current':current,'pairs':len(times),'baseline_median_ms':statistics.median(x[0] for x in times),'current_median_ms':statistics.median(x[1] for x in times),'ratios':ratios,'geometric_mean_ratio':math.exp(statistics.fmean(logs)),'bootstrap_95':boot(logs),'timing_evidence':'improvement' if boot(logs)[1]<1 else 'slowdown' if boot(logs)[0]>1 else 'inconclusive'}

out={'triplets':[stat(t) for t in range(1,13)],'pairs':[pair('C','B'),pair('C','A')]}
out['contaminated_triplets']=[t['triplet'] for t in out['triplets'] if t['contaminated']]
out['contamination_fraction']=len(out['contaminated_triplets'])/12
(ROOT/'stage-b-stats.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
