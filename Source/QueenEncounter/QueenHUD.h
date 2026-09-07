#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "QueenHUD.generated.h"
UCLASS()
class QUEENENCOUNTER_API AQueenHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
