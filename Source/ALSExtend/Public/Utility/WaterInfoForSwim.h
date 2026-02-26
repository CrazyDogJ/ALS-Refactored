#pragma once

#include "CoreMinimal.h"
#include "WaterInfoForSwim.generated.h"

USTRUCT(BlueprintType)
struct FWaterInfoForSwim
{
	GENERATED_USTRUCT_BODY()

public:
	FWaterInfoForSwim(){}
	
	FWaterInfoForSwim(float InWaterDepth, const FVector& InWaterPlaneLocation, const FVector& InWaterPlaneNormal,
	                  const FVector& InWaterSurfacePosition, const FVector& InWaterVelocity, int32 InWaterBodyIdx,
	                  float InWaterHeight)
	{
		WaterDepth = InWaterDepth;
		WaterPlaneLocation = InWaterPlaneLocation;
		WaterPlaneNormal = InWaterPlaneNormal;
		WaterSurfacePosition = InWaterSurfacePosition;
		WaterVelocity = InWaterVelocity;
		WaterBodyIdx = InWaterBodyIdx;
		WaterHeight = InWaterHeight;
	}
	
	UPROPERTY(BlueprintReadOnly)
	float WaterDepth = 0.0f;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterPlaneLocation = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterPlaneNormal = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterSurfacePosition = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterVelocity = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	int32 WaterBodyIdx = 0;
	
	UPROPERTY(BlueprintReadOnly)
	float WaterHeight = 0.0f;
};
