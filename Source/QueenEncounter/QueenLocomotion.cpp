#include "QueenBoss.h"
#include "QueenPlayer.h"
#include "SupportRules.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

// 기존 전투 데모의 절차적 IK 경로다. PhysicsQueen의 학습 기반 물리 보행과는 별도 구현이다.
bool AQueenBoss::SampleGround(FVector At, FVector& Point, FVector* Normal) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenGround), false, this);
    if (Target.IsValid()) Params.AddIgnoredActor(Target.Get());
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, 1800), FVector(At.X, At.Y, -500), ECC_WorldStatic, Params)) return false;
    Point = Hit.ImpactPoint;
    if (Normal) *Normal = Hit.ImpactNormal;
    return Hit.ImpactNormal.Z > 0.55f;
}

FVector AQueenBoss::CenterOfMass() const
{ return BodyFrame->GetComponentTransform().TransformPosition(FVector(65, 0, -20)); }

void AQueenBoss::ToggleInspection()
{
    bInspectionMode = !bInspectionMode;
    bDebug = true;
    Shells.Empty();
    if (State != EQueenState::Dead) EnterState(EQueenState::Chase, 0);
    if (!bInspectionMode) bTerrainTour = false;
}
void AQueenBoss::ToggleTour()
{
    bTerrainTour = !bTerrainTour;
    bInspectionMode = true;
    bDebug = true;
    RepathTimer = 0;
    Shells.Empty();
    if (State != EQueenState::Dead) EnterState(EQueenState::Chase, 0);
}
void AQueenBoss::BreakNextLeg()
{
    if (!bInspectionMode) ToggleInspection();
    for (int32 I : {0, 3, 1, 4, 2, 5})
    {
        if (Legs[I].Health > 0)
        { ReceiveShot(Joints[I], Legs[I].Armor + Legs[I].Health); break; }
    }
}
void AQueenBoss::BreakFrontFour()
{
    if (!bInspectionMode) ToggleInspection();
    for (int32 I : {0, 3, 1, 4})
        if (Legs[I].Health > 0) ReceiveShot(Joints[I], Legs[I].Armor + Legs[I].Health);
}
// 파괴된 다리는 지지 계산에서 제외하고 분리된 파편에 물리를 적용한다.
void AQueenBoss::DetachLeg(int32 I)
{
    FLeg& L = Legs[I];
    if (L.bDetached) return;
    L.bDetached = true;
    for (UStaticMeshComponent* C : {UpperLegs[I].Get(), LowerLegs[I].Get()})
    {
        C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        C->SetCollisionProfileName(TEXT("PhysicsActor"));
        C->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        C->SetSimulatePhysics(true);
        C->AddImpulse((C->GetComponentLocation() - GetActorLocation()).GetSafeNormal() * 250.f + FVector(0, 0, 150), NAME_None, true);
    }
    Joints[I]->SetVisibility(false);
    Joints[I]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RollVelocity += I < 3 ? 28.f : -28.f;
    PitchVelocity += I % 3 < 2 ? -22.f : 18.f;
    DamageImpulse = 1.f;
    RepathTimer = 0;
}

