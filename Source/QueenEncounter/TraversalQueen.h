#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "TraversalQueen.generated.h"
class UStaticMeshComponent;
class UBoxComponent;
class UPhysicsConstraintComponent;

// V16: authored procedural gait, not a learned policy or a force-driven locomotor.
// World-space stance anchors prevent the spherical feet from acting as wheels.
UCLASS()
class QUEENENCOUNTER_API ATraversalQueen : public AActor
{
    GENERATED_BODY()
public:
    ATraversalQueen();
    void Initialize(FVector Origin, float Heading);
    void Advance(float Dt);
    void WriteResult(const FString& File) const;
    void SetMotion(FVector2D LocalInput, float TurnInput);
    void SeverLeg(int32 I);
    bool ReceiveShot(UPrimitiveComponent* Hit, float Damage);
    int32 Mask=0;
    FString Scenario=TEXT("forward");
    float TurnedDegrees=0, TargetHeight=700;
    bool bManual=false;
    bool bEncounterAI=false, bShowDebug=true;
    bool bPhysicalBody=false;
    bool IsBodySimulating() const;
    void EnablePhysicalBody();
    FString Action=TEXT("WANDER");
    void Think(float Dt);
    void DrawDiagnostics() const;
    TArray<FString> JointReadout() const;
    int32 Shots=0, Jumps=0, Landings=0, Detections=0;
    bool bJumping=false;
    double FrameSeconds=0;
    int32 FrameCount=0;
    float Clock=0, Speed=0, ActualSpeed=0, Distance=0, Goal=50, TargetSpeed=5;
    bool bDone=false, bFailed=false;
    FString Failure;
private:
    UPROPERTY() TObjectPtr<UBoxComponent> Body;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> DebrisJoints;
    struct FLeg
    {
        FVector Foot, Anchor, From, To;
        FVector Points[6];
        bool Swing=false;
        float Elapsed=0, Lift=110, Duration=.462f, LastTouchdown=0;
        int32 Steps=0;
        float Health=100;
    };
    FLeg Legs[6];
    float SupportForces[6]={0,0,0,0,0,0};
    float BodyMass=6000, ForceRecovery=1;
    FVector LastPhysicalPosition;
    FVector SupportMount(int32 I) const;
    void ApplySupport(float Dt,FVector Target,FRotator Facing);
    void AdvancePhysicalJump(float Dt);
    bool JumpLaunched=false;
    FVector Home, WanderTarget, LastSeen, AimPoint;
    FVector JumpStart, JumpEnd, JumpFeet[6], LandingFeet[6];
    float BrainTime=0, WanderTime=0, LostSight=100, AttackClock=0, JumpClock=0, JumpCooldown=0;
    float JumpDuration=2.f, JumpHeight=500.f;
    bool Stomped=false;
    int32 StompCount=0;
    float MeasuredJumpRise=0, MeasuredJumpTravel=0;
    FVector StompStartFeet[6];
    float JumpImbalance=0;
    FRotator JumpBaseRotation;
    bool bWasVisible=false;
    bool BeginEscapeJump(FVector Away);
    void AdvanceJump(float Dt);
    void SetAction(const FString& NewAction);
    FVector Start, Forward;
    FVector RoutePosition, BodyBias, Velocity;
    FVector2D MotionInput=FVector2D(1,0);
    float TurnInput=0, TurnRate=0, DamageAge=100, SlotClock=0;
    int32 NextLeg=0;
    double SolverSeconds=0;
    float MaxSolverMs=0, UnsupportedTime=0;
    float UnsupportedStreak=0, MaxUnsupportedStreak=0;
    int32 SolverTicks=0;
    float Yaw=0, InitialYaw=0, Phase=0, HeightSum=0, HeightSq=0, TiltMax=0;
    float CruiseTime=0, CruiseDistance=0, CruiseGood=0;
    float MaxDrift=0, MaxPenetration=0, MaxIKError=0, MaxStep=0;
    float MaxJointYaw=0, MaxHipElevation=0, MinSupportMargin=BIG_NUMBER;
    float MinLegClearance=BIG_NUMBER;
    FString ClosestLegPair;
    float NextSample=0;
    int32 Samples=0, AirMax=0;
    TArray<FString> Rows;
    bool Ground(FVector Point, FVector& Hit, FVector* Normal=nullptr) const;
    FVector Neutral(int32 I) const;
    void PoseLeg(int32 I);
    void Segment(int32 Index, FVector A, FVector B, float Width);
    bool Missing(int32 I) const { return (Mask&(1<<I))!=0; }
    int32 ActiveLegs() const;
    bool Crawling() const;
    void StartStep(int32 I, float StepPeriod);
    TArray<FVector2D> SupportHull(int32 Exclude=-1) const;
    float SupportMargin(const TArray<FVector2D>& Hull) const;
};

UCLASS()
class QUEENENCOUNTER_API AQueenTraversalMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AQueenTraversalMode();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void RestartPlayer(AController* C) override;
    UPROPERTY() TObjectPtr<ATraversalQueen> Queen;
private:
    bool Loop=false, Saved=false;
    float Hold=0, SimulationAccumulator=0;
    FString ResultFile;
    FString Scenario=TEXT("forward");
    int32 DamageMask=0;
    float DamageAt=3;
    bool Damaged=false, Manual=false, PlayerTest=false;
    bool AITest=false;
    float TestClock=0;
};

UCLASS()
class QUEENENCOUNTER_API AQueenTraversalHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
