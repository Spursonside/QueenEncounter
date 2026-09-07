#pragma once
#include "CoreMinimal.h"

namespace QueenSelection
{
    struct Result { float Score; bool ReachedGoal; bool Failed; };
    // 점수가 높아도 미완주/물리 실패 후보는 번식하지 않는다. 빈 결과는 새 집단 생성을 뜻한다.
    inline TArray<int32> Parents(const TArray<Result>& Results)
    {
        TArray<int32> Indices;
        for (int32 I=0; I<Results.Num(); ++I)
            if (Results[I].ReachedGoal && !Results[I].Failed && FMath::IsFinite(Results[I].Score)) Indices.Add(I);
        Indices.Sort([&](int32 A, int32 B) { return Results[A].Score > Results[B].Score; });
        if (Indices.Num() > 4) Indices.SetNum(4);
        return Indices;
    }
}
