#include "QueenTerrainLibrary.h"
#include "Landscape.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

// 에디터에서만 높이맵을 생성한다. 저장된 Landscape와 충돌 데이터는 실행 시 그대로 사용한다.
bool UQueenTerrainLibrary::CreateTestLandscape(UObject* WorldContext, int32 Seed)
{
#if WITH_EDITOR
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World) return false;
    for (TActorIterator<ALandscape> It(World); It; ++It) return false;
    constexpr int32 Size = 253;
    constexpr float Spacing = 200.f; // 2 m samples, 504 m square, 16 components.
    FRandomStream Random(Seed);
    FVector2D Offset(Random.FRandRange(10, 500), Random.FRandRange(10, 500));
    TArray<uint16> Heights;
    Heights.SetNumUninitialized(Size * Size);
    for (int32 Y = 0; Y < Size; ++Y)
    for (int32 X = 0; X < Size; ++X)
    {
        FVector2D P((X - 126) * Spacing, (Y - 126) * Spacing);
        // Domain-warped fractal noise: hills, smaller undulations, broad ridges.
        FVector2D Warp(FMath::PerlinNoise2D(P / 14000.f + Offset),
                       FMath::PerlinNoise2D(P / 14000.f + Offset + FVector2D(31, 71)));
        FVector2D Q = P + Warp * 1600.f;
        float H = 0, Amplitude = 1200.f, Wavelength = 9000.f;
        for (int32 Octave = 0; Octave < 5; ++Octave)
        {
            H += Amplitude * FMath::PerlinNoise2D(Q / Wavelength + Offset);
            Amplitude *= 0.45f;
            Wavelength *= 0.5f;
        }
        H += 250.f * (0.5f - FMath::Abs(FMath::PerlinNoise2D(Q / 6000.f + Offset + FVector2D(91, 13))));
        // 38m 폭의 새 다리가 초기화 순간 지형에 파묻히지 않도록 반경 22m를 평탄화한다.
        // 이후 13m 구간에서 노이즈 지형으로 연결된다. 기존 저장 맵은 재생성하지 않는다.
        float Blend = FMath::Clamp((P.Size() - 2200.f) / 1300.f, 0.f, 1.f);
        H *= Blend * Blend * (3.f - 2.f * Blend);
        Heights[Y * Size + X] = uint16(FMath::Clamp(FMath::RoundToInt(32768.f + H * 128.f / 100.f), 0, 65535));
    }
    ALandscape* Terrain = World->SpawnActor<ALandscape>();
    Terrain->SetActorLabel(FString::Printf(TEXT("Queen_NoiseLandscape_Seed%d"), Seed));
    Terrain->Tags.Add(FName(*FString::Printf(TEXT("TerrainSeed_%d"), Seed)));
    Terrain->SetActorLocation(FVector(-25200, -25200, 0));
    Terrain->SetActorScale3D(FVector(Spacing, Spacing, 100));
    Terrain->LandscapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Terrain.M_Terrain"));
    TMap<FGuid, TArray<uint16>> HeightData;
    HeightData.Add(FGuid(), MoveTemp(Heights));
    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerData;
    LayerData.Add(FGuid(), {});
    Terrain->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, 1, 63, HeightData, nullptr, LayerData, ELandscapeImportAlphamapType::Additive, {});
    Terrain->UpdateAllComponentMaterialInstances();
    Terrain->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("QUEEN_TERRAIN_CREATED seed=%d samples=253 spacing=200cm"), Seed);
    return true;
#else
    return false;
#endif
}
