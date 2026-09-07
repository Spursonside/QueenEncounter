using UnrealBuildTool;
public class QueenEncounter : ModuleRules
{
    public QueenEncounter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine", "InputCore", "PhysicsCore", "Json", "Landscape"});
    }
}
