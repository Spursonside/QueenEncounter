"""Run real UE terrain traversal, then independently audit telemetry. No video capture.

python Scripts/VerifyTraversal.py --run v16_verified --workers 2
The procedural controller is not an ML policy; failed runs are retained as diagnostics.
"""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CASES = [
    ('origin_60', 0, 0, 0, 60, ''),
    ('origin_30', 0, 0, 0, 30, ''),
    ('origin_120', 0, 0, 0, 120, ''),
    ('north', 0, 0, 90, 60, ''),
    ('hills', 6000, 2000, 0, 60, ''),
    ('diagonal', -6000, -5000, 35, 60, ''),
    ('blocked', 0, 0, 0, 60, 'body_collision'),
    ('outside_landscape', 100000, 0, 0, 60, 'missing_start_ground'),
]


def audit(data, rows, expected_failure):
    if expected_failure:
        return data['failed'] and not data['completed'] and not data['passed'] and data['failure'] == expected_failure
    cruise = [r for r in rows if float(r['distance_m']) > 10]
    if len(cruise) < 2:
        return False
    # Recompute speed from sampled displacement/time, independently of the game's speed field.
    speed = (float(cruise[-1]['distance_m']) - float(cruise[0]['distance_m'])) / (float(cruise[-1]['seconds']) - float(cruise[0]['seconds']))
    good_fraction = sum(4.5 <= float(r['body_speed_m_s']) <= 5.5 for r in cruise) / len(cruise)
    return (data['completed'] and not data['failed'] and data['passed']
            and data['distance_m'] >= 50 and 4.5 <= speed <= 5.5 and good_fraction >= .8
            and data['cruise_seconds'] >= 5 and min(data['steps_per_leg']) >= 3
            and data['max_stance_drift_cm'] <= 1 and data['max_ik_error_cm'] <= 1
            and data['max_foot_penetration_cm'] <= 5 and data['max_airborne_feet'] <= 2
            and abs(data['mean_height_m'] - 7) <= .5 and data['height_std_m'] <= .3
            and data['max_tilt_deg'] <= 20 and data['min_support_margin_cm'] >= 0 and data['min_leg_clearance_cm'] >= 0
            and data['max_coxa_yaw_from_rest_deg'] <= 75 and data['max_hip_elevation_deg'] <= 85)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=Path('D:/Epic Games/UE_5.8'))
    parser.add_argument('--run', default='v16_verified')
    parser.add_argument('--workers', type=int, choices=(1, 2), default=2)
    args = parser.parse_args()
    directory = ROOT / 'LearningRuns' / args.run
    directory.mkdir(parents=True, exist_ok=False)  # Never silently overwrite an earlier experiment.
    exe = args.engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'

    def run(case):
        name, x, y, heading, fps, expected = case
        result = directory / (name + '.json')
        command = [str(exe), str(ROOT / 'QueenEncounter.uproject'),
                   '/Game/Maps/ArthropodTerrain?game=/Script/QueenEncounter.QueenTraversalMode',
                   '-game', '-nullrhi', '-unattended', '-nosound', '-benchmark', f'-fps={fps}',
                   f'-QueenStartX={x}', f'-QueenStartY={y}', f'-QueenHeading={heading}',
                   f'-QueenResult={result}', f'-abslog={directory / (name + ".log")}']
        if name == 'blocked':
            command.append('-QueenBlockTest')
        process = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=120)
        data = json.loads(result.read_text(encoding='utf-8-sig'))
        with result.with_suffix('.csv').open(encoding='utf-8-sig', newline='') as stream:
            rows = list(csv.DictReader(stream))
        valid = process.returncode == 0 and audit(data, rows, expected)
        print(f'{name}: audit={valid}, distance={data["distance_m"]:.3f}m, speed={data["cruise_speed_m_s"]:.3f}m/s, failure={data["failure"]}', flush=True)
        return dict(case=name, audit_passed=valid, expected_failure=expected, fps=fps, **data)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(run, CASES))
    sources = ['TraversalQueen.cpp', 'TraversalQueen.h']
    summary = dict(all_checks_passed=all(r['audit_passed'] for r in results), cases=results,
                   source_sha256={s: hashlib.sha256((ROOT / 'Source/QueenEncounter' / s).read_bytes()).hexdigest() for s in sources})
    (directory / 'verification.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    raise SystemExit(0 if summary['all_checks_passed'] else 1)


if __name__ == '__main__':
    main()

