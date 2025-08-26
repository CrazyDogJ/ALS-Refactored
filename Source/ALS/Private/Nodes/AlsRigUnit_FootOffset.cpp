#include "Nodes/AlsRigUnit_FootOffset.h"

#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Utility/AlsRotation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AlsRigUnit_FootOffset)

void FAlsRigUnit_FootOffset::Initialize()
{
	bInitialized = false;
}

FAlsRigUnit_FootOffset_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (!bInitialized)
	{
		bInitialized = true;

		OffsetTargetLocationZ = 0.0f;
		OffsetTargetRotation = FQuat::Identity;
		OffsetSpringState.Reset();
		OffsetLocationZ = 0.0f;
		OffsetRotation = FQuat::Identity;
		
		if (const auto SkeletalMeshComp = Cast<USkeletalMeshComponent>(ExecuteContext.GetOwningComponent()))
		{
			if (SkeletalMeshComp->GetPhysicsAsset())
			{
				for (auto Body : SkeletalMeshComp->GetPhysicsAsset()->SkeletalBodySetups)
				{
					if (Body->BoneName == CurrentFootName)
					{
						if (Body->AggGeom.BoxElems.IsValidIndex(0))
						{
							FootBox = Body->AggGeom.BoxElems[0];
							bFootBoxValid = true;
							break;
						}
					}
				}
			}
		}
	}

	if (bReset)
	{
		OffsetTargetLocationZ = 0.0f;
		OffsetTargetRotation = FQuat::Identity;
		OffsetSpringState.Reset();
		OffsetLocationZ = 0.0f;
		OffsetRotation = FQuat::Identity;

		ResultLocation = Location;
		ResultRotation = Rotation;
		return;
	}

	if (!bEnabled)
	{
		OffsetTargetLocationZ = 0.0f;
		OffsetTargetRotation = FQuat::Identity;
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

	// Trace downward from the foot location to find the geometry. If the surface is walkable, save the impact location and normal.

	const FVector TraceStart{Location.X, Location.Y, TraceDistanceUpward};
	const FVector TraceEnd{Location.X, Location.Y, -TraceDistanceDownward};
	
	FHitResult Hit;
	auto* DrawInterface{ExecuteContext.GetDrawInterface()};
	ExecuteContext.GetWorld()->LineTraceSingleByChannel(Hit, ExecuteContext.ToWorldSpace(TraceStart), ExecuteContext.ToWorldSpace(TraceEnd),
														TraceChannel, {__FUNCTION__, true, ExecuteContext.GetOwningActor()});
	
	if (DrawInterface != nullptr && bDrawDebug)
	{
		DrawInterface->DrawLine(FTransform::Identity, TraceStart, TraceEnd, {0.0f, 0.25f, 1.0f}, 1.0f);
	}

	const auto HitNormal{ExecuteContext.GetToWorldSpaceTransform().InverseTransformVector(Hit.ImpactNormal)};

	if (Hit.IsValidBlockingHit() && HitNormal.Z >= FMath::Cos(FMath::DegreesToRadians(WalkableFloorAngle)))
	{
		const auto HitLocation{ExecuteContext.ToVMSpace(Hit.ImpactPoint)};

		const auto SlopeAngleCos{UE_REAL_TO_FLOAT(HitNormal.Z)};

		const auto FootHeightOffset{SlopeAngleCos > UE_SMALL_NUMBER ? FootHeight / SlopeAngleCos - FootHeight : 0.0f};

		// Find the difference between the impact location and the expected (flat) floor location.
		// These values are offset by the foot height to get better behavior on sloped surfaces.

		OffsetTargetLocationZ = UE_REAL_TO_FLOAT(HitLocation.Z + FootHeightOffset);

		// Calculate the rotation offset.

		OffsetTargetRotation = FQuat::FindBetweenNormals(FVector::ZAxisVector, HitNormal);

		if (DrawInterface != nullptr && bDrawDebug && Hit.bBlockingHit)
		{
			DrawInterface->DrawPoint(FTransform::Identity, HitLocation, 12.0f, {0.0f, 0.75f, 1.0f});
		}

		if (bFootBoxValid)
		{
			const auto FinalTransform = FTransform(OffsetTargetRotation * Rotation, Location);
			const auto CalLocation = FinalTransform.TransformPosition(FootBox.Center);
			const auto CalRotation = FinalTransform.TransformRotation(FootBox.Rotation.Quaternion());
			const auto FootBoxTraceStart = ExecuteContext.ToWorldSpace(CalLocation + FVector(0,0,TraceDistanceUpward));
			const auto FootBoxTraceEnd = ExecuteContext.ToWorldSpace(CalLocation + FVector(0,0,-TraceDistanceDownward));
			const auto FootBoxTraceRotation = ExecuteContext.ToWorldSpace(CalRotation);
			FHitResult FootBoxHitResult;
			ExecuteContext.GetWorld()->SweepSingleByChannel(FootBoxHitResult,
				FootBoxTraceStart,
				FootBoxTraceEnd,
				FootBoxTraceRotation,
				TraceChannel,
				FCollisionShape::MakeBox(FVector(FootBox.X, FootBox.Y, FootBox.Z) / 2),
				{__FUNCTION__, true, ExecuteContext.GetOwningActor()});

			// Draw foot box
			if (DrawInterface != nullptr && bDrawDebug)
			{
				DrawInterface->DrawBox(FTransform::Identity, FTransform(CalRotation, CalLocation, FVector(FootBox.X, FootBox.Y, FootBox.Z)), FColor::Green, 1.0f);
				//if (FootBoxHitResult.IsValidBlockingHit())
				//{
				//	DrawInterface->DrawLine(FTransform::Identity, ExecuteContext.ToVMSpace(FootBoxHitResult.Location), ExecuteContext.ToVMSpace(FootBoxHitResult.ImpactPoint + FootBoxHitResult.ImpactNormal * 50), FColor::Green, 1.0f);
				//}
			}
			
			if (FootBoxHitResult.IsValidBlockingHit())
			{
				// REMEMBER! Foot Box X is foot height!!!
				FVector AdjustedCenter = (OffsetTargetRotation * Rotation).RotateVector(FootBox.Center);
				FVector RevertLocation = FootBoxHitResult.Location - AdjustedCenter;
				RevertLocation.Z = RevertLocation.Z - FootBox.X;
				
				const auto FootBoxHeightOffset{SlopeAngleCos > UE_SMALL_NUMBER ? FootBox.X / SlopeAngleCos - FootBox.X : 0.0f};
				OffsetTargetLocationZ = UE_REAL_TO_FLOAT(ExecuteContext.ToVMSpace(RevertLocation).Z + FootBoxHeightOffset);
			}
		}
	}
	else
	{
		OffsetTargetLocationZ = 0.0f;
		OffsetTargetRotation = FQuat::Identity;
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
