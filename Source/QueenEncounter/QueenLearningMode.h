#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "QueenLearningMode.generated.h"
class APhysicsQueen;

UCLASS()
class QUEENENCOUNTER_API AQueenLearningMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AQueenLearningMode();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void RestartPlayer(AController* C) override;
    int32 Generation = 0;
    int32 TotalGenerations = 48;
    int32 Mask = 0;
    int32 CurriculumStage = 0;
    int32 Population = 16;
    float EpisodeClock = 0;
    float EpisodeSeconds = 150;
    float TargetHeight = 700.f, TargetSpeed = 1.f, GoalMetres = 50.f;
    float BestScore = -BIG_NUMBER;
    float LastMean = 0;
    float BestDistance = 0;
    float BestUpright = 0;
    bool bReplay = false;
    bool bDemoLoop = false;
    bool bPlayerTest = false;
    bool bTerrainTest = false;
    FString RunDirectory;
    APhysicsQueen* VisibleRobot() const;
private:
    UPROPERTY() TArray<TObjectPtr<APhysicsQueen>> Robots;
    TArray<TArray<float>> Candidates;
    TArray<float> Mean;
    TArray<float> BestGenes;
    FRandomStream Random;
    int32 Seed = 20260906;
    float Sigma = 0.55f;
    float TiltWeight = 2.f, RotationWeight = 0.75f;
    bool bPolicyPlantFeet = false;
    bool bFinished = false;
    bool bDemoResultSaved = false;
    float DemoHoldSeconds = 0.f;
    bool bPlayerCaptureDone = false;
    bool bPlayerSmokeDone = false;
    bool bFreshPopulation = false;
    FString FrameDirectory;
    int32 FrameIndex = 0;
    float NextFrameTime = 0;
    int32 RecordFPS = 30;
    TArray<FString> ReplayFiles;
    int32 ReplayIndex = 0;
    void StartGeneration();
    void FinishGeneration();
    float Gaussian();
    bool LoadPolicy(const FString& File, TArray<float>& Out);
    void SavePolicy(const FString& File, const TArray<float>& Genes, float Score, float Distance, float Upright, const APhysicsQueen* MeasuredRobot);
};

UCLASS()
class QUEENENCOUNTER_API AQueenLearningHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
