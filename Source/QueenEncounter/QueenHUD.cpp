#include "QueenHUD.h"
#include "QueenPlayer.h"
#include "QueenBoss.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

void AQueenHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    AQueenPlayer* P = Cast<AQueenPlayer>(GetOwningPawn());
    AQueenBoss* B = nullptr;
    for (TActorIterator<AQueenBoss> It(GetWorld()); It; ++It) { B = *It; break; }
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const FLinearColor Ink(0.9f, 0.93f, 0.95f), Amber(1, 0.6f, 0.16f), Muted(0.5f, 0.6f, 0.65f);
    auto Text = [&](const FString& S, float X, float Y, FLinearColor C, float Scale = 1.f)
    { DrawText(S, C, X, Y, GEngine->GetSmallFont(), Scale); };
    DrawRect(FLinearColor(0.025f, 0.04f, 0.055f, 0.9f), 25, 22, 345, 92);
    Text(TEXT("QUEEN / ENCOUNTER STUDY"), 42, 34, Ink, 1.5f);
    Text(TEXT("INDEPENDENT C++ COMBAT PROTOTYPE"), 42, 64, Muted);
    Text(TEXT("BREAK ARMOR > DESTROY LIMBS > EXPOSE CORE"), 42, 86, Amber);
    if (B)
    {
        const float BW = FMath::Min(560.f, W * 0.44f), X = (W - BW) * 0.5f;
        DrawRect(FLinearColor(0.025f, 0.04f, 0.055f, 0.85f), X - 15, 125, BW + 30, 92);
        Text(FString::Printf(TEXT("QUEEN   /   %s"), *B->StateLabel()), X, 137, Ink, 1.2f);
        DrawRect(FLinearColor(0.2f, 0.22f, 0.23f), X, 164, BW, 7);
        DrawRect(Amber, X, 164, BW * FMath::Clamp(B->Health / B->MaxHealth, 0.f, 1.f), 7);
        Text(FString::Printf(TEXT("CORE %s    |    LIMBS %d / 6"), B->IsCoreExposed() ? TEXT("EXPOSED") : TEXT("ARMORED"), 6 - B->BrokenLegs()), X, 184, B->IsCoreExposed() ? Amber : Muted);
        if (B->State == EQueenState::Windup || B->State == EQueenState::Attack)
            Text(B->AttackLabel(), W * 0.5f - 175, H * 0.7f, Amber, 1.4f);
    }
    if (P)
    {
        DrawRect(FLinearColor(0.025f, 0.04f, 0.055f, 0.85f), 25, H - 106, 260,  70);
        Text(FString::Printf(TEXT("VITALS  %03.0f / 100"), P->Health), 42, H - 96, Ink, 1.3f);
        DrawRect(FLinearColor(0.15f, 0.2f, 0.23f), 42, H - 66, 225, 6);
        DrawRect(FLinearColor(0.3f, 0.85f, 0.78f), 42, H - 66, 225 * P->Health / 100, 6);
        const FLinearColor Cross = P->HitConfirm > 0 ? Amber : Ink;
        DrawLine(W / 2 - 10, H / 2, W / 2 - 4, H / 2, Cross, 1.5f);
        DrawLine(W / 2 + 4, H / 2, W / 2 + 10, H / 2, Cross, 1.5f);
        DrawLine(W / 2, H / 2 - 10, W / 2, H / 2 - 4, Cross, 1.5f);
        DrawLine(W / 2, H / 2 + 4, W / 2, H / 2 + 10, Cross, 1.5f);
        if (P->Health <= 0 || (B && B->Health <= 0))
        {
            DrawRect(FLinearColor(0.02f, 0.03f, 0.04f, 0.85f), W / 2 - 240, H / 2 - 65, 480, 120);
            Text(P->Health <= 0 ? TEXT("RAIDER DOWN") : TEXT("QUEEN NEUTRALIZED"), W / 2 - 155, H / 2 - 40, Amber, 2.f);
            Text(TEXT("PRESS R TO RESTART THE ENCOUNTER"), W / 2 - 150, H / 2 + 5, Ink);
        }
        if (P->bDebug && B)
        {
            DrawRect(FLinearColor(0.01f, 0.02f, 0.025f, 0.9f), W - 440, 240, 420, 300);
            Text(TEXT("RUNTIME INSPECTOR"), W - 405, 252, Amber, 1.2f);
            Text(FString::Printf(TEXT("FPS %.0f | state %.1fs | LOS %s"), 1.f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f), B->StateRemaining, B->bHasSight ? TEXT("YES") : TEXT("NO")), W - 405, 280, Ink);
            Text(B->LastDecision.Left(54), W - 405, 305, Muted, 0.9f);
            for (int32 I = 0; I < 6; ++I)
                Text(FString::Printf(TEXT("LEG %d   ARMOR %03.0f   HP %03.0f"), I + 1, B->ArmorAt(I), B->LegHealthAt(I)), W - 405, 331 + I * 20, B->LegHealthAt(I) > 0 ? Ink : Muted);
            Text(B->SupportLabel, W - 425, 462, Amber);
            Text(FString::Printf(TEXT("CONTACTS %d  |  SUPPORT MARGIN %.0f cm"), B->GroundedFeet, B->SupportMargin), W - 425, 484, Ink);
            Text(B->bInspectionMode ? TEXT("INSPECTION / DAMAGE DISABLED") : TEXT("COMBAT ACTIVE"), W - 425, 506, Muted);
        }
    }
    Text(TEXT("WASD MOVE  MOUSE AIM/FIRE  SHIFT SPRINT  SPACE JUMP  F1 DEBUG  R RESTART"), 30, H - 44, Muted);
    Text(TEXT("F2 INSPECTION  F3 TERRAIN TOUR  F4 BREAK NEXT LEG  F5 FRONT-FOUR COLLAPSE"), 30, H - 24, Amber);
}