void AQueenBoss::UpdateMovement(float Dt)
{
    if (State == EQueenState::Dead) return;
    RepathTimer -= Dt;
    FVector Ground;
    if (SampleGround(GetActorLocation(), Ground))
    {
        FVector Location = GetActorLocation();
        Location.Z = FMath::FInterpTo(Location.Z, Ground.Z + 420.f, Dt, 5.f);
        SetActorLocation(Location);
    }
    if (BrokenLegs() >= 6 || State == EQueenState::Stagger) return;
    if (!Target.IsValid() && !bTerrainTour) return;
    FVector Goal = Target.IsValid() ? Target->GetActorLocation() : GetActorLocation();
    if (bTerrainTour)
    {
        static const FVector Tour[] = {
            FVector(-1900, 0, 420), FVector(600, 0, 650), FVector(1650, 0, 680),
            FVector(3400, 0, 420), FVector(3700, -2900, 420), FVector(1600, -3100, 420),
            FVector(-2000, -3000, 420), FVector(-2600, -700, 420)
        };
        Goal = Tour[TourIndex];
        if (FVector::Dist2D(GetActorLocation(), Goal) < 260.f)
        { TourIndex = (TourIndex + 1) % UE_ARRAY_COUNT(Tour); Goal = Tour[TourIndex]; RepathTimer = 0; }
    }
    const bool Moving = (State == EQueenState::Chase || bInspectionMode) &&
        (bTerrainTour || FVector::Dist2D(GetActorLocation(), Goal) > (bDragging ? 500.f : 1100.f));
    if (!Moving)
    {
        if (!bInspectionMode && State != EQueenState::Windup && State != EQueenState::Attack)
        {
            FRotator Desired(0, (Goal - GetActorLocation()).Rotation().Yaw, 0);
            SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, Dt, 24.f));
        }
        return;
    }
    if (RepathTimer <= 0)
    {
        bPathFound = Terrain.Find(GetActorLocation(), Goal, Path);
        PathIndex = FMath::Min(1, Path.Num() - 1);
        RepathTimer = 1.f;
    }
    if (!bPathFound || !Path.IsValidIndex(PathIndex)) { LastDecision = TEXT("No walkable terrain path: hold position"); return; }
    while (PathIndex < Path.Num() - 1 && FVector::Dist2D(GetActorLocation(), Path[PathIndex]) < 100.f) ++PathIndex;
    FVector Direction = (Path[PathIndex] - GetActorLocation()).GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return;
    FRotator Desired(0, Direction.Rotation().Yaw, 0);
    SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, Dt, bDragging ? 12.f : 30.f));
    const float Facing = FMath::Clamp(FVector::DotProduct(GetActorForwardVector(), Direction), 0.15f, 1.f);
    float Speed = FMath::Max(65.f, 225.f - BrokenLegs() * 26.f);
    // Only the power stroke advances a belly-supported body: reach, plant, pull, recover.
    if (bDragging) Speed = 18.f + 72.f * FMath::Square(FMath::Max(0.f, FMath::Sin(GaitClock * 3.4f)));
    if (BrokenLegs() == 5) Speed *= 0.35f;
    FVector Next = GetActorLocation() + Direction * Speed * Facing * Dt;
    FVector NextGround;
    if (SampleGround(Next, NextGround) && FMath::Abs(NextGround.Z - Ground.Z) < 145.f)
    {
        Next.Z = FMath::FInterpTo(Next.Z, NextGround.Z + 420.f, Dt, 5.f);
        SetActorLocation(Next);
    }
    if (bDebug)
        for (int32 I = PathIndex; I < Path.Num() - 1; ++I)
            DrawDebugLine(GetWorld(), Path[I] + FVector(0, 0, 15), Path[I + 1] + FVector(0, 0, 15), FColor(60, 180, 255), false, 0, 0, 2);
}

