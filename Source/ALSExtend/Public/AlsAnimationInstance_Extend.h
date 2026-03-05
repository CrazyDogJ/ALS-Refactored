// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AlsAnimationInstance.h"
#include "Settings/AlsMorphSettings.h"
#include "Utility/BlinkMorphState.h"
#include "AlsAnimationInstance_Extend.generated.h"

class UAlsLinkedAnimationInstance_Extend;

/**
 * 
 */
UCLASS()
class ALSEXTEND_API UAlsAnimationInstance_Extend : public UAlsAnimationInstance
{
	GENERATED_BODY()

#pragma region Properties
public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings)
	TArray<FName> TagNames {TEXT("Overlay"), TEXT("PostOverlay"), TEXT("PostFinal")};
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings)
	TObjectPtr<UAlsMorphSettings> MorphSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=State, Transient)
	FBlinkMorphState MorphState;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings)
	TSubclassOf<UAnimInstance> DefaultOverlayAnimBP;

	bool bNeedToTurnInPlace = false;
#pragma endregion

#pragma region Functions
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaTime) override;	
	
	/*
	 * Return random blink delay time base on morph settings.
	 **/
	float GetBlinkDelay() const;
	UCurveFloat* GetBlinkCurve() const;
	void StartBlinkDelay();
	void UpdateBlink(float deltaTime);
	bool ShouldBlockBlink() const;
	void SetBlinkMorphTarget();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	float GetMorphTarget(FName MorphName) const;
	
	UFUNCTION(BlueprintCallable)
	void TurnInPlaceImmediately();
	
	UFUNCTION(BlueprintCallable, Category = "ALS|Animation Instance", Meta = (BlueprintThreadSafe))
	void RefreshSwimmingVelocityBlend();
#pragma endregion 
};
