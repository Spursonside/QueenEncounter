#pragma once
#include "CoreMinimal.h"

namespace QueenSupport
{
    inline double Cross(FVector2D A, FVector2D B, FVector2D C)
    { return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X); }
    inline TArray<FVector2D> Hull(TArray<FVector2D> Points)
    {
        Points.Sort([](const FVector2D& A, const FVector2D& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
        TArray<FVector2D> H;
        for (const FVector2D& P : Points)
        {
            while (H.Num() >= 2 && Cross(H[H.Num() - 2], H.Last(), P) <= 0) H.Pop(EAllowShrinking::No);
            H.Add(P);
        }
        const int32 LowerCount = H.Num();
        for (int32 I = Points.Num() - 2; I >= 0; --I)
        {
            while (H.Num() > LowerCount && Cross(H[H.Num() - 2], H.Last(), Points[I]) <= 0) H.Pop(EAllowShrinking::No);
            H.Add(Points[I]);
        }
        if (H.Num() > 1) H.Pop(EAllowShrinking::No);
        return H;
    }
    inline float Margin(const TArray<FVector2D>& Polygon, FVector2D COM)
    {
        if (Polygon.Num() < 3) return -300.f;
        double Result = BIG_NUMBER;
        for (int32 I = 0; I < Polygon.Num(); ++I)
        {
            const FVector2D A = Polygon[I], B = Polygon[(I + 1) % Polygon.Num()];
            Result = FMath::Min(Result, Cross(A, B, COM) / FMath::Max((B - A).Size(), 0.001));
        }
        return float(Result);
    }
    inline void Spring(float Target, float& Value, float& Velocity, float Dt, float Frequency = 3.f, float Damping = 0.75f)
    {
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Dt / 0.016f));
        const float Step = FMath::Min(Dt, 0.1f) / Steps;
        const float Omega = Frequency * 2.f * PI;
        for (int32 I = 0; I < Steps; ++I)
        {
            Velocity += (Omega * Omega * (Target - Value) - 2.f * Damping * Omega * Velocity) * Step;
            Value += Velocity * Step;
        }
    }
}
