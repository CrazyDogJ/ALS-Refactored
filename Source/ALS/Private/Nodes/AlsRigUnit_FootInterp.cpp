#include "Nodes/AlsRigUnit_FootInterp.h"

#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Utility/AlsRotation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AlsRigUnit_FootInterp)

void FAlsRigUnit_FootInterp::Initialize()
{
	bInitialized = false;
}

FAlsRigUnit_FootInterp_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (!bInitialized)
	{
		bInitialized = true;
		
		OffsetSpringState.Reset();
		OffsetLocationZ = 0.0f;
		OffsetRotation = FQuat::Identity;
	}

	if (bReset)
	{
		OffsetSpringState.Reset();
		OffsetLocationZ = 0.0f;
		OffsetRotation = FQuat::Identity;

		ResultLocation = Location;
		ResultRotation = Rotation;
		return;
	}

	if (!bEnabled)
	{
		OffsetSpringState.Reset();

		static constexpr auto InterpolationSpeed{15.0f};

		OffsetLocationZ *= 1.0f - UAlsMath::Clamp01(ExecuteContext.GetDeltaTime() * InterpolationSpeed);

		OffsetRotation = UAlsRotation::InterpolateQuaternionFast(OffsetRotation, FQuat::Identity,
																 ExecuteContext.GetDeltaTime(), InterpolationSpeed);

		ResultLocation.X = Location.X;
		ResultLocation.Y = Location.Y;
		ResultLocation.Z = Location.Z + OffsetLocationZ;
		ResultRotation = OffsetRotation * Rotation;
		return;
	}
	
	// Interpolate current offsets to the new target values.
	
	static constexpr auto LocationInterpolationFrequency{0.4f};
	static constexpr auto LocationInterpolationDampingRatio{4.0f};
	static constexpr auto LocationInterpolationTargetVelocityAmount{1.0f};

	OffsetLocationZ = UAlsMath::SpringDampFloat(OffsetSpringState, OffsetLocationZ, OffsetTargetLocationZ,
	                                            ExecuteContext.GetDeltaTime(), LocationInterpolationFrequency,
	                                            LocationInterpolationDampingRatio, LocationInterpolationTargetVelocityAmount);

	static constexpr auto RotationInterpolationSpeed{30.0f};

	OffsetRotation = UAlsRotation::InterpolateQuaternionFast(OffsetRotation, OffsetTargetRotation,
	                                                         ExecuteContext.GetDeltaTime(), RotationInterpolationSpeed);
	
	ResultLocation.X = Location.X;
	ResultLocation.Y = Location.Y;
	ResultLocation.Z = Location.Z + OffsetLocationZ;
	ResultRotation = OffsetRotation * Rotation;
}
