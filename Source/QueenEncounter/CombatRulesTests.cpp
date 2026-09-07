#include "CombatRules.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQueenPolicyTest, "Queen.Combat.AttackSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FQueenPolicyTest::RunTest(const FString&)
{
    using namespace QueenRules;
    TestTrue(TEXT("Cover prevents attacks"), SelectAttack(500, false, true, true, true) == Attack::None);
    TestTrue(TEXT("Close range prioritizes pulse"), SelectAttack(500, true, true, true, true) == Attack::Pulse);
    TestTrue(TEXT("Long range prioritizes mortar"), SelectAttack(2500, true, true, true, true) == Attack::Mortar);
    TestTrue(TEXT("Cooldown falls back to beam"), SelectAttack(2500, true, true, true, false) == Attack::Beam);
    TestTrue(TEXT("All cooling prevents attack"), SelectAttack(500, true, false, false, false) == Attack::None);
    TestTrue(TEXT("Out of detection range"), SelectAttack(7100, true, true, true, true) == Attack::None);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQueenArmorTest, "Queen.Combat.ArmorAndDamage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FQueenArmorTest::RunTest(const FString&)
{
    float Armor = 90, Health = 120;
    TestEqual(TEXT("Armor absorbs initial shot"), QueenRules::LegDamage(24, Armor, Health), 0.f);
    TestEqual(TEXT("Armor remainder"), Armor, 66.f);
    TestEqual(TEXT("Overflow reaches health"), QueenRules::LegDamage(80, Armor, Health), 14.f);
    TestEqual(TEXT("Overkill clamps"), QueenRules::LegDamage(500, Armor, Health), 106.f);
    TestEqual(TEXT("Destroyed part ignores shots"), QueenRules::LegDamage(24, Armor, Health), 0.f);
    TestEqual(TEXT("No negative health"), Health, 0.f);
    TestEqual(TEXT("Negative damage ignored"), QueenRules::LegDamage(-10, Armor, Health), 0.f);
    TestTrue(TEXT("Exposed core rewards aim"), QueenRules::CoreMultiplier(true) > QueenRules::CoreMultiplier(false));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQueenIKTest, "Queen.Movement.TwoBoneIK", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FQueenIKTest::RunTest(const FString&)
{
    const FVector Hip(0, 0, 420);
    for (FVector Target : {FVector(600, 0, 0), FVector(600, 0, 180), FVector(1800, 0, 0), Hip})
    {
        FVector Foot = Target;
        const FVector Knee = QueenRules::SolveKnee(Hip, Foot, FVector(1, 0, 1));
        TestTrue(TEXT("Upper length fixed"), FMath::IsNearlyEqual(FVector::Distance(Hip, Knee), 460.0, 0.1));
        TestTrue(TEXT("Lower length fixed"), FMath::IsNearlyEqual(FVector::Distance(Knee, Foot), 520.0, 0.1));
        TestFalse(TEXT("No invalid solution"), Knee.ContainsNaN());
    }
    return true;
}
#endif
