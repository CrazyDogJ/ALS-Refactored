using UnrealBuildTool;

public class ALSEditor : ModuleRules
{
	public ALSEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		bEnableNonInlinedGenCppWarnings = true;
		// UnsafeTypeCastWarningLevel = WarningLevel.Warning;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "AnimGraphRuntime", "AnimationModifiers", "AnimationBlueprintLibrary", "ALS", "SkeletalMeshEditor"
		});

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new[]
			{
				"AnimGraph"
			});

			PrivateDependencyModuleNames.AddRange(new[]
			{
				"BlueprintGraph"
			});
		}
	}
}