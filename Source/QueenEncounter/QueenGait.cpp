#include "PhysicsQueen.h"
#include "GaitCriteria.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

// 참조: NVIDIA Isaac Lab Spot의 air_time / foot_clearance / gait / foot_slip.
// 원본 Python 코드를 이식한 것이 아니라 동일한 평가 개념을 사용한 독립 구현이다.
// 접촉은 지형 ray와 구 반지름의 근접 추정이다. 힘 센서라고 표기하지 않는다.
void APhysicsQueen::MeasureGait(float Dt)
{
    if (!bGaitStarted) return;
    const float Radius = 27.5f * TargetHeight / 270.f;
    for (int32 I=0; I<6; ++I)
    {
        if (Mask & (1<<I)) continue;
        auto& F=GaitFeet[I];
        const FVector P=Feet[I]->GetComponentLocation();
        FHitResult Hit;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(GaitGround),false,this);
        if (!GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,Radius),P-FVector(0,0,2000),FCollisionObjectQueryParams(ECC_WorldStatic),Q)) continue;
        const float Clearance=FVector::DotProduct(P-Hit.ImpactPoint,Hit.ImpactNormal)-Radius;
        const bool Contact=Clearance < (F.Contact ? 8.f : 4.f);
        MaxClearance=FMath::Max(MaxClearance,Clearance/100.f);
        if (Contact)
        {
            if (!F.Contact) F.PlantedCenter=P;
            if (!F.Swing && F.ContactTime>.15f)
            {
                // 접지 속도와 별개로 실제 디딘 자리에서 얼마나 벗어났는지 측정한다.
                const float Drift=FVector::VectorPlaneProject(P-F.PlantedCenter,Hit.ImpactNormal).Size()/100.f;
                PlantDriftMax=FMath::Max(PlantDriftMax,Drift);
                PlantDriftIntegral+=Drift*Dt; PlantSeconds+=Dt;
            }
            // 접점 속도만으로는 순수 구르기를 놓친다. 발 중심의 접선 속도와 각속도를 따로 감점한다.
            const FVector V=Feet[I]->GetPhysicsLinearVelocity();
            StanceDriftIntegral += FVector::VectorPlaneProject(V,Hit.ImpactNormal).Size()/100.f*Dt;
            RollIntegral += Feet[I]->GetPhysicsAngularVelocityInRadians().Size()*Dt;
            StanceSeconds += Dt;
            if (!F.Contact && F.Armed && F.AirTime>=.15f && F.Peak>=25.f &&
                (CurriculumStage==0 || FVector::Dist2D(P,F.LiftStart)>=25.f))
            {
                ++F.Steps;
                const float Travel=FVector::Dist2D(P,F.LiftStart)/100.f;
                StepTravelSum+=Travel; StepTravelMax=FMath::Max(StepTravelMax,Travel);
            }
            F.HadContact=true; F.Armed=false; F.AirTime=0; F.Peak=0;
            F.ContactTime+=Dt;
        }
        else
        {
            if (F.Contact) { F.LiftStart=P; F.Armed=F.HadContact && F.Swing; }
            F.AirTime+=Dt; F.Peak=FMath::Max(F.Peak,Clearance); F.ContactTime=0;
        }
        F.Contact=Contact;
        // 공중에 있기만 해서는 보상이 되지 않는다. 예정된 지지/이동 단계와 접지 상태를 비교한다.
        MatchIntegral += ((F.Swing ? !Contact : Contact) ? 1.f : 0.f)*Dt;
        GaitSeconds += Dt;
    }
}

int32 APhysicsQueen::ValidSteps() const
{
    int32 N=0; for (int32 I=0;I<6;++I) if (!(Mask&(1<<I))) N+=GaitFeet[I].Steps; return N;
}
bool APhysicsQueen::PassedStage() const
{
    if (!bStructuredGait) return bReachedGoal && !bFailed;
    int32 Minimum=MAX_int32;
    for (int32 I=0;I<6;++I) if (!(Mask&(1<<I))) Minimum=FMath::Min(Minimum,GaitFeet[I].Steps);
    if (Minimum==MAX_int32) return false;
    return QueenGaitCriteria::Pass(CurriculumStage,bFailed,Clock,UprightFraction(),StanceDrift(),FootRoll(),GaitMatch(),Minimum,ForwardDistance());
}

