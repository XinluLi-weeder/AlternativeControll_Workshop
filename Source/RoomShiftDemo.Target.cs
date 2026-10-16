using UnrealBuildTool;
public class RoomShiftDemoTarget : TargetRules
{
    public RoomShiftDemoTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("RoomShiftDemo");
    }
}