FVector AQueenBoss::Hip(int32 I) const
{
    const float X[3] = {210, 0, -210};
    return BodyFrame->GetComponentTransform().TransformPosition(FVector(X[I % 3], I < 3 ? 185 : -185, -5));
}
FVector AQueenBoss::RestFoot(int32 I) const
{
    const float X[3] = {580, 0, -570};
    const float Y[3] = {460, 680, 470};
    return GetActorTransform().TransformPosition(FVector(X[I % 3], (I < 3 ? 1 : -1) * Y[I % 3], -420));
}
void AQueenBoss::PlaceSegment(UStaticMeshComponent* Mesh, FVector A, FVector B, float Radius)
{
    Mesh->SetWorldLocation((A + B) * 0.5f);
    Mesh->SetWorldRotation(FRotationMatrix::MakeFromZ(B - A).Rotator());
    Mesh->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, FVector::Distance(A, B) / 100.f));
}
void AQueenBoss::UpdateLegs(float Dt)
{
    GaitClock += Dt;
    int32 Swinging = 0;
    for (const FLeg& L : Legs) if (L.Health > 0 && L.Alpha < 1) ++Swinging;
    GroundedFeet = 0;
    TArray<FVector2D> Contacts;
    for (int32 I = 0; I < 6; ++I)
    {
        FLeg& L = Legs[I];
        if (L.Health <= 0) continue;
        FVector Desired;
        L.bGroundValid = SampleGround(RestFoot(I), Desired);
        if (!L.bGroundValid) Desired = RestFoot(I);
        if (!bFeetInitialized) { L.Foot = Desired; L.Start = Desired; L.Goal = Desired; }
        const bool Phase = (int32(GaitClock / 0.44f) % 2) == (I % 2);
        const bool MayStep = BrokenLegs() == 0 ? Phase : Swinging == 0;
        const float Error = FVector::Dist2D(L.Foot, Desired);
        const bool TerrainChanged = FMath::Abs(L.Foot.Z - Desired.Z) > 45.f;
        if (L.Alpha >= 1 && L.bGroundValid && MayStep && (Error > (bDragging ? 65.f : 105.f) || TerrainChanged))
        {
            L.Start = L.Foot;
            L.Goal = Desired;
            L.Alpha = 0;
            ++Swinging;
        }
        if (L.Alpha < 1)
        {
            L.Alpha = FMath::Min(1.f, L.Alpha + Dt / (bDragging ? 0.65f : 0.34f));
            const float Smooth = L.Alpha * L.Alpha * (3 - 2 * L.Alpha);
            const float Lift = (bDragging ? 65.f : 120.f) + FMath::Abs(L.Goal.Z - L.Start.Z) * 0.45f;
            L.Foot = FMath::Lerp(L.Start, L.Goal, Smooth) + FVector(0, 0, FMath::Sin(L.Alpha * PI) * Lift);
        }
        const FVector H = Hip(I);
        FVector Out = (RestFoot(I) - GetActorLocation()).GetSafeNormal2D();
        FVector SolvedFoot = L.Foot;
        FVector Knee = QueenRules::SolveKnee(H, SolvedFoot, Out + FVector::UpVector);
        PlaceSegment(UpperLegs[I], H, Knee, 31);
        PlaceSegment(LowerLegs[I], Knee, SolvedFoot, 21);
        Joints[I]->SetWorldLocation(Knee);
        if (L.Alpha >= 1 && L.bGroundValid && FVector::Distance(SolvedFoot, L.Foot) < 5.f)
        { Contacts.Add(FVector2D(L.Foot)); ++GroundedFeet; }
        if (bDebug)
        {
            DrawDebugSphere(GetWorld(), L.Foot, 20, 8, L.Alpha < 1 ? FColor::Yellow : FColor::Cyan, false, 0);
            DrawDebugLine(GetWorld(), Desired + FVector(0, 0, 250), Desired, FColor::Cyan, false, 0, 0, 1);
            DrawDebugString(GetWorld(), Knee + FVector(0, 0, 60), FString::Printf(TEXT("%s %s"), I < 3 ? TEXT("L") : TEXT("R"), I % 3 == 0 ? TEXT("FRONT") : I % 3 == 1 ? TEXT("MID") : TEXT("REAR")), nullptr, FColor::White, 0);
        }
    }
    bFeetInitialized = true;
    SupportPolygon = QueenSupport::Hull(Contacts);
    SupportMargin = QueenSupport::Margin(SupportPolygon, FVector2D(CenterOfMass()));
}

