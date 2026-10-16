using UnrealBuildTool;
public class RoomShiftDemo : ModuleRules
{
    public RoomShiftDemo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "RoomShift" });
    }
}
