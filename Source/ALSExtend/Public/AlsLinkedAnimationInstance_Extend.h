// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AlsAnimationInstance_Extend.h"
#include "AlsCharacter_Extend.h"
#include "AlsLinkedAnimationInstance.h"
#include "AlsLinkedAnimationInstance_Extend.generated.h"

/**
 * 
 */
UCLASS()
class ALSEXTEND_API UAlsLinkedAnimationInstance_Extend : public UAlsLinkedAnimationInstance
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, Category = "State", Transient)
	TWeakObjectPtr<UAlsAnimationInstance_Extend> ParentExtend;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State", Transient)
	TObjectPtr<AAlsCharacter_Extend> CharacterExtend;

public:
	UAlsLinkedAnimationInstance_Extend();

	virtual void NativeInitializeAnimation() override;

	virtual void NativeBeginPlay() override;

protected:
	// Be very careful when using this function to read your custom variables using the property access system. It is
	// safe to use this function to read variables that change only inside UAlsAnimationInstance::NativeUpdateAnimation()
	// because it is guaranteed that this function will be called before parallel animation evaluation. Reading
	// variables that change in other functions can be dangerous because they can be changed in the game thread
	// at the same time as being read in the worker thread, which can lead to undefined behavior or even a crash.
	UFUNCTION(BlueprintPure, Category = "ALS|Linked Animation Instance",
		Meta = (BlueprintThreadSafe, ReturnDisplayName = "Parent"))
	UAlsAnimationInstance_Extend* GetParentExtend() const;
};
