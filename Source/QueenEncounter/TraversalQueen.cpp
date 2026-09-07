#include "TraversalQueen.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "InputCoreTypes.h"
#include "QueenPlayer.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    constexpr float Period=1.65f, SwingTime=Period*.28f, Radius=50.f;
    constexpr float UpperLength=650.f, LowerLength=380.f, ShinLength=370.f;
    const int32 Groups[3][2]={{0,5},{2,3},{1,4}};
    float Smooth(float T) { T=FMath::Clamp(T,0.f,1.f); return T*T*T*(10.f+T*(-15.f+6.f*T)); }
    // Two fixed-length links meet on the circle perpendicular to their endpoint chord.
    FVector Bend(FVector A,FVector B,float L1,float L2,FVector Pole,float& Error)
    {
        const FVector Delta=B-A;
        const float Raw=Delta.Size(), D=FMath::Clamp(Raw,FMath::Abs(L1-L2)+.01f,L1+L2-.01f);
        Error=FMath::Max(Error,FMath::Abs(Raw-D));
        const FVector Axis=Delta.GetSafeNormal();
        const FVector Up=(Pole-Axis*FVector::DotProduct(Pole,Axis)).GetSafeNormal();
        const float Along=(L1*L1-L2*L2+D*D)/(2.f*D);
        return A+Axis*Along+Up*FMath::Sqrt(FMath::Max(0.f,L1*L1-Along*Along));
    }
}
ATraversalQueen::ATraversalQueen()
{
    PrimaryActorTick.bCanEverTick=false;
    Body=CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBody")); SetRootComponent(Body);
    Body->SetBoxExtent(FVector(540,450,85));
    Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Armor=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_QueenArmor.M_QueenArmor"));
    auto* Metal=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Metal.M_Metal"));
    auto* Red=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_QueenRed.M_QueenRed"));
    // Nine components per leg: four rigid links, spherical sole, and four joint housings.
    for (int32 I=0;I<54;++I)
    {
        auto* P=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("LegPart_%d"),I));
        P->SetupAttachment(Body); P->SetUsingAbsoluteScale(true);
        P->SetStaticMesh(I%9>=4?Sphere:Cube);
        P->SetCollisionEnabled(ECollisionEnabled::QueryOnly); P->SetCollisionResponseToAllChannels(ECR_Ignore); P->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        P->SetMaterial(0,I%9>=4?Metal:Armor); Parts.Add(P);
    }
    auto AddShell=[&](FName Name,FVector Location,FVector Scale,UMaterialInterface* Material,bool Round)
    {
        auto* P=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        P->SetupAttachment(Body); P->SetStaticMesh(Round?Sphere:Cube);
        P->SetRelativeLocation(Location); P->SetRelativeScale3D(Scale);
        P->SetCollisionEnabled(ECollisionEnabled::NoCollision); P->SetMaterial(0,Material);
        return P;
    };
    const float Scale=700.f/270.f;
    AddShell(TEXT("Carapace"),FVector(0,0,30)*Scale,FVector(4.9,5.4,1.4)*Scale,Armor,true);
    AddShell(TEXT("Underbody"),FVector(0,0,-65)*Scale,FVector(3.8,4.1,.9)*Scale,Metal,true);
    for(int32 I=0;I<14;++I)
    {
        const float A=I*2.f*PI/10.f;
        auto* P=AddShell(*FString::Printf(TEXT("ArmorPanel_%d"),I),FVector(215*FMath::Cos(A),235*FMath::Sin(A),30)*Scale,FVector(.48,1.35,.55)*Scale,Armor,false);
        P->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(A),0));
        if(I>=10)
        {
            const int32 J=I-10;
            P->SetRelativeLocation(FVector(J<2?-85:85,J%2?-95:95,85)*Scale);
            P->SetRelativeRotation(FRotator::ZeroRotator); P->SetRelativeScale3D(FVector(1.4,.7,.25)*Scale); P->SetMaterial(0,Metal);
        }
    }
    AddShell(TEXT("FrontOptic"),FVector(208,0,-30)*Scale,FVector(.4,.8,.42)*Scale,Red,true);

}
bool ATraversalQueen::Ground(FVector Point,FVector& Hit,FVector* Normal) const
{
    FHitResult R;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenTerrainIK),false,this);
    if (!GetWorld()->LineTraceSingleByObjectType(R,Point+FVector(0,0,10000),Point-FVector(0,0,10000),FCollisionObjectQueryParams(ECC_WorldStatic),Params)) return false;
    Hit=R.ImpactPoint; if(Normal) *Normal=R.ImpactNormal; return true;
}
FVector ATraversalQueen::Neutral(int32 I) const
{
    const int32 Row=I%3; const float Sign=I<3?-1.f:1.f;
    return FRotator(0,Yaw,0).RotateVector(FVector((1-Row)*1000.f,Sign*(Row==1?1300.f:900.f),0));
}
void ATraversalQueen::Initialize(FVector Origin,float Heading)
{
    Start=RoutePosition=Origin; InitialYaw=Yaw=Heading; Forward=FRotator(0,Yaw,0).Vector();
    FVector G;
    if(!Ground(Origin,G)) { bFailed=true; Failure=TEXT("missing_start_ground"); return; }
    SetActorLocation(FVector(Origin.X,Origin.Y,G.Z+700)); SetActorRotation(FRotator(0,Yaw,0));
    for(int32 I=0;I<6;++I)
    {
        FVector F,N;
        if(!Ground(GetActorLocation()+Neutral(I),F,&N)) { bFailed=true; Failure=TEXT("missing_foot_ground"); return; }
        // Offset along the surface normal: vertical-only offsets bury spheres on slopes.
        Legs[I].Foot=Legs[I].Anchor=F+N*Radius; PoseLeg(I);
    }
    Rows.Add(TEXT("seconds,distance_m,body_speed_m_s,height_m,tilt_deg,air_feet,max_ik_error_cm,max_stance_drift_cm,roll_deg,pitch_deg"));
}
void ATraversalQueen::Segment(int32 Index,FVector A,FVector B,float Width)
{
    Parts[Index]->SetWorldLocationAndRotation((A+B)*.5,(B-A).Rotation());
    Parts[Index]->SetWorldScale3D(FVector(FVector::Distance(A,B)/100,Width/100,Width/100));
}
void ATraversalQueen::PoseLeg(int32 I)
{
    const int32 Row=I%3, N=I*9; const float Sign=I<3?-1.f:1.f;
    const FVector Mount=GetActorTransform().TransformPosition(FVector((1-Row)*450.f,Sign*420.f,-35));
    const FVector RestOut=(Neutral(I)-FRotator(0,Yaw,0).RotateVector(FVector((1-Row)*450.f,Sign*420.f,0))).GetSafeNormal2D();
    // A tilted body can pass over a planted foot. Keep the knee pole outside the shell instead
    // of flipping its bend inward when the foot crosses the hip's vertical projection.
    const FVector Out=bPhysicalBody?RestOut:(Legs[I].Foot-Mount).GetSafeNormal2D();
    const FVector Hip=Mount+Out*100;
    const FVector Ankle=Legs[I].Foot+FVector(0,0,50);
    const float Fold=bPhysicalBody?FMath::Clamp((200.f-float(FVector::Distance(Hip,Ankle)))/200.f,0.f,1.f):0.f;
    const float BendAngle=FMath::DegreesToRadians(FMath::Lerp(22.f,60.f,Fold));
    const float Chord=FMath::Sqrt(LowerLength*LowerLength+ShinLength*ShinLength+2*LowerLength*ShinLength*FMath::Cos(BendAngle));
    FVector Knee=Bend(Hip,Ankle,UpperLength,Chord,FVector::UpVector+Out*.7f,MaxIKError);
    FVector Calf=Bend(Knee,Ankle,LowerLength,ShinLength,Out,MaxIKError);
    FVector Nodes[6]={Mount,Hip,Knee,Calf,Ankle,Legs[I].Foot};
    if(bPhysicalBody&&ActiveLegs()<=3)
    {
        // Search alternate IK bend planes only when the normal plane intersects surviving armor.
        // Endpoints and link lengths stay fixed; knees cannot be routed under the planted sole.
        auto Clearance=[&](const FVector* Candidate)
        {
            float Gap=BIG_NUMBER; const float R[5]={140,140,115,90,50};
            for(int32 Other=0;Other<6;++Other) if(Other!=I&&!Missing(Other))
                for(int32 A=0;A<5;++A) for(int32 B=0;B<5;++B)
                { FVector P,Q; FMath::SegmentDistToSegmentSafe(Candidate[A],Candidate[A+1],Legs[Other].Points[B],Legs[Other].Points[B+1],P,Q); Gap=FMath::Min(Gap,float(FVector::Dist(P,Q))-R[A]-R[B]); }
            return Gap;
        };
        float Best=Clearance(Nodes);
        if(Best<20)
        {
            const FVector Axis=(Ankle-Hip).GetSafeNormal();
            for(float Angle:{-30.f,30.f,-60.f,60.f,-90.f,90.f,-120.f,120.f})
            {
                float Error=0; const FVector Pole=(FVector::UpVector+Out*.7f).RotateAngleAxis(Angle,Axis);
                const FVector K=Bend(Hip,Ankle,UpperLength,Chord,Pole,Error),C=Bend(K,Ankle,LowerLength,ShinLength,Out,Error);
                if(K.Z<Legs[I].Foot.Z+50||C.Z<Legs[I].Foot.Z+40) continue;
                const FVector Trial[6]={Mount,Hip,K,C,Ankle,Legs[I].Foot}; const float Gap=Clearance(Trial);
                if(Gap>Best+5) { Best=Gap; Knee=K; Calf=C; Nodes[2]=K; Nodes[3]=C; }
                if(Best>20) break;
            }
        }
    }
    for(int32 J=0;J<6;++J) Legs[I].Points[J]=Nodes[J];
    MaxJointYaw=FMath::Max(MaxJointYaw,float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(RestOut,Out),-1.,1.)))));
    MaxHipElevation=FMath::Max(MaxHipElevation,float(FMath::Abs((Knee-Hip).Rotation().Pitch)));
    Segment(N,Mount,Hip,194); Segment(N+1,Hip,Knee,181);
    Segment(N+2,Knee,Calf,145); Segment(N+3,Calf,Ankle,104);
    Parts[N+4]->SetWorldLocationAndRotation(Legs[I].Foot,FRotator::ZeroRotator);
    Parts[N+4]->SetWorldScale3D(FVector(2*Radius/100));
    const FVector Joints[4]={Mount,Hip,Knee,Calf};
    for(int32 J=0;J<4;++J)
    {
        Parts[N+5+J]->SetWorldLocation(Joints[J]);
        Parts[N+5+J]->SetWorldScale3D(FVector(J<3?2.15f:1.6f));
    }
}
int32 ATraversalQueen::ActiveLegs() const
{
    int32 N=0; for(int32 I=0;I<6;++I) if(!Missing(I)) ++N; return N;
}
bool ATraversalQueen::Crawling() const
{
    int32 Left=0,Right=0;
    for(int32 I=0;I<6;++I) if(!Missing(I)) { if(I<3) ++Left; else ++Right; }
    // Four surviving legs retain a high stance; only three or fewer use belly recovery.
    return Left+Right<=3;
}
void ATraversalQueen::SetMotion(FVector2D LocalInput,float Turn)
{
    MotionInput=LocalInput.GetClampedToMaxSize(1); TurnInput=FMath::Clamp(Turn,-1.f,1.f);
}
bool ATraversalQueen::ReceiveShot(UPrimitiveComponent* Hit,float Damage)
{
    for(int32 I=0;I<Parts.Num();++I) if(Parts[I]==Hit&&!Missing(I/9))
    {
        Legs[I/9].Health-=Damage; if(Legs[I/9].Health<=0) SeverLeg(I/9); return true;
    }
    return false;
}
void ATraversalQueen::SeverLeg(int32 I)
{
    if(I<0||I>=6||Missing(I)) return;
    Mask|=1<<I; Legs[I].Swing=false; DamageAge=0; SlotClock=0;
    SupportForces[I]=0;
    if(bPhysicalBody) Body->WakeAllRigidBodies();
    // Only the severed assembly becomes Chaos debris. Surviving legs retain planted anchors.
    for(int32 J=5;J<9;++J)
        Parts[I*9+J]->AttachToComponent(Parts[I*9+FMath::Max(0,J-6)],FAttachmentTransformRules::KeepWorldTransform);
    for(int32 J=0;J<5;++J)
    {
        auto* P=Parts[I*9+J].Get(); P->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        P->SetCollisionProfileName(TEXT("PhysicsActor")); P->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Ignore);
        P->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Ignore);
        P->SetMassOverrideInKg(NAME_None,J==4?40:150,true); P->SetLinearDamping(.5f); P->SetAngularDamping(2);
        P->SetSimulatePhysics(true); P->SetPhysicsLinearVelocity(Velocity);
        if(J>0)
        {
            auto* Joint=NewObject<UPhysicsConstraintComponent>(this); Joint->RegisterComponent();
            Joint->SetWorldLocation(Legs[I].Points[J]);
            Joint->SetLinearXLimit(LCM_Locked,0); Joint->SetLinearYLimit(LCM_Locked,0); Joint->SetLinearZLimit(LCM_Locked,0);
            Joint->SetAngularSwing1Limit(ACM_Limited,35); Joint->SetAngularSwing2Limit(ACM_Limited,35); Joint->SetAngularTwistLimit(ACM_Limited,20);
            Joint->SetDisableCollision(true); Joint->SetConstrainedComponents(Parts[I*9+J-1],NAME_None,P,NAME_None);
            DebrisJoints.Add(Joint);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("QUEEN_V17_SEVER leg=%d mask=%d at=%.3f"),I,Mask,Clock);
}
TArray<FVector2D> ATraversalQueen::SupportHull(int32 Exclude) const
{
    TArray<FVector2D> P;
    for(int32 I=0;I<6;++I) if(!Missing(I)&&I!=Exclude&&!Legs[I].Swing) P.Add(FVector2D(Legs[I].Foot-GetActorLocation()));
    // A low-body recovery uses an authored belly support patch, not a learned dynamic balance.
    if(!bPhysicalBody&&Crawling()&&TargetHeight<400)
        for(float X:{-280.f,280.f}) for(float Y:{-280.f,280.f}) P.Add(FVector2D(FRotator(0,Yaw,0).RotateVector(FVector(X,Y,0))));
    P.Sort([](const FVector2D& A,const FVector2D& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;});
    if(P.Num()<3) return P;
    auto Cross=[](FVector2D A,FVector2D B,FVector2D C){return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);};
    TArray<FVector2D> H;
    for(auto V:P) { while(H.Num()>1&&Cross(H[H.Num()-2],H.Last(),V)<=0) H.Pop(EAllowShrinking::No); H.Add(V); }
    const int32 Lower=H.Num();
    for(int32 I=P.Num()-2;I>=0;--I) { while(H.Num()>Lower&&Cross(H[H.Num()-2],H.Last(),P[I])<=0) H.Pop(EAllowShrinking::No); H.Add(P[I]); }
    H.Pop(EAllowShrinking::No); return H;
}
float ATraversalQueen::SupportMargin(const TArray<FVector2D>& Hull) const
{
    if(Hull.Num()<3) return -1000;
    float M=BIG_NUMBER;
    for(int32 I=0;I<Hull.Num();++I)
    {
        const FVector2D A=Hull[I],E=Hull[(I+1)%Hull.Num()]-A;
        M=FMath::Min(M,float((E.X*(-A.Y)-E.Y*(-A.X))/FMath::Max(1.,E.Size())));
    }
    return M;
}
void ATraversalQueen::StartStep(int32 I,float StepPeriod)
{
    if(Missing(I)||Legs[I].Swing) return;
    FLeg& L=Legs[I]; L.Swing=true; L.Elapsed=0; L.From=L.Foot;
    L.Duration=Mask?.30f:SwingTime;
    const float Ahead=L.Duration+(StepPeriod-L.Duration)*.5f;
    // Predict both translation and yaw: feet on the outer side of a turn travel farther.
    const FVector RotatedNeutral=FRotator(0,TurnRate*Ahead,0).RotateVector(Neutral(I));
    const FVector Landing=RoutePosition+RotatedNeutral+Velocity*Ahead;
    FVector G,N;
    if(!Ground(Landing,G,&N)) { bFailed=true; Failure=TEXT("missing_touchdown"); return; }
    L.To=G+N*Radius; L.Lift=Crawling()?55.f:110.f;
    for(int32 K=1;K<8;++K)
    {
        const float T=K/8.f; FVector Sample,Normal;
        if(Ground(FMath::Lerp(L.From,L.To,Smooth(T)),Sample,&Normal))
            L.Lift=FMath::Max(L.Lift,float((Sample.Z+Radius/FMath::Max(.5,Normal.Z)+15-FMath::Lerp(L.From.Z,L.To.Z,Smooth(T)))/FMath::Square(FMath::Sin(PI*T))));
    }
    if(L.Lift>250) { bFailed=true; Failure=TEXT("step_obstacle_too_high"); }
}
void ATraversalQueen::Advance(float Dt)
{
    if(bDone||bFailed) return;
    if(bJumping) { if(bPhysicalBody) AdvancePhysicalJump(Dt); else AdvanceJump(Dt); return; }
    const double SolveStart=FPlatformTime::Seconds();
    Clock+=Dt; DamageAge+=Dt;
    const FVector Previous=bPhysicalBody?LastPhysicalPosition:GetActorLocation();
    if(bPhysicalBody) LastPhysicalPosition=GetActorLocation();
    const int32 Count=ActiveLegs();
    const bool Crawl=Crawling();
    TargetSpeed=Mask?(Crawl?(Count>=3?.9f:Count==2?.65f:.3f):Count==5?2.5f:.9f):FMath::Abs(MotionInput.Y)>.1f?2.f:MotionInput.X<0?3.f:FMath::Abs(TurnInput)>.01f?3.f:5.f;
    if(Count==0) TargetSpeed=0;
    const float WantedSpeed=TargetSpeed*MotionInput.Size()*(Mask&&DamageAge<.5f?0.f:1.f);

    TurnRate=FMath::FInterpConstantTo(TurnRate,Count?TurnInput*(Mask?3.f:8.f):0.f,Dt,8.f);
    Yaw+=TurnRate*Dt;
    Forward=FRotator(0,Yaw,0).Vector();
    const FVector Direction=FRotator(0,Yaw,0).RotateVector(FVector(MotionInput.X,MotionInput.Y,0)).GetSafeNormal();
    Velocity=FMath::VInterpConstantTo(Velocity,Direction*WantedSpeed*100,Dt,Mask?600.f:200.f); Speed=Velocity.Size()/100;
    RoutePosition+=Velocity*Dt;
    FVector WantedBias=FVector::ZeroVector;
    if(Mask)
    {
        int32 N=0;
        for(int32 I=0;I<6;++I) if(!Missing(I)) { WantedBias+=Neutral(I); ++N; }
        WantedBias=Crawl?(bPhysicalBody?(WantedBias/FMath::Max(1,N)*.95f).GetClampedToMaxSize(1100):FVector::ZeroVector):(WantedBias/FMath::Max(1,N)*.65f).GetClampedToMaxSize(600);

    }
    BodyBias=FMath::VInterpTo(BodyBias,WantedBias,Dt,2);
    FVector Desired=RoutePosition+BodyBias,GroundPoint,Normal;
    if(!Ground(Desired,GroundPoint,&Normal)) { bFailed=true; Failure=TEXT("terrain_ended"); return; }
    TargetHeight=FMath::FInterpTo(TargetHeight,Crawl?350.f:Count==4?640.f:Mask?660.f:700.f,Dt,2);
    Desired.Z=FMath::FInterpTo(Previous.Z,GroundPoint.Z+TargetHeight+6*FMath::Sin(Phase*2*PI/Period),Dt,8);
    FHitResult Block;
    FRotator Slope=FRotationMatrix::MakeFromXZ(Forward,FMath::Lerp(FVector::UpVector,Normal,.45f).GetSafeNormal()).Rotator();
    if(Crawl)
    {
        const int32 LostFront=int32(Missing(0))+int32(Missing(3)), LostRear=int32(Missing(2))+int32(Missing(5));
        int32 LostLeft=0,LostRight=0; for(int32 I=0;I<6;++I) if(Missing(I)) {if(I<3) ++LostLeft; else ++LostRight;}
        Slope.Pitch+=(LostRear-LostFront)*3.f; Slope.Roll+=(LostLeft-LostRight)*3.f;
    }
    if(!bPhysicalBody) Slope.Roll+=FMath::Exp(-DamageAge*2.f)*FMath::Sin(DamageAge*12.f)*3.f;
    FRotator Rotation=FMath::RInterpTo(GetActorRotation(),Slope,Dt,4); Rotation.Yaw=Yaw;
    if(Crawl)
    {
        // The lowered shell must clear rising terrain across its footprint, not only at its centre.
        float RequiredZ=GroundPoint.Z+TargetHeight;
        for(float X:{-540.f,540.f}) for(float Y:{-450.f,450.f})
        {
            const FVector Offset=Rotation.RotateVector(FVector(X,Y,-260)); FVector G;
            if(Ground(Desired+Offset+Velocity*.5f,G)) RequiredZ=FMath::Max(RequiredZ,float(G.Z-Offset.Z+25));
        }
        Desired.Z=FMath::FInterpTo(Previous.Z,RequiredZ,Dt,8);
    }
    const float PreviousYaw=GetActorRotation().Yaw;
    if(bPhysicalBody) { Desired.Z=GroundPoint.Z+TargetHeight; ApplySupport(Dt,Desired,Slope); }
    else Body->MoveComponent(Desired-Previous,Rotation,true,&Block);
    if(Block.bBlockingHit) { bFailed=true; Failure=TEXT("body_collision"); return; }
    TurnedDegrees+=FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousYaw,GetActorRotation().Yaw));
    const FVector Actual=GetActorLocation();
    const float OldDistance=Distance;
    if(Scenario==TEXT("forward")||Scenario==TEXT("left")||Scenario==TEXT("right")||Scenario==TEXT("back"))
    {
        const FVector LocalAxis=Scenario==TEXT("left")?FVector(0,-1,0):Scenario==TEXT("right")?FVector(0,1,0):Scenario==TEXT("back")?FVector(-1,0,0):FVector(1,0,0);
        Distance=FVector::DotProduct(Actual-Start,FRotator(0,InitialYaw,0).RotateVector(LocalAxis))/100.f;
    }
    else Distance+=FVector::Dist2D(Actual,Previous)/100.f;
    const float Measured=bPhysicalBody?float(Body->GetPhysicsLinearVelocity().Size2D()/100):float(FVector::Dist2D(Actual,Previous)/100.f/Dt);
    ActualSpeed=Measured;
    if(Distance>10)
    {
        const float Fraction=FMath::Clamp((Distance-10)/FMath::Max(.0001f,Distance-OldDistance),0.f,1.f);
        CruiseTime+=Dt*Fraction; CruiseDistance+=(Distance-OldDistance)*Fraction;
        if(Measured>=TargetSpeed*.8f&&Measured<=TargetSpeed*1.2f) CruiseGood+=Dt*Fraction;
    }
    const float OldPhase=Phase; Phase+=Dt;
    const bool Moving=Speed>.05f||FMath::Abs(TurnRate)>.1f;
    if(Moving&&!Mask)
    {
        for(int32 Group=0;Group<3;++Group)
        {
            const float Offset=Group*Period/3.f;
            if(FMath::FloorToInt((Phase-Offset)/Period)>FMath::FloorToInt((OldPhase-Offset)/Period)||(Group==0&&OldPhase==0))
                for(int32 J=0;J<2;++J) StartStep(Groups[Group][J],Period);
        }
    }
    else if(Moving&&Count>0&&DamageAge>.1f)
    {
        SlotClock+=Dt;
        bool InAir=false; for(int32 I=0;I<6;++I) if(!Missing(I)&&Legs[I].Swing) InAir=true;
        if(SlotClock>=.42f&&!InAir)
        {
            SlotClock=0;
            float Best=-1;
            for(int32 I=0;I<6;++I) if(!Missing(I))
            {
                if(Count==5&&SupportMargin(SupportHull(I))<30) continue;
                const float Need=float(FVector::Dist2D(RoutePosition+Neutral(I)+Velocity*.5f,Legs[I].Foot))+(Clock-Legs[I].LastTouchdown)*100;
                if(Need>Best) { Best=Need; NextLeg=I; }
            }
            if(Best>=0) StartStep(NextLeg,Count*.42f);
        }

    }
    int32 Air=0;
    for(int32 I=0;I<6;++I)
    {
        if(Missing(I)) continue;
        FLeg& L=Legs[I];
        if(L.Swing)
        {
            ++Air; L.Elapsed+=Dt;
            const float T=FMath::Clamp(L.Elapsed/L.Duration,0.f,1.f);
            L.Foot=FMath::Lerp(L.From,L.To,Smooth(T))+FVector(0,0,L.Lift*FMath::Square(FMath::Sin(PI*T)));
            if(T>=1) { L.Swing=false; L.Foot=L.Anchor=L.To; L.LastTouchdown=Clock; ++L.Steps; MaxStep=FMath::Max(MaxStep,float(FVector::Dist2D(L.From,L.To)/100)); }
        }
        else { L.Foot=L.Anchor; MaxDrift=FMath::Max(MaxDrift,float(FVector::Distance(L.Foot,L.Anchor))); }
        FVector G,N;
        if(Ground(L.Foot,G,&N)) MaxPenetration=FMath::Max(MaxPenetration,Radius-float(FVector::DotProduct(L.Foot-G,N)));
        PoseLeg(I);
        // Measure the displayed sole after component transforms, not just the desired IK target.
        if(!L.Swing) MaxDrift=FMath::Max(MaxDrift,float(FVector::Distance(Parts[I*9+4]->GetComponentLocation(),L.Anchor)));
    }
    TArray<FVector2D> Support;
    // Conservative capsule envelopes include the thick armor and joint housings.
    const float Radii[5]={140,140,115,90,50};
    for(int32 I=0;I<6;++I) for(int32 J=I+1;J<6;++J)
        if(!Missing(I)&&!Missing(J)) for(int32 A=0;A<5;++A) for(int32 B=0;B<5;++B)
        {
            FVector P,Q;
            FMath::SegmentDistToSegmentSafe(Legs[I].Points[A],Legs[I].Points[A+1],Legs[J].Points[B],Legs[J].Points[B+1],P,Q);
            const float Clearance=float(FVector::Distance(P,Q))-Radii[A]-Radii[B];
            if(Clearance<MinLegClearance)
            {
                MinLegClearance=Clearance;
                ClosestLegPair=FString::Printf(TEXT("leg%d/link%d vs leg%d/link%d"),I,A,J,B);
            }
        }
    const float CurrentMargin=SupportMargin(SupportHull());
    MinSupportMargin=FMath::Min(MinSupportMargin,CurrentMargin);
    if(CurrentMargin<0&&DamageAge>1.5f) { UnsupportedTime+=Dt; UnsupportedStreak+=Dt; MaxUnsupportedStreak=FMath::Max(MaxUnsupportedStreak,UnsupportedStreak); }
    else UnsupportedStreak=0;
    AirMax=FMath::Max(AirMax,Air);
    const float H=(Actual.Z-GroundPoint.Z)/100.f;
    const float Tilt=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GetActorUpVector().Z,-1.,1.)));
    HeightSum+=H; HeightSq+=H*H; ++Samples; TiltMax=FMath::Max(TiltMax,Tilt);
    if(Clock>=NextSample)
    {
        Rows.Add(FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.4f"),Clock,Distance,Measured,H,Tilt,Air,MaxIKError,MaxDrift,GetActorRotation().Roll,GetActorRotation().Pitch));
        NextSample+=.1f;
    }
    if(!bEncounterAI&&!bPhysicalBody) {
    if(MaxIKError>1.f) { bFailed=true; Failure=TEXT("unreachable_foot"); }
    else if(MaxJointYaw>75.f||MaxHipElevation>85.f) { bFailed=true; Failure=TEXT("joint_range"); }
    // High four-leg gait permits short dynamic support transfers, but never a prolonged unsupported pose.
    // Keep both cumulative and consecutive durations visible; do not relabel them as static balance.
    else if((!Mask&&MinSupportMargin<0)||(Mask&&(Count==4?MaxUnsupportedStreak>.65f:UnsupportedTime>2.f))) { bFailed=true; Failure=TEXT("outside_support_polygon"); }
    else if(MinLegClearance<0) { bFailed=true; Failure=TEXT("leg_intersection"); }
    else if(MaxPenetration>5.f) { bFailed=true; Failure=TEXT("foot_penetration"); }
    else if(!bManual&&Clock>180) { bFailed=true; Failure=TEXT("timeout"); }
    else if(!bManual&&Count==0&&DamageAge>4) bDone=true;
    else if(!bManual&&((Scenario==TEXT("rotate")&&TurnedDegrees>=90)||(Scenario!=TEXT("rotate")&&Distance>=Goal))) bDone=true;
    }
    const double Cost=FPlatformTime::Seconds()-SolveStart; SolverSeconds+=Cost; ++SolverTicks; MaxSolverMs=FMath::Max(MaxSolverMs,float(Cost*1000));
}
void ATraversalQueen::WriteResult(const FString& File) const
{
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("method"),bPhysicalBody?TEXT("v20 Chaos body / virtual-leg contact forces / terrain IK"):bEncounterAI?TEXT("v18 FSM / procedural jump / terrain IK"):TEXT("v17 omnidirectional terrain IK / authored damage recovery / Chaos detached limbs"));
    O->SetNumberField(TEXT("jump_imbalance"),JumpImbalance);
    O->SetBoolField(TEXT("body_simulating_physics"),Body->IsSimulatingPhysics());
    O->SetNumberField(TEXT("body_mass_kg"),Body->GetMass());
    O->SetNumberField(TEXT("stomp_count"),StompCount);
    O->SetNumberField(TEXT("measured_jump_rise_m"),MeasuredJumpRise);
    O->SetNumberField(TEXT("measured_jump_travel_m"),MeasuredJumpTravel);
    O->SetStringField(TEXT("scenario"),Scenario); O->SetNumberField(TEXT("missing_mask"),Mask);
    O->SetStringField(TEXT("ai_state"),Action);
    O->SetNumberField(TEXT("ai_shots"),Shots); O->SetNumberField(TEXT("ai_jumps"),Jumps); O->SetNumberField(TEXT("ai_landings"),Landings); O->SetNumberField(TEXT("ai_detections"),Detections);
    O->SetNumberField(TEXT("turned_degrees"),TurnedDegrees);
    O->SetNumberField(TEXT("unsupported_seconds"),UnsupportedTime);
    O->SetNumberField(TEXT("max_unsupported_streak_seconds"),MaxUnsupportedStreak);
    O->SetNumberField(TEXT("solver_mean_ms"),SolverSeconds*1000/FMath::Max(1,SolverTicks));
    O->SetNumberField(TEXT("solver_max_ms"),MaxSolverMs);
    O->SetNumberField(TEXT("game_frame_mean_ms"),FrameSeconds*1000/FMath::Max(1,FrameCount));
    O->SetStringField(TEXT("mobility_state"),ActiveLegs()==0?TEXT("immobile"):Crawling()?TEXT("belly_assisted_crawl"):Mask?TEXT("compensated_walk"):TEXT("normal_walk"));
    O->SetNumberField(TEXT("end_x_cm"),GetActorLocation().X); O->SetNumberField(TEXT("end_y_cm"),GetActorLocation().Y);
    O->SetBoolField(TEXT("machine_learned"),false);
    O->SetBoolField(TEXT("force_driven_locomotion"),bPhysicalBody);
    O->SetStringField(TEXT("map"),GetWorld()->GetMapName());
    O->SetBoolField(TEXT("completed"),bDone); O->SetBoolField(TEXT("failed"),bFailed);
    O->SetStringField(TEXT("failure"),Failure);
    O->SetNumberField(TEXT("distance_m"),Distance); O->SetNumberField(TEXT("seconds"),Clock);
    O->SetNumberField(TEXT("cruise_speed_m_s"),CruiseDistance/FMath::Max(.001f,CruiseTime));
    O->SetNumberField(TEXT("cruise_seconds"),CruiseTime);
    O->SetNumberField(TEXT("speed_target_fraction"),CruiseGood/FMath::Max(.001f,CruiseTime));
    O->SetNumberField(TEXT("mean_height_m"),HeightSum/FMath::Max(1,Samples));
    O->SetNumberField(TEXT("height_std_m"),FMath::Sqrt(FMath::Max(0.f,HeightSq/FMath::Max(1,Samples)-FMath::Square(HeightSum/FMath::Max(1,Samples)))));
    O->SetNumberField(TEXT("max_tilt_deg"),TiltMax);
    O->SetNumberField(TEXT("max_ik_error_cm"),MaxIKError);
    O->SetNumberField(TEXT("max_foot_penetration_cm"),MaxPenetration);
    O->SetNumberField(TEXT("max_stance_drift_cm"),MaxDrift);
    O->SetNumberField(TEXT("max_step_travel_m"),MaxStep);
    O->SetNumberField(TEXT("max_airborne_feet"),AirMax);
    O->SetNumberField(TEXT("max_coxa_yaw_from_rest_deg"),MaxJointYaw);
    O->SetNumberField(TEXT("max_hip_elevation_deg"),MaxHipElevation);
    O->SetNumberField(TEXT("min_support_margin_cm"),MinSupportMargin);
    O->SetNumberField(TEXT("min_leg_clearance_cm"),MinLegClearance);
    O->SetStringField(TEXT("closest_leg_pair"),ClosestLegPair);
    O->SetNumberField(TEXT("start_x_cm"),Start.X); O->SetNumberField(TEXT("start_y_cm"),Start.Y);
    O->SetNumberField(TEXT("heading_deg"),Yaw);
    TArray<TSharedPtr<FJsonValue>> Steps;
    bool AllStepped=true;
    for(int32 I=0;I<6;++I) { Steps.Add(MakeShared<FJsonValueNumber>(Legs[I].Steps)); if(!Missing(I)) AllStepped&=Legs[I].Steps>=3; }
    O->SetArrayField(TEXT("steps_per_leg"),Steps);
    const float MeanHeight=HeightSum/FMath::Max(1,Samples);
    const float HeightVariance=HeightSq/FMath::Max(1,Samples)-MeanHeight*MeanHeight;
    const bool ForwardSpeed=ActiveLegs()==0?ActualSpeed<.05f:Scenario!=TEXT("forward")||(CruiseTime>=5&&CruiseGood/CruiseTime>=.8f);
    O->SetBoolField(TEXT("passed"),bDone&&!bFailed&&ForwardSpeed&&MaxIKError<=1&&MaxPenetration<=5&&MaxDrift<=1&&AirMax<=2&&AllStepped
        &&(Mask||FMath::Abs(MeanHeight-7)<=.5f)&&(Mask||HeightVariance<=.09f)&&TiltMax<=25&&(ActiveLegs()==4?MaxUnsupportedStreak<=.65f:UnsupportedTime<=2)&&MinLegClearance>=0);
    FString Text; FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Text));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FFileHelper::SaveStringToFile(Text,*File);
    FFileHelper::SaveStringArrayToFile(Rows,*FPaths::ChangeExtension(File,TEXT("csv")));
}
AQueenTraversalMode::AQueenTraversalMode()
{
    PrimaryActorTick.bCanEverTick=true;
    PlayerTest=FParse::Param(FCommandLine::Get(),TEXT("QueenPlayerTest"));
    DefaultPawnClass=PlayerTest?AQueenPlayer::StaticClass():ASpectatorPawn::StaticClass(); HUDClass=AQueenTraversalHUD::StaticClass();
}
void AQueenTraversalMode::RestartPlayer(AController* C)
{
    if(!PlayerTest) { Super::RestartPlayer(C); return; }
    FVector Base=FVector::ZeroVector;
    FParse::Value(FCommandLine::Get(),TEXT("QueenStartX="),Base.X); FParse::Value(FCommandLine::Get(),TEXT("QueenStartY="),Base.Y);
    FVector Spawn=Base+FVector(1800,-2200,100); FHitResult Ground;
    if(GetWorld()->LineTraceSingleByObjectType(Ground,Spawn+FVector(0,0,10000),Spawn-FVector(0,0,10000),FCollisionObjectQueryParams(ECC_WorldStatic))) Spawn.Z=Ground.ImpactPoint.Z+100;
    FVector Focus=Base+FVector(0,0,700);
    if(GetWorld()->LineTraceSingleByObjectType(Ground,Base+FVector(0,0,10000),Base-FVector(0,0,10000),FCollisionObjectQueryParams(ECC_WorldStatic))) Focus.Z=Ground.ImpactPoint.Z+700;
    const FRotator Aim=(Focus-Spawn).Rotation();
    RestartPlayerAtTransform(C,FTransform(FRotator(0,Aim.Yaw,0),Spawn)); C->SetControlRotation(Aim);
}
void AQueenTraversalMode::BeginPlay()
{
    Super::BeginPlay();
    AITest=FParse::Param(FCommandLine::Get(),TEXT("QueenAITest"));
    Loop=FParse::Param(FCommandLine::Get(),TEXT("QueenDemoLoop"));
    ResultFile=FPaths::ProjectDir()/TEXT("LearningRuns/v16/result.json");
    FParse::Value(FCommandLine::Get(),TEXT("QueenResult="),ResultFile);
    FVector Start(0,0,0); float Heading=0;
    FParse::Value(FCommandLine::Get(),TEXT("QueenStartX="),Start.X);
    FParse::Value(FCommandLine::Get(),TEXT("QueenStartY="),Start.Y);
    FParse::Value(FCommandLine::Get(),TEXT("QueenHeading="),Heading);
    Queen=GetWorld()->SpawnActor<ATraversalQueen>();
    FParse::Value(FCommandLine::Get(),TEXT("QueenScenario="),Scenario);
    FParse::Value(FCommandLine::Get(),TEXT("QueenDamageMask="),DamageMask);
    FParse::Value(FCommandLine::Get(),TEXT("QueenDamageAt="),DamageAt);
    Manual=FParse::Param(FCommandLine::Get(),TEXT("QueenManual"));
    if(PlayerTest) { Manual=true; Scenario=TEXT("encounter"); Queen->bEncounterAI=true; }
    Queen->Scenario=Scenario; Queen->bManual=Manual; Queen->Initialize(Start,Heading);
    if(PlayerTest||FParse::Param(FCommandLine::Get(),TEXT("QueenPhysicalBody"))) Queen->EnablePhysicalBody();
    if(Scenario==TEXT("left")) Queen->SetMotion(FVector2D(0,-1),0);
    else if(Scenario==TEXT("right")) Queen->SetMotion(FVector2D(0,1),0);
    else if(Scenario==TEXT("back")) Queen->SetMotion(FVector2D(-1,0),0);
    else if(Scenario==TEXT("rotate")) Queen->SetMotion(FVector2D(0,0),1);
    else if(Scenario==TEXT("arc")) Queen->SetMotion(FVector2D(1,0),1);
    // Negative integration test: a vertical barrier must block swept movement, never count as completion.
    if(FParse::Param(FCommandLine::Get(),TEXT("QueenBlockTest")))
    {
        auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(Start+FRotator(0,Heading,0).Vector()*2000+FVector(0,0,900),FRotator(0,Heading,0));
        Wall->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->SetActorScale3D(FVector(1,100,30));
        Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    }
    auto* Sun=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,3000),FRotator(-55,-35,0));
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sun->GetLightComponent()->SetIntensity(4);
    Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->SetAtmosphereSunLight(true);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    auto* Sky=GetWorld()->SpawnActor<ASkyLight>(); Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sky->GetLightComponent()->SetIntensity(1); Sky->GetLightComponent()->RecaptureSky();
}
void AQueenTraversalMode::Tick(float Dt)
{
    Super::Tick(Dt); if(!Queen) return;
    if(FParse::Param(FCommandLine::Get(),TEXT("QueenBodyTest")))
    {
        TestClock+=Dt;
        if(TestClock>18) {Queen->WriteResult(ResultFile); FPlatformMisc::RequestExit(false); return;}
    }
    if(!Queen->bDone&&!Queen->bFailed) { Queen->FrameSeconds+=Dt; ++Queen->FrameCount; }
    if(!Damaged&&DamageMask&&Queen->Clock>=DamageAt)
    {
        for(int32 I=0;I<6;++I) if(DamageMask&(1<<I)) Queen->SeverLeg(I);
        Damaged=true;
    }
    if(Queen->bEncounterAI)
    {
        if(AITest)
        {
            TestClock+=Dt;
            auto* P=Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this,0));
            if(P) { P->Health=100; P->SetActorLocation(Queen->GetActorLocation()+FVector(TestClock<8?9000:2000,0,-500)); }
            if(TestClock>25&&!Damaged) { int32 TestMask=7; FParse::Value(FCommandLine::Get(),TEXT("QueenAITestMask="),TestMask); for(int32 I=0;I<6;++I) if(TestMask&(1<<I)) Queen->SeverLeg(I); Damaged=true; }
            if(TestClock>=75) { Queen->WriteResult(ResultFile); FPlatformMisc::RequestExit(false); return; }
        }
        Queen->Think(Dt);
        if(auto* PC=GetWorld()->GetFirstPlayerController())
        {
            if(PC->WasInputKeyJustPressed(EKeys::F6)) for(int32 I:{0,1,2}) Queen->SeverLeg(I);
            if(PC->WasInputKeyJustPressed(EKeys::F7)) for(int32 I:{3,4,5}) Queen->SeverLeg(I);
        }
    }
    if(Scenario==TEXT("change"))
    {
        const float T=Queen->Clock;
        Queen->SetMotion(T<4?FVector2D(1,0):T<8?FVector2D(0,-1):T<12?FVector2D(0,0):T<16?FVector2D(0,1):FVector2D(1,0),T>=8&&T<12?1:0);
    }
    if(!PlayerTest) if(auto* PC=GetWorld()->GetFirstPlayerController())
    {
        if(Manual) Queen->SetMotion(FVector2D((PC->IsInputKeyDown(EKeys::W)?1:0)-(PC->IsInputKeyDown(EKeys::S)?1:0),(PC->IsInputKeyDown(EKeys::D)?1:0)-(PC->IsInputKeyDown(EKeys::A)?1:0)),(PC->IsInputKeyDown(EKeys::E)?1:0)-(PC->IsInputKeyDown(EKeys::Q)?1:0));
        if(PC->WasInputKeyJustPressed(EKeys::F4)) for(int32 I:{0,3,1,4,2,5}) if(!(Queen->Mask&(1<<I))) { Queen->SeverLeg(I); break; }
        if(PC->WasInputKeyJustPressed(EKeys::F5)) for(int32 I:{0,3,1,4}) Queen->SeverLeg(I);
        if(PC->WasInputKeyJustPressed(EKeys::R)) UGameplayStatics::OpenLevel(this,FName(*UGameplayStatics::GetCurrentLevelName(this)),true,TEXT("game=/Script/QueenEncounter.QueenTraversalMode"));
    }
    // Retain the remainder: a tiny final substep can fall below MoveComponent's movement tolerance
    // and overwrite measured speed with zero. Fixed 120Hz also bounds movement between overlap checks.
    if(Queen->bPhysicalBody) Queen->Advance(Dt); // Apply forces once per physics frame, never twice per substep.
    else SimulationAccumulator+=Dt;
    constexpr float Step=1.f/120.f;
    while(SimulationAccumulator>=Step) { Queen->Advance(Step); SimulationAccumulator-=Step; }

    if(!PlayerTest) if(auto* PC=GetWorld()->GetFirstPlayerController())
        if(APawn* Pawn=PC->GetPawn())
        {
            const FVector Focus=Queen->GetActorLocation();
            const FVector Camera=Focus+FVector(1900,-2900,1400);
            Pawn->SetActorLocation(Camera); PC->SetControlRotation((Focus-Camera).Rotation());
        }
    if(Manual&&!Queen->bDone&&!Queen->bFailed) { Hold+=Dt; if(Hold>=5) { Queen->WriteResult(ResultFile); Hold=0; } }
    if(Queen->bEncounterAI&&Queen->bShowDebug) Queen->DrawDiagnostics();
    if(Queen->bDone||Queen->bFailed)
    {
        if(!Saved) { Queen->WriteResult(ResultFile); Saved=true; }
        if(PlayerTest) return; // Interactive encounter restarts only on the player's R input.
        if(!Loop) { FPlatformMisc::RequestExit(false); return; }
        Hold+=Dt;
        if(Hold>3) UGameplayStatics::OpenLevel(this,FName(*UGameplayStatics::GetCurrentLevelName(this)),true,TEXT("game=/Script/QueenEncounter.QueenTraversalMode"));
    }
}
void AQueenTraversalHUD::DrawHUD()
{
    Super::DrawHUD(); auto* Mode=Cast<AQueenTraversalMode>(GetWorld()->GetAuthGameMode());
    if(!Mode||!Mode->Queen) return;
    ATraversalQueen* Q=Mode->Queen;
    DrawRect(FLinearColor(0,0,0,.75f),20,20,1000,140);
    DrawText(TEXT("QUEEN / V18 ENCOUNTER / TERRAIN IK + DAMAGE AI"),FLinearColor::White,35,30,nullptr,1.4f);
    const FString Progress=Q->Scenario==TEXT("rotate")?FString::Printf(TEXT("%.1f / 90 deg"),Q->TurnedDegrees):FString::Printf(TEXT("%.1f / 50 m"),Q->Distance);
    DrawText(FString::Printf(TEXT("%.2f m/s    %s    %.1f s    %s"),Q->bDone?0.f:Q->ActualSpeed,*Progress,Q->Clock,Q->bFailed?*Q->Failure:Q->bDone?TEXT("COMPLETE"):Q->ActualSpeed>.05f?TEXT("MOVING"):TEXT("READY / TURNING")),FLinearColor(.5f,1,.75f),35,65,nullptr,1.3f);
    int32 Count=6; for(int32 I=0;I<6;++I) if(Q->Mask&(1<<I)) --Count;
    const bool PlayerMode=Q->bEncounterAI;
    DrawText(FString::Printf(TEXT("%s | legs %d/6 | %s"),PlayerMode?*Q->Action:Q->bManual?TEXT("manual"):*Q->Scenario,Count,
        PlayerMode?TEXT("WASD player / mouse fire / F2 debug / F4,F5 damage / F6,F7 side / R reset"):TEXT("WASD move / Q,E turn / R reset")),FLinearColor::White,35,102,nullptr,1.f);
    if(PlayerMode&&Q->bShowDebug)
    {
        DrawRect(FLinearColor(0,0,0,.7f),20,170,760,205);
        auto* P=Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this,0));
        DrawText(FString::Printf(TEXT("Speed %.2f / target %.2f m/s | roll %.1f pitch %.1f | HP %.0f | shots %d jumps %d"),Q->ActualSpeed,Q->TargetSpeed,Q->GetActorRotation().Roll,Q->GetActorRotation().Pitch,P?P->Health:0,Q->Shots,Q->Jumps),FLinearColor::White,30,178);
        int32 Row=0; for(const auto& Line:Q->JointReadout()) DrawText(Line,FLinearColor::White,30,202+Row++*20);
        DrawText(Q->bPhysicalBody?TEXT("CHAOS BODY | Yellow: physical COM | Blue: leg force | Green: support"):TEXT("Yellow: control COM (not physical mass) | Green: support | Cyan: swing"),FLinearColor::Yellow,30,330);
    }
    if(GetOwningPlayerController()&&Cast<AQueenPlayer>(GetOwningPlayerController()->GetPawn()))
    {
        const auto* P=Cast<AQueenPlayer>(GetOwningPlayerController()->GetPawn());
        DrawText(P->bSandbox?TEXT("SANDBOX / DAMAGE DISABLED / MANUAL RESET ONLY"):FString::Printf(TEXT("PLAYER HP %.0f / 100"),P->Health),FLinearColor::Yellow,35,135);
        if(P->Health<=0)
        {
            DrawRect(FLinearColor(0,0,0,.85f),Canvas->SizeX*.5f-260,Canvas->SizeY*.5f-70,520,130);
            DrawText(TEXT("PLAYER DOWN"),FLinearColor::Red,Canvas->SizeX*.5f-150,Canvas->SizeY*.5f-50,nullptr,2);
            DrawText(TEXT("Movement and fire disabled. Press R to restart."),FLinearColor::White,Canvas->SizeX*.5f-220,Canvas->SizeY*.5f+10);
        }
        DrawLine(Canvas->SizeX*.5f-7,Canvas->SizeY*.5f,Canvas->SizeX*.5f+7,Canvas->SizeY*.5f,FLinearColor::White);
        DrawLine(Canvas->SizeX*.5f,Canvas->SizeY*.5f-7,Canvas->SizeX*.5f,Canvas->SizeY*.5f+7,FLinearColor::White);
    }
}








