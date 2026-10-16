using UnrealBuildTool;
public class RoomShift : ModuleRules
{
    public RoomShift(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "Sockets", "Networking", "Json" });
    }
}
