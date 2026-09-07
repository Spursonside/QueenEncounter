#pragma once
#include "CoreMinimal.h"

// Pure policy: no actor/world dependencies. Distances are centimetres.
namespace QueenRules
{
    inline FVector SolveKnee(const FVector& Hip, FVector& Foot, const FVector& Pole, float Upper = 460.f, float Lower = 520.f)
    {
        FVector Axis = (Foot - Hip).GetSafeNormal();
        if (Axis.IsNearlyZero()) Axis = FVector::DownVector;
        const float Distance = FMath::Clamp(FVector::Distance(Hip, Foot), FMath::Abs(Upper - Lower) + 0.1f, Upper + Lower - 0.1f);
        Foot = Hip + Axis * Distance;
        const float Along = (Upper * Upper - Lower * Lower + Distance * Distance) / (2.f * Distance);
        const float Height = FMath::Sqrt(FMath::Max(0.f, Upper * Upper - Along * Along));
        FVector Bend = FVector::VectorPlaneProject(Pole, Axis).GetSafeNormal();
        if (Bend.IsNearlyZero()) Bend = FVector::CrossProduct(Axis, FVector::RightVector).GetSafeNormal();
        return Hip + Axis * Along + Bend * Height;
    }
    enum class Attack { None, Pulse, Beam, Mortar };
    inline Attack SelectAttack(float Distance, bool HasSight, bool PulseReady, bool BeamReady, bool MortarReady)
    {
        if (!HasSight || Distance > 7000.f) return Attack::None;
        if (Distance < 850.f && PulseReady) return Attack::Pulse;
        if (Distance > 1800.f && MortarReady) return Attack::Mortar;
        if (BeamReady) return Attack::Beam;
        return Attack::None;
    }
    inline float CoreMultiplier(bool Exposed) { return Exposed ? 2.5f : 0.2f; }
    inline float LegDamage(float Incoming, float& Armor, float& Health)
    {
        if (Incoming <= 0.f || Health <= 0.f) return 0.f;
        const float Absorbed = FMath::Min(Armor, Incoming);
        Armor -= Absorbed;
        const float Effective = FMath::Min(Health, Incoming - Absorbed);
        Health -= Effective;
        return Effective;
    }
}
