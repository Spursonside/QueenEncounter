#pragma once
#include "CoreMinimal.h"
namespace QueenGaitCriteria
{
    inline bool PassSpeedTarget(bool Completed, bool Failed, float CruiseSeconds, float CruiseSpeed, float TargetFraction)
    {
        return Completed && !Failed && FMath::IsFinite(CruiseSeconds+CruiseSpeed+TargetFraction)
            && CruiseSeconds>=5.f && CruiseSpeed>=4.5f && CruiseSpeed<=5.5f && TargetFraction>=.8f;
    }
    // 완주 거리와 정상적인 보행을 함께 확인한다. 정지/구르기/넘어진 전진은 불합격이다.
    inline bool Pass(int32 Stage, bool Failed, float Seconds, float Upright, float Drift,
                     float Roll, float Match, int32 MinSteps, float Forward)
    {
        if (Failed || !FMath::IsFinite(Seconds+Upright+Drift+Roll+Match+Forward) || Seconds<=0 || (Stage==0 && Seconds<20) ||
            Upright<.9f || Drift>.3f || Roll>.6f || Match<.75f || MinSteps<1) return false;
        const float Goals[4]={0,5,15,50};
        return Stage==0 ? FMath::Abs(Forward)<2.f : Forward>=Goals[FMath::Clamp(Stage,0,3)];
    }
}
