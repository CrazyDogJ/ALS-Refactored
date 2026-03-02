// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AlsGameplayTags_Extend.h"
#include "GameplayTagContainer.h"
#include "GameFramework/DamageType.h"
#include "AlsDamageType.generated.h"

UCLASS()
class ALSEXTEND_API UAlsDamageType : public UDamageType
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	FGameplayTag DamageTypeTag;
};

UCLASS()
class ALSEXTEND_API UAlsDamageType_Fall : public UAlsDamageType
{
	GENERATED_BODY()

public:
	UAlsDamageType_Fall()
	{
		DamageTypeTag = AlsDamageTypeTags::Fall;
	}
};

UCLASS()
class ALSEXTEND_API UAlsDamageType_Ragdoll : public UAlsDamageType
{
	GENERATED_BODY()
	
public:
	UAlsDamageType_Ragdoll()
	{
		DamageTypeTag = AlsDamageTypeTags::Ragdoll;
	}
};
