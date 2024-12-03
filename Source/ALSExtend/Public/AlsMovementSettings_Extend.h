// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AlsMovementSettings_Extend.generated.h"

USTRUCT(BlueprintType)
struct ALSEXTEND_API FAlsGlidingSettings
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float MaxGlideDownSpeed{120.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float InterpToTargetGlideSpeed{150.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float GlidingLateralFriction{1.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float GlidingAirControl{0.7f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float BrakingDecelerationGliding{600.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float MaxGlideAcceleration{600.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float MaxGlideSpeed{450.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float CanStartGlideHeight{250.0f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float GlideRotationInterpSpeed{0.5f};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin="0", UIMin="0"))
	float GlideToFallCheckHeight{100.0f};
};

USTRUCT(BlueprintType)
struct ALSEXTEND_API FAlsSlidingSettings
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinSlideSpeed = 400.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxSlideSpeed = 250.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SlideEnterImpulse = 400.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SlideGravityForce = 3200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SlideFrictionFactor = .06f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BrakingDecelerationSliding = 1000.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxSlideAcceleration = 300.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SlideRotationMultiplier = 2.0f;
};

USTRUCT(BlueprintType)
struct ALSEXTEND_API FAlsClimbingSettings
{
	GENERATED_BODY()
	
	//Dash
	UPROPERTY(EditDefaultsOnly)
	UCurveFloat* ClimbDashCurve;

	//Enter climb duration.
	UPROPERTY(EditDefaultsOnly)
	float TryEnterClimbDuration = 0.4f;
	
	//Max steep angle
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "1.0", ClampMax = "75.0"))
	float MinVerticalDegreesToStartClimbing = 45.0f;

	//Climb -> Walk angle
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "1.0", ClampMax = "75.0"))
	float StopClimbWalkableAngleThreshold = 10.0f;

	//Face wall angle
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "1.0", ClampMax = "75.0"))
	float MinHorizontalDegreesToStartClimbing = 50.0f;

	//Max climbing speed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float MaxClimbingSpeed = 120.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float MaxClimbingAcceleration = 380.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "3000.0"))
	float BrakingDecelerationClimbing = 550.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1.0", ClampMax = "12.0"))
	int ClimbingRotationSpeed = 6.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float ClimbingSnapSpeed = 4.0f;
	
	UPROPERTY(EditAnywhere, meta=(ClampMin="1.0", ClampMax="500.0"))
	float FloorCheckDistance = 100.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "80.0"), BlueprintReadOnly)
	float DistanceFromSurface = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float EyeHeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ClimbUpLedgeEyeHeight = 20.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "80.0"), BlueprintReadOnly)
	float ClimbingCollisionShrinkAmount = 30.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0"), BlueprintReadOnly)
	float SlopeSpeedMultiplier = 80.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb Down")
	UAnimMontage* ClimbDownMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb Down")
	UAnimMontage* ClimbDownFloorMontage;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb Down")
	FName WarpTarget_A = "ClimbDown_A";
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb Down")
	FName WarpTarget_B = "ClimbDown_B";
};

USTRUCT(BlueprintType)
struct ALSEXTEND_API FAlsSwimmingSettings
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ClampMin = 0, ForceUnits = "cm/s"))
	float RunSpeed{300.0f};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ClampMin = 0, ForceUnits = "cm/s"))
	float SprintSpeed{500.0f};

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanJumpOutOfWater = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (EditCondition = "bCanJumpOutOfWater", EditConditionHides, ClampMin = 0, ForceUnits = "cm/s"))
	float OutWaterSpeed{550.0f};

	UPROPERTY(EditDefaultsOnly)
	bool bIncludeWave = true;
	
	/**
	 * Water surface to capsule bottom distance to start swim.
	 */
	UPROPERTY(EditDefaultsOnly)
	float BeginSwimDepth = 75.0f;

	UPROPERTY(EditDefaultsOnly)
	float SwimCapsuleRadius = 30.0f;

	UPROPERTY(EditDefaultsOnly)
	float SwimCapsuleHalfHeight = 90.0f;

	UPROPERTY(EditDefaultsOnly)
	float SwimToSurfaceBuoyancyAdditive = 20.0f;

	UPROPERTY(EditDefaultsOnly)
	float SwimOnSurfaceDepth = 200.0f;

	UPROPERTY(EditDefaultsOnly)
	float SwimOnSurfaceAdditiveDepth = 0.0f;

	UPROPERTY(EditDefaultsOnly)
	float FluidFriction = 5.0f;

	UPROPERTY(EditDefaultsOnly)
	float TerminalVelocity = 4000.0f;

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	float WaterVelocityForceMultiplier = 0.7f;
};

USTRUCT(BlueprintType)
struct ALSEXTEND_API FAlsFlyingSettings
{
	GENERATED_BODY()

	/** The maximum flying speed. */
	UPROPERTY(Category="Character Movement: Flying", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float MaxFlySpeed = 300.0f;

	/**
		 * Deceleration when flying and not applying acceleration.
		 * @see MaxAcceleration
		 */
	UPROPERTY(Category="Character Movement: Flying", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float BrakingDecelerationFlying = 1.0f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Flying")
	float FlyFasterMaxSpeed = 600.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Flying")
	bool bShouldCheckLand = true;
};

/**
 * 
 */
UCLASS()
class ALSEXTEND_API UAlsMovementSettings_Extend : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FAlsSwimmingSettings SwimmingSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FAlsClimbingSettings ClimbingSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FAlsSlidingSettings SlidingSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FAlsGlidingSettings GlidingSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FAlsFlyingSettings FlyingSettings;
};
