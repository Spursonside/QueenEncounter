#include "QueenLearningMode.h"
#include "PhysicsQueen.h"
#include "QueenPlayer.h"
#include "LearningSelection.h"
#include "GaitCriteria.h"
#include "LandscapeProxy.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

AQueenLearningMode::AQueenLearningMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    bDemoLoop = FParse::Param(FCommandLine::Get(), TEXT("QueenDemoLoop"));
    bPlayerTest = FParse::Param(FCommandLine::Get(), TEXT("QueenPlayerTest"));
    if (bPlayerTest) DefaultPawnClass = AQueenPlayer::StaticClass();
    else DefaultPawnClass = ASpectatorPawn::StaticClass();
    HUDClass = AQueenLearningHUD::StaticClass();
}
void AQueenLearningMode::RestartPlayer(AController* C)
{
    if (!bPlayerTest) { RestartPlayerAtTransform(C, FTransform(FRotator(-18,75,0),FVector(-550,-1600,760))); return; }
    FVector Spawn(3000,-2000,120);
    FHitResult Ground;
    if (GetWorld()->LineTraceSingleByObjectType(Ground,Spawn+FVector(0,0,10000),Spawn-FVector(0,0,10000),FCollisionObjectQueryParams(ECC_WorldStatic))) Spawn.Z=Ground.ImpactPoint.Z+100;
    const FRotator Aim=(FVector(0,0,700)-(Spawn+FVector(0,0,65))).Rotation();
    RestartPlayerAtTransform(C,FTransform(FRotator(0,Aim.Yaw,0),Spawn));
    C->SetControlRotation(Aim);
}
float AQueenLearningMode::Gaussian()
{
    return FMath::Sqrt(-2.f * FMath::Loge(FMath::Max(Random.FRand(), 0.00001f))) * FMath::Cos(2 * PI * Random.FRand());
}
bool AQueenLearningMode::LoadPolicy(const FString& File, TArray<float>& Out)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *File)) return false;
    TSharedPtr<FJsonObject> Obj;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Obj)) return false;
    bPolicyPlantFeet=false;
    Obj->TryGetBoolField(TEXT("world_level_ankle"),bPolicyPlantFeet);
    const TArray<TSharedPtr<FJsonValue>>* Array;
    if (!Obj->TryGetArrayField(TEXT("genes"), Array) || (Array->Num() != APhysicsQueen::GeneCount && Array->Num() != 40 && Array->Num() != 76)) return false;
    FString Version;
    if (!FParse::Param(FCommandLine::Get(),TEXT("QueenLegacyGait")) && (!Obj->TryGetStringField(TEXT("controller_version"),Version) || (Version!=TEXT("v12_contact_feedback") && Version!=TEXT("v13_speed_feedback") && Version!=TEXT("v14_large_stride") && Version!=TEXT("v15_paired_feedback")))) return false;
    if (!bReplay && !FParse::Param(FCommandLine::Get(),TEXT("QueenLegacyGait")))
    {
        bool Eligible=false;
        if (!Obj->TryGetBoolField(TEXT("eligible_parent"),Eligible) || !Eligible) return false;
    }
    if (bReplay)
    {
        Generation=int32(Obj->GetNumberField(TEXT("generation")));
        int32 OverrideStage=0;
        if (!FParse::Value(FCommandLine::Get(),TEXT("QueenStage="),OverrideStage) && Obj->HasField(TEXT("curriculum_stage")))
            CurriculumStage=FMath::Clamp(int32(Obj->GetNumberField(TEXT("curriculum_stage"))),0,3);
    }
    Out.Empty(); for (auto& V : *Array) Out.Add(float(V->AsNumber()));
    // 기존 40/76개 유전자 정책은 초기값으로만 이관한다. 추가 관절의 학습 완료를 의미하지 않는다.
    Out.SetNumZeroed(APhysicsQueen::GeneCount);
    return true;
}
// 영상에 사용한 정책과 실제 측정값을 함께 보관한다. 다른 보상 버전의 점수를 직접 비교하지 않는다.
void AQueenLearningMode::SavePolicy(const FString& File, const TArray<float>& Genes, float Score, float Distance, float Upright, const APhysicsQueen* MeasuredRobot)
{
    TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("algorithm"), TEXT("elite Gaussian evolution / terrain IK wave gait; Isaac Lab inspired rewards"));
    Obj->SetStringField(TEXT("rig_version"), TEXT("v12_low_clearance_feedback"));
    Obj->SetStringField(TEXT("engine"), TEXT("Unreal Engine 5.8.2 Chaos"));
    Obj->SetStringField(TEXT("test_map"), GetWorld()->GetMapName());
    Obj->SetNumberField(TEXT("generation"), Generation);
    Obj->SetNumberField(TEXT("seed"), Seed);
    Obj->SetNumberField(TEXT("missing_mask"), MeasuredRobot->Mask);
    Obj->SetNumberField(TEXT("score"), Score);
    Obj->SetNumberField(TEXT("forward_metres"), Distance);
    Obj->SetNumberField(TEXT("upright_fraction"), Upright);
    Obj->SetNumberField(TEXT("episode_seconds"), EpisodeSeconds);
    Obj->SetNumberField(TEXT("actual_seconds"), MeasuredRobot->ElapsedSeconds());
    Obj->SetNumberField(TEXT("target_height_m"), TargetHeight / 100.f);
    Obj->SetNumberField(TEXT("target_speed_m_s"), TargetSpeed);
    Obj->SetNumberField(TEXT("goal_metres"), GoalMetres);
    Obj->SetBoolField(TEXT("reached_goal"), MeasuredRobot->bReachedGoal);
    Obj->SetBoolField(TEXT("eligible_parent"), MeasuredRobot->PassedStage());
    Obj->SetStringField(TEXT("selection_version"), TEXT("v11_stage_completion_only"));
    Obj->SetStringField(TEXT("controller_version"), MeasuredRobot->bStructuredGait ? TEXT("v15_paired_feedback") : TEXT("legacy_cpg"));
    Obj->SetNumberField(TEXT("curriculum_stage"), MeasuredRobot->CurriculumStage);
    TArray<TSharedPtr<FJsonValue>> PerLegSteps;
    for (int32 I=0;I<6;++I) PerLegSteps.Add(MakeShared<FJsonValueNumber>(MeasuredRobot->StepsForLeg(I)));
    Obj->SetArrayField(TEXT("steps_per_leg"),PerLegSteps);
    Obj->SetNumberField(TEXT("active_gene_count"),MeasuredRobot->FastGait() ? 9 : MeasuredRobot->bStructuredGait ? 11 : APhysicsQueen::GeneCount);
    Obj->SetBoolField(TEXT("world_level_ankle"),MeasuredRobot->bPlantFeet);
    // 원시 유전자와 함께 실제 제어 단위를 기록하여 세대 간 변화를 비교한다.
    Obj->SetNumberField(TEXT("gait_slot_seconds"),MeasuredRobot->GaitSlot());
    Obj->SetNumberField(TEXT("foot_forward_offset_cm"),MeasuredRobot->FootOffset());
    Obj->SetNumberField(TEXT("touchdown_confirmation_seconds"),.15f+.10f*Genes[6]);
    Obj->SetNumberField(TEXT("foot_feedback_gain"),MeasuredRobot->FastGait()?7.f+3.f*Genes[7]:3.f+1.8f*Genes[7]);
    Obj->SetNumberField(TEXT("joint_target_rate_deg_s"),MeasuredRobot->FastGait()?70.f+30.f*Genes[9]:15.f+6.f*Genes[7]);
    Obj->SetNumberField(TEXT("max_stance_displacement_m"),MeasuredRobot->MaxPlantDrift());
    Obj->SetNumberField(TEXT("mean_stance_displacement_m"),MeasuredRobot->MeanPlantDrift());
    Obj->SetNumberField(TEXT("physics_substep_hz"),120);
    Obj->SetStringField(TEXT("criteria_version"),TEXT("v14_no_moving_time_floor"));
    Obj->SetStringField(TEXT("failure_reason"),MeasuredRobot->FailureReason);
    Obj->SetNumberField(TEXT("mean_actual_step_travel_m"),MeasuredRobot->StepTravelSum/FMath::Max(1,MeasuredRobot->ValidSteps()));
    Obj->SetNumberField(TEXT("max_actual_step_travel_m"),MeasuredRobot->StepTravelMax);
    Obj->SetNumberField(TEXT("swing_response_multiplier"),1.f+FMath::Max(0.f,Genes[9]));
    Obj->SetNumberField(TEXT("commanded_body_speed_m_s"),MeasuredRobot->CommandedSpeed());
    Obj->SetBoolField(TEXT("paired_gait"),MeasuredRobot->FastGait());
    Obj->SetBoolField(TEXT("speed_target_passed"),GoalMetres>=50.f && MeasuredRobot->PassedStage() && QueenGaitCriteria::PassSpeedTarget(MeasuredRobot->bReachedGoal,MeasuredRobot->bFailed,MeasuredRobot->CruiseSeconds,MeasuredRobot->CruiseDistance/FMath::Max(.001f,MeasuredRobot->CruiseSeconds),MeasuredRobot->CruiseTargetSeconds/FMath::Max(.001f,MeasuredRobot->CruiseSeconds)));
    Obj->SetNumberField(TEXT("time_to_10m_seconds"),MeasuredRobot->TenMetreTime);
    Obj->SetNumberField(TEXT("cruise_seconds"),MeasuredRobot->CruiseSeconds);
    Obj->SetNumberField(TEXT("cruise_speed_m_s"),MeasuredRobot->CruiseDistance/FMath::Max(.001f,MeasuredRobot->CruiseSeconds));
    Obj->SetNumberField(TEXT("cruise_target_fraction"),MeasuredRobot->CruiseTargetSeconds/FMath::Max(.001f,MeasuredRobot->CruiseSeconds));
    TArray<TSharedPtr<FJsonValue>> SpeedSamples;
    for (float V:MeasuredRobot->BodySpeedSamples) SpeedSamples.Add(MakeShared<FJsonValueNumber>(V));
    Obj->SetArrayField(TEXT("body_speed_1s_m_s"),SpeedSamples);
    Obj->SetNumberField(TEXT("valid_steps"), MeasuredRobot->ValidSteps());
    Obj->SetNumberField(TEXT("stance_center_speed_m_s"), MeasuredRobot->StanceDrift());
    Obj->SetNumberField(TEXT("contact_roll_rad_s"), MeasuredRobot->FootRoll());
    Obj->SetNumberField(TEXT("gait_match"), MeasuredRobot->GaitMatch());
    Obj->SetNumberField(TEXT("peak_clearance_m"), MeasuredRobot->PeakClearance());
    Obj->SetBoolField(TEXT("failed"), MeasuredRobot->bFailed);
    Obj->SetNumberField(TEXT("mean_height_m"), MeasuredRobot->MeanHeight());
    Obj->SetNumberField(TEXT("height_std_m"), MeasuredRobot->HeightDeviation());
    Obj->SetNumberField(TEXT("height_rmse_m"), MeasuredRobot->HeightErrorRMS());
    Obj->SetNumberField(TEXT("mean_speed_m_s"), MeasuredRobot->MeanSpeed());
    Obj->SetStringField(TEXT("reward_version"), TEXT("v13_completion_time_plant_height"));
    Obj->SetNumberField(TEXT("tilt_weight"), TiltWeight);
    Obj->SetNumberField(TEXT("rotation_weight"), RotationWeight);
    Obj->SetBoolField(TEXT("legacy_feet"),MeasuredRobot->bLegacyFeet);
    Obj->SetNumberField(TEXT("foot_length_cm"),(MeasuredRobot->bLegacyFeet?105.f:55.f)*TargetHeight/270.f);
    Obj->SetNumberField(TEXT("foot_width_cm"),(MeasuredRobot->bLegacyFeet?85.f:55.f)*TargetHeight/270.f);
    Obj->SetStringField(TEXT("foot_shape"),MeasuredRobot->bLegacyFeet?TEXT("legacy_box"):TEXT("sphere"));
    Obj->SetStringField(TEXT("foot_material"),MeasuredRobot->bLegacyFeet?TEXT("legacy_0.9_default_combine"):TEXT("dynamic_3_static_4_max_combine"));
    Obj->SetNumberField(TEXT("near_ground_foot_slip_m_s"),MeasuredRobot->MeanFootSlipSpeed());
    Obj->SetNumberField(TEXT("foot_contact_sample_seconds"),MeasuredRobot->FootContactSeconds());
    Obj->SetNumberField(TEXT("height_motion_weight"),1.f);
    Obj->SetNumberField(TEXT("goal_min_up_z"),.8f);
    Obj->SetNumberField(TEXT("mean_tilt_degrees"), MeasuredRobot->MeanTiltDegrees());
    Obj->SetNumberField(TEXT("mean_rotation_deg_s"), MeasuredRobot->MeanRotationSpeed());
    Obj->SetNumberField(TEXT("stable_progress_metres"), MeasuredRobot->StableDistance());
    TArray<TSharedPtr<FJsonValue>> Values;
    for (float G : Genes) Values.Add(MakeShared<FJsonValueNumber>(G));
    Obj->SetArrayField(TEXT("genes"), Values);
    FString Text; FJsonSerializer::Serialize(Obj, TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text, *File);
}
void AQueenLearningMode::BeginPlay()
{
    Super::BeginPlay();
    if (bDemoLoop)
    {
        // Benchmark는 60fps 물리 시간을 설정해도 실제 시간보다 빨리 실행될 수 있다.
        // 관찰용은 실시간 대기를 포함하는 고정 프레임 모드를 사용한다.
        FApp::SetBenchmarking(false); FApp::SetUseFixedTimeStep(false);
        GEngine->bUseFixedFrameRate=true; GEngine->FixedFrameRate=60.f;
    }
    // 실패 후보가 KillZ로 자동 삭제되면 세대 결과를 잃는다. 실험의 높이/범위 판정으로 종료한다.
    GetWorld()->GetWorldSettings()->bEnableWorldBoundsChecks = false;
    FParse::Value(FCommandLine::Get(), TEXT("QueenGenerations="), TotalGenerations);
    FParse::Value(FCommandLine::Get(), TEXT("QueenPopulation="), Population);
    FParse::Value(FCommandLine::Get(), TEXT("QueenMask="), Mask);
    FParse::Value(FCommandLine::Get(), TEXT("QueenStage="), CurriculumStage);
    CurriculumStage=FMath::Clamp(CurriculumStage,0,3);
    FParse::Value(FCommandLine::Get(), TEXT("QueenSeed="), Seed);
    FParse::Value(FCommandLine::Get(), TEXT("QueenEpisode="), EpisodeSeconds);
    FParse::Value(FCommandLine::Get(), TEXT("QueenHeight="), TargetHeight);
    FParse::Value(FCommandLine::Get(), TEXT("QueenSpeed="), TargetSpeed);
    FParse::Value(FCommandLine::Get(), TEXT("QueenGoal="), GoalMetres);
    TargetHeight = FMath::Clamp(TargetHeight, 310.f, 1000.f);
    TargetSpeed = FMath::Clamp(TargetSpeed, 0.2f, 8.f);
    GoalMetres = FMath::Clamp(GoalMetres, 1.f, 120.f);
    FParse::Value(FCommandLine::Get(), TEXT("QueenFrames="), FrameDirectory);
    FParse::Value(FCommandLine::Get(), TEXT("QueenRecordFPS="), RecordFPS);
    // 변이 폭과 안정성 가중치는 실행 인자로 조절하고 결과 JSON에 기록한다.
    FParse::Value(FCommandLine::Get(), TEXT("QueenSigma="), Sigma);
    FParse::Value(FCommandLine::Get(), TEXT("QueenTiltWeight="), TiltWeight);
    FParse::Value(FCommandLine::Get(), TEXT("QueenRotationWeight="), RotationWeight);
    Sigma = FMath::Clamp(Sigma, 0.01f, 1.f);
    TiltWeight = FMath::Max(TiltWeight, 0.f);
    RotationWeight = FMath::Max(RotationWeight, 0.f);
    RecordFPS = FMath::Clamp(RecordFPS, 1, 60);
    Population = FMath::Clamp(Population, 1, 32);
    TotalGenerations = FMath::Max(TotalGenerations, 1);
    Random.Initialize(Seed);
    RunDirectory = FPaths::ProjectDir() / TEXT("LearningRuns") / FString::Printf(TEXT("mask_%02d_seed_%d"), Mask, Seed);
    FParse::Value(FCommandLine::Get(), TEXT("QueenRunDir="), RunDirectory);
    IFileManager::Get().MakeDirectory(*RunDirectory, true);
    if (!FrameDirectory.IsEmpty()) IFileManager::Get().MakeDirectory(*FrameDirectory, true);
    Mean.Init(0, APhysicsQueen::GeneCount);
    FString SeedPolicy;
    if (FParse::Value(FCommandLine::Get(), TEXT("QueenSeedPolicy="), SeedPolicy) && !LoadPolicy(SeedPolicy, Mean)) { UE_LOG(LogTemp,Error,TEXT("Incompatible seed policy")); FPlatformMisc::RequestExitWithStatus(false,2); return; }
    FString ReplayPolicy;
    bReplay = FParse::Value(FCommandLine::Get(), TEXT("QueenReplay="), ReplayPolicy);
    FString ReplayDirectory;
    if (FParse::Value(FCommandLine::Get(), TEXT("QueenReplayDir="), ReplayDirectory))
    {
        IFileManager::Get().FindFiles(ReplayFiles, *(ReplayDirectory / TEXT("generation_*.json")), true, false);
        ReplayFiles.Sort();
        for (FString& File : ReplayFiles) File = ReplayDirectory / File;
        if (ReplayFiles.IsEmpty()) { FPlatformMisc::RequestExitWithStatus(false, 2); return; }
        bReplay = true;
        ReplayPolicy = ReplayFiles[0];
        TotalGenerations = ReplayFiles.Num();
    }
    if (bReplay)
    {
        if (!LoadPolicy(ReplayPolicy, Mean)) { UE_LOG(LogTemp, Error, TEXT("Cannot load replay policy")); FPlatformMisc::RequestExitWithStatus(false, 2); return; }
        Population = 1;
    }
    bool bLandscape = false;
    for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It) { bLandscape = true; break; }
    bTerrainTest = bLandscape;
    // 모든 후보가 똑같은 지형을 경험하도록 출발점을 공유한다. 후보 간 물리 충돌은 비활성화되어 있다.
    if (bLandscape) UE_LOG(LogTemp, Display, TEXT("QUEEN_TERRAIN_TEST shared terrain spawn; non-colliding population=%d"), Population);
    if (!bLandscape)
    {
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Floor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(15000, 15000, -70), FRotator::ZeroRotator);
    Floor->SetMobility(EComponentMobility::Movable);
    Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Floor->SetActorScale3D(FVector(700, 700, 1.4f));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    UMaterialInterface* Ground = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Concrete.M_Concrete"));
    if (Ground) Floor->GetStaticMeshComponent()->SetMaterial(0, Ground);
    }
    ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-55, -35, 0));
    Sun->GetLightComponent()->SetIntensity(4);
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->SetAtmosphereSunLight(true);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(1);
    Sky->GetLightComponent()->RecaptureSky();
    if (!bReplay)
        FFileHelper::SaveStringToFile(TEXT("generation,candidate,score,forward_metres,upright_fraction,missing_mask,mean_tilt_degrees,mean_rotation_deg_s,stable_progress_metres,reached_goal,physical_failed,eligible_parent\n"), *(RunDirectory / TEXT("evaluations.csv")));
    BestGenes = Mean;
    StartGeneration();
}
// 한 세대의 후보를 새 물리 바디로 초기화하여 이전 세대의 속도와 접촉 상태가 남지 않게 한다.
void AQueenLearningMode::StartGeneration()
{
    for (APhysicsQueen* R : Robots) if (IsValid(R)) R->Destroy();
    Robots.Empty(); Candidates.Empty();
    EpisodeClock = 0;
    NextFrameTime = 0;
    for (int32 I = 0; I < Population; ++I)
    {
        TArray<float> G = I == 0 ? BestGenes : Mean;
        if (bFreshPopulation) G.Init(0.f, APhysicsQueen::GeneCount);
        if (!bReplay && (bFreshPopulation || I > 0))
        {
            if (FParse::Param(FCommandLine::Get(),TEXT("QueenLegacyGait")))
                for (float& V:G) V=FMath::Clamp(V+Gaussian()*Sigma,-1.f,1.f);
            else for (int32 J:{0,1,2,3,4,5,6,7,8,9,37}) G[J]=FMath::Clamp(G[J]+Gaussian()*Sigma,-1.f,1.f);
        }
        Candidates.Add(G);
        APhysicsQueen* R = GetWorld()->SpawnActor<APhysicsQueen>();
        R->bTerrainTest = bTerrainTest;
        R->bPlantFeet = bPolicyPlantFeet || FParse::Param(FCommandLine::Get(),TEXT("QueenPlantFeet"));
        R->bStructuredGait = !FParse::Param(FCommandLine::Get(),TEXT("QueenLegacyGait"));
        R->CurriculumStage = CurriculumStage;
        R->bLegacyFeet = FParse::Param(FCommandLine::Get(),TEXT("QueenLegacyFeet"));
        R->TargetHeight = TargetHeight; R->TargetSpeed = TargetSpeed; R->GoalMetres = GoalMetres;
        R->TiltWeight = TiltWeight; R->RotationWeight = RotationWeight;
        R->Configure(bTerrainTest ? FVector(0, 0, TargetHeight * 310.f / 270.f) : FVector((I % 4) * 6500, (I / 4) * 6500, TargetHeight * 310.f / 270.f), G, Mask);
        Robots.Add(R);
    }
    UE_LOG(LogTemp, Display, TEXT("QUEEN_LEARN generation=%d population=%d mask=%d sigma=%.3f"), Generation, Population, Mask, Sigma);
}
APhysicsQueen* AQueenLearningMode::VisibleRobot() const { return Robots.Num() ? Robots[0].Get() : nullptr; }
void AQueenLearningMode::Tick(float Dt)
{
    Super::Tick(Dt);
    if (bFinished || Robots.IsEmpty()) return;
    if (bDemoLoop && bDemoResultSaved)
    {
        DemoHoldSeconds+=Dt;
        if (DemoHoldSeconds>=2.f)
            UGameplayStatics::OpenLevel(this,FName(*UGameplayStatics::GetCurrentLevelName(this)),true,TEXT("game=/Script/QueenEncounter.QueenLearningMode"));
        return;
    }
    EpisodeClock += Dt;
    for (APhysicsQueen* R : Robots) R->StepController(Dt);
    // 명시적인 검증 실행에서만 다리를 분리하여 새 관절까지 파괴되는지 검사한다.
    if (FParse::Param(FCommandLine::Get(), TEXT("QueenJointSmoke")) && EpisodeClock > 5.f && !(Robots[0]->Mask & 1))
        Robots[0]->SeverLeg(0);
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        if (APawn* Camera = bPlayerTest ? nullptr : PC->GetPawn())
        {
            const FVector Focus = Robots[0]->GetBody()->GetComponentLocation();
            Camera->SetActorLocation(Focus + FVector(1000, -1600, 260) * (TargetHeight / 310.f));
            PC->SetControlRotation((Focus - Camera->GetActorLocation()).Rotation());
        }
        if (!bPlayerTest && bReplay && PC->WasInputKeyJustPressed(EKeys::F4))
            for (int32 I : {0, 3, 1, 4, 2, 5}) if (!(Robots[0]->Mask & (1 << I))) { Robots[0]->SeverLeg(I); break; }
    }
    if (!FrameDirectory.IsEmpty() && EpisodeClock >= NextFrameTime)
    {
        FScreenshotRequest::RequestScreenshot(FrameDirectory / FString::Printf(TEXT("frame_%06d.png"), FrameIndex++), true, false);
        NextFrameTime += 1.f / RecordFPS;
    }
    if (bPlayerTest)
    {
        // 체험 모드는 제한 시간이나 목표 도달로 앱을 종료하지 않는다. 카메라는 플레이어 입력이 제어한다.
        FString ScreenshotPath;
        if (!bPlayerCaptureDone && EpisodeClock>3 && FParse::Value(FCommandLine::Get(),TEXT("QueenPlayerScreenshot="),ScreenshotPath))
        { FScreenshotRequest::RequestScreenshot(ScreenshotPath,true,false); bPlayerCaptureDone=true; }
        if (!bPlayerSmokeDone && EpisodeClock>4 && FParse::Param(FCommandLine::Get(),TEXT("QueenPlayerSmoke")))
        {
            auto* Player=Cast<AQueenPlayer>(UGameplayStatics::GetPlayerPawn(this,0));
            int32 Failures=0;
            auto Check=[&](bool OK,const TCHAR* Label) { Failures+=!OK; UE_LOG(LogTemp,Display,TEXT("QUEEN_PLAYER_SMOKE %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),Label); };
            Check(Player && Player->GetController(),TEXT("Possessed player and physical queen share map"));
            TArray<UStaticMeshComponent*> Parts; Robots[0]->GetComponents(Parts);
            UStaticMeshComponent* Target=nullptr;
            for (auto* Part:Parts) if (Part->GetFName()==TEXT("Upper_0")) Target=Part;
            Check(Target && Target->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Block,TEXT("Leg blocks player shot trace"));
            if (Target) { Check(Robots[0]->ReceivePlayerShot(Target,120),TEXT("Shot damage accepted")); Check((Robots[0]->Mask&1)!=0,TEXT("Five hits worth of damage severs leg")); }
            bPlayerSmokeDone=true;
            UE_LOG(LogTemp,Display,TEXT("QUEEN_PLAYER_SMOKE_COMPLETE failures=%d"),Failures);
            FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
        }
        return;
    }
    bool AllTerminal = true;
    for (const APhysicsQueen* R : Robots) AllTerminal &= R->bFailed || R->bReachedGoal;
    if (EpisodeClock >= EpisodeSeconds || AllTerminal)
    {
        if (bReplay)
        {
            SavePolicy(RunDirectory / TEXT("replay_result.json"), Mean, Robots[0]->Fitness(), Robots[0]->ForwardDistance(), Robots[0]->UprightFraction(), Robots[0]);
            SavePolicy(RunDirectory / FString::Printf(TEXT("replay_%04d.json"), Generation), Mean, Robots[0]->Fitness(), Robots[0]->ForwardDistance(), Robots[0]->UprightFraction(), Robots[0]);
            UE_LOG(LogTemp, Display, TEXT("QUEEN_REPLAY_RESULT gen=%d distance=%.3fm upright=%.3f frame=%d"), Generation, Robots[0]->ForwardDistance(), Robots[0]->UprightFraction(), FrameIndex);
            if (bDemoLoop)
            {
                SavePolicy(RunDirectory / (TEXT("demo_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT(".json")),Mean,Robots[0]->Fitness(),Robots[0]->ForwardDistance(),Robots[0]->UprightFraction(),Robots[0]);
                bDemoResultSaved=true; DemoHoldSeconds=0.f;
                return;
            }
            ++ReplayIndex;
            if (ReplayIndex < ReplayFiles.Num())
            {
                if (!LoadPolicy(ReplayFiles[ReplayIndex], Mean)) { bFinished = true; FPlatformMisc::RequestExitWithStatus(false, 2); return; }
                BestGenes = Mean;
                StartGeneration();
            }
            else { bFinished = true; FPlatformMisc::RequestExitWithStatus(false, 0); }
        }
        else FinishGeneration();
    }
}
void AQueenLearningMode::FinishGeneration()
{
    TArray<int32> Order;
    TArray<QueenSelection::Result> Results;
    FString Rows;
    LastMean = 0;
    for (int32 I = 0; I < Robots.Num(); ++I)
    {
        Order.Add(I);
        const float Score = Robots[I]->Fitness();
        // 모든 후보 정책을 보존해 최고 점수와 별개로 실제 최장 이동 개체도 재생할 수 있게 한다.
        SavePolicy(RunDirectory / FString::Printf(TEXT("candidate_%04d_%02d.json"),Generation,I),Candidates[I],Score,Robots[I]->ForwardDistance(),Robots[I]->UprightFraction(),Robots[I]);
        LastMean += Score;
        Results.Add({Score, Robots[I]->PassedStage(), Robots[I]->bFailed});
        Rows += FString::Printf(TEXT("%d,%d,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%d,%d,%d\n"), Generation, I, Score, Robots[I]->ForwardDistance(), Robots[I]->UprightFraction(), Mask, Robots[I]->MeanTiltDegrees(), Robots[I]->MeanRotationSpeed(), Robots[I]->StableDistance(), Robots[I]->bReachedGoal, Robots[I]->bFailed, Robots[I]->PassedStage());
    }
    LastMean /= Robots.Num();
    Order.Sort([&](int32 A, int32 B) { return Robots[A]->Fitness() > Robots[B]->Fitness(); });
    const int32 Winner = Order[0];
    const float WinnerScore = Robots[Winner]->Fitness();
    // Re-evaluate the incumbent each generation; export measured generation winners, not a fabricated smooth curve.
    BestScore = WinnerScore;
    BestGenes = Candidates[Winner];
    BestDistance = Robots[Winner]->ForwardDistance();
    BestUpright = Robots[Winner]->UprightFraction();
    SavePolicy(RunDirectory / FString::Printf(TEXT("generation_%04d.json"), Generation), BestGenes, BestScore, BestDistance, BestUpright, Robots[Winner]);
    SavePolicy(RunDirectory / TEXT("latest.json"), BestGenes, BestScore, BestDistance, BestUpright, Robots[Winner]);
    FFileHelper::SaveStringToFile(Rows, *(RunDirectory / TEXT("evaluations.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
    UE_LOG(LogTemp, Display, TEXT("QUEEN_LEARN_RESULT gen=%d best=%.3f mean=%.3f distance=%.3fm upright=%.3f"), Generation, BestScore, LastMean, BestDistance, BestUpright);
    // 최고 점수와 단계 통과를 분리한다. 50m 미완주라도 현재 단계의 보행 기준은 통과해야 번식한다.
    const TArray<int32> Parents = QueenSelection::Parents(Results);
    bFreshPopulation = Parents.IsEmpty();
    TSharedRef<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetNumberField(TEXT("generation"), Generation);
    Selection->SetNumberField(TEXT("goal_metres"), GoalMetres);
    Selection->SetNumberField(TEXT("curriculum_stage"),CurriculumStage);
    Selection->SetStringField(TEXT("selection_version"),TEXT("v11_stage_completion_only"));
    Selection->SetBoolField(TEXT("fresh_population_next"), bFreshPopulation);
    TArray<TSharedPtr<FJsonValue>> ParentIDs;
    for (int32 P : Parents) ParentIDs.Add(MakeShared<FJsonValueNumber>(P));
    Selection->SetArrayField(TEXT("parent_candidate_ids"), ParentIDs);
    FString SelectionText; FJsonSerializer::Serialize(Selection, TJsonWriterFactory<>::Create(&SelectionText));
    FFileHelper::SaveStringToFile(SelectionText, *(RunDirectory / FString::Printf(TEXT("selection_%04d.json"), Generation)));
    if (bFreshPopulation)
    {
        // 실패 유전자와 이전 탐색 평균을 모두 버린다. 다음 세대의 0번도 새 무작위 후보다.
        Mean.Init(0.f, APhysicsQueen::GeneCount);
        BestGenes = Mean;
        UE_LOG(LogTemp, Display, TEXT("QUEEN_SELECTION gen=%d parents=0 action=fresh_population"), Generation);
    }
    else
    {
        BestGenes = Candidates[Parents[0]];
        for (int32 J=0; J<Mean.Num(); ++J)
        {
            float Sum = 0;
            for (int32 P : Parents) Sum += Candidates[P][J];
            Mean[J] = Sum / Parents.Num();
        }
        Sigma = FMath::Max(0.04f, Sigma * 0.97f);
        UE_LOG(LogTemp, Display, TEXT("QUEEN_SELECTION gen=%d parents=%d action=successful_parents_only"), Generation, Parents.Num());
    }
    ++Generation;
    if (Generation >= TotalGenerations)
    { bFinished = true; UE_LOG(LogTemp, Display, TEXT("QUEEN_LEARN_COMPLETE")); FPlatformMisc::RequestExitWithStatus(false, 0); }
    else StartGeneration();
}
void AQueenLearningHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    AQueenLearningMode* M = Cast<AQueenLearningMode>(GetWorld()->GetAuthGameMode());
    if (!M) return;
    if (M->bPlayerTest)
    {
        DrawRect(FLinearColor(.015f,.025f,.04f,.85f),20,20,560,100);
        DrawText(TEXT("PLAYER + ARMORED QUEEN / PHYSICS TEST"),FLinearColor::White,35,32,GEngine->GetSmallFont(),1.4f);
        if (auto* R=M->VisibleRobot()) DrawText(FString::Printf(TEXT("LEGS %d / 6   BODY HEIGHT %.2fm"),6-FMath::CountBits(uint32(R->Mask)),R->CurrentHeight/100.f),FLinearColor(1,.65,.2),35,65);
        DrawText(TEXT("Queen attack AI is not connected in this physics test."),FLinearColor::White,35,94);
        DrawText(TEXT("WASD move | Mouse aim/fire | Shift sprint | Space jump | R reset"),FLinearColor::White,25,Canvas->SizeY-50);
        DrawText(TEXT("F4 break one leg | F5 break front four | Five shots destroy one leg"),FLinearColor(1,.65,.2),25,Canvas->SizeY-30);
        const float X=Canvas->SizeX*.5f,Y=Canvas->SizeY*.5f;
        auto* P=Cast<AQueenPlayer>(GetOwningPawn());
        const FLinearColor Cross=P && P->HitConfirm>0 ? FLinearColor(1,.5,0):FLinearColor::White;
        DrawLine(X-9,Y,X+9,Y,Cross); DrawLine(X,Y-9,X,Y+9,Cross);
        return;
    }
    auto Line = [&](FString Text, float Y, FLinearColor Color, float Scale = 1.2f)
    { DrawText(Text, Color, 35, Y, GEngine->GetSmallFont(), Scale); };
    DrawRect(FLinearColor(0.015f, 0.025f, 0.04f, 0.9f), 20, 20, 810, 240);
    const FLinearColor Amber(1, 0.65f, 0.2f), White(0.9f, 0.94f, 0.98f);
    Line(TEXT("QUEEN / PHYSICS LEARNING LAB"), 32, Amber, 1.7f);
    Line(M->bReplay ? TEXT("RECORDED POLICY REPLAY / REAL CHAOS PHYSICS") : TEXT("EVOLUTION STRATEGY / FORCE-LIMITED JOINT MOTORS"), 68, White);
    Line(FString::Printf(TEXT("GEN %03d / %03d     MISSING MASK %02d     TIME %.1f / %.1f"), M->Generation, M->TotalGenerations, M->Mask, M->EpisodeClock, M->EpisodeSeconds), 98, White);
    if (APhysicsQueen* R = M->VisibleRobot())
        Line(FString::Printf(TEXT("FORWARD %+.2f / %.0f m     UPRIGHT %.0f%%     SCORE %.2f"), R->ForwardDistance(), M->GoalMetres, R->UprightFraction() * 100, R->Fitness()), 127, Amber);
    if (APhysicsQueen* R = M->VisibleRobot())
        Line(FString::Printf(TEXT("MEAN TILT %.1f deg     ROTATION %.1f deg/s     STABLE %+.2f m"), R->MeanTiltDegrees(), R->MeanRotationSpeed(), R->StableDistance()), 156, Amber, 1);
    if (APhysicsQueen* R = M->VisibleRobot())
        Line(FString::Printf(TEXT("HEIGHT %.2f / %.1f m   HEIGHT RMS %.2f m   SPEED %.2f / %.1f m/s"), R->CurrentHeight / 100.f, M->TargetHeight / 100.f, R->HeightErrorRMS(), R->MeanSpeed(), M->TargetSpeed), 182, White, 1);
    if (M->bDemoLoop)
        if (auto* R=M->VisibleRobot()) Line(R->bReachedGoal ? TEXT("5m COMPLETE / RESTARTING IN 2 SECONDS") : R->bFailed ? TEXT("FAILED / RESTARTING IN 2 SECONDS") : TEXT("LIVE WALKING / REAL TIME / AUTOMATIC REPEAT"),264,White,1);
    Line(TEXT("V14 / LARGE STRIDE / CONTACT FEEDBACK"), 207, Amber, 1);
    if (auto* R=M->VisibleRobot()) Line(FString::Printf(TEXT("STAGE %d  STEPS %d  STANCE %.2fm/s  ROLL %.2frad/s  MATCH %.0f%%"),M->CurriculumStage,R->ValidSteps(),R->StanceDrift(),R->FootRoll(),100*R->GaitMatch()),232,White,.9f);
}




