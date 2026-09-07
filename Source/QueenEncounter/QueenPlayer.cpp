#include "QueenPlayer.h"
#include "QueenBoss.h"
#include "PhysicsQueen.h"
#include "QueenLearningMode.h"
#include "TraversalQueen.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

AQueenPlayer::AQueenPlayer()
{
    PrimaryActorTick.bCanEverTick = true;
    // Opt-in demo mode keeps idle inspection from silently disabling movement after death.
    bSandbox = FParse::Param(FCommandLine::Get(), TEXT("QueenSandbox"));
    GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 65));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 95.f;
    GetCharacterMovement()->MaxWalkSpeed = 620.f;
    GetCharacterMovement()->JumpZVelocity = 520.f;
}
void AQueenPlayer::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("Forward", this, &AQueenPlayer::Forward);
    Input->BindAxis("Right", this, &AQueenPlayer::Right);
    Input->BindAxis("Turn", this, &AQueenPlayer::Turn);
    Input->BindAxis("Look", this, &AQueenPlayer::Look);
    Input->BindAction("Fire", IE_Pressed, this, &AQueenPlayer::StartFire);
    Input->BindAction("Fire", IE_Released, this, &AQueenPlayer::StopFire);
    Input->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
    Input->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);
    Input->BindAction("Restart", IE_Pressed, this, &AQueenPlayer::RestartEncounter);
    Input->BindAction("Debug", IE_Pressed, this, &AQueenPlayer::ToggleDebug);
    Input->BindAction("Sprint", IE_Pressed, this, &AQueenPlayer::Sprint);
    Input->BindAction("Sprint", IE_Released, this, &AQueenPlayer::StopSprint);
    Input->BindAction("Inspect", IE_Pressed, this, &AQueenPlayer::Inspect);
    Input->BindAction("Tour", IE_Pressed, this, &AQueenPlayer::Tour);
    Input->BindAction("BreakLeg", IE_Pressed, this, &AQueenPlayer::BreakLeg);
    Input->BindAction("CollapseFront", IE_Pressed, this, &AQueenPlayer::CollapseFront);
}
void AQueenPlayer::Forward(float V) { if (Health > 0) AddMovementInput(FRotationMatrix(FRotator(0, GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), V); }
void AQueenPlayer::Right(float V) { if (Health > 0) AddMovementInput(FRotationMatrix(FRotator(0, GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V); }
void AQueenPlayer::Turn(float V) { AddControllerYawInput(V); }
void AQueenPlayer::Look(float V) { AddControllerPitchInput(V); }
void AQueenPlayer::StartFire() { bFiring = true; }
void AQueenPlayer::StopFire() { bFiring = false; }
void AQueenPlayer::Sprint() { GetCharacterMovement()->MaxWalkSpeed = 1000.f; }
void AQueenPlayer::StopSprint() { GetCharacterMovement()->MaxWalkSpeed = 620.f; }
void AQueenPlayer::Tick(float Dt)
{
    Super::Tick(Dt);
    FireCooldown -= Dt;
    HitConfirm = FMath::Max(0.f, HitConfirm - Dt);
    if (Health > 0 && bFiring && FireCooldown <= 0) { Shoot(); FireCooldown = 0.12f; }
}
// 카메라 방향으로 피격 부위를 조회한다. 발사 간격은 Tick에서 제한한다.
void AQueenPlayer::Shoot()
{
    FVector Start = Camera->GetComponentLocation();
    FVector End = Start + Camera->GetForwardVector() * 14000.f;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerShot), false, this);
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        End = Hit.ImpactPoint;
        if (AQueenBoss* Boss = Cast<AQueenBoss>(Hit.GetActor()))
        {
            if (Boss->Health > 0) { Boss->ReceiveShot(Hit.GetComponent(), 24.f); HitConfirm = 0.13f; }
        }
        if (auto* Queen=Cast<APhysicsQueen>(Hit.GetActor()))
            if (Queen->ReceivePlayerShot(Hit.GetComponent(),24.f)) HitConfirm=.13f;
        if (auto* Queen=Cast<ATraversalQueen>(Hit.GetActor()))
            if (Queen->ReceiveShot(Hit.GetComponent(),24.f)) HitConfirm=.13f;
        DrawDebugSphere(GetWorld(), End, 9.f, 8, FColor::Orange, false, 0.15f);
    }
    DrawDebugLine(GetWorld(), Start + Camera->GetRightVector() * 18.f - FVector(0, 0, 12), End, FColor(255, 215, 120), false, 0.055f, 0, 1.5f);
}
void AQueenPlayer::Hurt(float Damage)
{
    if (bSandbox) return;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) if (It->bInspectionMode) return;
    if (Health <= 0) return;
    Health = FMath::Max(0.f, Health - Damage);
    if (Health == 0) { bFiring = false; GetCharacterMovement()->DisableMovement(); }
}
// APawn::Restart와 이름을 분리하여 엔진의 플레이어 재시작 콜백과 충돌하지 않게 한다.
void AQueenPlayer::RestartEncounter()
{
    auto* Mode=Cast<AQueenLearningMode>(GetWorld()->GetAuthGameMode());
    const FString Options=Cast<AQueenTraversalMode>(GetWorld()->GetAuthGameMode())?TEXT("game=/Script/QueenEncounter.QueenTraversalMode"):Mode && Mode->bPlayerTest ? TEXT("game=/Script/QueenEncounter.QueenLearningMode") : TEXT("");
    UGameplayStatics::OpenLevel(this,FName(*UGameplayStatics::GetCurrentLevelName(this)),true,Options);
}
void AQueenPlayer::Inspect()
{
    bDebug = true;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) It->ToggleInspection();
}
void AQueenPlayer::Tour()
{
    bDebug = true;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) It->ToggleTour();
}
void AQueenPlayer::BreakLeg()
{
    bDebug = true;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) It->BreakNextLeg();
    for (TActorIterator<APhysicsQueen> It(GetWorld()); It; ++It)
        for (int32 I : {0,3,1,4,2,5}) if (!(It->Mask & (1<<I))) { It->SeverLeg(I); break; }
    for (TActorIterator<ATraversalQueen> It(GetWorld()); It; ++It)
        for (int32 I : {0,3,1,4,2,5}) if (!(It->Mask & (1<<I))) { It->SeverLeg(I); break; }
}
void AQueenPlayer::CollapseFront()
{
    bDebug = true;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) It->BreakFrontFour();
    for (TActorIterator<APhysicsQueen> It(GetWorld()); It; ++It)
        for (int32 I : {0,3,1,4}) It->SeverLeg(I);
    for (TActorIterator<ATraversalQueen> It(GetWorld()); It; ++It)
        for (int32 I : {0,3,1,4}) It->SeverLeg(I);
}
void AQueenPlayer::ToggleDebug()
{
    bDebug = !bDebug;
    for(TActorIterator<ATraversalQueen> It(GetWorld()); It; ++It) It->bShowDebug=!It->bShowDebug;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) It->bDebug = bDebug;
}

