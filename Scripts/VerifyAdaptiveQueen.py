"""UE integration checks for V17 movement, limb loss and negative cases. No video capture."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
CASES=[(s,s,0,0,0,'') for s in ('forward','left','right','back','rotate','arc','change')]
CASES += [(f'loss_{i}','forward',1<<i,0,0,'') for i in range(6)]
CASES += [('front_pair','forward',9,0,0,''),('same_side','forward',3,0,0,''),
          ('front4','forward',27,0,0,''),('all_lost','forward',63,0,0,''),
          ('damaged_arc','arc',1,0,0,''),('damaged_rotate','rotate',27,0,0,''),
          ('rotate_hills','rotate',0,6000,2000,''),('left_hills','left',0,6000,2000,''),
          ('loss_hills','forward',1,6000,2000,''),('crawl_hills','forward',27,6000,2000,''),
          ('blocked','forward',0,0,0,'body_collision')]


def audit(d,rows,case):
    name,scenario,mask,x,y,expected=case
    if expected:
        return d['failed'] and not d['passed'] and not d['completed'] and d['failure']==expected
    if not (d['completed'] and not d['failed'] and d['passed'] and d['missing_mask']==mask): return False
    if not (d['max_stance_drift_cm']<=1 and d['max_ik_error_cm']<=1 and d['max_foot_penetration_cm']<=5
            and d['min_leg_clearance_cm']>=0 and d['unsupported_seconds']<=2 and d['max_airborne_feet']<=2): return False
    if mask==63:
        return d['mobility_state']=='immobile' and d['distance_m']<15 and float(rows[-1]['body_speed_m_s'])<.05
    if any(d['steps_per_leg'][i]<3 for i in range(6) if not mask&(1<<i)): return False
    if scenario=='rotate':
        return d['turned_degrees']>=90 and d['distance_m']<.5 and abs(d['heading_deg']-90)<1
    if d['distance_m']<50: return False
    # Straight tests must prove net progress, not body swaying or accumulated backwards travel.
    if scenario in ('forward','left','right','back'):
        actual={'forward':d['end_x_cm']-x,'back':x-d['end_x_cm'],'left':y-d['end_y_cm'],'right':d['end_y_cm']-y}[scenario]/100
        if actual<50 or abs(actual-d['distance_m'])>.02: return False
    if scenario=='forward':
        speed=d['cruise_speed_m_s']
        if mask==0 and not 4.5<=speed<=5.5: return False
        if mask.bit_count()==1 and not 2.2<=speed<=2.8: return False
        if mask.bit_count()>=2 and not .2<=speed<=1.2: return False
    if scenario in ('left','right') and not 1.8<=d['cruise_speed_m_s']<=2.2: return False
    return True


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run',required=True)
    p.add_argument('--engine',type=Path,default=Path('D:/Epic Games/UE_5.8'))
    p.add_argument('--cases',nargs='+',choices=[c[0] for c in CASES])
    args=p.parse_args()
    run=ROOT/'LearningRuns'/args.run;run.mkdir(exist_ok=False)
    def execute(c):
        n,scenario,mask,x,y,expected=c;out=run/(n+'.json')
        cmd=[str(args.engine/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),str(ROOT/'QueenEncounter.uproject'),
             '/Game/Maps/ArthropodTerrain?game=/Script/QueenEncounter.QueenTraversalMode','-game','-nosplash','-nullrhi','-unattended','-nosound','-benchmark','-fps=60',
             f'-QueenScenario={scenario}',f'-QueenDamageMask={mask}',f'-QueenStartX={x}',f'-QueenStartY={y}',f'-QueenResult={out}',f'-abslog={run/(n+".log")}']
        if expected:cmd.append('-QueenBlockTest')
        result=subprocess.run(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,timeout=180)
        d=json.loads(out.read_text(encoding='utf-8-sig'))
        with out.with_suffix('.csv').open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
        ok=result.returncode==0 and audit(d,rows,c)
        print(f'{n}: audit={ok}, distance={d["distance_m"]:.2f}m, speed={d["cruise_speed_m_s"]:.2f}m/s, failure={d["failure"]}',flush=True)
        return dict(case=n,audit_passed=ok,expected_failure=expected,**d)
    selected=[c for c in CASES if not args.cases or c[0] in args.cases]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(execute,selected))
    summary=dict(all_checks_passed=all(r['audit_passed'] for r in results),cases=results,
                 source_sha256={n:hashlib.sha256((ROOT/'Source/QueenEncounter'/n).read_bytes()).hexdigest() for n in ('TraversalQueen.cpp','TraversalQueen.h','QueenPlayer.cpp')})
    (run/'verification.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(0 if summary['all_checks_passed'] else 1)


if __name__=='__main__':main()

