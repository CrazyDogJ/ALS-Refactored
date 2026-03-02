#pragma once

#include "CoreMinimal.h"
#include "WaterInfoForSwim.generated.h"

USTRUCT(BlueprintType)
struct FWaterInfoForSwim
{
	GENERATED_USTRUCT_BODY()

public:
	FWaterInfoForSwim(){}
	
	FWaterInfoForSwim(const FVector& InWaterPlaneLocation, const FVector& InWaterPlaneNormal,
	                  const FVector& InWaterSurfacePosition, const FVector& InWaterVelocity)
	{
		WaterPlaneLocation = InWaterPlaneLocation;
		WaterPlaneNormal = InWaterPlaneNormal;
		WaterSurfacePosition = InWaterSurfacePosition;
		WaterVelocity = InWaterVelocity;
	}
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterPlaneLocation = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterPlaneNormal = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterSurfacePosition = FVector::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector WaterVelocity = FVector::ZeroVector;
};
