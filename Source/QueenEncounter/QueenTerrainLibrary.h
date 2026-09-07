#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "QueenTerrainLibrary.generated.h"

UCLASS()
class QUEENENCOUNTER_API UQueenTerrainLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Queen Terrain")
    static bool CreateTestLandscape(UObject* WorldContext, int32 Seed = 20260906);
};