// 초기 자세 기준 순운동학이다. 계산 결과는 각도 목표에만 사용하고 물체 위치를 직접 옮기지 않는다.
FVector APhysicsQueen::FootFK(int32 I,const float* A) const
{
    const float Side=I<3 ? 1.f : -1.f;
    const float X[3]={140,0,-140};
    const float Yaw=Side*(45.f+(I%3)*45.f);
    const FVector Hip=FRotator(0,Yaw+A[0],0).RotateVector(FVector(55,0,0));
    const FVector UpperV=FRotator(A[1],0,0).RotateVector(FVector(230,0,130));
    const FVector LowerV=FRotator(A[1]+A[2],0,0).RotateVector(FVector(130,0,-356));
    const FVector FootV=FRotator(A[1]+A[2],0,0).RotateVector(FVector(0,0,-27.5));
    return (FVector(X[I%3],210*Side,0)+Hip+FRotator(0,Yaw+A[0],0).RotateVector(UpperV+LowerV+FootV))*TargetHeight/270.f;
}
void APhysicsQueen::SolveFoot(int32 I,FVector WorldTarget,float Dt,FVector TargetVelocity)
{
    auto& F=GaitFeet[I];
    // 정지 자세에서 잠든 강체도 새 모터 목표를 적용받도록 깨운다.
    for (UStaticMeshComponent* Part : {Coxae[I].Get(),Upper[I].Get(),Lower[I].Get(),Shins[I].Get(),Feet[I].Get()}) Part->WakeAllRigidBodies();
    const FVector BodyP=Body->GetComponentLocation();
    FVector Reference=Start;
    Reference.Z=BodyP.Z-CurrentHeight+TargetHeight;
    // 추가 유전자 4~7이 0이면 기존 V12 보행 파라미터를 그대로 재현한다.
    const float Slot=GaitSlot();
    const float Speed=FastGait() ? TargetSpeed*100.f : FootOffset()*2.f/(6.f*Slot);
    // 3초 정착 후 12초 동안 가속한다. 몸체를 순간 이동하거나 외력으로 끌지 않는다.
    const float Advance=FastGait() ? Speed*(GaitClock<12.f ? GaitClock*GaitClock/24.f : GaitClock-6.f) : GaitClock*Speed;
    if (CurriculumStage>0) Reference.X+=FMath::Min(Advance,float(BodyP.X-Start.X)+(FastGait()?100.f:30.f));
    // 지지 다리는 목표 몸체 자세 기준, 이동 다리는 실제 월드 착지점 기준으로 보정한다.
    const FVector Desired=F.Swing ? WorldTarget : BodyP+Body->GetComponentQuat().RotateVector(WorldTarget-Reference);
    const FVector Foot=Feet[I]->GetComponentLocation();
    // 위치 오차만 적분하면 발이 목표를 지나 튀어 오른다. 측정 속도로 제동한다.
    const float Damping=.20f+Genes[3]*.10f;
    // 긴 보폭에서 이동 중인 발만 더 빠르게 추종한다. 지지 다리의 제어 이득은 유지한다.
    const float SwingResponse=F.Swing ? 1.f+FMath::Max(0.f,Genes[9]) : 1.f;
    const FVector Error=(Desired-Foot-Feet[I]->GetPhysicsLinearVelocity()*Damping).GetClampedToMaxSize(100.f*SwingResponse);
    FVector TaskVelocity=Error*(3.f+1.8f*Genes[7])*SwingResponse;
    if (FastGait())
    {
        // 속도 선행 항의 증폭을 피하고 실제 발 위치/속도 피드백으로만 제어한다.
        const float Gain=7.f+3.f*Genes[7];
        TaskVelocity=((Desired-Foot)-Feet[I]->GetPhysicsLinearVelocity()*(.06f+.04f*Genes[3]))
            .GetClampedToMaxSize(200.f)*Gain;
    }
    UPhysicsConstraintComponent* Joints[3]={CoxaJoints[I],Hips[I],Knees[I]};
    UStaticMeshComponent* Parents[3]={Body,Coxae[I],Upper[I]};
    FVector Jacobian[3];
    for (int32 J=0;J<3;++J)
    {
        const auto Frame=Joints[J]->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1);
        const FVector LocalAxis=Frame.GetRotation().RotateVector(J==0 ? FVector(0,0,-1) : FVector(0,1,0));
        const FVector Axis=Parents[J]->GetComponentQuat().RotateVector(LocalAxis);
        const FVector Pivot=Joints[J]->ConstraintInstance.GetConstraintLocation();
        Jacobian[J]=FVector::CrossProduct(Axis,Foot-Pivot)*(PI/180.f);
    }
    // 실제 발 위치와 실제 관절 축으로 감쇠 최소제곱 해를 구한다.
    // 목표 각도를 누적하되 관절 한계와 각속도로 제한해 접지 오차가 폭주하지 않게 한다.
    double M[3][4]={};
    for (int32 Row=0;Row<3;++Row)
    {
        for (int32 Col=0;Col<3;++Col) M[Row][Col]=FVector::DotProduct(Jacobian[Row],Jacobian[Col])+(Row==Col ? 4.0 : 0.0);
        M[Row][3]=FastGait() ? FVector::DotProduct(Jacobian[Row],TaskVelocity)*Dt : FVector::DotProduct(Jacobian[Row],Error)*(3.f+1.8f*Genes[7])*SwingResponse*Dt;
    }
    for (int32 Pivot=0;Pivot<3;++Pivot)
    {
        const double D=M[Pivot][Pivot];
        for (int32 Col=Pivot;Col<4;++Col) M[Pivot][Col]/=D;
        for (int32 Row=0;Row<3;++Row) if (Row!=Pivot)
        {
            const double Factor=M[Row][Pivot];
            for (int32 Col=Pivot;Col<4;++Col) M[Row][Col]-=Factor*M[Pivot][Col];
        }
    }
    const float Limits[3]={35,45,60};
    for (int32 J=0;J<3;++J)
    {
        const float Rate=FastGait() ? 70.f+30.f*Genes[9] : (15.f+6.f*Genes[7])*SwingResponse;
        const float Delta=FMath::Clamp(float(M[J][3]),-Rate*Dt,Rate*Dt);
        const float Next=FMath::Clamp(F.Angles[J]+Delta,-Limits[J],Limits[J]);
        Effort+=FMath::Abs(Next-F.Angles[J]); F.Angles[J]=Next;
    }
    CoxaJoints[I]->SetAngularOrientationTarget(FRotator(0,F.Angles[0],0));
    Hips[I]->SetAngularOrientationTarget(FRotator(F.Angles[1],0,0));
    Knees[I]->SetAngularOrientationTarget(FRotator(F.Angles[2],0,0));
    CalfJoints[I]->SetAngularOrientationTarget(FRotator::ZeroRotator);
    FRotator AnkleTarget=FRotator::ZeroRotator;
    if (bPlantFeet)
    {
        // 발목 기준 좌표의 기울기를 상쇄해 구형 발이 종아리를 따라 굴러가지 않게 한다.
        const FQuat A=Shins[I]->GetComponentQuat()*Ankles[I]->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1).GetRotation();
        const FQuat B=FRotator(0,A.Rotator().Yaw,0).Quaternion();
        AnkleTarget=(B.Inverse()*A).Rotator();
        AnkleTarget.Yaw=0;
        AnkleTarget.Pitch=FMath::Clamp(AnkleTarget.Pitch,-30.f,30.f);
        AnkleTarget.Roll=FMath::Clamp(AnkleTarget.Roll,-18.f,18.f);
    }
    Ankles[I]->SetAngularOrientationTarget(AnkleTarget);
}
void APhysicsQueen::DriveGait(float Dt)
{
    if (Clock<3.f) return;
    if (!bGaitStarted)
    {
        for (int32 I=0;I<6;++I) GaitFeet[I].Anchor=Feet[I]->GetComponentLocation();
        bGaitStarted=true;
    }
    Body->WakeAllRigidBodies();
    // 추가 유전자 4~7이 0이면 기존 V12 보행 파라미터를 그대로 재현한다.
    const float Slot=GaitSlot();
    // 좌우를 번갈아 도는 wave gait. 파괴된 다리는 순서에서 제외한다.
    const int32 Order[6]={0,4,2,3,1,5};
    TArray<int32> Active; for (int32 I:Order) if (!(Mask&(1<<I))) Active.Add(I);
    if (Active.IsEmpty()) return;
    // 대각선 두 다리를 함께 움직여 나머지 네 다리가 몸체를 지지한다.
    const int32 Pairs[3][2]={{0,5},{2,3},{1,4}};
    const int32 Count=FastGait()?3:Active.Num();
    const int32 PreviousSlot=int32(GaitClock/Slot)%Count;
    const float OldPhase=FMath::Fmod(GaitClock,Slot)/Slot;
    bool WaitForLanding=false;
    for (int32 I:Active)
    {
        const bool Previous=FastGait() ? I==Pairs[PreviousSlot][0] || I==Pairs[PreviousSlot][1] : I==Active[PreviousSlot];
        if (Previous && OldPhase>.94f && GaitFeet[I].ContactTime<(.15f+.10f*Genes[6])) WaitForLanding=true;
    }
    TouchdownWait=WaitForLanding ? TouchdownWait+Dt : 0.f;
    if (TouchdownWait>4.f) { FailureReason=TEXT("touchdown_timeout"); bFailed=true; return; }
    if (!WaitForLanding) GaitClock+=Dt;
    const int32 SelectedSlot=int32(GaitClock/Slot)%Count;
    const float Phase=FMath::Fmod(GaitClock,Slot)/Slot;
    for (int32 I:Active)
    {
        auto& F=GaitFeet[I];
        const bool Selected=FastGait() ? I==Pairs[SelectedSlot][0] || I==Pairs[SelectedSlot][1] : I==Active[SelectedSlot];
        const bool Swing=Selected && Phase<.8f;
        if (Swing && !F.Swing)
        {
            F.From=Feet[I]->GetComponentLocation(); F.To=F.Anchor;
            if (CurriculumStage>0)
            {
                // 몸체의 중립 발 위치 앞에서 다음 발판을 찾고, 지지 중 목표는 월드 좌표로 유지한다.
                const float Zero[3]={};
                F.To=FVector(Body->GetComponentLocation().X,Start.Y,Body->GetComponentLocation().Z)+FootFK(I,Zero);
                F.To.X+=FootOffset()*(FastGait() ? .3f+.7f*FMath::Clamp(GaitClock/12.f,0.f,1.f) : 1.f);
            }
            FHitResult Ground; FCollisionQueryParams Q(SCENE_QUERY_STAT(GaitLanding),false,this);
            if (GetWorld()->LineTraceSingleByObjectType(Ground,F.To+FVector(0,0,2000),F.To-FVector(0,0,2000),FCollisionObjectQueryParams(ECC_WorldStatic),Q))
                F.To.Z=Ground.ImpactPoint.Z+(27.5f*TargetHeight/270.f)/FMath::Max(float(Ground.ImpactNormal.Z),.5f);
        }
        if (!Swing && F.Swing) F.Anchor=F.To;
        F.Swing=Swing;
        FVector Target=F.Anchor, Velocity=FVector::ZeroVector;
        if (Swing)
        {
            const float T=Phase/.8f, Smooth=T*T*(3-2*T);
            Target=FMath::Lerp(F.From,F.To,Smooth);
            const float Lift=FastGait()?70.f+30.f*Genes[2]:65.f+Genes[2]*15.f;
            Target.Z+=Lift*FMath::Sin(PI*T);
            Velocity=(F.To-F.From)*(6.f*T*(1.f-T)/(Slot*.8f));
            Velocity.Z+=Lift*PI*FMath::Cos(PI*T)/(Slot*.8f);
        }
        SolveFoot(I,Target,Dt,Velocity);
    }
}
