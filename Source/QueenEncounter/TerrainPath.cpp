#include "TerrainPath.h"
#include "Engine/World.h"

// 충돌 조회로 지형 높이를 격자에 캐시한다. 경로 탐색마다 전체 지형을 다시 조회하지 않는다.
void FQueenTerrainPath::Build(UWorld* World, AActor* Ignore)
{
    Nodes.SetNum(Width * Width);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(QueenTerrainGraph), false, Ignore);
    for (int32 Y = 0; Y < Width; ++Y) for (int32 X = 0; X < Width; ++X)
    {
        FNode& N = Nodes[Y * Width + X];
        FVector P((X - Width / 2) * Spacing, (Y - Width / 2) * Spacing, 0);
        FHitResult Hit;
        if (!World->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 1800), P - FVector(0, 0, 600), ECC_WorldStatic, Params)) continue;
        N.Point = Hit.ImpactPoint;
        N.Walkable = Hit.ImpactNormal.Z >= 0.75f && !World->OverlapBlockingTestByChannel(N.Point + FVector(0, 0, 330), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeBox(FVector(230, 230, 85)), Params);
    }
}
bool FQueenTerrainPath::CanStep(int32 A, int32 B) const
{
    return Nodes.IsValidIndex(A) && Nodes.IsValidIndex(B) && Nodes[A].Walkable && Nodes[B].Walkable && FMath::Abs(Nodes[A].Point.Z - Nodes[B].Point.Z) <= 145.f;
}
int32 FQueenTerrainPath::Closest(const FVector& P) const
{
    int32 Best = INDEX_NONE; double Distance = BIG_NUMBER;
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        if (!Nodes[I].Walkable) continue;
        double D = FVector::DistSquared2D(P, Nodes[I].Point) + FMath::Square((P.Z - 420.f) - Nodes[I].Point.Z) * 0.25;
        if (D < Distance) { Distance = D; Best = I; }
    }
    return Best;
}
int32 FQueenTerrainPath::WalkableCount() const
{
    int32 Count = 0; for (const FNode& N : Nodes) Count += N.Walkable ? 1 : 0; return Count;
}
// A* 경로는 이동 목표를 제공한다. 실제 다리 접지와 관절 제어는 별도 계층에서 처리한다.
bool FQueenTerrainPath::Find(const FVector& Start, const FVector& Goal, TArray<FVector>& Out) const
{
    Out.Empty();
    const int32 S = Closest(Start), G = Closest(Goal);
    if (S == INDEX_NONE || G == INDEX_NONE) return false;
    TArray<float> Cost; Cost.Init(BIG_NUMBER, Nodes.Num());
    TArray<int32> Parent; Parent.Init(INDEX_NONE, Nodes.Num());
    TArray<uint8> Closed; Closed.Init(0, Nodes.Num());
    TArray<int32> Open; Open.Add(S); Cost[S] = 0;
    while (!Open.IsEmpty())
    {
        int32 Best = 0; float BestScore = BIG_NUMBER;
        for (int32 I = 0; I < Open.Num(); ++I)
        {
            float Score = Cost[Open[I]] + FVector::Distance(Nodes[Open[I]].Point, Nodes[G].Point);
            if (Score < BestScore) { BestScore = Score; Best = I; }
        }
        const int32 Current = Open[Best]; Open.RemoveAtSwap(Best, 1, EAllowShrinking::No);
        if (Closed[Current]) continue;
        Closed[Current] = 1;
        if (Current == G)
        {
            int32 Cursor = G;
            while (Cursor != INDEX_NONE) { Out.Insert(Nodes[Cursor].Point, 0); Cursor = Parent[Cursor]; }
            return true;
        }
        const int32 X = Current % Width, Y = Current / Width;
        for (int32 DY = -1; DY <= 1; ++DY) for (int32 DX = -1; DX <= 1; ++DX)
        {
            if ((!DX && !DY) || X + DX < 0 || X + DX >= Width || Y + DY < 0 || Y + DY >= Width) continue;
            const int32 Next = (Y + DY) * Width + X + DX;
            if (Closed[Next] || !CanStep(Current, Next)) continue;
            if (DX && DY && (!CanStep(Current, Y * Width + X + DX) || !CanStep(Current, (Y + DY) * Width + X))) continue;
            const float Candidate = Cost[Current] + FVector::Distance(Nodes[Current].Point, Nodes[Next].Point);
            if (Candidate < Cost[Next]) { Cost[Next] = Candidate; Parent[Next] = Current; Open.Add(Next); }
        }
    }
    return false;
}
