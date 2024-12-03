// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsAnimationInstance_Extend.h"

#include "AlsGameplayTags_Extend.h"
#include "Settings/AlsAnimationInstanceSettings.h"
#include "Utility/AlsConstants.h"
#include "Utility/AlsDebugUtility.h"

float UAlsAnimationInstance_Extend::GetBlinkDelay() const
{
	if (MorphSettings)
	{
		const float min = MorphSettings->RandomTimeToBlink.X;
		const float max = MorphSettings->RandomTimeToBlink.Y;
		return FMath::FRandRange(min, max);
	}
	// Default for min 1, max 3
	return FMath::FRandRange(1.0f,3.0f);
}

UCurveFloat* UAlsAnimationInstance_Extend::GetBlinkCurve() const
{
	if (MorphSettings)
	{
		return MorphSettings->BlinkCurve;
	}
	return nullptr;
}

void UAlsAnimationInstance_Extend::StartBlinkDelay()
{
	if (MorphState.bIsBlinkDelay)
	{
		return;
	}

	MorphState.CurrentBlinkDelayTime = 0.0f;
	MorphState.TargetBlinkDelayTime = GetBlinkDelay();
	MorphState.bIsBlinkDelay = true;
}

void UAlsAnimationInstance_Extend::UpdateBlink(float deltaTime)
{
	if (ShouldBlockBlink())
	{
		MorphState.bIsBlinkDelay = false;
		MorphState.bBlinking = false;
		MorphState.CurrentBlinkDelayTime = 0.0f;
		MorphState.TargetBlinkDelayTime = 0.0f;
		MorphState.BlinkingTime = 0.0f;
		MorphState.BlinkAlpha = 0.0f;
		SetBlinkMorphTarget();
		return;
	}
	
	StartBlinkDelay();
	
	MorphState.CurrentBlinkDelayTime += deltaTime;

	if (MorphState.CurrentBlinkDelayTime >= MorphState.TargetBlinkDelayTime)
	{
		if (!MorphState.bBlinking)
		{
			MorphState.BlinkAlpha = 0.0f;
			MorphState.BlinkingTime = 0.0f;
			MorphState.bBlinking = true;
		}
	}
	
	if (MorphState.bBlinking)
	{
		MorphState.BlinkingTime += deltaTime;
		if (GetBlinkCurve())
		{
			MorphState.BlinkAlpha = GetBlinkCurve()->GetFloatValue(MorphState.BlinkingTime);
			float minTime;
			float maxTime;
			GetBlinkCurve()->GetTimeRange(minTime, maxTime);
			if (MorphState.BlinkingTime >= maxTime)
			{
				MorphState.bBlinking = false;
				MorphState.bIsBlinkDelay = false;
			}
		}
		SetBlinkMorphTarget();
	}
}

bool UAlsAnimationInstance_Extend::ShouldBlockBlink() const
{
	if (MorphSettings)
	{
		//Block auto blink if curve value > 0;
		if (GetCurveValue(MorphSettings->DisableAutoBlinkCurveName) > 0)
		{
			return true;
		}

		//Block auto blink if it has eyes morph;
		for (auto EyeMorphName : MorphSettings->EyesMorphArray)
		{
			if (GetMorphTarget(EyeMorphName) > 0.0f)
			{
				return true;
			}
		}
	}
	return false;
}

void UAlsAnimationInstance_Extend::SetBlinkMorphTarget()
{
	if (ShouldBlockBlink())
	{
		return;
	}
	
	if (MorphSettings)
	{
		for (auto BlinkMap : MorphSettings->BlinkMorphMap)
		{
			SetMorphTarget(BlinkMap.Key, BlinkMap.Value * MorphState.BlinkAlpha);
		}
	}
}

float UAlsAnimationInstance_Extend::GetMorphTarget(FName MorphName) const
{
	if (USkeletalMeshComponent* Component = GetOwningComponent())
	{
		return Component->GetMorphTarget(MorphName);
	}
	return -1;
}

void UAlsAnimationInstance_Extend::NativeThreadSafeUpdateAnimation(float DeltaTime)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaTime);

	UpdateBlink(DeltaTime);
}

void UAlsAnimationInstance_Extend::TurnInPlaceImmediately()
{
	bNeedToTurnInPlace = true;
}
