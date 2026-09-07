#include "QueenBoss.h"
#include "QueenPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AQueenBoss::AQueenBoss()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    BodyFrame = CreateDefaultSubobject<USceneComponent>(TEXT("BodyFrame"));
    BodyFrame->SetupAttachment(Root);
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto Mesh = [&](FName Name, UStaticMesh* Shape)
    {
        UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        C->SetupAttachment(BodyFrame);
        C->SetStaticMesh(Shape);
        C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        C->SetCollisionResponseToAllChannels(ECR_Ignore);
        C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        return C;
    };
    Body = Mesh(TEXT("Carapace"), Sphere);
    Body->SetRelativeScale3D(FVector(6.5f, 5.f, 2.8f));
    Core = Mesh(TEXT("Core"), Sphere);
    Core->SetRelativeLocation(FVector(265.f, 0, -35.f));
    Core->SetRelativeScale3D(FVector(1.8f, 2.f, 1.8f));
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    for (int32 I = 0; I < 7; ++I)
    {
        UStaticMeshComponent* Plate = Mesh(FName(*FString::Printf(TEXT("ArmorPlate%d"), I)), Cube);
        Plate->SetRelativeLocation(FVector(-160 + (I % 4) * 95, I < 4 ? -140 : 140, 85));
        Plate->SetRelativeScale3D(FVector(1.25f, 2.1f, 0.6f));
        Plate->SetRelativeRotation(FRotator(0, 0, I < 4 ? -22 : 22));
        Plates.Add(Plate);
    }
    for (int32 I = 0; I < 6; ++I)
    {
        UpperLegs.Add(Mesh(FName(*FString::Printf(TEXT("Upper%d"), I)), Cylinder));
        LowerLegs.Add(Mesh(FName(*FString::Printf(TEXT("Lower%d"), I)), Cylinder));
        Joints.Add(Mesh(FName(*FString::Printf(TEXT("Joint%d"), I)), Sphere));
        Joints[I]->SetRelativeScale3D(FVector(0.85f));
    }
}
void AQueenBoss::BeginPlay()
{
    Super::BeginPlay();
    Target = Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
    UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Metal.M_Metal"));
    UMaterialInterface* Amber = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Amber.M_Amber"));
    if (Metal) Body->SetMaterial(0, Metal);
    for (UStaticMeshComponent* Plate : Plates) if (Metal) Plate->SetMaterial(0, Metal);
    if (Amber) Core->SetMaterial(0, Amber);
    for (int32 I = 0; I < 6; ++I)
    {
        if (Metal) { UpperLegs[I]->SetMaterial(0, Metal); LowerLegs[I]->SetMaterial(0, Metal); }
        if (Amber) Joints[I]->SetMaterial(0, Amber);
    }
    Terrain.Build(GetWorld(), this);
    UE_LOG(LogTemp, Display, TEXT("Terrain graph: %d walkable cells"), Terrain.WalkableCount());
}
int32 AQueenBoss::BrokenLegs() const
{
    int32 N = 0; for (const FLeg& L : Legs) if (L.Health <= 0.f) ++N; return N;
}
float AQueenBoss::ArmorAt(int32 I) const { return I >= 0 && I < 6 ? Legs[I].Armor : 0.f; }
float AQueenBoss::LegHealthAt(int32 I) const { return I >= 0 && I < 6 ? Legs[I].Health : 0.f; }
bool AQueenBoss::IsCoreExposed() const { return State == EQueenState::Stagger || State == EQueenState::Attack || BrokenLegs() >= 3; }
FString AQueenBoss::StateLabel() const
{
    switch (State)
    {
    case EQueenState::Idle: return TEXT("ACQUIRING");
    case EQueenState::Chase: return TEXT("REPOSITIONING");
    case EQueenState::Windup: return TEXT("CHARGING");
    case EQueenState::Attack: return TEXT("ATTACKING");
    case EQueenState::Recover: return TEXT("RECOVERING");
    case EQueenState::Stagger: return TEXT("STAGGERED");
    default: return TEXT("NEUTRALIZED");
    }
}
FString AQueenBoss::AttackLabel() const
{
    switch (CurrentAttack)
    {
    case QueenRules::Attack::Beam: return TEXT("BEAM / BREAK LINE OF SIGHT");
    case QueenRules::Attack::Mortar: return TEXT("MORTAR / LEAVE MARKED ZONES");
    case QueenRules::Attack::Pulse: return TEXT("PULSE / MOVE OUT OF RANGE");
    default: return TEXT("SCANNING");
    }
}
// 전투 상태 진입 시 타이머를 함께 갱신해 공격 예고/공격/회복의 순서를 유지한다.
void AQueenBoss::EnterState(EQueenState Next, float Duration)
{
    State = Next; StateRemaining = Duration; StateElapsed = 0; bAttackApplied = false;
    UE_LOG(LogTemp, Display, TEXT("Queen state: %s; %s"), *StateLabel(), *LastDecision);
}
bool AQueenBoss::CanSeeTarget() const
{
    if (!Target.IsValid() || Target->Health <= 0.f) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenSight), false, this);
    return GetWorld()->LineTraceSingleByChannel(Hit, Core->GetComponentLocation(), Target->GetActorLocation(), ECC_Visibility, Params)
        && Hit.GetActor() == Target.Get();
}
void AQueenBoss::UpdateDecision()
{
    if (bInspectionMode)
    {
        CurrentAttack = QueenRules::Attack::None;
        LastDecision = bTerrainTour ? TEXT("Terrain tour: sampled collision grid A*") : TEXT("Inspection: follows player without attacks");
        if (State != EQueenState::Chase) EnterState(EQueenState::Chase, 0);
        return;
    }
    bHasSight = CanSeeTarget();
    const float Distance = Target.IsValid() ? FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) : BIG_NUMBER;
    if (!Target.IsValid() || Target->Health <= 0.f)
    {
        LastDecision = TEXT("No living target");
        if (State != EQueenState::Idle) EnterState(EQueenState::Idle, 0);
        return;
    }
    CurrentAttack = QueenRules::SelectAttack(Distance, bHasSight, PulseCooldown <= 0, BeamCooldown <= 0, MortarCooldown <= 0);
    if (CurrentAttack == QueenRules::Attack::None)
    {
        LastDecision = !bHasSight ? TEXT("Sight blocked: close distance") : TEXT("Attacks cooling: reposition");
        if (State != EQueenState::Chase) EnterState(EQueenState::Chase, 0);
        return;
    }
    AimPoint = Target->GetActorLocation();
    LastDecision = FString::Printf(TEXT("Sight clear, distance %.0f cm: %s"), Distance, *AttackLabel());
    EnterState(EQueenState::Windup, CurrentAttack == QueenRules::Attack::Pulse ? 1.3f : 1.8f);
}
void AQueenBoss::Tick(float Dt)
{
    Super::Tick(Dt);
    if (State == EQueenState::Dead) return;
    if (!Target.IsValid()) Target = Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
    BeamCooldown = FMath::Max(0.f, BeamCooldown - Dt);
    MortarCooldown = FMath::Max(0.f, MortarCooldown - Dt);
    PulseCooldown = FMath::Max(0.f, PulseCooldown - Dt);
    StateElapsed += Dt;
    StateRemaining -= Dt;
    DecisionTimer -= Dt;
    if ((State == EQueenState::Idle || State == EQueenState::Chase) && DecisionTimer <= 0)
    { UpdateDecision(); DecisionTimer = 0.15f; }
    if (State == EQueenState::Windup && StateRemaining <= 0)
    {
        if (!Target.IsValid() || Target->Health <= 0) EnterState(EQueenState::Idle, 0);
        else
        {
            EnterState(EQueenState::Attack, CurrentAttack == QueenRules::Attack::Beam ? 2.f : 0.3f);
            if (CurrentAttack == QueenRules::Attack::Beam) BeamCooldown = 6.f;
            if (CurrentAttack == QueenRules::Attack::Mortar) MortarCooldown = 9.f;
            if (CurrentAttack == QueenRules::Attack::Pulse) PulseCooldown = 5.f;
        }
    }
    if (State == EQueenState::Attack) UpdateAttack(Dt);
    if (State == EQueenState::Attack && StateRemaining <= 0) EnterState(EQueenState::Recover, 1.6f);
    if ((State == EQueenState::Recover || State == EQueenState::Stagger) && StateRemaining <= 0) EnterState(EQueenState::Chase, 0);
    UpdateMovement(Dt);
    UpdateLegs(Dt);
    UpdateSupport(Dt);
    UpdateShells(Dt);
    if (State == EQueenState::Windup)
    {
        if (CurrentAttack == QueenRules::Attack::Beam)
            DrawDebugLine(GetWorld(), Core->GetComponentLocation(), AimPoint, FColor::Red, false, 0, 0, 3);
        if (CurrentAttack == QueenRules::Attack::Pulse)
            DrawDebugCircle(GetWorld(), GetActorLocation() - FVector(0, 0, 390), 1050, 64, FColor::Orange, false, 0, 0, 6, FVector::ForwardVector, FVector::RightVector, false);
    }
    if (bDebug)
    {
        DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 250), StateLabel(), nullptr, FColor::White, 0);
        if (Target.IsValid()) DrawDebugLine(GetWorld(), Core->GetComponentLocation(), Target->GetActorLocation(), bHasSight ? FColor::Green : FColor::Red, false, 0, 0, 1);
    }
}
void AQueenBoss::UpdateAttack(float Dt)
{
    if (CurrentAttack == QueenRules::Attack::Beam)
    {
        // Aim locks at telegraph start; players can evade it or use cover.
        FVector Start = Core->GetComponentLocation();
        FVector Direction = (AimPoint - Start).GetSafeNormal();
        FVector End = Start + Direction * 9000.f;
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenBeam), false, this);
        if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
        {
            End = Hit.ImpactPoint;
            if (AQueenPlayer* P = Cast<AQueenPlayer>(Hit.GetActor())) P->Hurt(35.f * Dt);
        }
        BeamEnd = End;
        DrawDebugLine(GetWorld(), Start, End, FColor(255, 70, 25), false, 0, 0, 20.f);
        DrawDebugSphere(GetWorld(), End, 45, 12, FColor::Orange, false, 0);
    }
    if (bAttackApplied) return;
    bAttackApplied = true;
    if (CurrentAttack == QueenRules::Attack::Pulse) Blast(GetActorLocation() - FVector(0, 0, 350), 1050.f, 40.f);
    if (CurrentAttack == QueenRules::Attack::Mortar)
    {
        for (int32 I = 0; I < 3; ++I)
        {
            FVector Point = AimPoint + FVector((I - 1) * 310.f, (I % 2) * 280.f, 0);
            FHitResult Hit;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(MortarGround), false, this);
            if (Target.IsValid()) Params.AddIgnoredActor(Target.Get());
            if (GetWorld()->LineTraceSingleByChannel(Hit, Point + FVector(0, 0, 1000), Point - FVector(0, 0, 1500), ECC_WorldStatic, Params)) Point = Hit.ImpactPoint;
            Shells.Add({Point, 1.7f + I * 0.22f});
        }
    }
}
void AQueenBoss::UpdateShells(float Dt)
{
    for (int32 I = Shells.Num() - 1; I >= 0; --I)
    {
        FShell& S = Shells[I]; S.Remaining -= Dt;
        DrawDebugCircle(GetWorld(), S.Target + FVector(0, 0, 8), 260, 48, FColor::Red, false, 0, 0, 5, FVector::ForwardVector, FVector::RightVector, false);
        DrawDebugLine(GetWorld(), S.Target + FVector(0, 0, FMath::Max(0.f, S.Remaining) * 1200), S.Target + FVector(0, 0, FMath::Max(0.f, S.Remaining) * 1200 + 100), FColor::Orange, false, 0, 0, 15);
        if (S.Remaining <= 0) { Blast(S.Target + FVector(0, 0, 25), 260, 32); Shells.RemoveAtSwap(I); }
    }
}
void AQueenBoss::Blast(const FVector& Center, float Radius, float Damage)
{
    DrawDebugSphere(GetWorld(), Center, Radius, 24, FColor::Orange, false, 0.3f, 0, 4);
    if (!Target.IsValid() || FVector::Dist(Center, Target->GetActorLocation()) > Radius) return;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenBlast), false, this);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Center, Target->GetActorLocation(), ECC_Visibility, Params) || Hit.GetActor() == Target.Get())
        Target->Hurt(Damage);
}
// 피격한 컴포넌트에서 다리/코어를 구분하고 장갑 소진 후 실제 체력을 차감한다.
void AQueenBoss::ReceiveShot(UPrimitiveComponent* Part, float Damage)
{
    if (State == EQueenState::Dead || Damage <= 0) return;
    float BodyDamage = Damage * 0.12f;
    if (Part == Core) BodyDamage = Damage * QueenRules::CoreMultiplier(IsCoreExposed());
    for (int32 I = 0; I < 6; ++I)
    {
        if (Part != Joints[I] && Part != UpperLegs[I] && Part != LowerLegs[I]) continue;
        const bool WasAlive = Legs[I].Health > 0;
        BodyDamage = QueenRules::LegDamage(Damage, Legs[I].Armor, Legs[I].Health);
        if (WasAlive && Legs[I].Health <= 0)
        {
            DetachLeg(I);
            BodyDamage += 130;
            LastDecision = FString::Printf(TEXT("Leg %d destroyed: interrupt attack, expose core"), I + 1);
            EnterState(EQueenState::Stagger, 3.f);
        }
        break;
    }
    Health = FMath::Max(0.f, Health - BodyDamage);
    if (Health <= 0)
    {
        LastDecision = TEXT("Health depleted"); Shells.Empty(); EnterState(EQueenState::Dead, 0);
        Body->SetRelativeRotation(FRotator(0, 0, 22));
    }
}

