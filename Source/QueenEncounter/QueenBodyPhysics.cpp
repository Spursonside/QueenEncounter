#include "TraversalQueen.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"

bool ATraversalQueen::IsBodySimulating() const { return Body->IsSimulatingPhysics(); }
void ATraversalQueen::EnablePhysicalBody()
{
    bPhysicalBody=true;
    LastPhysicalPosition=GetActorLocation();
    Body->SetBoxExtent(FVector(540,450,250)); // The shell, not an almost flat centre plate, contacts the ground.
    Body->SetMassOverrideInKg(NAME_None,BodyMass,true);
    Body->SetLinearDamping(.05f); Body->SetAngularDamping(.8f);
    Body->SetEnableGravity(true); Body->SetSimulatePhysics(true);
    Body->SetPhysicsMaxAngularVelocityInDegrees(100);
    for(int32 I=0;I<6;++I) SupportForces[I]=BodyMass*980/6;
    UE_LOG(LogTemp,Display,TEXT("QUEEN_PHYSICS enabled=%d mass=%.1f"),Body->IsSimulatingPhysics(),Body->GetMass());
}
FVector ATraversalQueen::SupportMount(int32 I) const
{
    return GetActorTransform().TransformPosition(FVector((1-I%3)*450.f,I<3?-420.f:420.f,-35));
}
void ATraversalQueen::ApplySupport(float Dt,FVector Target,FRotator Facing)
{
    const FVector Position=GetActorLocation(),V=Body->GetPhysicsLinearVelocity();
    const FVector Omega=Body->GetPhysicsAngularVelocityInRadians(),Inertia=Body->GetInertiaTensor();
    const float Gravity=FMath::Abs(GetWorld()->GetGravityZ());
    // A lost actuator disappears instantly; surviving actuators respond after a short control delay.
    ForceRecovery=FMath::Clamp((DamageAge-.12f)/.35f,0.f,1.f);
    const float Total=BodyMass*FMath::Clamp(Gravity+16*(Target.Z-Position.Z)-8*V.Z,0.f,Gravity*3);
    const FVector RotationError=FVector::CrossProduct(GetActorUpVector(),Facing.RotateVector(FVector::UpVector));
    const FVector Alpha=RotationError*22-Omega*7;
    const float Lever=500;
    const FVector Demand(Total,Inertia.X*Alpha.X/Lever,Inertia.Y*Alpha.Y/Lever);
    FVector A[6]; float Force[6]={0,0,0,0,0,0}; bool Contact[6]; int32 N=0;
    for(int32 I=0;I<6;++I)
    {
        FVector G;
        Contact[I]=!Missing(I)&&!Legs[I].Swing&&Ground(Legs[I].Foot,G)&&FMath::Abs(Legs[I].Foot.Z-G.Z-50)<35&&FVector::Dist(SupportMount(I),Legs[I].Foot)<1500;
        if(!Contact[I]) {SupportForces[I]=0; continue;}
        const FVector Arm=Legs[I].Foot-Body->GetCenterOfMass();
        A[I]=FVector(1,Arm.Y/Lever,-Arm.X/Lever); Force[I]=SupportForces[I]; ++N;
    }
    // Nonnegative least squares allocates contact forces and roll/pitch moments. No direct tilt torque.
    for(int32 Iter=0;Iter<80;++Iter)
    {
        FVector Actual=FVector::ZeroVector;
        for(int32 I=0;I<6;++I) if(Contact[I]) Actual+=A[I]*Force[I];
        for(int32 I=0;I<6;++I) if(Contact[I]) Force[I]=FMath::Clamp(Force[I]+.025f*float(FVector::DotProduct(A[I],Demand-Actual)),0.f,BodyMass*Gravity*.85f);
    }
    for(int32 I=0;I<6;++I) if(Contact[I])
    {
        SupportForces[I]=FMath::Lerp(SupportForces[I],Force[I],FMath::Clamp(Dt*18*ForceRecovery,0.f,1.f));
        // Equivalent wrench of the ground reaction transmitted through a massless virtual leg.
        Body->AddForceAtLocation(FVector(0,0,SupportForces[I]),Legs[I].Foot);
        if(bShowDebug) DrawDebugDirectionalArrow(GetWorld(),Legs[I].Foot,Legs[I].Foot+FVector(0,0,SupportForces[I]/BodyMass),70,FColor::Blue,false,0,0,5);
    }
    if(N)
    {
        // Horizontal traction and steering are available only while at least one foot supports the body.
        FVector Acc=(Target-Position)*4+(Velocity-V)*4; Acc.Z=0;
        Body->AddForce(Acc.GetClampedToMaxSize(600)*BodyMass*ForceRecovery);
        const float YawError=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Facing.Yaw));
        Body->AddTorqueInRadians(FVector(0,0,Inertia.Z*FMath::Clamp(YawError*10-Omega.Z*5,-2.f,2.f))*ForceRecovery);
    }
}
void ATraversalQueen::AdvancePhysicalJump(float Dt)
{
    Distance+=FVector::Dist2D(GetActorLocation(),LastPhysicalPosition)/100;
    LastPhysicalPosition=GetActorLocation();
    Clock+=Dt; DamageAge+=Dt; JumpClock+=Dt;
    if(JumpClock<0)
    {
        const float Prep=JumpClock+.85f;
        // Keep supporting feet planted during physical compression.
        for(int32 I=0;I<6;++I) if(!Missing(I)) { Legs[I].Swing=false; Legs[I].Foot=Legs[I].Anchor=JumpFeet[I]; PoseLeg(I); }
        const float Compress=Prep<.6f?80*FMath::Clamp(Prep/.6f,0.f,1.f):80*(1-FMath::Clamp((Prep-.6f)/.25f,0.f,1.f));
        ApplySupport(Dt,JumpStart-FVector(0,0,Compress),JumpBaseRotation);
        SetAction(Prep<.6f?TEXT("ESCAPE / PHYSICAL CROUCH"):TEXT("ESCAPE / PHYSICAL DRIVE"));
        if(Prep>=.4f&&!Stomped) { Stomped=true; ++StompCount; }
        return;
    }
    if(!JumpLaunched)
    {
        JumpLaunched=true; JumpStart=GetActorLocation();
        Body->SetAngularDamping(5.f); // Flight damping limits runaway spin without assigning a rotation.
        const float UpSpeed=FMath::Sqrt(2*FMath::Abs(GetWorld()->GetGravityZ())*JumpHeight);
        const FVector DesiredV=(JumpEnd-JumpStart).GetSafeNormal2D()*700+FVector(0,0,UpSpeed);
        const FVector Impulse=(DesiredV-Body->GetPhysicsLinearVelocity())*BodyMass/FMath::Max(1,ActiveLegs());
        for(int32 I=0;I<6;++I) if(!Missing(I))
        {
            Body->AddImpulseAtLocation(Impulse,SupportMount(I));
            JumpFeet[I]=GetActorTransform().InverseTransformPosition(Legs[I].Foot);
        }
        UE_LOG(LogTemp,Display,TEXT("QUEEN_PHYSICS takeoff mask=%d"),Mask);
    }
    SetAction(TEXT("ESCAPE / PHYSICAL FLIGHT"));
    const FVector P=GetActorLocation(),V=Body->GetPhysicsLinearVelocity();
    Yaw=GetActorRotation().Yaw; ActualSpeed=V.Size2D()/100;
    MeasuredJumpRise=FMath::Max(MeasuredJumpRise,float((P.Z-JumpStart.Z)/100));
    MeasuredJumpTravel=FMath::Max(MeasuredJumpTravel,float(FVector::Dist2D(P,JumpStart)/100));
    for(int32 I=0;I<6;++I) if(!Missing(I)) { Legs[I].Foot=GetActorTransform().TransformPosition(JumpFeet[I]); PoseLeg(I); }
    FVector G;
    if(JumpClock>.35f&&V.Z<0&&Ground(P,G)&&P.Z-G.Z<=TargetHeight+50)
    {
        bJumping=false; ++Landings; JumpCooldown=4+3*FMath::Abs(JumpImbalance);
        Body->SetAngularDamping(.8f);
        RoutePosition=P; BodyBias=FVector::ZeroVector; Velocity=FVector::ZeroVector;
        for(int32 I=0;I<6;++I) if(!Missing(I))
        {
            FVector Hit,N;
            if(Ground(P+Neutral(I),Hit,&N)) { Legs[I].Foot=Legs[I].Anchor=Hit+N*50; Legs[I].Swing=false; }
        }
        UE_LOG(LogTemp,Display,TEXT("QUEEN_PHYSICS landing=%d roll=%.2f"),Landings,GetActorRotation().Roll);
    }
}
