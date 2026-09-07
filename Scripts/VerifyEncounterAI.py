"""Real UE FSM and asymmetric escape integration tests; retains logs without video."""
from pathlib import Path
import subprocess,json,concurrent.futures,hashlib,argparse
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--run',required=True);a=p.parse_args()
run=root/'LearningRuns'/a.run;run.mkdir(exist_ok=False)
def check(mask):
    out=run/f'mask_{mask}.json';log=out.with_suffix('.log')
    command=['D:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',str(root/'QueenEncounter.uproject'),'/Game/Maps/ArthropodTerrain?game=/Script/QueenEncounter.QueenTraversalMode','-game','-nosplash','-nullrhi','-unattended','-nosound','-benchmark','-fps=60','-QueenPlayerTest','-QueenAITest',f'-QueenAITestMask={mask}',f'-QueenResult={out}',f'-abslog={log}']
    proc=subprocess.run(command,stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,timeout=180)
    d=json.loads(out.read_text(encoding='utf-8-sig'));text=log.read_text(encoding='utf-8-sig')
    okay=(proc.returncode==0 and not d['failed'] and not d['completed'] and d['seconds']>74 and d['missing_mask']==mask and d['ai_shots']>=1 and d['ai_detections']>=1 and 'player_health=85.0' in text)
    if mask==63:okay &= d['ai_jumps']==0 and d['ai_state']=='IMMOBILE'
    else:okay &= d['ai_jumps']>=3 and d['ai_landings']>=3 and d['max_ik_error_cm']<=1 and d['min_leg_clearance_cm']>=0
    expected={7:1,56:-1,27:0,63:0}[mask]
    okay &= abs(d['jump_imbalance']-expected)<.001
    print(f'mask {mask}: {okay}, shots={d["ai_shots"]}, jumps={d["ai_jumps"]}, landings={d["ai_landings"]}, failure={d["failure"]}',flush=True)
    return {'mask':mask,'passed':bool(okay),'result':d}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: cases=list(pool.map(check,[7,56,27,63]))
summary={'passed':all(c['passed'] for c in cases),'cases':cases,'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'Source/QueenEncounter').glob('*') if p.name in ['QueenEncounterAI.cpp','TraversalQueen.cpp','TraversalQueen.h','QueenPlayer.cpp']}}
(run/'verification.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
raise SystemExit(0 if summary['passed'] else 1)
