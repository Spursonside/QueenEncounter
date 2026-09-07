"""Check real Chaos body loss-of-support and recovery, plus normal physical travel."""
from pathlib import Path
import argparse,subprocess,csv,json,concurrent.futures,hashlib
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--run',required=True);args=p.parse_args()
run=root/'LearningRuns'/args.run;run.mkdir(exist_ok=False)
def check(case):
    name,mask,move=case;out=run/f'{name}.json'
    command=['D:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',str(root/'QueenEncounter.uproject'),'/Game/Maps/ArthropodTerrain?game=/Script/QueenEncounter.QueenTraversalMode','-game','-nosplash','-nullrhi','-unattended','-nosound','-benchmark','-fps=60','-QueenPhysicalBody','-QueenBodyTest',f'-QueenDamageMask={mask}','-QueenDamageAt=5',f'-QueenResult={out}',f'-abslog={out.with_suffix(".log")}']
    if not move:command.append('-QueenManual')
    proc=subprocess.run(command,stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,timeout=120)
    d=json.loads(out.read_text(encoding='utf-8-sig'))
    with out.with_suffix('.csv').open(encoding='utf-8-sig') as f:rows=list(csv.DictReader(f))
    before=[x for x in rows if 3<float(x['seconds'])<5]
    shock=[x for x in rows if 5<float(x['seconds'])<8]
    tail=[x for x in rows if 14<float(x['seconds'])<18]
    peak=max(float(x['tilt_deg']) for x in shock);settled=max(float(x['tilt_deg']) for x in tail)
    low=min(float(x['height_m']) for x in tail)
    okay=proc.returncode==0 and d['body_simulating_physics'] and d['body_mass_kg']>1000 and not d['failed'] and d['seconds']>17
    if not move:
        okay &= max(float(x['tilt_deg']) for x in before)<1 and low>6.25 and settled<4
        if mask:okay &= 2<peak<45 and peak>settled+2
    else:okay &= d['distance_m']>70 and 4.5<sum(float(x['body_speed_m_s']) for x in tail)/len(tail)<5.5 and settled<20
    okay &= d['max_ik_error_cm']<=1 and d['min_leg_clearance_cm']>=0
    print(f'{name}: {okay}, peak={peak:.2f}deg, settled={settled:.2f}deg, height={low:.2f}m, distance={d["distance_m"]:.1f}m',flush=True)
    return dict(case=name,passed=bool(okay),peak_tilt_deg=peak,recovered_tilt_deg=settled,recovered_min_height_m=low,result=d)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:cases=list(pool.map(check,[('intact',0,False),('left_front',1,False),('right_front',8,False),('front_pair',9,False),('left_pair',3,False),('travel',0,True)]))
summary=dict(passed=all(x['passed'] for x in cases),cases=cases,source_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'Source/QueenEncounter').glob('*') if p.suffix in ['.h','.cpp']})
(run/'verification.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
raise SystemExit(0 if summary['passed'] else 1)
