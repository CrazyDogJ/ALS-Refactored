// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AlsMorphSettings.generated.h"

/**
 * 
 */
UCLASS()
class ALSEXTEND_API UAlsMorphSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TMap<FName, float> BlinkMorphMap;

	/*
	 * Vector 2d X for Min, Y for Max.
	 **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector2D RandomTimeToBlink = FVector2D(1.0f,3.0f);

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UCurveFloat* BlinkCurve;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FName> EyesMorphArray;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FName DisableAutoBlinkCurveName = FName(TEXT("BlockAutoBlink"));
};
