#pragma once

#include "BlinkMorphState.generated.h"

USTRUCT(BlueprintType)
struct ALSEXTEND_API FBlinkMorphState
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	bool bIsBlinkDelay = false;
	
	UPROPERTY(BlueprintReadOnly)
	bool bBlinking = false;
	
	UPROPERTY(BlueprintReadOnly)
	float BlinkingTime = 0.0f;
	
	UPROPERTY(BlueprintReadOnly)
	float BlinkAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float TargetBlinkDelayTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float CurrentBlinkDelayTime = 0.0f;
};
