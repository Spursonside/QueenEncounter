#pragma once
#include "CoreMinimal.h"
class UWorld;
class AActor;

// Small fixed arena graph. Walkability comes from collision queries, not hand-authored waypoints.
class FQueenTerrainPath
{
public:
    void Build(UWorld* World, AActor* Ignore);
    bool Find(const FVector& Start, const FVector& Goal, TArray<FVector>& Out) const;
    int32 WalkableCount() const;
private:
    struct FNode { FVector Point = FVector::ZeroVector; bool Walkable = false; };
    static constexpr int32 Width = 49;
    static constexpr float Spacing = 225.f;
    TArray<FNode> Nodes;
    bool CanStep(int32 A, int32 B) const;
    int32 Closest(const FVector& P) const;
};
