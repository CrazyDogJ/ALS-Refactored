#include "Settings/AlsCapsuleSizeSettings.h"

#include "AlsCharacter.h"

FAlsCapsuleSizeStateSettings UAlsCapsuleSizeSettings::QueryCapsuleSize(const AAlsCharacter* AlsCharacter, bool& Valid, FString& UserDesc)
{
	Valid = false;
	UserDesc = "Invalid";
	
	const auto LocomotionModeTag = AlsCharacter->GetLocomotionMode();
	const auto StanceTag = AlsCharacter->GetStance();
	const auto LocomotionActionTag = AlsCharacter->GetLocomotionAction();
	const auto RotationModeTag = AlsCharacter->GetRotationMode();
	const auto ViewModeTag = AlsCharacter->GetViewMode();
	const auto GaitTag = AlsCharacter->GetGait();
	const auto OverlayTag = AlsCharacter->GetOverlayMode();

	const TArray<FGameplayTag> Tags{LocomotionModeTag, LocomotionActionTag, StanceTag, RotationModeTag, ViewModeTag, GaitTag, OverlayTag};
	const FGameplayTagContainer CharContainer = FGameplayTagContainer::CreateFromArray(Tags);

	const auto Found = CapsuleSizeStateSettings.FindByPredicate([CharContainer](const FAlsCapsuleSizeStateSettings& Settings)
	{
		return Settings.TagQuery.Matches(CharContainer);
	});

	if (Found)
	{
		Valid = true;
		UserDesc = Found->TagQuery.GetDescription();
		return *Found;
	}
	
	return FAlsCapsuleSizeStateSettings();
}

FAlsCapsuleSizeStateSettings UAlsCapsuleSizeSettings::QueryCapsuleSizeByTag(const FGameplayTag& Tag, bool& Valid)
{
	Valid = false;
	const auto Found = CapsuleSizeStateSettings.FindByPredicate([Tag](const FAlsCapsuleSizeStateSettings& Settings)
	{
		return Settings.IdentityTag == Tag;
	});

	if (Found)
	{
		Valid = true;
		return *Found;
	}
	
	return FAlsCapsuleSizeStateSettings();
}