// 접지한 발의 지지 영역과 무게중심을 비교하는 기존 데모의 자세 연출이다.
void AQueenBoss::UpdateSupport(float Dt)
{
    const int32 Alive = 6 - BrokenLegs();
    const bool RearOnly = Legs[0].Health <= 0 && Legs[1].Health <= 0 && Legs[3].Health <= 0 && Legs[4].Health <= 0 && Alive > 0;
    bDragging = Alive <= 2 || RearOnly;
    FVector CenterGround = GetActorLocation() - FVector(0, 0, 420);
    SampleGround(GetActorLocation(), CenterGround);
    FVector Front = CenterGround, Back = CenterGround, Left = CenterGround, Right = CenterGround;
    SampleGround(GetActorLocation() + GetActorForwardVector() * 240, Front);
    SampleGround(GetActorLocation() - GetActorForwardVector() * 240, Back);
    SampleGround(GetActorLocation() + GetActorRightVector() * 220, Left);
    SampleGround(GetActorLocation() - GetActorRightVector() * 220, Right);
    float DesiredPitch = FMath::RadiansToDegrees(FMath::Atan2(Front.Z - Back.Z, 480.f));
    float DesiredRoll = -FMath::RadiansToDegrees(FMath::Atan2(Left.Z - Right.Z, 440.f));
    float DesiredHeight = 0;
    FVector LocalSupport = FVector::ZeroVector;
    int32 SupportCount = 0;
    for (const FLeg& L : Legs)
    {
        if (L.Health <= 0 || !L.bGroundValid) continue;
        LocalSupport += GetActorTransform().InverseTransformPosition(L.Foot);
        ++SupportCount;
    }
    if (SupportCount) LocalSupport /= SupportCount;
    SupportLabel = TEXT("SUPPORTED");
    if (Alive < 6)
    {
        // Lean towards the unsupported side. The support hull exposes why balance is lost.
        DesiredRoll += FMath::Clamp(LocalSupport.Y * 0.045f, -25.f, 25.f);
        DesiredPitch += FMath::Clamp(LocalSupport.X * 0.04f, -22.f, 22.f);
        DesiredHeight -= BrokenLegs() * 18.f;
        SupportLabel = SupportMargin >= 20 ? TEXT("COMPENSATING") : TEXT("UNSTABLE");
        const float Wobble = SupportMargin < 0 ? 3.f : 1.f;
        DesiredRoll += FMath::Sin(GaitClock * 4.3f) * Wobble;
    }
    if (bDragging)
    {
        DesiredPitch += RearOnly ? -53.f : FMath::Clamp(LocalSupport.X * 0.09f, -48.f, 48.f);
        DesiredRoll += FMath::Clamp(LocalSupport.Y * 0.07f, -35.f, 35.f);
        DesiredHeight = -185.f + FMath::Sin(GaitClock * 3.4f) * 14.f;
        DesiredPitch += FMath::Sin(GaitClock * 3.4f) * 5.f;
        SupportLabel = RearOnly ? TEXT("FRONT COLLAPSE / REAR-LEG DRAG") : TEXT("BELLY SUPPORT / STRUGGLING");
    }
    if (Alive == 0)
    {
        DesiredHeight = -260.f;
        DesiredPitch = -12;
        DesiredRoll = 18;
        SupportLabel = TEXT("IMMOBILIZED");
    }
    // Terrain/body clearance: prevent the nose from sinking below the sampled contact plane.
    const float NoseRelativeZ = FMath::Sin(FMath::DegreesToRadians(DesiredPitch)) * 290.f;
    const float NoseGroundZ = Front.Z;
    DesiredHeight = FMath::Max(DesiredHeight, NoseGroundZ + 90.f - GetActorLocation().Z - NoseRelativeZ);
    QueenSupport::Spring(DesiredPitch, PosePitch, PitchVelocity, Dt, bDragging ? 1.4f : 2.2f, 0.65f);
    QueenSupport::Spring(DesiredRoll, PoseRoll, RollVelocity, Dt, 1.8f, 0.68f);
    QueenSupport::Spring(DesiredHeight, PoseHeight, HeightVelocity, Dt, 1.6f, 0.8f);
    BodyFrame->SetRelativeLocation(FVector(0, 0, PoseHeight));
    BodyFrame->SetRelativeRotation(FRotator(PosePitch, 0, PoseRoll));
    DamageImpulse = FMath::Max(0.f, DamageImpulse - Dt);
    if (bDebug)
    {
        const FVector COM = CenterOfMass();
        const FColor Color = SupportMargin >= 0 ? FColor::Green : FColor::Red;
        DrawDebugSphere(GetWorld(), COM, 30, 12, Color, false, 0, 0, 3);
        DrawDebugLine(GetWorld(), COM, FVector(COM.X, COM.Y, CenterGround.Z + 20), Color, false, 0, 0, 3);
        for (int32 I = 0; I < SupportPolygon.Num(); ++I)
        {
            FVector2D A = SupportPolygon[I], B = SupportPolygon[(I + 1) % SupportPolygon.Num()];
            DrawDebugLine(GetWorld(), FVector(A.X, A.Y, CenterGround.Z + 20), FVector(B.X, B.Y, CenterGround.Z + 20), Color, false, 0, 0, 3);
        }
    }
}
