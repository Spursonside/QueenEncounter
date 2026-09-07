#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "QueenGameMode.generated.h"
UCLASS()
class QUEENENCOUNTER_API AQueenGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AQueenGameMode();
    virtual void BeginPlay() override;
    virtual void RestartPlayer(AController* NewPlayer) override;
private:
    void RunSmokeChecks();
};
