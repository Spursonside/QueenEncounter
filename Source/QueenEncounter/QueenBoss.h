#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatRules.h"
#include "TerrainPath.h"
#include "QueenBoss.generated.h"

class UStaticMeshComponent;
class AQueenPlayer;
UENUM()
enum class EQueenState : uint8 { Idle, Chase, Windup, Attack, Recover, Stagger, Dead };

UCLASS()
class QUEENENCOUNTER_API AQueenBoss : public AActor
{
    GENERATED_BODY()
public:
    AQueenBoss();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void ReceiveShot(UPrimitiveComponent* Part, float Damage);
    FString StateLabel() const;
    FString AttackLabel() const;
    float Health = 2200.f;
    float MaxHealth = 2200.f;
    EQueenState State = EQueenState::Idle;
    QueenRules::Attack CurrentAttack = QueenRules::Attack::None;
    bool bDebug = false;
    bool bHasSight = false;
    bool IsCoreExposed() const;
    int32 BrokenLegs() const;
    float StateRemaining = 0.f;
    float ArmorAt(int32 Index) const;
    float LegHealthAt(int32 Index) const;
    FString LastDecision = TEXT("Waiting for target");
    FString SupportLabel = TEXT("SUPPORTED");
    float SupportMargin = 0;
    int32 GroundedFeet = 6;
    bool bInspectionMode = false;
    bool bTerrainTour = false;
    bool bDragging = false;
    void ToggleInspection();
    void ToggleTour();
    void BreakNextLeg();
    void BreakFrontFour();
    FVector CenterOfMass() const;
private:
    UPROPERTY() TObjectPtr<USceneComponent> Root;
    UPROPERTY() TObjectPtr<USceneComponent> BodyFrame;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Core;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> UpperLegs;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LowerLegs;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Joints;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Plates;
    struct FLeg
    {
        FVector Foot = FVector::ZeroVector;
        FVector Start = FVector::ZeroVector;
        FVector Goal = FVector::ZeroVector;
        float Alpha = 1.f;
        float Armor = 90.f;
        float Health = 120.f;
        bool bGroundValid = false;
        bool bDetached = false;
    };
    FLeg Legs[6];
    struct FShell { FVector Target; float Remaining; };
    TArray<FShell> Shells;
    TWeakObjectPtr<AQueenPlayer> Target;
    FVector AimPoint = FVector::ZeroVector;
    FVector BeamEnd = FVector::ZeroVector;
    float BeamCooldown = 0.f;
    float MortarCooldown = 0.f;
    float PulseCooldown = 0.f;
    float DecisionTimer = 0.f;
    float GaitClock = 0.f;
    float StateElapsed = 0.f;
    bool bAttackApplied = false;
    bool bFeetInitialized = false;
    FQueenTerrainPath Terrain;
    TArray<FVector> Path;
    TArray<FVector2D> SupportPolygon;
    int32 PathIndex = 0;
    int32 TourIndex = 0;
    float RepathTimer = 0;
    float PosePitch = 0, PoseRoll = 0, PoseHeight = 0;
    float PitchVelocity = 0, RollVelocity = 0, HeightVelocity = 0;
    float DamageImpulse = 0;
    bool bPathFound = false;
    void EnterState(EQueenState Next, float Duration);
    void UpdateDecision();
    void UpdateMovement(float Dt);
    void UpdateLegs(float Dt);
    void UpdateSupport(float Dt);
    void DetachLeg(int32 Index);
    bool SampleGround(FVector At, FVector& Point, FVector* Normal = nullptr) const;
    void UpdateAttack(float Dt);
    void UpdateShells(float Dt);
    bool CanSeeTarget() const;
    void Blast(const FVector& Center, float Radius, float Damage);
    FVector Hip(int32 Index) const;
    FVector RestFoot(int32 Index) const;
    void PlaceSegment(UStaticMeshComponent* Mesh, FVector A, FVector B, float Radius);
};
