// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsRigUnit_FootBoxTrace.h"

#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AlsRigUnit_FootBoxTrace)

void FAlsRigUnit_FootBoxTrace::Initialize()
{
	bInitialized = false;
}

FAlsRigUnit_FootBoxTrace_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (!bInitialized)
	{
		bInitialized = true;
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

	if (!bEnabled)
	{
		return;
	}
	
	FHitResult FootBoxHit;
	if (bFootBoxValid)
	{
		auto FinalTransform = FTransform(Rotation, Location);
		auto CalLocation = FinalTransform.TransformPosition(FootBox.Center);
		auto CalRotation = FinalTransform.TransformRotation(FootBox.Rotation.Quaternion());
		auto TraceStart = ExecuteContext.ToWorldSpace(CalLocation + FVector(0,0,TraceDistanceUpward));
		auto TraceEnd = ExecuteContext.ToWorldSpace(CalLocation + FVector(0,0,-TraceDistanceDownward));
		auto TraceRotation = ExecuteContext.ToWorldSpace(CalRotation);
		ExecuteContext.GetWorld()->SweepSingleByChannel(
			FootBoxHit,
			TraceStart,
			TraceEnd,
			TraceRotation,
			TraceChannel,
			FCollisionShape::MakeBox(FVector(FootBox.X, FootBox.Y, FootBox.Z) / 2),
			{__FUNCTION__, true, ExecuteContext.GetOwningActor()}
			);

		auto* DrawInterface{ExecuteContext.GetDrawInterface()};
		if (DrawInterface != nullptr && bDrawDebug)
		{
			// Draw foot box
			DrawInterface->DrawBox(FTransform::Identity, FTransform(CalRotation, CalLocation, FVector(FootBox.X, FootBox.Y, FootBox.Z)), FColor::Green, 1.0f);
			if (FootBoxHit.IsValidBlockingHit())
			{
				// Draw ImpactPoint and ImpactNormal
				DrawInterface->DrawLine(FTransform::Identity, ExecuteContext.ToVMSpace(FootBoxHit.ImpactPoint), ExecuteContext.ToVMSpace(FootBoxHit.ImpactPoint + FootBoxHit.ImpactNormal * 50), FColor::Green, 1.0f);
			}
		}
	}
	
	FootBoxHitResult = FootBoxHit;
}
