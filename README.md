# Queen Encounter

여섯 다리 보스의 지형 적응 보행과 부위 파괴를 구현한 독립 게임플레이 프로토타입입니다.

**이동준 · 개인 프로젝트 · Unreal Engine 5.8.2 · C++ · GPT-6 Astra와 AI 협업 개발**

ARC Raiders의 퀸이 주는 규모감과 손실 후 이동을 참고했습니다. 원작 모델, 애니메이션, 소스 코드와 회사 내부 자료를 사용하지 않습니다. 기본 도형과 자체 생성 지형으로 구성했습니다. 원작 개발 방식의 재현을 주장하지 않습니다.

## 담당 역할과 AI 활용

이동준: 목표 행동과 형태 정의, 보폭·속도·접지·다리 손실에 대한 플레이 관찰과 피드백, 평가 조건 설정, 구현 방법 변경 판단.

GPT-6 Astra / Codex: 대화로 정한 요구사항을 바탕으로 코드 초안·수정, 실험 스크립트, 빌드·검증 및 기록 정리를 보조했습니다. 이 저장소는 AI가 작성·수정한 코드를 포함합니다. 모든 코드를 직접 타이핑했거나 현재 행동이 모두 머신러닝으로 학습되었다고 주장하지 않습니다.

## 주요 소스부터 보기

| 파일 | 구현 내용 |
|---|---|
| [TraversalQueen.cpp](Source/QueenEncounter/TraversalQueen.cpp) | 보행 주기, 발 고정, 지형 IK, 다리 간섭 검사, 플레이어 모드 |
| [QueenBodyPhysics.cpp](Source/QueenEncounter/QueenBodyPhysics.cpp) | Chaos 몸체, 접지 다리별 힘 배분, 손실 후 균형 회복 |
| [QueenEncounterAI.cpp](Source/QueenEncounter/QueenEncounterAI.cpp) | 배회·감지·추격·공격·손실 시 도주 FSM |
| [QueenPlayer.cpp](Source/QueenEncounter/QueenPlayer.cpp) | 이동·조준·사격·피격 및 수동 초기화 |
| [QueenTerrainLibrary.cpp](Source/QueenEncounter/QueenTerrainLibrary.cpp) | 노이즈 기반 Landscape 생성 |
| [PhysicsQueen.cpp](Source/QueenEncounter/PhysicsQueen.cpp) | 초기 관절 힘 기반 학습 실험용 물리 로봇 |
| [QueenLearningMode.cpp](Source/QueenEncounter/QueenLearningMode.cpp) | 초기 세대별 후보 평가·선택·기록 |

## 왜 방법을 바꿨는가

1. 관절 각도·힘을 탐색했지만 구형 발을 굴리는 보상 편법과 느린 보행이 나타났습니다.
2. Isaac Lab의 보행 평가 방식을 참고해 접지·발 들림·속도·자세 항목을 분리했습니다. Isaac Lab 라이브러리를 직접 통합한 것은 아닙니다.
3. 보폭을 늘려도 힘 기반 방식이 5m/s 목표를 충족하지 못했습니다. V15의 36개 후보와 추가 6개 진단은 완주 0건이었습니다.
4. 목표를 만족시키기 위해 절차적 보행·지형 IK로 전환했습니다. V16 정상 코스는 50m 약 11.25초, 순항 5m/s를 확인했습니다.
5. V20에서는 몸체를 실제 Chaos 강체로 전환해 손실한 다리의 지지력이 사라지고 기울어진 뒤 회복하도록 만들었습니다.

## 현재 구현과 검증 범위

- 현재는 **절차적 다리 IK + 실제 Chaos 몸체 + 가상 지지력 제어**의 혼합 방식입니다. 정상 다리가 모두 관절 모터 강체인 완전한 로봇 시뮬레이션은 아닙니다.
- 몸체 질량 6,000kg, 정상 정지 높이 약 7m. 접지 다리별 힘을 계산해 몸체에 적용합니다.
- 살아 있는 다리의 손실 시 힘을 즉시 제거하고 나머지 다리의 제어를 지연·증가시켜 기울기와 회복을 구분합니다.
- 전진, 좌우 이동, 회전, 다리 파괴, 배회·추격·공격·점프 도주. AI는 C++ FSM이며 Unreal Behavior Tree 자산을 사용하지 않습니다.
- V20 물리 6조건, AI·점프 4조건 통과. 정상 물리 이동은 18초 약 83.7m. 이 수치는 V16의 절차적 50m 측정과 별개의 시험입니다.
- 경사·장애물·모든 손실 조합에서의 성공을 보장하지 않습니다. 영상의 초기 실험과 최종 제어기를 구분해 보아 주세요.

[V20 상세](docs/PHYSICAL_BODY_V20.md) · [물리 검증 결과](Evidence/v20_physics_release.json) · [AI 검증 결과](Evidence/v20_physics_ai_release.json)

Evidence는 실행 당시의 기록입니다. 기록의 소스 해시는 당시 버전을 가리키며, 이후 HUD·시연 편의 수정이 포함된 현재 소스와 다를 수 있습니다.

## 실행

Windows, Unreal Engine 5.8.2, Visual Studio 2022 C++/Windows SDK가 필요합니다. 엔진 기본 도형과 편집기 Python 플러그인을 사용합니다. 저장소에는 엔진 바이너리나 생성된 Content가 포함되지 않습니다.

```powershell
./Scripts/Build.ps1 -EngineRoot "D:/Epic Games/UE_5.8"
./Scripts/PrepareTerrain.ps1 -EngineRoot "D:/Epic Games/UE_5.8"
./Scripts/PlayAdaptiveQueen.ps1 -Player
# 피해를 받는 전투 시연
./Scripts/PlayAdaptiveQueen.ps1 -Player -Combat
```

WASD 이동, 마우스 조준·좌클릭 사격, Shift 달리기, Space 점프, F2 디버그, F4 다리 하나 파괴, F5 앞 네 다리, F6/F7 좌우 세 다리, R 수동 리셋. 기본 플레이어 시연은 무적이며 자동 리셋하지 않습니다. Combat 모드 사망 시 R 안내가 표시됩니다.

검증 스크립트 `VerifyBodyPhysics.py`, `VerifyEncounterAI.py`는 기본 엔진 경로를 사용합니다. 다른 위치에 설치했다면 스크립트의 엔진 경로를 바꾼 후 `python Scripts/VerifyBodyPhysics.py --run my_body_test`로 새 기록을 만듭니다.

## 공개 범위

본 저장소는 채용 검토를 위한 독립 프로젝트 소스 공개본입니다. 회사 소스·내부 화면·데이터와 인증정보는 포함하지 않습니다. 원작 게임 및 Unreal Engine의 상표·권리는 각 권리자에게 있습니다. 별도의 포괄적 재사용 라이선스를 부여하지 않습니다.
