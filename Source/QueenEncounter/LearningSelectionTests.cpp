#include "LearningSelection.h"
#include "GaitCriteria.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQueenStrictSelectionTest, "Queen.Learning.StrictCompletionParents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FQueenStrictSelectionTest::RunTest(const FString&)
{
    auto P = QueenSelection::Parents({{1000, false, false}, {900, true, true}, {10, true, false}, {20, true, false}});
    TestEqual(TEXT("Timeout and physical failure cannot reproduce even with higher scores"), P.Num(), 2);
    if (P.Num()==2) { TestEqual(TEXT("Best successful parent"), P[0], 3); TestEqual(TEXT("Second successful parent"), P[1], 2); }
    TestEqual(TEXT("All failures require fresh population"), QueenSelection::Parents({{1000,false,false},{5,false,true}}).Num(), 0);
    P = QueenSelection::Parents({{1,true,false},{2,true,false},{3,true,false},{4,true,false},{5,true,false}});
    TestEqual(TEXT("At most four parents"), P.Num(), 4);
    TestFalse(TEXT("Lowest successful score excluded when four better exist"), P.Contains(0));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQueenGaitGateTest, "Queen.Learning.GaitStageGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FQueenGaitGateTest::RunTest(const FString&)
{
    using namespace QueenGaitCriteria;
    TestTrue(TEXT("Completed sustained five metres per second"),PassSpeedTarget(true,false,8,5,.9f));
    TestFalse(TEXT("A fast failed trial is not success"),PassSpeedTarget(false,true,8,5,.9f));
    TestFalse(TEXT("A short speed spike is not sustained"),PassSpeedTarget(true,false,1,5,1));
    TestFalse(TEXT("Slow completion is not five metres per second"),PassSpeedTarget(true,false,40,1,0));
    TestFalse(TEXT("Standing still is not a step"),Pass(0,false,60,1,0,0,1,0,0));
    TestFalse(TEXT("Rolling 50m is not walking"),Pass(3,false,150,1,.1f,2,1,10,51));
    TestFalse(TEXT("Sliding 50m cannot reproduce"),Pass(3,false,150,1,1,.1f,1,10,51));
    TestFalse(TEXT("Toppled travel fails"),Pass(3,false,150,.2f,.1f,.1f,1,10,51));
    TestTrue(TEXT("All legs lift in place"),Pass(0,false,60,1,.1f,.1f,.9f,1,-.5f));
    TestTrue(TEXT("A real fast walk need not wait twenty seconds"),Pass(1,false,12,1,.1f,.1f,.9f,1,5));
    TestFalse(TEXT("Fast sliding without steps still fails"),Pass(1,false,8,1,.1f,.1f,.9f,0,5));
    TestTrue(TEXT("5m passes only its stage"),Pass(1,false,60,1,.1f,.1f,.9f,2,5));
    TestFalse(TEXT("5m is not 15m"),Pass(2,false,60,1,.1f,.1f,.9f,2,5));
    TestTrue(TEXT("Clean 50m walking passes final stage"),Pass(3,false,150,1,.1f,.1f,.9f,5,50));
    TestFalse(TEXT("Physical failure overrides metrics"),Pass(3,true,150,1,.1f,.1f,.9f,5,50));
    return true;
}
#endif
