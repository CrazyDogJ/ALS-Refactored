// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "AlsCapsuleSizeSettings.generated.h"

class AAlsCharacter;

USTRUCT(BlueprintType)
struct ALSEXTEND_API FAlsCapsuleSizeStateSettings
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FGameplayTag IdentityTag;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FGameplayTagQuery TagQuery;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float CapsuleHalfHeight = 90.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float CapsuleRadius = 30.0f;
};

UCLASS(BlueprintType)
class ALSEXTEND_API UAlsCapsuleSizeSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	TArray<FAlsCapsuleSizeStateSettings> CapsuleSizeStateSettings;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	FAlsCapsuleSizeStateSettings QueryCapsuleSize(const AAlsCharacter* AlsCharacter, bool& Valid, FString& UserDesc);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	FAlsCapsuleSizeStateSettings QueryCapsuleSizeByTag(const FGameplayTag& Tag, bool& Valid);
};
