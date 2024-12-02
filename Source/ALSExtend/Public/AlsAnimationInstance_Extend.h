// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AlsAnimationInstance.h"
#include "AlsMorphSettings.h"
#include "BlinkMorphState.h"
#include "AlsAnimationInstance_Extend.generated.h"

/**
 * 
 */
UCLASS()
class ALSEXTEND_API UAlsAnimationInstance_Extend : public UAlsAnimationInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings)
	TObjectPtr<UAlsMorphSettings> MorphSettings;

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
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=State, Transient)
	FBlinkMorphState MorphState;
	
	virtual void NativeThreadSafeUpdateAnimation(float DeltaTime) override;
	
	bool bNeedToTurnInPlace = false;
	
	UFUNCTION(BlueprintCallable)
	void TurnInPlaceImmediately();
};
