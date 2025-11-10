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
	float WaterDepth;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterPlaneLocation;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterPlaneNormal;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterSurfacePosition;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterVelocity;
	
	UPROPERTY(BlueprintReadOnly)
	int32 WaterBodyIdx;
	
	UPROPERTY(BlueprintReadOnly)
	float WaterHeight;
};
