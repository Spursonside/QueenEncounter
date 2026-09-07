#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicsQueen.generated.h"
class UStaticMeshComponent;
class UPhysicsConstraintComponent;

UCLASS()
class QUEENENCOUNTER_API APhysicsQueen : public AActor
{
    GENERATED_BODY()
public:
    APhysicsQueen();
    void Configure(FVector Origin, const TArray<float>& Parameters, int32 MissingMask);
    void StepController(float Dt);
    float Fitness() const;
    float ForwardDistance() const;
    float UprightFraction() const;
    void SeverLeg(int32 Index);
    bool ReceivePlayerShot(UPrimitiveComponent* Part, float Damage);
    UStaticMeshComponent* GetBody() const { return Body; }
    FVector GetStart() const { return Start; }
    int32 Mask = 0;
    // 기존 40개 + 종아리·발목·수평 기부 각각 진폭·위상·편향 18개: 총 94개.
    static constexpr int32 GeneCount = 94;
    bool FastGait() const { return Genes.IsValidIndex(10) && Genes[10]>.5f && Mask==0; }
    float GaitSlot() const { return FastGait() ? .65f+.25f*Genes[0] : (2.8f+Genes[0]*.4f)*(1.f+.45f*Genes[4]); }
    float FootOffset() const { return FastGait() ? TargetSpeed*100.f*3.f*GaitSlot()*.5f*(.8f+.2f*Genes[1]) : (70.f+Genes[1]*20.f)*(1.f+.9f*Genes[5])*(1.f+2.5f*FMath::Max(0.f,Genes[8])); }
    float CommandedSpeed() const { return FastGait() ? TargetSpeed : FootOffset()*2.f/(600.f*GaitSlot()); }
    float TenMetreTime=-1.f, CruiseDistance=0.f, CruiseSeconds=0.f, CruiseTargetSeconds=0.f;
    TArray<float> BodySpeedSamples;
    float SpeedBinDistance=0.f, SpeedBinSeconds=0.f;
    bool bFailed = false;
    FString FailureReason;
    float StepTravelSum=0.f, StepTravelMax=0.f;
    bool bFailureFrozen = false;
    bool bTerrainTest = false;
    // 평가 가중치는 학습 대상이 아니다. 정책이 벌점을 낮춰 점수를 편법으로 높이지 못하게 고정한다.
    float TiltWeight = 2.f;
    float RotationWeight = 0.75f;
    float MeanTiltDegrees() const { return TiltIntegral / FMath::Max(MeasuredTime, 0.01f); }
    float MeanRotationSpeed() const { return RotationIntegral / FMath::Max(MeasuredTime, 0.01f); }
    float StableDistance() const { return StableProgress; }
    // v3: 몸통 중심 기준 7 m. 높이를 강제로 고정하지 않고 실제 물리 결과를 평가한다.
    bool bLegacyFeet = false;
    float MeanFootSlipSpeed() const { return FootSlipIntegral/FMath::Max(FootContactTime,.01f); }
    float FootContactSeconds() const { return FootContactTime; }
    float TargetHeight = 700.f;
    float TargetSpeed = 1.f;
    float GoalMetres = 50.f;
    bool bReachedGoal = false;
    float MeanHeight() const { return float(HeightMean); }
    float HeightDeviation() const { return float(FMath::Sqrt(FMath::Max(0.0, HeightM2 / FMath::Max(HeightTime, 0.01)))); }
    float HeightErrorRMS() const { return FMath::Sqrt(HeightError / FMath::Max(MeasuredTime, 0.01f)); }
    float MeanSpeed() const { return ForwardDistance() / FMath::Max(Clock, 0.01f); }
    float ElapsedSeconds() const { return Clock; }
    float CurrentHeight = 700.f;
    // Isaac Lab의 clearance/contact/gait 보상 개념을 Chaos에서 평가한다.
    bool bStructuredGait = true;
    bool bPlantFeet = false;
    int32 CurriculumStage = 0; // 0: 제자리 발 들기, 1: 5m, 2: 15m, 3: 50m
    bool PassedStage() const;
    int32 ValidSteps() const;
    int32 StepsForLeg(int32 I) const { return GaitFeet[I].Steps; }
    float StanceDrift() const { return StanceDriftIntegral / FMath::Max(StanceSeconds, .01f); }
    float FootRoll() const { return RollIntegral / FMath::Max(StanceSeconds, .01f); }
    float GaitMatch() const { return MatchIntegral / FMath::Max(GaitSeconds, .01f); }
    float PeakClearance() const { return MaxClearance; }
    float MaxPlantDrift() const { return PlantDriftMax; }
    float MeanPlantDrift() const { return PlantDriftIntegral/FMath::Max(PlantSeconds,.01f); }
private:
    struct FFootState
    {
        FVector Anchor = FVector::ZeroVector, From = FVector::ZeroVector, To = FVector::ZeroVector;
        FVector LiftStart = FVector::ZeroVector, PlantedCenter = FVector::ZeroVector;
        float AirTime = 0, Peak = 0, ContactTime = 0;
        float Angles[3] = {};
        bool Contact = false, HadContact = false, Swing = false, Armed = false;
        int32 Steps = 0;
    };
    FFootState GaitFeet[6];
    bool bGaitStarted = false;
    float PlantDriftMax = 0, PlantDriftIntegral = 0, PlantSeconds = 0;
    float TouchdownWait = 0;
    float GaitClock = 0, GaitSeconds = 0, MatchIntegral = 0;
    float StanceSeconds = 0, StanceDriftIntegral = 0, RollIntegral = 0, MaxClearance = 0;
    void MeasureGait(float Dt);
    void DriveGait(float Dt);
    FVector FootFK(int32 Leg, const float* Angles) const;
    void SolveFoot(int32 Leg, FVector Target, float Dt, FVector TargetVelocity=FVector::ZeroVector);
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    // 몸통 충돌은 하나로 유지하고 두흉부/배의 비율을 두 개의 외형으로 표현한다.
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Cephalothorax;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Abdomen;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ArmorRim;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> JointHousings;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CoreLens;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Upper;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Coxae;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> CoxaJoints;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Lower;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Shins;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Feet;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> Hips;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> Knees;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> CalfJoints;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> Ankles;
    TArray<float> Genes;
    FVector Start = FVector::ZeroVector;
    float Clock = 0;
    float UprightTime = 0;
    float Effort = 0;
    float LastTargets[30] = {};
    float TestLegHealth[6] = {120,120,120,120,120,120};
    float TiltIntegral = 0, RotationIntegral = 0, TiltCost = 0, RotationCost = 0;
    float MeasuredTime = 0, StableProgress = 0;
    FVector PreviousPosition = FVector::ZeroVector;
    float FootSlipIntegral = 0, FootContactTime = 0;
    float HeightIntegral = 0, HeightSquared = 0, HeightError = 0, HeightMotion = 0, SpeedCost = 0;
    float PreviousHeight = 700, GoalDistance = 0;
    double HeightMean = 0, HeightM2 = 0, HeightTime = 0;
    FVector GoalPosition = FVector::ZeroVector;
    float GoalHeading = 1.f;
    float FurthestX = 0, FilteredSpeed = 0;
    void SetJoint(UPhysicsConstraintComponent* Joint, UStaticMeshComponent* A, UStaticMeshComponent* B, FVector Position, float Yaw, float Limit);
};
