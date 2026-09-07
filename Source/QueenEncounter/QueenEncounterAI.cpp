#include "TraversalQueen.h"
#include "Components/BoxComponent.h"
#include "QueenPlayer.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

// Priority FSM: immobile > airborne > escape > visible target > last seen > wander.
// All movement requests pass through the existing terrain gait; no animation assets are required.
void ATraversalQueen::SetAction(const FString& Next)
{
    if(Action==Next) return;
    Action=Next;
    UE_LOG(LogTemp,Display,TEXT("QUEEN_AI state=%s time=%.2f mask=%d"),*Action,Clock,Mask);
}
void ATraversalQueen::Think(float Dt)
{
    BrainTime+=Dt; JumpCooldown=FMath::Max(0.f,JumpCooldown-Dt);
    auto* Player=Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!Player) return;
    if(Home.IsNearlyZero()) Home=GetActorLocation();
    if(ActiveLegs()==0) { SetAction(TEXT("IMMOBILE")); SetMotion(FVector2D::ZeroVector,0); return; }
    if(bJumping) return; // The jump controller owns stomp, compression, drive and flight labels.
    const FVector ToPlayer=Player->GetActorLocation()-GetActorLocation();
    const FVector Eye=GetActorLocation()+FVector(0,0,150);
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenSight),false,this);
    const bool Blocked=GetWorld()->LineTraceSingleByChannel(Hit,Eye,Player->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Params);
    const bool Visible=Player->Health>0&&ToPlayer.Size2D()<4500&&(!Blocked||Hit.GetActor()==Player);
    if(Visible) { LastSeen=Player->GetActorLocation(); LostSight=0; if(!bWasVisible) ++Detections; }
    else LostSight+=Dt;
    bWasVisible=Visible;
    FVector Destination;
    float Throttle=1;
    if(ActiveLegs()<=3)
    {
        AttackClock=0;
        const FVector Away=(-ToPlayer).GetSafeNormal2D();
        Destination=GetActorLocation()+Away*1600;
        if(JumpCooldown<=0&&BeginEscapeJump(Away)) return;
        SetAction(JumpCooldown>3?TEXT("ESCAPE / RECOVER"):TEXT("ESCAPE / CRAWL"));
        Throttle=JumpCooldown>3?.25f:1.f;
    }
    else if(Visible)
    {
        Destination=LastSeen; Throttle=ToPlayer.Size2D()>2400?1.f:0.f;
        AttackClock+=Dt;
        if(AttackClock<2) { AimPoint=Player->GetActorLocation()+FVector(0,0,35); SetAction(TEXT("CHASE")); }
        else
        {
            SetAction(TEXT("ATTACK / WINDUP"));
            DrawDebugLine(GetWorld(),Eye,AimPoint,FColor::Orange,false,0,0,8);
            DrawDebugSphere(GetWorld(),AimPoint,70,12,FColor::Orange,false,0);
            if(AttackClock>=3)
            {
                // Aim locks during windup. The player can dodge; terrain can block the shot.
                FHitResult Shot;
                const FVector End=AimPoint+(AimPoint-Eye).GetSafeNormal()*120;
                if(GetWorld()->LineTraceSingleByChannel(Shot,Eye,End,ECC_Visibility,Params))
                    if(Shot.GetActor()==Player) Player->Hurt(15);
                DrawDebugLine(GetWorld(),Eye,Shot.bBlockingHit?Shot.ImpactPoint:End,FColor::Red,false,.25f,0,18);
                ++Shots; AttackClock=0;
                UE_LOG(LogTemp,Display,TEXT("QUEEN_AI shot=%d player_health=%.1f"),Shots,Player->Health);
            }
        }
    }
    else if(LostSight<5&&Player->Health>0) { SetAction(TEXT("SEARCH")); Destination=LastSeen; AttackClock=0; }
    else
    {
        SetAction(TEXT("WANDER")); AttackClock=0; WanderTime-=Dt; Throttle=.6f;
        if(WanderTime<=0||FVector::Dist2D(GetActorLocation(),WanderTarget)<500)
        {
            // Bounded deterministic wander keeps the demo inside its landscape.
            const float Angle=BrainTime*.17f;
            WanderTarget=Home+FVector(FMath::Cos(Angle)*4000,FMath::Sin(Angle)*4000,0);
            WanderTime=14;
        }
        Destination=WanderTarget;
    }
    const FVector Local=GetActorRotation().UnrotateVector((Destination-GetActorLocation()).GetSafeNormal2D());
    const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    // Turning in place before a large heading change avoids dragging feet across the body.
    SetMotion(FVector2D(FMath::Abs(Angle)<45?Throttle:0,0),FMath::Clamp(Angle/20.f,-1.f,1.f));
}
bool ATraversalQueen::BeginEscapeJump(FVector Away)
{
    if(bJumping||ActiveLegs()==0||DamageAge<1) return false;
    int32 Left=0,Right=0;
    for(int32 I=0;I<6;++I) if(!Missing(I)) { if(I<3) ++Left; else ++Right; }
    JumpImbalance=float(Right-Left)/3.f;
    const FVector Skewed=FRotator(0,JumpImbalance*24,0).RotateVector(Away);
    JumpStart=GetActorLocation(); JumpEnd=JumpStart+Skewed*(ActiveLegs()>=2?1400:650);
    FVector G;
    if(!Ground(JumpEnd,G)) { JumpCooldown=2; return false; }
    JumpEnd.Z=G.Z+TargetHeight;
    JumpBaseRotation=GetActorRotation();
    const float LandingYaw=Yaw+JumpImbalance*18;
    for(int32 I=0;I<6;++I) if(!Missing(I))
    {
        FVector N; const FVector Offset=FRotator(0,LandingYaw-Yaw,0).RotateVector(Neutral(I));
        if(!Ground(JumpEnd+Offset,G,&N)) { JumpCooldown=2; return false; }
        StompStartFeet[I]=Legs[I].Foot; LandingFeet[I]=G+N*50;
        if(!Ground(Legs[I].Foot,G,&N)) { JumpCooldown=2; return false; }
        JumpFeet[I]=G+N*50;
    }
    // Reject a flight whose body would cross terrain or a wall. No teleport through obstacles.
    FVector Previous=JumpStart;
    for(int32 I=1;I<=16;++I)
    {
        const float T=I/16.f;
        const FVector P=FMath::Lerp(JumpStart,JumpEnd,T)+FVector(0,0,4*JumpHeight*T*(1-T));
        FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(QueenJumpPath),false,this);
        if(GetWorld()->SweepSingleByChannel(H,Previous,P,JumpBaseRotation.Quaternion(),ECC_WorldStatic,FCollisionShape::MakeBox(FVector(540,450,85)),Q))
        { JumpCooldown=2; return false; }
        Previous=P;
    }
    JumpClock=-.85f; JumpLaunched=false; Stomped=false; bJumping=true; ++Jumps; SetMotion(FVector2D::ZeroVector,0);
    SetAction(TEXT("ESCAPE / STOMP"));
    UE_LOG(LogTemp,Display,TEXT("QUEEN_AI jump=%d imbalance=%.2f"),Jumps,JumpImbalance);
    return true;
}
void ATraversalQueen::AdvanceJump(float Dt)
{
    Clock+=Dt; DamageAge+=Dt; JumpClock+=Dt;
    if(JumpClock<0)
    {
        // Large foot lift, fast downward strike, body compression, then a short powerful extension.
        // Planted feet stay above terrain; this is authored propulsion rather than a force simulation.
        const float Prep=JumpClock+.85f;
        const float FootT=FMath::Clamp(Prep/.4f,0.f,1.f);
        const float Lift=160*FMath::Sin(PI*FootT);
        const float Compression=Prep<.6f?110*FMath::Clamp(Prep/.6f,0.f,1.f):110*(1-FMath::Clamp((Prep-.6f)/.25f,0.f,1.f));
        FHitResult H; Body->MoveComponent(JumpStart-FVector(0,0,Compression)-GetActorLocation(),JumpBaseRotation,true,&H);
        if(H.bBlockingHit) { bJumping=false; bFailed=true; Failure=TEXT("stomp_collision"); return; }
        for(int32 I=0;I<6;++I) if(!Missing(I))
        {
            Legs[I].Swing=false;
            Legs[I].Foot=FMath::Lerp(StompStartFeet[I],JumpFeet[I],FootT)+FVector(0,0,Lift);
            PoseLeg(I);
            if(FootT>=1&&!Stomped) DrawDebugCircle(GetWorld(),JumpFeet[I],180,24,FColor(255,180,70),false,.25f,0,7,FVector::ForwardVector,FVector::RightVector,false);
        }
        if(FootT>=1&&!Stomped) { Stomped=true; ++StompCount; UE_LOG(LogTemp,Display,TEXT("QUEEN_AI stomp time=%.2f"),Clock); }
        SetAction(Prep<.4f?TEXT("ESCAPE / STOMP"):Prep<.6f?TEXT("ESCAPE / COMPRESS"):TEXT("ESCAPE / DRIVE"));
        ActualSpeed=0; return;
    }
    SetAction(TEXT("ESCAPE / AIRBORNE"));
    const float T=FMath::Clamp(JumpClock/JumpDuration,0.f,1.f);
    const FVector Previous=GetActorLocation();
    const FVector P=FMath::Lerp(JumpStart,JumpEnd,T)+FVector(0,0,4*JumpHeight*T*(1-T));
    FRotator R=JumpBaseRotation;
    R.Roll+=JumpImbalance*22*FMath::Sin(PI*T);
    R.Pitch-=8*FMath::Sin(2*PI*T); R.Yaw+=JumpImbalance*18*T;
    FHitResult Hit; Body->MoveComponent(P-Previous,R,true,&Hit);
    if(Hit.bBlockingHit) { bJumping=false; bFailed=true; Failure=TEXT("jump_collision"); SetAction(TEXT("BLOCKED / R RESET")); return; }
    MeasuredJumpRise=FMath::Max(MeasuredJumpRise,float((GetActorLocation().Z-FMath::Lerp(JumpStart.Z,JumpEnd.Z,T))/100));
    MeasuredJumpTravel=FMath::Max(MeasuredJumpTravel,float(FVector::Dist2D(GetActorLocation(),JumpStart)/100));
    ActualSpeed=FVector::Dist2D(Previous,P)/Dt/100; Distance+=FVector::Dist2D(Previous,P)/100;
    Yaw=R.Yaw;
    for(int32 I=0;I<6;++I) if(!Missing(I))
    {
        Legs[I].Swing=false;
        Legs[I].Foot=FMath::Lerp(JumpFeet[I],LandingFeet[I],T)+FVector(0,0,4*(JumpHeight+60)*T*(1-T));
        PoseLeg(I);
    }
    if(T>=1)
    {
        bJumping=false; ++Landings; JumpCooldown=4+FMath::Abs(JumpImbalance)*3;
        RoutePosition=P; BodyBias=FVector::ZeroVector; Velocity=FVector::ZeroVector; Speed=ActualSpeed=0;
        for(int32 I=0;I<6;++I) if(!Missing(I)) { Legs[I].Anchor=Legs[I].Foot=LandingFeet[I]; Legs[I].LastTouchdown=Clock; }
        UE_LOG(LogTemp,Display,TEXT("QUEEN_AI landed=%d recovery=%.2f"),Landings,JumpCooldown);
    }
}
TArray<FString> ATraversalQueen::JointReadout() const
{
    TArray<FString> Out;
    for(int32 I=0;I<6;++I)
    {
        if(Missing(I)) { Out.Add(FString::Printf(TEXT("L%d LOST"),I)); continue; }
        const auto& L=Legs[I];
        const FVector RootAxis=GetActorRotation().UnrotateVector(L.Points[1]-L.Points[0]);
        FString S=FString::Printf(TEXT("L%d %s | root yaw %.1f | bends:"),I,bJumping?TEXT("AIR"):L.Swing?TEXT("SWING"):TEXT("STANCE"),RootAxis.Rotation().Yaw);
        for(int32 J=1;J<5;++J)
        {
            const FVector A=(L.Points[J]-L.Points[J-1]).GetSafeNormal(),B=(L.Points[J+1]-L.Points[J]).GetSafeNormal();
            S+=FString::Printf(TEXT(" %.1f"),FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A,B),-1.,1.))));
        }
        if(bPhysicalBody) S+=FString::Printf(TEXT(" | %.1f kN"),SupportForces[I]/100000.f);
        Out.Add(S);
    }
    return Out;
}
void ATraversalQueen::DrawDiagnostics() const
{
    const FVector C=bPhysicalBody?Body->GetCenterOfMass():GetActorLocation(); FVector G;
    DrawDebugSphere(GetWorld(),C,45,12,FColor::Yellow,false,0,0,4);
    DrawDebugString(GetWorld(),C+FVector(0,0,100),bPhysicalBody?TEXT("PHYSICS BODY / COM"):TEXT("CONTROL COM (kinematic)"),nullptr,FColor::Yellow,0,true);
    if(Ground(C,G)) DrawDebugLine(GetWorld(),C,G,FColor::Yellow,false,0,0,5);
    const auto Hull=SupportHull();
    for(int32 I=0;I<Hull.Num();++I)
    {
        FVector A=C+FVector(Hull[I],0),B=C+FVector(Hull[(I+1)%Hull.Num()],0),GA,GB;
        if(Ground(A,GA)&&Ground(B,GB)) DrawDebugLine(GetWorld(),GA+FVector(0,0,15),GB+FVector(0,0,15),FColor::Green,false,0,0,5);
    }
    for(int32 I=0;I<6;++I) if(!Missing(I)) for(int32 J=0;J<5;++J)
        DrawDebugLine(GetWorld(),Legs[I].Points[J],Legs[I].Points[J+1],Legs[I].Swing?FColor::Cyan:FColor::Green,false,0,0,4);
}
