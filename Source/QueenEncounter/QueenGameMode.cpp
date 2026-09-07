#include "QueenGameMode.h"
#include "QueenPlayer.h"
#include "QueenBoss.h"
#include "QueenHUD.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

AQueenGameMode::AQueenGameMode()
{
    DefaultPawnClass = AQueenPlayer::StaticClass();
    HUDClass = AQueenHUD::StaticClass();
}
void AQueenGameMode::RestartPlayer(AController* NewPlayer)
{
    RestartPlayerAtTransform(NewPlayer, FTransform(FRotator(0, 0, 0), FVector(-2400, 0, 120)));
}
void AQueenGameMode::BeginPlay()
{
    Super::BeginPlay();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Ground = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ground.M_Ground"));
    UMaterialInterface* Concrete = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Concrete.M_Concrete"));
    auto Block = [&](FVector Position, FVector Scale, FRotator Rotation, UMaterialInterface* Mat)
    {
        AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Position, Rotation);
        A->SetMobility(EComponentMobility::Movable);
        A->GetStaticMeshComponent()->SetStaticMesh(Cube);
        A->SetActorScale3D(Scale);
        A->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        if (Mat) A->GetStaticMeshComponent()->SetMaterial(0, Mat);
    };
    Block(FVector(0, 0, -60), FVector(120, 120, 1.2), FRotator::ZeroRotator, Ground);
    for (int32 Side : {-1, 1})
    {
        Block(FVector(6000 * Side, 0, 250), FVector(1, 120, 6), FRotator::ZeroRotator, Concrete);
        Block(FVector(0, 6000 * Side, 250), FVector(120, 1, 6), FRotator::ZeroRotator, Concrete);
        Block(FVector(-900, 1400 * Side, 170), FVector(3, 9, 3.4), FRotator(0, Side * 12, 0), Concrete);
        Block(FVector(2300, 2200 * Side, 100), FVector(12, 9, 2), FRotator::ZeroRotator, Concrete);
        Block(FVector(1000, 2200 * Side, 30), FVector(18, 9, 1), FRotator(7, 0, 0), Concrete);
        for (int32 I = 0; I < 5; ++I)
            Block(FVector(-3000 + I * 1400, Side * 4900, 350), FVector(2, 2, 7), FRotator::ZeroRotator, Concrete);
    }
    ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-50, -25, 0));
    Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->SetAtmosphereSunLight(true);
    Sun->GetLightComponent()->SetIntensity(4.f);
    Sun->GetLightComponent()->SetLightColor(FLinearColor(1.f, 0.83f, 0.66f));
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(0.8f);
    Sky->GetLightComponent()->SetLightColor(FLinearColor(0.46f, 0.62f, 0.9f));
    Sky->GetLightComponent()->RecaptureSky();
    GetWorld()->SpawnActor<AQueenBoss>(FVector(1400, 0, 420), FRotator(0, 180, 0));
    if (FParse::Param(FCommandLine::Get(), TEXT("QueenSmokeTest")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, this, &AQueenGameMode::RunSmokeChecks, 2.f, false);
    }
}

void AQueenGameMode::RunSmokeChecks()
{
    int32 Failures = 0;
    auto Check = [&](bool Passed, const TCHAR* Name)
    {
        if (!Passed) ++Failures;
        UE_LOG(LogTemp, Display, TEXT("QUEEN_SMOKE %s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    };
    AQueenPlayer* P = Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
    AQueenBoss* B = nullptr;
    int32 BossCount = 0;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) { B = *It; ++BossCount; }
    Check(P && B && BossCount == 1, TEXT("Single player and boss spawn"));
    if (P && B)
    {
        Check(P->GetActorLocation().Z > 70 && P->GetActorLocation().Z < 140, TEXT("Player rests on floor"));
        Check(B->bHasSight, TEXT("Boss detects player through visibility collision"));
        Check(B->State == EQueenState::Windup || B->State == EQueenState::Attack, TEXT("AI enters real attack sequence"));
        TArray<UStaticMeshComponent*> Parts;
        B->GetComponents(Parts);
        UStaticMeshComponent* Joint = nullptr;
        UStaticMeshComponent* Core = nullptr;
        for (UStaticMeshComponent* Part : Parts)
        {
            if (Part->GetFName() == FName("Joint0")) Joint = Part;
            if (Part->GetFName() == FName("Core")) Core = Part;
        }
        Check(Joint && Core, TEXT("Damageable parts registered"));
        if (Joint && Core)
        {
            B->ReceiveShot(Joint, 90);
            Check(B->ArmorAt(0) == 0 && B->LegHealthAt(0) == 120, TEXT("Armor absorbs exactly one layer"));
            B->ReceiveShot(Joint, 120);
            Check(B->BrokenLegs() == 1 && B->State == EQueenState::Stagger && B->IsCoreExposed(), TEXT("Limb break interrupts attack and exposes core"));
            float Before = B->Health;
            B->ReceiveShot(Joint, 500);
            Check(B->Health == Before, TEXT("Destroyed limb cannot award damage twice"));
            B->ReceiveShot(Core, 24);
            Check(FMath::IsNearlyEqual(Before - B->Health, 60.f), TEXT("Exposed core receives multiplied damage"));
            B->ReceiveShot(Core, 100000);
            Check(B->Health == 0 && B->State == EQueenState::Dead, TEXT("Boss reaches terminal death state"));
            B->ReceiveShot(Core, 100);
            Check(B->Health == 0, TEXT("Dead boss ignores damage"));
        }
        P->Hurt(1000);
        P->Hurt(10);
        Check(P->Health == 0, TEXT("Player death clamps health"));
    }
    UE_LOG(LogTemp, Display, TEXT("QUEEN_SMOKE_COMPLETE failures=%d"), Failures);
    FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
