#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "QueenPlayer.generated.h"
class UCameraComponent;
UCLASS()
class QUEENENCOUNTER_API AQueenPlayer : public ACharacter
{
    GENERATED_BODY()
public:
    AQueenPlayer();
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void Tick(float Dt) override;
    void Hurt(float Damage);
    float Health = 100.f;
    float HitConfirm = 0.f;
    bool bDebug = false;
    bool bSandbox = false;
private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    bool bFiring = false;
    float FireCooldown = 0.f;
    void Forward(float Value);
    void Right(float Value);
    void Turn(float Value);
    void Look(float Value);
    void StartFire();
    void StopFire();
    void Shoot();
    void RestartEncounter();
    void ToggleDebug();
    void Sprint();
    void StopSprint();
    void Inspect();
    void Tour();
    void BreakLeg();
    void CollapseFront();
};

