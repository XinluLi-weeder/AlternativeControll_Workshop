using UnrealBuildTool;
public class RoomShiftDemoEditorTarget : TargetRules
{
    public RoomShiftDemoEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("RoomShiftDemo");
    }
}
