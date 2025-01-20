using UnrealBuildTool;

public class ALSExtend : ModuleRules
{
    public ALSExtend(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core", 
                "ALS", 
                "ALSCamera", 
                "Water", 
                "MotionWarping", 
                "GameplayAbilities",
                "GameplayTags",
                "AIModule",
                "RigVM",
                "ControlRig"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "RHI",
                "Engine",
                "Slate",
                "SlateCore"
            }
        );
    }
}