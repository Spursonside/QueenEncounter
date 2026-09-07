#include "PhysicsQueen.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialInterface.h"

APhysicsQueen::APhysicsQueen()
{
    PrimaryActorTick.bCanEverTick = false;
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhysicalBody"));
    SetRootComponent(Body);
    Body->SetStaticMesh(Cube);
    Body->SetWorldScale3D(FVector(3.8, 2.2, 0.9));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetVisibility(false);
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Cephalothorax = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cephalothorax"));
    Abdomen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Abdomen"));
    for (UStaticMeshComponent* Shell : {Cephalothorax.Get(), Abdomen.Get()})
    {
        Shell->SetupAttachment(Body);
        Shell->SetStaticMesh(Sphere);
        Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Shell->SetUsingAbsoluteScale(true);
    }
    // 갑각 가장자리의 분할 장갑과 전방 렌즈는 몸통에 붙는 외형이다. 물리 바디를 추가하지 않는다.
    for (int32 I=0; I<14; ++I)
    {
        auto* Panel = CreateDefaultSubobject<UStaticMeshComponent>(FName(*FString::Printf(TEXT("CarapacePanel_%d"), I)));
        Panel->SetupAttachment(Body); Panel->SetStaticMesh(Cube);
        Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision); Panel->SetUsingAbsoluteScale(true);
        ArmorRim.Add(Panel);
    }
    CoreLens = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CoreLens"));
    CoreLens->SetupAttachment(Body); CoreLens->SetStaticMesh(Sphere);
    CoreLens->SetCollisionEnabled(ECollisionEnabled::NoCollision); CoreLens->SetUsingAbsoluteScale(true);
    for (int32 I = 0; I < 6; ++I)
    {
        auto Segment = [&](const FString& Name)
        {
            UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(FName(*Name));
            C->SetupAttachment(Body);
            C->SetStaticMesh(Cube);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            return C;
        };
        Coxae.Add(Segment(FString::Printf(TEXT("Coxa_%d"), I)));
        CoxaJoints.Add(CreateDefaultSubobject<UPhysicsConstraintComponent>(FName(*FString::Printf(TEXT("CoxaYaw_%d"), I))));
        CoxaJoints[I]->SetupAttachment(Body);
        Upper.Add(Segment(FString::Printf(TEXT("Upper_%d"), I)));
        Lower.Add(Segment(FString::Printf(TEXT("Lower_%d"), I)));
        Shins.Add(Segment(FString::Printf(TEXT("Shin_%d"), I)));
        Feet.Add(Segment(FString::Printf(TEXT("Foot_%d"), I)));
        CalfJoints.Add(CreateDefaultSubobject<UPhysicsConstraintComponent>(FName(*FString::Printf(TEXT("CalfJoint_%d"), I))));
        Ankles.Add(CreateDefaultSubobject<UPhysicsConstraintComponent>(FName(*FString::Printf(TEXT("Ankle_%d"), I))));
        CalfJoints[I]->SetupAttachment(Body);
        Ankles[I]->SetupAttachment(Body);
        Hips.Add(CreateDefaultSubobject<UPhysicsConstraintComponent>(FName(*FString::Printf(TEXT("Hip_%d"), I))));
        Knees.Add(CreateDefaultSubobject<UPhysicsConstraintComponent>(FName(*FString::Printf(TEXT("Knee_%d"), I))));
        Hips[I]->SetupAttachment(Body);
        Knees[I]->SetupAttachment(Body);
        for (int32 J=0; J<3; ++J)
        {
            auto* Housing = CreateDefaultSubobject<UStaticMeshComponent>(FName(*FString::Printf(TEXT("JointHousing_%d_%d"), I, J)));
            Housing->SetupAttachment(J==0 ? Coxae[I].Get() : J==1 ? Upper[I].Get() : Lower[I].Get());
            Housing->SetStaticMesh(Sphere); Housing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Housing->SetUsingAbsoluteScale(true); JointHousings.Add(Housing);
        }
    }
}
void APhysicsQueen::SetJoint(UPhysicsConstraintComponent* Joint, UStaticMeshComponent* A, UStaticMeshComponent* B, FVector Position, float Yaw, float Limit)
{
    // 위치 자유도는 잠그고 한 축의 회전만 허용한다. 목표 각도에 도달하는 운동은 Chaos가 계산한다.
    Joint->SetWorldLocation(Position);
    Joint->SetWorldRotation(FRotator(0, Yaw, 0));
    Joint->SetLinearXLimit(LCM_Locked, 0);
    Joint->SetLinearYLimit(LCM_Locked, 0);
    Joint->SetLinearZLimit(LCM_Locked, 0);
    Joint->SetAngularSwing1Limit(ACM_Locked, 0);
    Joint->SetAngularSwing2Limit(ACM_Limited, Limit);
    Joint->SetAngularTwistLimit(ACM_Locked, 0);
    Joint->SetDisableCollision(true);
    Joint->SetConstrainedComponents(A, NAME_None, B, NAME_None);
    Joint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);
    Joint->SetOrientationDriveTwistAndSwing(false, true);
    Joint->SetAngularVelocityDriveTwistAndSwing(false, true);
    Joint->SetAngularDriveAccelerationMode(true);
    const float Gain = bStructuredGait ? 80000.f + Genes[37]*40000.f : 4000.f + (Genes[37] + 1.f) * 6000.f;
    Joint->SetAngularDriveParams(Gain, bStructuredGait ? 1200.f : 300.f, 100000000.f * FMath::Pow(TargetHeight / 270.f, 5.f));
}
// 위치/회전 직접 설정은 에피소드 초기화에서만 수행한다. 실행 중 보행은 관절 모터만 사용한다.
void APhysicsQueen::Configure(FVector Origin, const TArray<float>& Parameters, int32 MissingMask)
{
    Genes = Parameters; Genes.SetNumZeroed(GeneCount);
    Mask = MissingMask;
    Start = Origin;
    FurthestX = Origin.X; FilteredSpeed = 0;
    const float Scale = TargetHeight / 270.f;
    // 구형 발의 증가한 높이만큼 발목을 올려 초기 지면 접촉 높이를 유지한다.
    const float LowerDrop = bLegacyFeet ? 395.f : 356.f;
    Body->SetWorldScale3D(FVector(4.6, 5.2, 1.1) * Scale);
    HeightMean = HeightM2 = HeightTime = 0;
    FootSlipIntegral = FootContactTime = 0;
    HeightIntegral = HeightSquared = HeightError = HeightMotion = SpeedCost = 0;
    CurrentHeight = PreviousHeight = TargetHeight;
    bReachedGoal = false; bFailed = false;
    FailureReason.Empty(); StepTravelSum=StepTravelMax=0.f;
    Clock = UprightTime = Effort = 0;
    TenMetreTime=-1.f; CruiseDistance=CruiseSeconds=CruiseTargetSeconds=0.f; BodySpeedSamples.Empty(); SpeedBinDistance=SpeedBinSeconds=0.f;
    TiltIntegral = RotationIntegral = TiltCost = RotationCost = MeasuredTime = StableProgress = 0;
    PreviousPosition = Origin;
    Body->SetWorldLocation(Origin);
    Body->SetWorldRotation(FRotator::ZeroRotator);
    Cephalothorax->SetWorldLocation(Origin + FVector(0,0,30)*Scale);
    Cephalothorax->SetWorldRotation(FRotator::ZeroRotator);
    Cephalothorax->SetWorldScale3D(FVector(4.9,5.4,1.4)*Scale);
    Abdomen->SetWorldLocation(Origin + FVector(0,0,-65)*Scale);
    Abdomen->SetWorldRotation(FRotator::ZeroRotator);
    Abdomen->SetWorldScale3D(FVector(3.8,4.1,0.9)*Scale);
    UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Metal.M_Metal"));
    UMaterialInterface* Amber = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Amber.M_Amber"));
    UMaterialInterface* Armor = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_QueenArmor.M_QueenArmor"));
    UMaterialInterface* Red = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_QueenRed.M_QueenRed"));
    if (!Armor) Armor = Metal;
    if (Metal) { Body->SetMaterial(0, Metal); Cephalothorax->SetMaterial(0, Armor); Abdomen->SetMaterial(0, Metal); }
    for (int32 I=0; I<ArmorRim.Num(); ++I)
    {
        const float A=I*2.f*PI/10.f;
        ArmorRim[I]->SetWorldLocation(Origin+FVector(215*FMath::Cos(A),235*FMath::Sin(A),30)*Scale);
        ArmorRim[I]->SetWorldRotation(FRotator(0,FMath::RadiansToDegrees(A),0));
        ArmorRim[I]->SetWorldScale3D(FVector(.48,1.35,.55)*Scale);
        if (Armor) ArmorRim[I]->SetMaterial(0,Armor);
        if (I>=10)
        {
            // 갑각 상부의 낮은 기계 덮개는 원반이 지나치게 매끈하게 보이지 않게 한다.
            const int32 J=I-10;
            ArmorRim[I]->SetWorldLocation(Origin+FVector(J<2 ? -85 : 85, J%2 ? -95 : 95,85)*Scale);
            ArmorRim[I]->SetWorldRotation(FRotator::ZeroRotator);
            ArmorRim[I]->SetWorldScale3D(FVector(1.4,.7,.25)*Scale);
            if (Metal) ArmorRim[I]->SetMaterial(0,Metal);
        }
    }
    CoreLens->SetWorldLocation(Origin+FVector(208,0,-30)*Scale);
    CoreLens->SetWorldScale3D(FVector(.4,.8,.42)*Scale);
    if (Red) CoreLens->SetMaterial(0,Red);
    UPhysicalMaterial* Friction = NewObject<UPhysicalMaterial>(this);
    Friction->Friction = 0.9f;
    Friction->Restitution = 0.f;
    // 발만 고마찰 재질을 사용한다. Max 결합으로 지형의 낮은 마찰값에 희석되지 않게 한다.
    UPhysicalMaterial* FootGrip = NewObject<UPhysicalMaterial>(this);
    FootGrip->Friction = 3.f;
    FootGrip->StaticFriction = 4.f;
    FootGrip->bOverrideFrictionCombineMode = true;
    FootGrip->FrictionCombineMode = EFrictionCombineMode::Max;
    FootGrip->Restitution = 0.f;
    auto Physics = [&](UStaticMeshComponent* C, float Mass)
    {
        // 후보끼리 충돌하지 않으므로 동일 지형/동일 출발점에서 공정하게 병렬 평가할 수 있다.
        C->SetCollisionProfileName(TEXT("PhysicsActor"));
        C->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
        C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        C->SetPhysMaterialOverride(Friction);
        C->SetMassOverrideInKg(NAME_None, Mass * FMath::Pow(Scale, 3.f), true);
        C->SetLinearDamping(0.2f);
        C->SetAngularDamping(1.f);
        C->BodyInstance.PositionSolverIterationCount = FastGait()?32:12;
        C->BodyInstance.VelocitySolverIterationCount = FastGait()?12:4;
        C->SetSimulatePhysics(true);
    };
    // 넓은 갑각 양옆에 세 다리를 분산한다. 짧고 굵은 상부와 높은 무릎으로 중장갑 실루엣을 만든다.
    const float X[3] = {140, 0, -140};
    for (int32 I = 0; I < 6; ++I)
    {
        const float Side = I < 3 ? 1.f : -1.f;
        const float Yaw = Side * (45.f + (I % 3) * 45.f);
        const FVector Radial(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0);
        const FVector Mount = Origin + FVector(X[I % 3], 210.f * Side, 0) * Scale;
        const FVector Hip = Mount + Radial * 55.f * Scale;
        const FVector Knee = Hip + (Radial * 230.f + FVector(0, 0, 130)) * Scale;
        const FVector Foot = Knee + (Radial * 130.f - FVector(0, 0, LowerDrop)) * Scale;
        auto Place = [&](UStaticMeshComponent* C, FVector A, FVector B, float Width)
        {
            C->SetWorldLocation((A + B) / 2);
            C->SetWorldRotation(FRotationMatrix::MakeFromZ(B - A).Rotator());
            C->SetWorldScale3D(FVector(Width * Scale / 100, Width * Scale / 100, FVector::Distance(A, B) / 100));
        };
        Place(Coxae[I], Mount, Hip, 75);
        Place(Upper[I], Hip, Knee, 70);
        // 기존 종아리를 두 물리 바디로 나누고 중간에 굽힘 관절을 배치한다.
        const FVector Calf = FMath::Lerp(Knee, Foot, 0.55f) + Radial * 20.f * Scale;
        Place(Lower[I], Knee, Calf, 56);
        Place(Shins[I], Calf, Foot, 40);
        // 발목은 구의 상단에 연결한다. 균일 스케일과 실제 구형 단순 충돌을 사용한다.
        Feet[I]->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,bLegacyFeet?TEXT("/Engine/BasicShapes/Cube.Cube"):TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Feet[I]->SetWorldLocation(Foot - FVector(0, 0, (bLegacyFeet ? 8.f : 27.5f) * Scale));
        Feet[I]->SetWorldRotation(FRotator(0, Yaw, 0));
        Feet[I]->SetWorldScale3D((bLegacyFeet ? FVector(1.05,0.85,0.16) : FVector(0.55,0.55,0.55)) * Scale);
        if (Metal) { Upper[I]->SetMaterial(0, Metal); Coxae[I]->SetMaterial(0, Metal); }
        if (Armor) { Upper[I]->SetMaterial(0, Armor); Lower[I]->SetMaterial(0, Armor); }
        if (Metal) Shins[I]->SetMaterial(0, Metal);
        if (Metal) Feet[I]->SetMaterial(0, Metal);
        const FVector HousingLocations[3]={Hip,Knee,Calf};
        const float HousingWidths[3]={85,82,62};
        for (int32 J=0; J<3; ++J)
        {
            auto* H=JointHousings[I*3+J].Get();
            H->SetWorldLocation(HousingLocations[J]); H->SetWorldRotation(FRotator::ZeroRotator);
            H->SetWorldScale3D(FVector(HousingWidths[J]/100.f)*Scale);
            if (Metal) H->SetMaterial(0,Metal);
            H->SetVisibility(!(Mask & (1 << I)));
        }
        if (Mask & (1 << I)) { Coxae[I]->SetVisibility(false); Upper[I]->SetVisibility(false); Lower[I]->SetVisibility(false); Shins[I]->SetVisibility(false); Feet[I]->SetVisibility(false); continue; }
        Physics(Coxae[I], 4.f);
        Physics(Upper[I], 12.f);
        Physics(Lower[I], 8.f);
        Physics(Shins[I], 6.f);
        Physics(Feet[I], 2.f);
        if (bStructuredGait) Feet[I]->SetAngularDamping(8.f);
        if (!bLegacyFeet) Feet[I]->SetPhysMaterialOverride(FootGrip);
    }
    Physics(Body, 110.f);
    Body->SetCenterOfMass(FVector(0, 0, -20.f * Scale));
    for (int32 I = 0; I < 6; ++I)
    {
        if (Mask & (1 << I)) continue;
        const float Side = I < 3 ? 1.f : -1.f;
        const float Yaw = Side * (45.f + (I % 3) * 45.f);
        const FVector Radial(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0);
        const FVector Mount = Origin + FVector(X[I % 3], 210.f * Side, 0) * Scale;
        FVector Hip = Mount + Radial * 55.f * Scale;
        FVector Knee = Hip + (FVector(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0) * 230 + FVector(0, 0, 130)) * Scale;
        // 몸통 가까운 짧은 기부는 수평 회전, 다음 관절은 수직 들기를 담당한다.
        SetJoint(CoxaJoints[I], Body, Coxae[I], Mount, Yaw, 0.f);
        CoxaJoints[I]->SetAngularSwing2Limit(ACM_Locked, 0);
        CoxaJoints[I]->SetAngularSwing1Limit(ACM_Limited, 40.f);
        SetJoint(Hips[I], Coxae[I], Upper[I], Hip, Yaw, 50.f);
        SetJoint(Knees[I], Upper[I], Lower[I], Knee, Yaw, 65.f);
        const FVector Foot = Knee + (Radial * 130.f - FVector(0,0,LowerDrop)) * Scale;
        const FVector Calf = FMath::Lerp(Knee, Foot, 0.55f) + Radial * 20.f * Scale;
        SetJoint(CalfJoints[I], Lower[I], Shins[I], Calf, Yaw, 35.f);
        SetJoint(Ankles[I], Shins[I], Feet[I], Foot, Yaw, 35.f);
        // 발목은 앞뒤 굽힘에 더해 좌우 기울기도 허용한다. 부드러운 모터로 지면 접촉에 순응한다.
        Ankles[I]->SetAngularTwistLimit(ACM_Limited, 20.f);
        Ankles[I]->SetOrientationDriveTwistAndSwing(true, true);
        Ankles[I]->SetAngularVelocityDriveTwistAndSwing(true, true);
        Ankles[I]->SetAngularDriveParams(bPlantFeet ? 12000.f : bStructuredGait ? 2500.f : 120.f, bPlantFeet ? 800.f : bStructuredGait ? 250.f : 35.f, 10000000.f * FMath::Pow(Scale, 5.f));
    }
    int32 ActiveBodies = Body->IsSimulatingPhysics() ? 1 : 0, ActiveJoints = 0;
    for (int32 I=0; I<6; ++I)
    {
        for (UStaticMeshComponent* Part : {Coxae[I].Get(), Upper[I].Get(), Lower[I].Get(), Shins[I].Get(), Feet[I].Get()})
            ActiveBodies += Part->IsSimulatingPhysics() ? 1 : 0;
        for (UPhysicsConstraintComponent* Joint : {CoxaJoints[I].Get(), Hips[I].Get(), Knees[I].Get(), CalfJoints[I].Get(), Ankles[I].Get()})
            ActiveJoints += Joint->ConstraintInstance.IsValidConstraintInstance() ? 1 : 0;
    }
    const int32 ActiveLegs = 6 - FMath::CountBits(uint32(Mask));
    ensureAlwaysMsgf(ActiveBodies == 1+ActiveLegs*5 && ActiveJoints == ActiveLegs*5, TEXT("Invalid articulated rig"));
    const int32 SphereShapes = Feet[0]->GetBodySetup() ? Feet[0]->GetBodySetup()->AggGeom.SphereElems.Num() : 0;
    ensureAlwaysMsgf(bLegacyFeet || SphereShapes==1,TEXT("Foot must use actual spherical collision"));
    UE_LOG(LogTemp,Display,TEXT("QUEEN_SPHERE_FOOT collision_spheres=%d diameter_cm=%.2f"),SphereShapes,55.f*Scale);
    UE_LOG(LogTemp,Display,TEXT("QUEEN_FOOT_SETUP legacy=%d length_cm=%.2f width_cm=%.2f friction=%.1f static=%.1f"),bLegacyFeet,(bLegacyFeet?105.f:55.f)*Scale,(bLegacyFeet?85.f:55.f)*Scale,bLegacyFeet?Friction->Friction:FootGrip->Friction,bLegacyFeet?Friction->StaticFriction:FootGrip->StaticFriction);
    UE_LOG(LogTemp, Display, TEXT("QUEEN_RIG_V10 active_bodies=%d active_constraints=%d genes=%d"), ActiveBodies, ActiveJoints, Genes.Num());
}
void APhysicsQueen::StepController(float Dt)
{
    if (bFailed && !bFailureFrozen)
    {
        // 실패 후에도 몸체가 계속 추락하거나 폭주하지 않도록 마지막 측정 위치에 정지한다.
        // 실패 개체는 보행 성공/부모 후보로 사용하지 않는다.
        Body->SetSimulatePhysics(false);
        for (int32 I=0; I<6; ++I)
            for (UStaticMeshComponent* Part : {Coxae[I].Get(), Upper[I].Get(), Lower[I].Get(), Shins[I].Get(), Feet[I].Get()})
                Part->SetSimulatePhysics(false);
        bFailureFrozen = true;
    }
    if (bFailed || bReachedGoal) return;
    Clock += Dt;
    const FVector Up = Body->GetUpVector();
    const FVector P = Body->GetComponentLocation();
    float GroundZ = 0;
    if (bTerrainTest)
    {
        // 중앙+몸통 앞뒤/좌우 5점을 평균해 작은 요철 하나에 높이 측정이 튀지 않게 한다.
        FCollisionQueryParams Query(SCENE_QUERY_STAT(QueenTerrainHeight), false, this);
        for (FVector Offset : {FVector::ZeroVector, FVector(300,0,0), FVector(-300,0,0), FVector(0,180,0), FVector(0,-180,0)})
        {
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByObjectType(Hit, P + Offset + FVector(0,0,10000), P + Offset - FVector(0,0,10000), FCollisionObjectQueryParams(ECC_WorldStatic), Query))
            { FailureReason=TEXT("body_ground_trace_missed"); bFailed = true; return; }
            GroundZ += Hit.ImpactPoint.Z / 5.f;
        }
    }
    CurrentHeight = P.Z - GroundZ;
    if (bStructuredGait) MeasureGait(Dt);
    if (Up.Z > 0.7 && CurrentHeight > TargetHeight * 0.65f) UprightTime += Dt;
    if ((bStructuredGait && Clock>3.f && Up.Z<.5f) || P.ContainsNaN() || CurrentHeight < 100 || FMath::Abs(P.X - Start.X) > 15000 || FMath::Abs(P.Y - Start.Y) > 5000)
    { FailureReason=TEXT("body_pose_or_bounds"); bFailed = true; return; }
    // 초기 착지 2초는 안정성 측정에서 제외한다. 중력 기준 기울기로 Euler 각도의 뒤집힘 문제를 피한다.
    if (Clock > 2.f)
    {
        // 지면과 발 충돌면 사이가 5cm 이내인 순간의 접선 속도를 측정한다.
        // 실제 접촉 임펄스가 아닌 근접 접지 추정치이며, 공중의 발 속도는 제외한다.
        for (int32 I=0; I<6; ++I)
        {
            if (Mask & (1<<I)) continue;
            const FVector Center=Feet[I]->GetComponentLocation();
            FHitResult Ground;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(QueenFootSlip),false,this);
            if (GetWorld()->LineTraceSingleByObjectType(Ground,Center,Center-FVector(0,0,400),FCollisionObjectQueryParams(ECC_WorldStatic),Query))
            {
                FVector Closest;
                const float Distance=Feet[I]->GetClosestPointOnCollision(Ground.ImpactPoint,Closest);
                if (Distance>=0 && Distance<=5.f)
                {
                    const FVector Velocity=Feet[I]->GetPhysicsLinearVelocityAtPoint(Ground.ImpactPoint);
                    const FVector Tangent=Velocity-Ground.ImpactNormal*FVector::DotProduct(Velocity,Ground.ImpactNormal);
                    FootSlipIntegral+=Tangent.Size()/100.f*Dt;
                    FootContactTime+=Dt;
                }
            }
        }
        const float Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(Up.Z), -1.f, 1.f)));
        const float Rotation = Body->GetPhysicsAngularVelocityInDegrees().Size();
        MeasuredTime += Dt;
        const float HeightM = CurrentHeight / 100.f;
        const float HeightDeltaSpeed = (CurrentHeight - PreviousHeight) / (100.f * Dt);
        const float Speed = (P.X - PreviousPosition.X) / (100.f * Dt);
        const float Before=(PreviousPosition.X-Start.X)/100.f, After=(P.X-Start.X)/100.f;
        if (TenMetreTime<0.f && Before<10.f && After>=10.f)
            TenMetreTime=Clock-Dt+Dt*(10.f-Before)/FMath::Max(After-Before,.001f);
        if (Clock>3.f)
        {
            SpeedBinDistance+=After-Before; SpeedBinSeconds+=Dt;
            if (SpeedBinSeconds>=1.f) { BodySpeedSamples.Add(SpeedBinDistance/SpeedBinSeconds); SpeedBinDistance=SpeedBinSeconds=0.f; }
        }
        if (TenMetreTime>=0.f)
        {
            const float Slice=FMath::Min(Dt,Clock-TenMetreTime);
            CruiseDistance+=Speed*Slice; CruiseSeconds+=Slice;
            if (Speed>=4.5f && Speed<=5.5f) CruiseTargetSeconds+=Slice;
        }
        // 장시간 측정의 분산 계산에서 수치 오차를 줄이는 가중 Welford 방식이다.
        HeightTime += Dt;
        const double DeltaHeight = HeightM - HeightMean;
        HeightMean += Dt * DeltaHeight / HeightTime;
        HeightM2 += Dt * DeltaHeight * (HeightM - HeightMean);
        HeightIntegral += HeightM * Dt;
        HeightSquared += HeightM * HeightM * Dt;
        HeightError += FMath::Square(HeightM - TargetHeight / 100.f) * Dt;
        HeightMotion += FMath::Min(FMath::Square(HeightDeltaSpeed), 25.f) * Dt;
        FilteredSpeed += Dt / (0.5f + Dt) * (Speed - FilteredSpeed);
        SpeedCost += FMath::Min(FMath::Square((FilteredSpeed - TargetSpeed) / TargetSpeed), 25.f) * Dt;
        TiltIntegral += Tilt * Dt;
        RotationIntegral += Rotation * Dt;
        TiltCost += FMath::Min(FMath::Square(Tilt / 30.f), 9.f) * Dt;
        RotationCost += FMath::Min(FMath::Square(Rotation / 90.f), 9.f) * Dt;
        // 넘어진 채 미끄러지거나 구르는 전진은 보상하지 않는다. 후퇴는 항상 손해로 계산한다.
        const float Stability = FMath::Clamp(1.f - Tilt / 70.f, 0.f, 1.f) * FMath::Clamp(1.f - FMath::Abs(HeightM - TargetHeight / 100.f) / 2.f, 0.f, 1.f);
        // 이전 최전방 지점을 넘어선 거리만 보상하므로 제자리 왕복으로 보상을 쌓을 수 없다.
        const float NewProgress = FMath::Max(0.f, float(P.X) - FurthestX) / 100.f;
        StableProgress += FMath::Min(NewProgress, TargetSpeed * 1.2f * Dt) * Stability;
        FurthestX = FMath::Max(FurthestX, float(P.X));
    }
    PreviousPosition = P;
    PreviousHeight = CurrentHeight;
    // 누적 왕복 거리가 아닌 시작점에서 전방 목표 거리(기본 50m)를 통과해야 한다. 넘어진 상태의 통과는 성공이 아니다.
    if ((!bStructuredGait || PassedStage()) && (P.X - Start.X) / 100.f >= GoalMetres && FMath::Abs(P.Y - Start.Y) < 1000 && Up.Z > 0.8f && FMath::Abs(CurrentHeight - TargetHeight) < 150)
    { GoalPosition = P; GoalHeading = Body->GetForwardVector().X; bReachedGoal = true; GoalDistance = (P.X - Start.X) / 100.f; return; }
    if (bStructuredGait) { DriveGait(Dt); return; }
    // CPG의 진폭/위상/편향/주파수와 자세 피드백 계수가 유전자다. 관절 제한은 최종 출력에도 적용한다.
    const float Frequency = 0.12f + (Genes[36] + 1.f) * 0.19f;
    const float Phase = FMath::Max(0.f, Clock - 2.f) * 2.f * PI * Frequency;
    const float Ramp = FMath::Clamp((Clock - 2.f) / 2.f, 0.f, 1.f);
    const float Pitch = Body->GetComponentRotation().Pitch;
    const float Roll = Body->GetComponentRotation().Roll;
    for (int32 I = 0; I < 6; ++I)
    {
        if (Mask & (1 << I)) continue;
        const float Feedback = Genes[38] * Pitch * 0.25f + Genes[39] * Roll * (I < 3 ? 0.2f : -0.2f);
        const float HipAngle = FMath::Clamp(Ramp * (Genes[I] * 35.f * FMath::Sin(Phase + Genes[12 + I] * PI) + Genes[24 + I] * 20.f + Feedback), -48.f, 48.f);
        const float KneeAngle = FMath::Clamp(Ramp * (Genes[6 + I] * 45.f * FMath::Sin(Phase + Genes[18 + I] * PI) + Genes[30 + I] * 25.f), -62.f, 62.f);
        const float SweepAngle = FMath::Clamp(Ramp*(Genes[76+I]*25.f*FMath::Sin(Phase+Genes[82+I]*PI)+Genes[88+I]*10.f), -37.f, 37.f);
        CoxaJoints[I]->SetAngularOrientationTarget(FRotator(0, SweepAngle, 0));
        Effort += FMath::Abs(SweepAngle-LastTargets[24+I]);
        LastTargets[24+I] = SweepAngle;
        Hips[I]->SetAngularOrientationTarget(FRotator(HipAngle, 0, 0));
        Knees[I]->SetAngularOrientationTarget(FRotator(KneeAngle, 0, 0));
        // 새 관절도 CPG가 목표 각도를 학습한다. 이전 정책을 불러오면 추가 유전자는 0에서 시작한다.
        const float CalfAngle = FMath::Clamp(Ramp * (Genes[40+I]*25.f*FMath::Sin(Phase+Genes[46+I]*PI)+Genes[52+I]*8.f), -32.f, 32.f);
        const float AnkleAngle = FMath::Clamp(Ramp * (Genes[58+I]*20.f*FMath::Sin(Phase+Genes[64+I]*PI)+Genes[70+I]*10.f), -32.f, 32.f);
        CalfJoints[I]->SetAngularOrientationTarget(FRotator(CalfAngle, 0, 0));
        Ankles[I]->SetAngularOrientationTarget(FRotator(AnkleAngle, 0, 0));
        Effort += FMath::Abs(CalfAngle-LastTargets[12+I])+FMath::Abs(AnkleAngle-LastTargets[18+I]);
        LastTargets[12+I]=CalfAngle; LastTargets[18+I]=AnkleAngle;
        Effort += FMath::Abs(HipAngle - LastTargets[I]) + FMath::Abs(KneeAngle - LastTargets[6 + I]);
        LastTargets[I] = HipAngle; LastTargets[6 + I] = KneeAngle;
    }
}
float APhysicsQueen::ForwardDistance() const { return bReachedGoal ? GoalDistance : (Body->GetComponentLocation().X - Start.X) / 100.f; }
float APhysicsQueen::UprightFraction() const { return UprightTime / FMath::Max(Clock, 0.01f); }
float APhysicsQueen::Fitness() const
{
    if (bFailed) return -150.f;
    if (bStructuredGait)
    {
        // 걸음 수는 통과 조건으로만 사용한다. 잔걸음을 반복해 점수를 올릴 수 없다.
        // 완주자끼리는 시간(초)과 실제 접지 변위를 함께 최소화한다.
        return (bReachedGoal ? 1000.f : ForwardDistance()*4.f)
            - Clock - 40.f*MeanPlantDrift() - 10.f*StanceDrift() - 5.f*FootRoll()
            // 높이 오차/상하 흔들림/기울기도 평가하되 자연스러운 몸체 회전은 약하게 감점한다.
            - 4.f*FMath::Square(HeightErrorRMS()) - .5f*FMath::Square(HeightDeviation())
            - .02f*MeanTiltDegrees();
    }
    const FVector P = bReachedGoal ? GoalPosition : Body->GetComponentLocation();
    const float Lateral = FMath::Abs(P.Y - Start.Y) / 100.f;
    const float Heading = bReachedGoal ? GoalHeading : FVector::DotProduct(Body->GetForwardVector(), FVector::ForwardVector);
    // 목표 거리 완주 보너스, 목표 속도 1m/s, 7m 높이 오차와 상하 흔들림을 함께 평가한다.
    // 실제 전진 거리와 별도로 안정적으로 이동한 거리만 보상한다. 평균 기울기와 각속도에 벌점을 준다.
    const float T = FMath::Max(MeasuredTime, 0.01f);
    return StableProgress * 4.f + (bReachedGoal ? 100.f : 0.f) + UprightFraction() * 5.f - TiltWeight * TiltCost / T
        - RotationWeight * RotationCost / T - 12.f * HeightError / T - 1.f * HeightMotion / T - 6.f * SpeedCost / T - FMath::Max(0.f, FurthestX - float(P.X)) / 100.f * 2.f - Lateral * 1.5f - (1.f - Heading) * 1.5f - Effort * 0.00008f;
}
// 제약을 끊고 해당 다리의 모터 출력을 중지한다. 이후 무게중심 변화는 물리 엔진이 처리한다.
void APhysicsQueen::SeverLeg(int32 I)
{
    if (I < 0 || I >= 6 || (Mask & (1 << I))) return;
    Mask |= (1 << I);
    CoxaJoints[I]->BreakConstraint();
    Hips[I]->BreakConstraint();
    Knees[I]->BreakConstraint();
    CalfJoints[I]->BreakConstraint();
    Ankles[I]->BreakConstraint();
    UE_LOG(LogTemp, Display, TEXT("QUEEN_V6_SEVER_RELEASED leg=%d coxa=%d hip=%d knee=%d calf=%d ankle=%d"), I, !CoxaJoints[I]->ConstraintInstance.IsValidConstraintInstance(),
        !Hips[I]->ConstraintInstance.IsValidConstraintInstance(), !Knees[I]->ConstraintInstance.IsValidConstraintInstance(),
        !CalfJoints[I]->ConstraintInstance.IsValidConstraintInstance(), !Ankles[I]->ConstraintInstance.IsValidConstraintInstance());
    Upper[I]->AddImpulse(FVector(0, I < 3 ? 150 : -150, 50), NAME_None, true);
}





// 플레이어 테스트에서 실제 맞은 물리 부품에 속한 다리만 손상시킨다.
bool APhysicsQueen::ReceivePlayerShot(UPrimitiveComponent* Part, float Damage)
{
    if (!Part || Damage<=0) return false;
    for (int32 I=0; I<6; ++I)
    {
        if (Mask & (1<<I)) continue;
        if (Part==Coxae[I] || Part==Upper[I] || Part==Lower[I] || Part==Shins[I] || Part==Feet[I])
        {
            TestLegHealth[I]=FMath::Max(0.f,TestLegHealth[I]-Damage);
            if (TestLegHealth[I]<=0) SeverLeg(I);
            return true;
        }
    }
    return false;
}
