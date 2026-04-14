// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AlsAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class ALSEXTEND_API UAlsAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_WalkSpeed)
	FGameplayAttributeData WalkSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, WalkSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_CrouchSpeed)
	FGameplayAttributeData CrouchSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, CrouchSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_SwimSpeed)
	FGameplayAttributeData SwimSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, SwimSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_ClimbSpeed)
	FGameplayAttributeData ClimbSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, ClimbSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_GlideSpeed)
	FGameplayAttributeData GlideSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, GlideSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_SlideSpeed)
	FGameplayAttributeData SlideSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, SlideSpeed)
	
	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_FlySpeed)
	FGameplayAttributeData FlySpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, FlySpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Als", ReplicatedUsing = OnRep_JumpSpeed)
	FGameplayAttributeData JumpSpeed;
	ATTRIBUTE_ACCESSORS(ThisClass, JumpSpeed)
	
protected:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_WalkSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, WalkSpeed, OldValue); }
	UFUNCTION()
	void OnRep_CrouchSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, CrouchSpeed, OldValue); }
	UFUNCTION()
	void OnRep_SwimSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, SwimSpeed, OldValue); }
	UFUNCTION()
	void OnRep_ClimbSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, ClimbSpeed, OldValue); }
	UFUNCTION()
	void OnRep_GlideSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, GlideSpeed, OldValue); }
	UFUNCTION()
	void OnRep_SlideSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, SlideSpeed, OldValue); }
	UFUNCTION()
	void OnRep_FlySpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, FlySpeed, OldValue); }
	UFUNCTION()
	void OnRep_JumpSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, JumpSpeed, OldValue); }
};
