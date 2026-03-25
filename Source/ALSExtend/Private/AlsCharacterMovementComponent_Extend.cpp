// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsCharacterMovementComponent_Extend.h"

#include "AlsCharacter_Extend.h"
#include "Utility/CustomMovementMode.h"
#include "WaterBodyActor.h"
#include "WaterSplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Utility/AlsDebugUtility.h"
#include "Utility/AlsGameplayTags_Extend.h"

DECLARE_CYCLE_STAT(TEXT("Char FindFloor"), STAT_CharFindFloor, STATGROUP_Character);

// Sets default values for this component's properties

void UAlsCharacterMovementComponent_Extend::PhysSwimming(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	const auto Position = GetWaterSurface();
	const auto CompLoc = UpdatedComponent->GetComponentLocation();
	const auto Distance = GetMovementSettingsExtendSafe()->SwimmingSettings.WaterSurfaceBelowDistance;
	const auto SwimSurfaceZ = Position.Z - Distance;
	const auto SurfaceDelta = SwimSurfaceZ - CompLoc.Z;
	float OriginalAccelZ = Acceleration.Z;
	bool bLimitedUpAccel = false;

	// Swim on surface clamp.
	if (bIsSwimOnSurface)
	{
		// Don't update velocity.z when character wants to swim down.
		if (Acceleration.Z >= 0.0f)
		{
			Velocity.Z = SurfaceDelta;
		}
		bLimitedUpAccel = (Acceleration.Z > 0.f);
		Acceleration.Z = FMath::Min<FVector::FReal>(0.0f, Acceleration.Z);
	}

	Iterations++;
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	bJustTeleported = false;
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		const float Friction = 0.5f * GetMovementSettingsExtendSafe()->SwimmingSettings.FluidFriction;
		CalcVelocity(deltaTime, Friction, true, GetMaxBrakingDeceleration());
	}

	ApplyRootMotionToVelocity(deltaTime);

	FVector Adjusted = Velocity * deltaTime;

	// Swimming store current touched floor. used to decide should we walk or fall.
	FFindFloorResult HasFloorResult;
	FindFloor(UpdatedComponent->GetComponentLocation(), HasFloorResult, false);
	CurrentFloor = HasFloorResult;

	// Keep surface height
	if (bIsSwimOnSurface &&
		UpdatedComponent->GetComponentLocation().Z >= GetWaterSurface().Z - 5.0f &&
		Acceleration.Z >= 0 &&
		!HasFloorResult.IsWalkableFloor())
	{
		//Avoid jump in water immediately stop.
		if (Adjusted.Z > GetWaterSurface().Z - 5.0f - UpdatedComponent->GetComponentLocation().Z)
		{
			Adjusted.Z = GetWaterSurface().Z - 5.0f - UpdatedComponent->GetComponentLocation().Z;
		}
	}

	// Swim main
	FHitResult Hit(1.f);
	float remainingTime = deltaTime * Swim(Adjusted, Hit);
	
	//may have left water - if so, script might have set new physics mode
	if (!IsSwimming())
	{
		StartNewPhysics(remainingTime, Iterations);
		return;
	}

	// Swim adjust
	if (Hit.Time < 1.f && CharacterOwner)
	{
		HandleSwimmingWallHit(Hit, deltaTime);
		if (bLimitedUpAccel && (Velocity.Z >= 0.f))
		{
			// allow upward velocity at surface if against obstacle
			Velocity.Z += OriginalAccelZ * deltaTime;
			Adjusted = Velocity * (1.f - Hit.Time) * deltaTime;
			Swim(Adjusted, Hit);
			if (!IsSwimming())
			{
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
		}

		const FVector GravDir = FVector(0.f, 0.f, -1.f);
		const FVector VelDir = Velocity.GetSafeNormal();
		const float UpDown = GravDir | VelDir;

		bool bSteppedUp = false;
		if ((FMath::Abs(Hit.ImpactNormal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
		{
			float stepZ = UpdatedComponent->GetComponentLocation().Z;
			const FVector RealVelocity = Velocity;
			Velocity.Z = 1.f;	// HACK: since will be moving up, in case pawn leaves the water
			bSteppedUp = StepUp(GravDir, Adjusted * (1.f - Hit.Time), Hit);
			if (bSteppedUp)
			{
				//may have left water - if so, script might have set new physics mode
				if (!IsSwimming())
				{
					StartNewPhysics(remainingTime, Iterations);
					return;
				}
				OldLocation.Z = UpdatedComponent->GetComponentLocation().Z + (OldLocation.Z - stepZ);
			}
			Velocity = RealVelocity;
		}

		if (!bSteppedUp)
		{
			//adjust and try again
			HandleImpact(Hit, deltaTime, Adjusted);
			SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
		}
	}

	// Jump out of water I
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported && ((deltaTime - remainingTime) > UE_KINDA_SMALL_NUMBER) && CharacterOwner)
	{
		bool bWaterJump = !IsInWater();
		float velZ = Velocity.Z;
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / (deltaTime - remainingTime);
		if (bWaterJump)
		{
			Velocity.Z = velZ;
		}
	}

	// Jump out of water II
	if (bWantsToJumpOutOfWater && UpdatedComponent->GetComponentLocation().Z <= GetWaterSurface().Z && bIsSwimOnSurface)
	{
		if (GetMovementSettingsExtendSafe()->SwimmingSettings.bCanJumpOutOfWater)
		{
			SetMovementMode(MOVE_Falling);
		
			Velocity.Z += GetMovementSettingsExtendSafe()->SwimmingSettings.OutWaterSpeed;
			bJumpingOutOfWater = true;
			Cast<AAlsCharacter_Extend>(CharacterOwner)->MulticastJumpOutOfWater();
		}
		bWantsToJumpOutOfWater = false;
	}

	//river velocity
	const FVector Delta = WaterInfoForSwim.WaterVelocity * GetMovementSettingsExtendSafe()->SwimmingSettings.WaterVelocityForceMultiplier * deltaTime;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
	
	// Mantle and step on land
	if (Acceleration.Length() > 1.0f && Cast<AAlsCharacter_Extend>(CharacterOwner)->StartMantlingSwimming())
	{
		return;
	}
	
	// Out water using Walking Movement Mode.
	if (!IsInWater() && IsSwimming())
	{
		if (CurrentFloor.IsWalkableFloor())
		{
			SetMovementMode(MOVE_Walking);
			return;
		}

		SetMovementMode(MOVE_Falling);
	}

	//may have left water - if so, script might have set new physics mode
	if (!IsSwimming())
	{
		StartNewPhysics(remainingTime, Iterations);
	}
}

void UAlsCharacterMovementComponent_Extend::StartSwimming(FVector OldLocation, FVector OldVelocity, float timeTick,
	float remainingTime, int32 Iterations)
{
	if (remainingTime < MIN_TICK_TIME || timeTick < MIN_TICK_TIME)
	{
		return;
	}

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported)
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; //actual average velocity
		Velocity = 2.f * Velocity - OldVelocity; //end velocity has 2* accel of avg
		Velocity = Velocity.GetClampedToMaxSize(GetMovementSettingsExtendSafe()->SwimmingSettings.TerminalVelocity);
	}
	const FVector End = FindWaterLine(UpdatedComponent->GetComponentLocation(), OldLocation);

	if (End != UpdatedComponent->GetComponentLocation())
	{
		const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size();
		if (ActualDist > UE_KINDA_SMALL_NUMBER)
		{
			float waterTime = timeTick * (End - UpdatedComponent->GetComponentLocation()).Size() / ActualDist;
			remainingTime += waterTime;
		}
		MoveUpdatedComponent(End - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true);
	}
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (Velocity.Z > 2.f * -80.f) && (Velocity.Z < 0.f)) //allow for falling out of water
	{
		Velocity.Z = -80.f - Velocity.Size2D() * 0.7f; //smooth bobbing
	}
	if ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{
		PhysSwimming(remainingTime, Iterations);
	}
}

bool UAlsCharacterMovementComponent_Extend::CanCrouchInCurrentState() const
{
	// Fix swimming capsule return to uncrouch.
	if (IsSwimming())
	{
		return true;
	}
	
	return Super::CanCrouchInCurrentState();
}

bool UAlsCharacterMovementComponent_Extend::IsWalkable(const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit())
	{
		// No hit, or starting in penetration
		return false;
	}

	// Never walk up vertical surfaces.
	const FVector::FReal ImpactNormalZ = GetGravitySpaceZ(Hit.ImpactNormal);
	if (ImpactNormalZ < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float TestWalkableZ = GetWalkableFloorZ();

	// See if this component overrides the walkable floor z.
	const UPrimitiveComponent* HitComponent = Hit.Component.Get();
	if (HitComponent)
	{
		const FWalkableSlopeOverride& SlopeOverride = HitComponent->GetWalkableSlopeOverride();
		TestWalkableZ = SlopeOverride.ModifyWalkableFloorZ(TestWalkableZ);
	}

	// Slide walkable threshold.
	if (IsSliding())
	{
		TestWalkableZ = GetMovementSettingsExtendSafe()->SlidingSettings.GetSlideWalkableZ();
	}
	
	// Can't walk on this surface if it is too steep.
	if (ImpactNormalZ < TestWalkableZ)
	{
		return false;
	}

	return true;
}

float UAlsCharacterMovementComponent_Extend::Swim(const FVector& Delta, FHitResult& Hit)
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	float airTime = 0.f;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (WaterBodyComponents.Num() == 0) //Use Water Plugin,then left water
	{
		const FVector End = FindWaterLine(Start, UpdatedComponent->GetComponentLocation());
		const float DesiredDist = Delta.Size();
		if (End != UpdatedComponent->GetComponentLocation() && DesiredDist > UE_KINDA_SMALL_NUMBER)
		{
			airTime = (End - UpdatedComponent->GetComponentLocation()).Size() / DesiredDist;
			if (((UpdatedComponent->GetComponentLocation() - Start) | (End - UpdatedComponent->GetComponentLocation())) > 0.f)
			{
				airTime = 0.f;
			}
			SafeMoveUpdatedComponent(End - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, Hit);
		}
	}
	return airTime;
}

FVector UAlsCharacterMovementComponent_Extend::FindWaterLine(const FVector& InWater, const FVector& OutofWater) const
{
	FVector Result = OutofWater;

	if (WaterBodyComponents.Num() > 0)
	{
		FVector Dir = (InWater - OutofWater).GetSafeNormal();
		Result = GetWaterSurface();
		if ( Result.Z > InWater.Z && Result.Z < OutofWater.Z )
			Result += 0.1f * Dir;
		else
			Result -= 0.1f * Dir;
	}

	return Result;
}

FVector UAlsCharacterMovementComponent_Extend::GetWaterSurface() const
{
	return GetMovementSettingsExtendSafe()->SwimmingSettings.bIncludeWave ?
		WaterInfoForSwim.WaterSurfacePosition :
		WaterInfoForSwim.WaterPlaneLocation;
}

void UAlsCharacterMovementComponent_Extend::UpdateWaterInfoForSwim()
{
	WaterBodyComponents = GetWaterBodyComponents();
	
	if (CharacterOwner && WaterBodyComponents.Num() > 0)
	{
		EWaterBodyQueryFlags QueryFlags =
			EWaterBodyQueryFlags::ComputeLocation
			| EWaterBodyQueryFlags::ComputeNormal
			| EWaterBodyQueryFlags::ComputeImmersionDepth
			| EWaterBodyQueryFlags::ComputeVelocity
			| EWaterBodyQueryFlags::IncludeWaves;

		FVector HighestPlaneLocation = FVector::ZeroVector;
		FVector HighestPlaneNormal = FVector::ZeroVector;
		FVector HighestSurfacePosition = FVector::ZeroVector;
		FVector BlendedVelocity = FVector::ZeroVector;
		
		for (int i = 0; i < WaterBodyComponents.Num(); ++i)
		{
			const auto WaterBody = WaterBodyComponents[i];
			const auto QueryResult = WaterBody->QueryWaterInfoClosestToWorldLocation(UpdatedComponent->GetComponentLocation(), QueryFlags);
			if (!QueryResult.IsInExclusionVolume())
			{
				if (i == 0)
				{
					HighestPlaneLocation = QueryResult.GetWaterPlaneLocation();
					HighestPlaneNormal = QueryResult.GetWaterPlaneNormal();
					HighestSurfacePosition = QueryResult.GetWaterSurfaceLocation();
					BlendedVelocity = QueryResult.GetVelocity();
				}
				else
				{
					const auto CurrentPlaneLocation = QueryResult.GetWaterPlaneLocation();
					if (CurrentPlaneLocation.Z > HighestPlaneLocation.Z)
					{
						HighestPlaneLocation = CurrentPlaneLocation;
						HighestPlaneNormal = QueryResult.GetWaterPlaneNormal();
					}
					const auto CurrentSurfaceLocation = QueryResult.GetWaterSurfaceLocation();
					if (CurrentSurfaceLocation.Z > HighestSurfacePosition.Z)
					{
						HighestSurfacePosition = CurrentSurfaceLocation;
					}
					BlendedVelocity += QueryResult.GetVelocity();
				}
			}
		}
		
		WaterInfoForSwim = FWaterInfoForSwim(HighestPlaneLocation, HighestPlaneNormal, HighestSurfacePosition, BlendedVelocity);
	}
}

TArray<UWaterBodyComponent*> UAlsCharacterMovementComponent_Extend::GetWaterBodyComponents() const
{
	TArray<UWaterBodyComponent*> OverlappedWaterBodyComponents;
	
	if (!CharacterOwner)
	{
		return OverlappedWaterBodyComponents;
	}
	if (!CharacterOwner->GetCapsuleComponent())
	{
		return OverlappedWaterBodyComponents;
	}
	
	TArray<AActor*> OverlappedActors;
	CharacterOwner->GetCapsuleComponent()->GetOverlappingActors(OverlappedActors);
	for (const auto Actor : OverlappedActors)
	{
		if (const auto WaterBodyActor = Cast<AWaterBody>(Actor))
		{
			OverlappedWaterBodyComponents.Add(WaterBodyActor->GetWaterBodyComponent());
		}
	}

	return OverlappedWaterBodyComponents;
}

UWaterBodyComponent* UAlsCharacterMovementComponent_Extend::GetCurrentWaterBodyComponent() const
{
	TArray<UWaterBodyComponent*> OverlappedWaterBodyComponents = WaterBodyComponents;

	OverlappedWaterBodyComponents.Sort([](const UWaterBodyComponent& ABody, const UWaterBodyComponent& BBody)
	{
		// Using PP calculation ---------------------------------------------------------------------------------------
		// If both water bodies either have waves or both don't have waves, use the overlap priority to determine which to use, since in this case we need to respect the surface waves
		if (ABody.HasWaves() == BBody.HasWaves())
		{
			const int32 APriority = ABody.GetOverlapMaterialPriority();
			const int32 BPriority = BBody.GetOverlapMaterialPriority();
			return APriority > BPriority;
		}

		// Otherwise, prefer the water body with waves to ensure the PP calculates the waves correctly.
		return ABody.HasWaves() && !BBody.HasWaves();
	});

	if (OverlappedWaterBodyComponents.Num() > 0)
	{
		return OverlappedWaterBodyComponents[0];
	}
	
	return nullptr;
}

void UAlsCharacterMovementComponent_Extend::GetDefaultUnscaledCapsule(float& OutCapsuleHalfHeight,
	float& OutCapsuleRadius) const
{
	Cast<AAlsCharacter_Extend>(GetCharacterOwner())->GetDefaultCapsule(OutCapsuleHalfHeight, OutCapsuleRadius);
}

void UAlsCharacterMovementComponent_Extend::GetDefaultScaledCapsule(float& OutCapsuleHalfHeight,
	float& OutCapsuleRadius) const
{
	float UnscaledCapsuleHalfHeight;
	float UnscaledCapsuleRadius;
	GetDefaultUnscaledCapsule(UnscaledCapsuleHalfHeight, UnscaledCapsuleRadius);
	OutCapsuleRadius = Cast<AAlsCharacter_Extend>(GetCharacterOwner())->GetScaledRadius(UnscaledCapsuleRadius);
	OutCapsuleHalfHeight = Cast<AAlsCharacter_Extend>(GetCharacterOwner())->GetScaledHaleHeight(UnscaledCapsuleHalfHeight);
}

void UAlsCharacterMovementComponent_Extend::GetUnscaledCrouchHalfHeight(float& OutCapsuleHalfHeight) const
{
	OutCapsuleHalfHeight = GetCrouchedHalfHeight();

	const auto AlsCharExtend = Cast<AAlsCharacter_Extend>(GetCharacterOwner());
	if (!AlsCharExtend) return;
	if (const auto FoundSettings = AlsCharExtend->GetCapsuleSettings())
	{
		bool bValid;
		const auto CapsuleSize = FoundSettings->QueryCapsuleSizeByTag(AlsStanceTags::Crouching, bValid);
		if (bValid)
		{
			OutCapsuleHalfHeight = CapsuleSize.CapsuleHalfHeight;
		}
	}
}

UAlsCharacterMovementComponent_Extend::UAlsCharacterMovementComponent_Extend()
{
	bWantsToJumpOutOfWater = false,
	bWantsToClimb = false;
	bWantsToGlide = false;

	SetIsReplicatedByDefault(true);
}

void UAlsCharacterMovementComponent_Extend::Crouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	if (!bClientSimulation && !CanCrouchInCurrentState())
	{
		return;
	}

	// Get Crouch half height.
	float UnscaledCrouchedHalfHeight;
	GetUnscaledCrouchHalfHeight(UnscaledCrouchedHalfHeight);
	
	// See if collision is already at desired size.
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == UnscaledCrouchedHalfHeight)
	{
		if (!bClientSimulation)
		{
			CharacterOwner->SetIsCrouched(true);
		}
		CharacterOwner->OnStartCrouch( 0.f, 0.f );
		return;
	}

	float DefaultStandRadius;
	float DefaultStandHalfHeight;
	GetDefaultUnscaledCapsule(DefaultStandHalfHeight, DefaultStandRadius);
	
	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// restore collision size before crouching
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultStandRadius, DefaultStandHalfHeight);
		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	// Height is not allowed to be smaller than radius.
	const float ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, UnscaledCrouchedHalfHeight);
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	float HalfHeightAdjust = (OldUnscaledHalfHeight - ClampedCrouchedHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	if( !bClientSimulation )
	{
		// Crouching to a larger height? (this is rare)
		if (ClampedCrouchedHalfHeight > OldUnscaledHalfHeight)
		{
			FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);
			const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() + ScaledHalfHeightAdjust * GetGravityDirection(), GetWorldToGravityTransform(),
				UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

			// If encroached, cancel
			if( bEncroached )
			{
				CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
				return;
			}
		}

		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
			UpdatedComponent->MoveComponent(ScaledHalfHeightAdjust * GetGravityDirection(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}

		CharacterOwner->SetIsCrouched(true);
	}

	bForceNextFloorCheck = true;

	// OnStartCrouch takes the change from the Default size, not the current one (though they are usually the same).
	const float MeshAdjust = ScaledHalfHeightAdjust;
	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultUnscaledCapsule(DefaultHalfHeight, DefaultRadius);
	HalfHeightAdjust = (DefaultHalfHeight - ClampedCrouchedHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch( HalfHeightAdjust, ScaledHalfHeightAdjust );

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset -= MeshAdjust * -GetGravityDirection();
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UAlsCharacterMovementComponent_Extend::UnCrouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}
	
	float DefaultStandRadius;
	float DefaultStandHalfHeight;
	GetDefaultUnscaledCapsule(DefaultStandHalfHeight, DefaultStandRadius);
	
	// See if collision is already at desired size.
	if( CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == DefaultStandHalfHeight )
	{
		if (!bClientSimulation)
		{
			CharacterOwner->SetIsCrouched(false);
		}
		CharacterOwner->OnEndCrouch( 0.f, 0.f );
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float HalfHeightAdjust = DefaultStandHalfHeight - OldUnscaledHalfHeight;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	// Grow to uncrouched size.
	check(CharacterOwner->GetCapsuleComponent());

	if( !bClientSimulation )
	{
		// Try to stay in place and see if the larger capsule fits. We use a slightly taller capsule to avoid penetration.
		const UWorld* MyWorld = GetWorld();
		const float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.f;
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and standing size
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by negative amount, so actually grow it.
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
		bool bEncroached = true;

		if (!bCrouchMaintainsBaseLocation)
		{
			// Expand in place
			bEncroached = MyWorld->OverlapBlockingTestByChannel(PawnLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
		
			if (bEncroached)
			{
				// Try adjusting capsule position to see if we can avoid encroachment.
				if (ScaledHalfHeightAdjust > 0.f)
				{
					// Shrink to a short capsule, sweep down to base to find where that would hit something, and then try to stand up from there.
					float PawnRadius, PawnHalfHeight;
					CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
					const float ShrinkHalfHeight = PawnHalfHeight - PawnRadius;
					const float TraceDist = PawnHalfHeight - ShrinkHalfHeight;
					const FVector Down = TraceDist * GetGravityDirection();

					FHitResult Hit(1.f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + Down, GetWorldToGravityTransform(), CollisionChannel, ShortCapsuleShape, CapsuleParams);
					if (Hit.bStartPenetrating)
					{
						bEncroached = true;
					}
					else
					{
						// Compute where the base of the sweep ended up, and see if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector Adjustment = (-DistanceToBase + StandingCapsuleShape.Capsule.HalfHeight + SweepInflation + MIN_FLOOR_DIST / 2.f) * -GetGravityDirection();
						const FVector NewLoc = PawnLocation + Adjustment;
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						}
					}
				}
			}
		}
		else
		{
			// Expand while keeping base location the same.
			FVector StandingLocation = PawnLocation + (StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight) * -GetGravityDirection();
			bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				if (IsMovingOnGround())
				{
					// Something might be just barely overhead, try moving down closer to the floor to avoid it.
					const float MinFloorDist = UE_KINDA_SMALL_NUMBER * 10.f;
					if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
					{
						StandingLocation -= (CurrentFloor.FloorDist - MinFloorDist) * -GetGravityDirection();
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
					}
				}				
			}

			if (!bEncroached)
			{
				// Commit the change in location.
				UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
				bForceNextFloorCheck = true;
			}
		}

		// If still encroached then abort.
		if (bEncroached)
		{
			return;
		}

		CharacterOwner->SetIsCrouched(false);
	}	
	else
	{
		bShrinkProxyCapsule = true;
	}

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultStandRadius, DefaultStandHalfHeight, true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch( HalfHeightAdjust, ScaledHalfHeightAdjust );

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset += MeshAdjust * -GetGravityDirection();
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UAlsCharacterMovementComponent_Extend::PhysClimbing(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}
	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();

	ComputeSurfaceInfo(CurrentWallHits, CurrentClimbingPosition, CurrentClimbingNormal, VelocityWallHit.ImpactNormal, UpdatedComponent->GetComponentLocation());

	//SetBase
	FHitResult BaseHit;
	const FCollisionShape CollisionSphere = FCollisionShape::MakeSphere(6);
	const auto TraceChannel = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbTraceChannel;
	GetWorld()->SweepSingleByChannel(BaseHit, CurrentClimbingPosition, CurrentClimbingPosition, FQuat::Identity, TraceChannel, CollisionSphere, ClimbQueryParams);
	if (BaseHit.bBlockingHit)
	{
		SetBase(BaseHit.Component.Get(), BaseHit.BoneName);
	}

	EStopClimbingType StopClimbingType;
	if (ShouldStopClimbing(StopClimbingType))
	{
#if WITH_EDITOR
		if (DebugDrawSwitch)
		{
			FString DebugString;
			switch (StopClimbingType)
			{
			case SCT_Normal:
				DebugString = "Normal";
				break;
			case SCT_ClimbDownFloor:
				DebugString = "ClimbDownFloor";
				break;
			case SCT_ClimbToWalk:
				DebugString = "ClimbToWalk";
				break;
			default: DebugString = "INVALID";
			}
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, FString::Printf(TEXT("Stop Climb Type : %s"), *DebugString));
		}
#endif
		StopClimbing(deltaTime, Iterations, StopClimbingType);
		return;
	}

	UpdateClimbDashState(deltaTime);
	
	ComputeClimbingVelocity(deltaTime);

	MoveAlongClimbingSurface(deltaTime);

	//Move on ledge
	const float UpSpeed = GetGravitySpaceZ(Velocity);
	const bool bIsMovingUp = UpSpeed >= 1.0f && GetGravitySpaceZ(Acceleration) > 1.0f;
	
	if (bIsMovingUp && HasReachedEdge() && !HasAnimRootMotion())
	{
		if (auto* Character = Cast<AAlsCharacter_Extend>(GetOwner()))
		{
			if (Character->StartMantlingFreeClimb())
			{
#if WITH_EDITOR
				if (DebugDrawSwitch) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Try mantle"));
#endif
				StopClimbing(deltaTime, Iterations, SCT_Normal);
			}
		}
	}
	
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	SnapToClimbingSurface(deltaTime);
}

bool UAlsCharacterMovementComponent_Extend::ShouldStopClimbing(EStopClimbingType& StopClimbingType) const
{
	// Climb down floor check.
	if (ClimbDownToFloor())
	{
		StopClimbingType = SCT_ClimbDownFloor;
		return true;
	}

	// Down water floor check.
	if (WaterBodyComponents.Num() > 0 &&
		GetGravitySpaceZ(Velocity) < 0 &&
		!bSwimToClimb)
	{
		StopClimbingType = SCT_Normal;
		return true;
	}
	
	TArray<float> HitsVerticalDegrees;
	for (auto& Hit : CurrentWallHits)
	{
		const auto Dot = FVector::DotProduct(Hit.ImpactNormal, GetGravityDirection());
		HitsVerticalDegrees.Add(FMath::RadiansToDegrees(FMath::Acos(Dot)));
	}

	bool bVertical = true;
	for (const auto& Degree : HitsVerticalDegrees)
	{
		if (Degree > GetMovementSettingsExtendSafe()->ClimbingSettings.MinVerticalDegreesToStartClimbing)
		{
			bVertical = false;
			break;
		}
	}

	bool bWalkable = true;
	for (const auto& Degree : HitsVerticalDegrees)
	{
		if (180 - Degree > GetWalkableFloorAngle() - GetMovementSettingsExtendSafe()->ClimbingSettings.StopClimbWalkableAngleThreshold)
		{
			bWalkable = false;
			break;
		}
	}

	const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FFindFloorResult Floor;
	FindFloor(UpdatedComponent->GetComponentLocation() - UpdatedComponent->GetUpVector() * HalfHeight, Floor, true);
	if (!Floor.IsWalkableFloor())
	{
		bWalkable = false;
	}
	
	if (!bWantsToClimb
		|| CurrentClimbingNormal.IsZero()
		|| bVertical
		|| bWalkable
		)
	{
		if (bWalkable)
		{
			StopClimbingType = SCT_ClimbToWalk;
		}
		else
		{
			StopClimbingType = SCT_Normal;
		}
#if WITH_EDITOR
		if (DebugDrawSwitch) DrawDebugLine(GetWorld(), CurrentClimbingPosition, CurrentClimbingPosition + CurrentClimbingNormal * 50, FColor::Red, true, 5);
#endif
		return true;
	}
	
	return false;
}

bool UAlsCharacterMovementComponent_Extend::HasReachedEdge() const
{
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float TraceDistance = Capsule->GetUnscaledCapsuleRadius() * 2.5f;

	return !EyeHeightTrace(TraceDistance, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetUpVector(), UpdatedComponent->GetForwardVector());
}

void UAlsCharacterMovementComponent_Extend::StopClimbing(float deltaTime, int32 Iterations, const EStopClimbingType& StopClimbingType)
{
	bWantsToClimb = false;
	
	if (StopClimbingType != SCT_Normal)
	{
		SetMovementMode(MOVE_Walking);
		StartNewPhysics(deltaTime, Iterations);
		const auto Character = Cast<AAlsCharacter_Extend>(CharacterOwner);
		if (StopClimbingType == SCT_ClimbDownFloor)
		{
			Character->GetMesh()->GetAnimInstance()->Montage_Play(GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbDownFloorMontage);
		}
		else
		{
			Character->NativeClimbToWalk();
		}
	}
	else
	{
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(deltaTime, Iterations);
	}
}

void UAlsCharacterMovementComponent_Extend::MoveAlongClimbingSurface(float deltaTime)
{
	const FVector Adjusted = Velocity * deltaTime;

	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted, GetClimbingRotation(deltaTime), true, Hit);

	if (Hit.Time < 1.f)
	{
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}
}

void UAlsCharacterMovementComponent_Extend::SnapToClimbingSurface(float deltaTime) const
{
	const FVector Forward = UpdatedComponent->GetForwardVector();
	const FVector Location = UpdatedComponent->GetComponentLocation();
	const FQuat Rotation = UpdatedComponent->GetComponentQuat();

	const FVector ForwardDifference = (CurrentClimbingPosition - Location).ProjectOnTo(Forward);
	const FVector Offset = -CurrentClimbingNormal * (ForwardDifference.Length() - GetMovementSettingsExtendSafe()->ClimbingSettings.DistanceFromSurface);

	constexpr bool bSweep = true;
	UpdatedComponent->MoveComponent(Offset * GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbingSnapSpeed * deltaTime, Rotation, bSweep);
}

bool UAlsCharacterMovementComponent_Extend::ClimbDownToFloor() const
{
	if (GetGravitySpaceZ(Velocity) < 0 && GetGravitySpaceZ(Acceleration) < 0)
	{
		FFindFloorResult FloorResult;
		// TODO : Fix climbing down floor teleport visual bug. maybe we can use root motion montage to lerp to the ground.
		const float ForwardDot = GetGravitySpaceZ(UpdatedComponent->GetForwardVector());
		const bool FacingDown = ForwardDot < 0;
		float HalfHeight;
		float Radius;
		GetDefaultScaledCapsule(HalfHeight, Radius);
		const FVector CapsuleUp = UpdatedComponent->GetUpVector();
		const FVector CapsuleLoc = UpdatedComponent->GetComponentLocation();
		const FVector GravityFloor = ProjectToGravityFloor(CapsuleUp * HalfHeight * (FacingDown ? -1.0f : 1.0f));
		const FVector FindFloorLocation = CapsuleLoc + GravityFloor;
		
		// First simple check
		FindFloor(FindFloorLocation, FloorResult, false);
#if WITH_EDITOR
		if (DebugDrawSwitch)
		{
			if (FloorResult.bBlockingHit)
			{
				const auto Impact = FloorResult.HitResult.ImpactPoint;
				const auto Normal = FloorResult.HitResult.ImpactNormal;
				DrawDebugLine(GetWorld(), Impact, Impact + Normal * 100.0f, FColor::Yellow, false, 0);
			}
		}
#endif
		if (IsValidLandingSpot(FindFloorLocation, FloorResult.HitResult) && FloorResult.IsWalkableFloor())
		{
			// Enough space check
			float ScaledStandRadius;
			float ScaledStandHalfHeight;
			GetDefaultScaledCapsule(ScaledStandHalfHeight, ScaledStandRadius);
			
			FHitResult EnoughSpaceCheckHitResult;
			FVector End = FindFloorLocation + GetGravityDirection() * ScaledStandHalfHeight;
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(GetCharacterOwner());
			GetWorld()->SweepSingleByChannel(EnoughSpaceCheckHitResult, FindFloorLocation,
			                                 End, FQuat::Identity, ECC_Visibility,
			                                 FCollisionShape::MakeSphere(ScaledStandRadius), Params);

			// Ignore when still can climb.
			const auto StartClimbDegrees = GetMovementSettingsExtendSafe()->ClimbingSettings.MinVerticalDegreesToStartClimbing;
			const auto StartClimbCos = AngleToZ(StartClimbDegrees);
			if (IsWalkable(EnoughSpaceCheckHitResult) && EnoughSpaceCheckHitResult.ImpactNormal.Z > StartClimbCos)
			{
				return true;
			}
		}
	}
	
	return false;
}

void UAlsCharacterMovementComponent_Extend::UpdateClimbDashState(float deltaTime)
{
	if (!bIsClimbDashing)
	{
		return;
	}

	CurrentClimbDashTime += deltaTime;

	// Better to cache it when dash starts
	float MinTime, MaxTime;
	GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbDashCurve->GetTimeRange(MinTime, MaxTime);
	
	if (CurrentClimbDashTime >= MaxTime)
	{
		StopClimbDashing();
	}
}

void UAlsCharacterMovementComponent_Extend::StopClimbDashing()
{
	bIsClimbDashing = false;
	Cast<AAlsCharacter_Extend>(GetCharacterOwner())->K2_ExitClimbDash();
	CurrentClimbDashTime = 0.f;
	ClimbDashDirection = FVector::ZeroVector;
}

UAlsMovementSettings_Extend* UAlsCharacterMovementComponent_Extend::GetMovementSettingsExtendSafe() const
{
	if (MovementSettings_Extend)
	{
		return MovementSettings_Extend;
	}
	return NewObject<UAlsMovementSettings_Extend>();
}

void UAlsCharacterMovementComponent_Extend::EnterClimbing()
{
	if (bWantsToClimb == false)
	{
		bWantsToClimb = true;
	}
}

void UAlsCharacterMovementComponent_Extend::CancelClimbing()
{
	if (bWantsToClimb == true)
	{
		bWantsToClimb = false;
	}
}

void UAlsCharacterMovementComponent_Extend::CheckClimbDownLedge(FVector& Forward, FVector& Down, FRotator& FaceTo, bool& bInCanClimbDown) const
{
	bInCanClimbDown = false;

	//0.Check stand still, and not crouching(crouch will trigger some issue.)
	if (!Acceleration.IsNearlyZero() ||
		!Velocity.IsNearlyZero() ||
		!IsMovingOnGround() ||
		CharacterOwner->IsPlayingRootMotion() ||
		IsCrouching())
	{
		return;
	}

	//1.Const Variables;
	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultScaledCapsule(DefaultHalfHeight, DefaultRadius);
	const FVector CompLoc = UpdatedComponent->GetComponentLocation();
	const FVector CompBottomLoc = Cast<AAlsCharacter_Extend>(GetCharacterOwner())->GetCapsuleBottom();
	const FVector CompForward = UpdatedComponent->GetForwardVector();

	//2.Check forward wall.
	const FVector WallCheckLoc = CompLoc + CompForward * DefaultRadius * 2.5;
	FFindFloorResult FloorResult;
	FindFloor(CompLoc + CompForward * DefaultRadius * 4.0f, FloorResult, false);
	FHitResult ForwardWallResult;
	GetWorld()->SweepSingleByChannel(ForwardWallResult, CompLoc, WallCheckLoc, FQuat::Identity, ECC_Visibility,
									 FCollisionShape::MakeSphere(DefaultRadius), ClimbQueryParams);
#if WITH_EDITOR
	if (DebugDrawSwitch)
	{
		// Walkable floor check.
		if (FloorResult.bBlockingHit)
		{
			const auto ImpactPoint = FloorResult.HitResult.ImpactPoint;
			const auto ImpactNormal = FloorResult.HitResult.ImpactNormal;
			const auto Walkable = FloorResult.IsWalkableFloor();
			DrawDebugPoint(GetWorld(), ImpactPoint, 4, Walkable ? FColor::Green : FColor::Red, false, 0, 0);
			DrawDebugLine(GetWorld(), ImpactPoint, ImpactPoint + ImpactNormal * 100.0f, Walkable ? FColor::Green : FColor::Red, false, 0, 0);
		}

		// Wall check.
		UAlsDebugUtility::DrawSweepSphere(this, CompLoc, WallCheckLoc, DefaultRadius, FColor::Red);
	}
#endif
	if (ForwardWallResult.bBlockingHit
		|| FloorResult.IsWalkableFloor()
		)
	{
		return;
	}
	
	//3.Find ledge wall;
	const float DownDistance = DefaultHalfHeight;
	const float AngleBaseDistance = GetAngleBaseDistance(DefaultRadius, DownDistance);
	FHitResult LineWallHit;
	const FVector LineWallStart = CompBottomLoc + CompForward * AngleBaseDistance + GetGravityDirection() * DownDistance;
	const FVector LineWallEnd = CompBottomLoc + GetGravityDirection() * DownDistance - CompForward * AngleBaseDistance;
	const auto TraceChannel = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbTraceChannel;
	GetWorld()->LineTraceSingleByChannel(LineWallHit, LineWallStart, LineWallEnd, TraceChannel, ClimbQueryParams);
#if WITH_EDITOR
	if (DebugDrawSwitch)
	{
		DrawDebugLine(GetWorld(), LineWallStart, LineWallEnd, FColor::Blue, false, 0, 0, 1);
	}
#endif
	if (!LineWallHit.IsValidBlockingHit())
	{
		return;
	}

	//4.Check can start climbing;
	const FVector CheckStartClimbLoc = LineWallHit.ImpactPoint + LineWallHit.ImpactNormal * DefaultRadius;
	const FVector CheckStartClimbNormal = (LineWallHit.ImpactNormal * -1).GetSafeNormal2D();
	// Check water
	FHitResult WaterCheckHit;
	FCollisionQueryParams WaterCheckHitQueryParams;
	WaterCheckHitQueryParams.AddIgnoredActor(GetOwner());
	GetWorld()->LineTraceSingleByChannel(WaterCheckHit, CheckStartClimbLoc, CheckStartClimbLoc + GetGravityDirection(), ECC_WorldStatic, WaterCheckHitQueryParams);
	if (Cast<AWaterBody>(WaterCheckHit.GetActor()))
	{
		return;
	}
	// Check predict location can climb.
	TArray<FHitResult> InClimbWallHits;
	FHitResult InVelocityWallHit;
	float NoUse;
	if (!CanStartClimbing(NoUse, InClimbWallHits, InVelocityWallHit, CheckStartClimbLoc, CheckStartClimbNormal))
	{
		return;
	}
	// Get normal.
	FVector Position;
	FVector Normal;
	ComputeSurfaceInfo(InClimbWallHits, Position, Normal, FVector::Zero(), CheckStartClimbLoc);

	//5.Find edge point;
	//FHitResult EdgeHit;
	//const FVector EdgeFindStart = FVector(CheckStartClimbLoc.X, CheckStartClimbLoc.Y, CompBottomLoc.Z);
	//const FVector EdgeFindEnd = EdgeFindStart + CheckStartClimbNormal * DefaultRadius * 2;
	//GetWorld()->SweepSingleByChannel(EdgeHit, EdgeFindStart, EdgeFindEnd, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(),
	//                                 FCollisionShape::MakeSphere(DefaultRadius), ClimbQueryParams);
	//if (!EdgeHit.bBlockingHit)
	//{
	//	return;
	//}

	//6.Check enough space(Forward);
	//const FVector CheckSpace_1 = EdgeHit.ImpactPoint + (DefaultRadius * 2) * LineWallHit.ImpactNormal.GetSafeNormal2D();
	//FHitResult CheckSpaceHit;
	//GetWorld()->SweepSingleByChannel(CheckSpaceHit, CheckSpace_1, CheckSpace_1 + FVector(0, 0, (DefaultHalfHeight - DefaultRadius) * 2),
	//                                 FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(DefaultRadius), ClimbQueryParams);
	//if (CheckSpaceHit.bBlockingHit)
	//{
	//	return;
	//}
	
	//7.Set params;
	Forward = WallCheckLoc;
	Down = CheckStartClimbLoc + GetGravityDirection() * DefaultHalfHeight;
	FaceTo = (-Normal).Rotation();
	bInCanClimbDown = true;
}

void UAlsCharacterMovementComponent_Extend::EnterSwing(ASwingRopeActor* RopeActor)
{
	if (!RopeActor)
	{
		return;
	}
	
	SwingActor = RopeActor;
	OnRopeDistance = SwingActor->SplineComponent->GetDistanceAlongSplineAtLocation(UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::World);
	SwingActor->PlayersOnRope.Add(GetCharacterOwner());
	SwingActor->SetIgnore();
	SetMovementMode(MOVE_Custom, ECustomMovementMode::CMOVE_RopeSwing);
}

void UAlsCharacterMovementComponent_Extend::ExitSwing(bool bWantsToJump)
{
	if (!SwingActor)
	{
		return;
	}
	
	SwingActor->PlayersOnRope.Remove(GetCharacterOwner());
	SwingActor->SetIgnore();
	GetCharacterOwner()->MoveIgnoreActorRemove(SwingActor);

	if (ROLE_Authority)
	{
		RemoveIgnoreSwingRope(SwingActor);
	}

	if (IsSwinging())
	{
		FFindFloorResult FloorResult;
		FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);
		if (FloorResult.IsWalkableFloor())
		{
			SetMovementMode(MOVE_Walking);
		}
		else
		{
			SetMovementMode(MOVE_Falling);
			if (bWantsToJump)
			{
				FVector HorizontalVelocity = FVector(1,1,0) * Acceleration.GetSafeNormal2D() * JumpZVelocity;
				Velocity = FVector(HorizontalVelocity.X,HorizontalVelocity.Y,JumpZVelocity);
			}
		}
	}

	SwingActor = nullptr;
	OnRopeDistance = 0.0f;
}

int32 GetNearestSplinePointIndex(const USplineComponent* Spline, float Distance)
{
	if (!Spline) return -1;

	int32 NumPoints = Spline->GetNumberOfSplinePoints();
	int32 ClosestIndex = 0;
	float ClosestDistance = FLT_MAX;

	for (int32 i = 0; i < NumPoints; ++i)
	{
		float PointDistance = Spline->GetDistanceAlongSplineAtSplinePoint(i);
		float DistanceDiff = FMath::Abs(PointDistance - Distance);

		if (DistanceDiff < ClosestDistance)
		{
			ClosestDistance = DistanceDiff;
			ClosestIndex = i;
		}
	}

	if (ClosestIndex == NumPoints - 1)
	{
		ClosestIndex = NumPoints - 2;
	}
	
	return ClosestIndex;
}

void UAlsCharacterMovementComponent_Extend::PhysSwing(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!SwingActor)
	{
		ExitSwing(false);
		return;
	}

	Velocity = FVector::Zero();
	
	int32 Index = SwingActor->SplineComponent->GetNumberOfSplinePoints() - 1;
	float MaxDistance = SwingActor->SplineComponent->GetDistanceAlongSplineAtSplinePoint(Index);
	OnRopeDistance = FMath::Clamp(OnRopeDistance - FMath::Sign(GetGravitySpaceZ(Acceleration)) * MoveUpDownSpeed * deltaTime, 0.0f, MaxDistance);
	
	int32 Key = GetNearestSplinePointIndex(SwingActor->SplineComponent, OnRopeDistance);
	if (SwingActor->CapsuleComponents.IsValidIndex(Key))
	{
		SwingActor->CapsuleComponents[Key]->AddForceAtLocation(GetGravityDirection() * 980.0f, UpdatedComponent->GetComponentLocation());
		SwingActor->CapsuleComponents[Key]->AddForceAtLocation(Acceleration, UpdatedComponent->GetComponentLocation());
		DrawDebugLine(GetWorld(), SwingActor->CapsuleComponents[Key]->GetComponentLocation(), SwingActor->CapsuleComponents[Key]->GetComponentLocation()+Acceleration, FColor::Red, false, 0, 0, 1);
	}

	FHitResult Hit(1.f);
	Velocity = SwingActor->SplineComponent->GetLocationAtDistanceAlongSpline(OnRopeDistance, ESplineCoordinateSpace::World) - UpdatedComponent->GetComponentLocation();
	SafeMoveUpdatedComponent(Velocity, UpdatedComponent->GetComponentRotation(), true, Hit);

	if (Hit.Time < 1.f)
	{
		HandleImpact(Hit, deltaTime, Velocity);
		SlideAlongSurface(Velocity, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}
}

bool UAlsCharacterMovementComponent_Extend::IsSwinging() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == CMOVE_RopeSwing;
}

void UAlsCharacterMovementComponent_Extend::RemoveIgnoreSwingRope_Implementation(AActor* RemoveActor)
{
	GetCharacterOwner()->MoveIgnoreActorRemove(SwingActor);
}

void UAlsCharacterMovementComponent_Extend::TryClimbDashing()
{
	if (GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbDashCurve && bIsClimbDashing == false && IsClimbing())
	{
		bIsClimbDashing = true;
		Cast<AAlsCharacter_Extend>(GetCharacterOwner())->K2_EnterClimbDash();
		CurrentClimbDashTime = 0.f;
		
		StoreClimbDashDirection();
	}
}

void UAlsCharacterMovementComponent_Extend::StoreClimbDashDirection()
{
	ClimbDashDirection = UpdatedComponent->GetUpVector();

	const float AccelerationThreshold = GetMovementSettingsExtendSafe()->ClimbingSettings.MaxClimbingAcceleration / 10;
	if (Acceleration.Length() > AccelerationThreshold)
	{
		ClimbDashDirection = Acceleration.GetSafeNormal();
	}
}

bool UAlsCharacterMovementComponent_Extend::IsClimbDashing() const
{
	return IsClimbing() && bIsClimbDashing;
}

FVector UAlsCharacterMovementComponent_Extend::GetClimbDashDirection() const
{
	return ClimbDashDirection;
}

bool UAlsCharacterMovementComponent_Extend::IsClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == CMOVE_FreeClimb;
}

bool UAlsCharacterMovementComponent_Extend::CanStartClimbing(float& HorizontalAccelerationDegrees, TArray<FHitResult>& InCurrentWallHits, FHitResult& InVelocityWallHit, const FVector& CompLoc, const FVector& CompForwardVec) const
{
	// Check movement mode
	if (IsClimbing() || (!bIsSwimOnSurface && IsSwimming()))
	{
		return false;
	}
	
	bool bAllCollided;
	SweepAndStoreWallHits(InCurrentWallHits, InVelocityWallHit, bAllCollided, CompLoc, CompForwardVec);

	FVector Position;
	FVector Normal;
	ComputeSurfaceInfo(InCurrentWallHits, Position, Normal, FVector::Zero(), CompLoc);
	
	const FVector HorizontalNormal = Normal.GetSafeNormal2D();

	const float HorizontalForwardDot = FVector::DotProduct(CompForwardVec, -HorizontalNormal);
	const float HorizontalAccelerationDot = FVector::DotProduct(Acceleration.GetSafeNormal2D(), -HorizontalNormal);
	const float VerticalDot = FVector::DotProduct(Normal, HorizontalNormal);

	const float HorizontalForwardDegrees = FMath::RadiansToDegrees(FMath::Acos(HorizontalForwardDot));
	HorizontalAccelerationDegrees = FMath::RadiansToDegrees(FMath::Acos(HorizontalAccelerationDot));
	const float VerticalDegrees = FMath::RadiansToDegrees(FMath::Acos(VerticalDot));

	const bool bIsCeiling = FMath::IsNearlyZero(VerticalDot);
#if WITH_EDITOR
	if (DebugDrawSwitch)
	{
		DrawDebugLine(GetWorld(), Position, Position+(Normal*100), FColor::Red, false, 0, 0, 1);
	}
#endif
	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultScaledCapsule(DefaultHalfHeight, DefaultRadius);
	if (IsFacingSurface(CompLoc, -GetGravityDirection(), CompForwardVec, DefaultHalfHeight, DefaultRadius) &&
		HorizontalForwardDegrees <= GetMovementSettingsExtendSafe()->ClimbingSettings.MinHorizontalDegreesToStartClimbing &&
		VerticalDegrees <= 90 - GetWalkableFloorAngle() &&
		!bIsCeiling &&
		bAllCollided)
	{
		return true;
	}
	
	return false;
}

bool UAlsCharacterMovementComponent_Extend::CheckCanStartClimbing(const FVector CompLoc, const FVector CompForwardVec)
{
	float a;
	TArray<FHitResult> b;
	FHitResult c;
	return CanStartClimbing(a, b, c, CompLoc, CompForwardVec);
}

void UAlsCharacterMovementComponent_Extend::StartClimbingTimer(float HorizontalAccelerationDegrees, float DeltaSeconds, bool bCanStartClimbing)
{
	if (!bCanStartClimbing)
	{
		return;
	}
	
	if (HorizontalAccelerationDegrees <= GetMovementSettingsExtendSafe()->ClimbingSettings.MinHorizontalDegreesToStartClimbing)
	{
		//Increase enter climb time
		TryEnterClimbTime = FMath::Clamp(TryEnterClimbTime + DeltaSeconds, 0.0f, GetMovementSettingsExtendSafe()->ClimbingSettings.TryEnterClimbDuration);
		TryEnterClimbAlpha = TryEnterClimbTime / GetMovementSettingsExtendSafe()->ClimbingSettings.TryEnterClimbDuration;
			
		if (TryEnterClimbAlpha >= 1.0f)
		{
			Cast<AAlsCharacter_Extend>(CharacterOwner)->K2_AutoTryClimb();
			TryEnterClimbTime = 0.0f;
			TryEnterClimbAlpha = 0.0f;
		}
	}
	else
	{
		//Decrease enter climb time
		TryEnterClimbTime = FMath::Clamp(TryEnterClimbTime - DeltaSeconds, 0.0f, GetMovementSettingsExtendSafe()->ClimbingSettings.TryEnterClimbDuration);
		TryEnterClimbAlpha = TryEnterClimbTime / GetMovementSettingsExtendSafe()->ClimbingSettings.TryEnterClimbDuration;
	}
}

bool UAlsCharacterMovementComponent_Extend::IsClimbMoving() const
{
	const bool bIsClimbing = IsClimbing();
	const float fInputAccelerationLength = GetCurrentAcceleration().Size();
	const float fVelocityLength = Velocity.Size();

	return bIsClimbing && fInputAccelerationLength > 0 && fVelocityLength > 0 && !HasAnimRootMotion();
}

FNetworkPredictionData_Client* UAlsCharacterMovementComponent_Extend::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		auto* MutableThis{const_cast<ThisClass*>(this)};

		MutableThis->ClientPredictionData = new FAlsNetworkPredictionData_Extend{*this};
	}

	return ClientPredictionData;
}

float UAlsCharacterMovementComponent_Extend::GetImmerseDepth() const
{
	if (!CharacterOwner)
	{
		return -1;
	}

	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultScaledCapsule(DefaultHalfHeight, DefaultRadius);
	const FVector WaterSurface = GetWaterSurface();
	return (WaterSurface.Z - (UpdatedComponent->GetComponentLocation().Z - DefaultHalfHeight));
}

void UAlsCharacterMovementComponent_Extend::SetCharacterBase(UPrimitiveComponent* BaseComponent)
{
	SetBase(BaseComponent, FName("None"));
}

FQuat UAlsCharacterMovementComponent_Extend::GetClimbingRotation(float deltaTime) const
{
	const FQuat Current = UpdatedComponent->GetComponentQuat();
	const FQuat Target = FRotationMatrix::MakeFromX(-CurrentClimbingNormal).ToQuat();

	if (HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity())
	{
		return Current;
	}

	const float RotationSpeed = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbingRotationSpeed * FMath::Max(1, Velocity.Length() / GetMovementSettingsExtendSafe()->ClimbingSettings.MaxClimbingSpeed);

	return FMath::QInterpTo(Current, Target, deltaTime, RotationSpeed);
}

void UAlsCharacterMovementComponent_Extend::SingleSweep(FHitResult& Hit, const FVector& Start, const FVector& End, const FCollisionShape Shape) const
{
	const auto TraceChannel = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbTraceChannel;
	GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, TraceChannel, Shape, ClimbQueryParams);
#if WITH_EDITOR
	if (DebugDrawSwitch)
	{
		UAlsDebugUtility::DrawSweepSphere(GetWorld(), Start, End, Shape.GetSphereRadius(), FColor::Red);
	}
#endif
}

void UAlsCharacterMovementComponent_Extend::SweepAndStoreWallHits(TArray<FHitResult>& Results, FHitResult& Hits_Velocity, bool& bAllSweepCollided, const FVector& CompLocation, const FVector& CompForwardVector) const
{
	Results.Empty();
	
	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultScaledCapsule(DefaultHalfHeight, DefaultRadius);
	const FCollisionShape CollisionShape = FCollisionShape::MakeSphere(DefaultRadius);

	const FVector ClampedAccelerationDir = UKismetMathLibrary::GetDirectionUnitVector(
		FVector::Zero(), FVector(Acceleration.X, Acceleration.Y, FMath::Max(GetGravitySpaceZ(Acceleration), 0)));

	const FVector ClampedVelocityDir = UKismetMathLibrary::GetDirectionUnitVector(
		FVector::Zero(), FVector(Acceleration.X, Acceleration.Y, FMath::Max(GetGravitySpaceZ(Acceleration), 0)));

	const FVector Dir = Velocity.IsNearlyZero() ? ClampedAccelerationDir : ClampedVelocityDir;
	
	const FVector Start_Forward = CompLocation;
	const FVector Start_Up = Start_Forward + (DefaultHalfHeight - DefaultRadius) * GetCharacterOwner()->GetActorUpVector();
	const FVector Start_Down = Start_Forward - (DefaultHalfHeight - DefaultRadius) * GetCharacterOwner()->GetActorUpVector();

	// Fixing sweep can't trace by angle distance when walking.
	const float PredictDistance = GetAngleBaseDistance(DefaultRadius, 2 * DefaultHalfHeight - DefaultRadius);
	const FVector End_ForwardAmount = CompForwardVector * PredictDistance;
	const FVector End_VelocityTrace = IsClimbing() ? Dir * FMath::Clamp(Velocity.Length(), 0.0f, DefaultRadius * 2) : Dir * PredictDistance;
	
	FHitResult Hits_Up;
	FHitResult Hits_Forward;
	FHitResult Hits_Down;

	SingleSweep(Hits_Up, Start_Up, Start_Up + End_ForwardAmount, CollisionShape);
	SingleSweep(Hits_Forward, Start_Forward, Start_Forward + End_ForwardAmount, CollisionShape);
	SingleSweep(Hits_Down, Start_Down, Start_Down + End_ForwardAmount, CollisionShape);
	SingleSweep(Hits_Velocity, Start_Forward, Start_Forward + End_VelocityTrace, CollisionShape);
	
	bAllSweepCollided = Hits_Up.bBlockingHit && Hits_Forward.bBlockingHit;
	
	Results.Add(Hits_Up);
	Results.Add(Hits_Forward);
	Results.Add(Hits_Down);
}

float UAlsCharacterMovementComponent_Extend::GetAngleBaseDistance(const float CompRadius, const float HeightOffset) const
{
	const auto StartClimbDegrees = GetMovementSettingsExtendSafe()->ClimbingSettings.MinVerticalDegreesToStartClimbing;
	const auto StartClimbCos = AngleToZ(StartClimbDegrees);
	const auto StartClimbTan = FMath::Tan(FMath::DegreesToRadians(StartClimbDegrees));
	const auto PredictFloorDistance = (1 - StartClimbCos) * CompRadius / StartClimbCos;
	return (HeightOffset + PredictFloorDistance) / StartClimbTan;
}

FVector UAlsCharacterMovementComponent_Extend::GetEyeLocation(const FVector& CompLoc, const FVector& CompUp, const float Offset) const
{
	return CompLoc + CompUp * (CharacterOwner->BaseEyeHeight + Offset);
}

bool UAlsCharacterMovementComponent_Extend::EyeHeightTrace(const float TraceDistance, const FVector& CompLoc, const FVector& CompUp, const FVector& CompForward) const
{
	FHitResult UpperEdgeHit;

	// Calculate eye height offset.
	const FVector Start = GetEyeLocation(CompLoc, CompUp, GetMovementSettingsExtendSafe()->ClimbingSettings.EyeHeightOffset);
	const FVector End = Start + CompForward * TraceDistance;
	const auto TraceChannel = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbTraceChannel;
	const bool TraceHit = GetWorld()->LineTraceSingleByChannel(UpperEdgeHit, Start, End, TraceChannel, ClimbQueryParams);
#if WITH_EDITOR
	if (DebugDrawSwitch)
	{
		// Eye height trace debug draw
		DrawDebugLine(GetWorld(), Start, End, FColor::Black, false, 0, 1, 1);
		if (TraceHit)
		{
			DrawDebugPoint(GetWorld(), UpperEdgeHit.Location, 8.0f, FColor::Green, false, 0, 1);
		}
	}
#endif
	return TraceHit;
}

bool UAlsCharacterMovementComponent_Extend::IsFacingSurface(const FVector& CompLoc, const FVector& CompUp, const FVector& CompForward, const float CompHalfHeight, const float CompRadius) const
{
	// Get climbing degrees
	const auto PredictDistance = GetAngleBaseDistance(CompRadius, CompHalfHeight + CharacterOwner->BaseEyeHeight);

	// Add a radius to make trace further to check current slope
	return EyeHeightTrace(PredictDistance + CompRadius, CompLoc, CompUp, CompForward);
}

void UAlsCharacterMovementComponent_Extend::ComputeSurfaceInfo(TArray<FHitResult>& WallHits, FVector& Position, FVector& Normal, const FVector& VelocityHitNormal, const FVector& Start) const
{
	Normal = FVector::ZeroVector;
	Position = FVector::ZeroVector;

	if (WallHits.IsEmpty())
	{
		return;
	}
	
	const FCollisionShape CollisionSphere = FCollisionShape::MakeSphere(6);

	TArray<FHitResult> WallHitsAfterCompute;
	for (const FHitResult& WallHit : WallHits)
	{
		const FVector End = Start + (WallHit.ImpactPoint - Start).GetSafeNormal() * 120;

		FHitResult AssistHit;
		const auto TraceChannel = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbTraceChannel;
		GetWorld()->SweepSingleByChannel(AssistHit, Start, End, FQuat::Identity,
			TraceChannel, CollisionSphere, ClimbQueryParams);
		
		Position += AssistHit.Location;
		Normal += AssistHit.Normal;

		WallHitsAfterCompute.Add(WallHit);
	}
	WallHits = WallHitsAfterCompute;

	Position /= WallHits.Num();
	Normal = Normal.GetSafeNormal();
	Normal = (Normal + VelocityHitNormal).GetSafeNormal();
}

void UAlsCharacterMovementComponent_Extend::ComputeClimbingVelocity(float deltaTime)
{
	RestorePreAdditiveRootMotionVelocity();

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		if (bIsClimbDashing)
		{
			AlignClimbDashDirection();

			const float CurrentCurveSpeed = GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbDashCurve->GetFloatValue(CurrentClimbDashTime);
			Velocity = ClimbDashDirection * CurrentCurveSpeed;
		}
		else
		{
			constexpr float Friction = 0.0f;
			constexpr bool bFluid = false;
			CalcVelocity(deltaTime, Friction, bFluid, GetMovementSettingsExtendSafe()->ClimbingSettings.BrakingDecelerationClimbing);
		}
	}

	ApplyRootMotionToVelocity(deltaTime);
}

void UAlsCharacterMovementComponent_Extend::AlignClimbDashDirection()
{
	const FVector HorizontalSurfaceNormal = GetClimbSurfaceNormal();
	
	ClimbDashDirection = FVector::VectorPlaneProject(ClimbDashDirection, HorizontalSurfaceNormal);
}

void UAlsCharacterMovementComponent_Extend::BeginPlay()
{
	Super::BeginPlay();

	ClimbQueryParams.AddIgnoredActor(GetOwner());
	ClimbQueryParams.bTraceComplex = true;
}

void UAlsCharacterMovementComponent_Extend::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Slide) ExitSlide();
	if (IsSliding()) EnterSlide();

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_RopeSwing && !IsSwinging())
	{
		ExitSwing(false);
	}

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Gliding && !IsGliding())
	{
		bWantsToGlide = false;
	}
	
	// Set back to default half height and radius
	if (!IsSwimming() || !IsClimbing())
	{
		float DefaultHalfHeight;
		float DefaultRadius;
		float UnscaledCrouchHalfHeight;
		GetDefaultUnscaledCapsule(DefaultHalfHeight, DefaultRadius);
		GetUnscaledCrouchHalfHeight(UnscaledCrouchHalfHeight);

		const float FinalHalfHeight = Stance == AlsStanceTags::Crouching ? UnscaledCrouchHalfHeight : DefaultHalfHeight;
		GetCharacterOwner()->GetCapsuleComponent()->SetCapsuleSize(DefaultRadius, FinalHalfHeight);
		Cast<AAlsCharacter_Extend>(CharacterOwner)->UpdateMeshRelativeLocation(FinalHalfHeight, true);
	}

	const auto AlsCharExtend = Cast<AAlsCharacter_Extend>(GetCharacterOwner());
	if (!AlsCharExtend) return;
	const auto FoundSettings = AlsCharExtend->GetCapsuleSettings();
	if (IsSwimming() && FoundSettings)
	{
		bool bValid;
		const auto Settings = FoundSettings->QueryCapsuleSizeByTag(AlsLocomotionModeTags::Swimming, bValid);
		if (bValid)
		{
			GetCharacterOwner()->GetCapsuleComponent()->SetCapsuleSize(Settings.CapsuleRadius, Settings.CapsuleHalfHeight);
			Cast<AAlsCharacter_Extend>(CharacterOwner)->UpdateMeshRelativeLocation(Settings.CapsuleHalfHeight, true);
		}
	}
	
	if (IsClimbing())
	{
		if (PreviousMovementMode == MOVE_Swimming)
		{
			bSwimToClimb = true;
		}
		if (FoundSettings)
		{
			bool bValid;
			const auto Settings = FoundSettings->QueryCapsuleSizeByTag(AlsLocomotionModeTags::FreeClimbing, bValid);
			if (bValid)
			{
				GetCharacterOwner()->GetCapsuleComponent()->SetCapsuleSize(Settings.CapsuleRadius, Settings.CapsuleHalfHeight);
				// We don't treat climbing like crouch logic(move mesh and capsule to the ground. just set size not move.)
				//Cast<AAlsCharacter_Extend>(CharacterOwner)->UpdateMeshRelativeLocation(Settings.CapsuleHalfHeight, true);
			}
		}
		
		bool Temp;
		SweepAndStoreWallHits(CurrentWallHits, VelocityWallHit, Temp, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetForwardVector());
	}

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_FreeClimb)
	{
		StopClimbDashing();
		
		const FRotator StandRotation = FRotator(0, UpdatedComponent->GetComponentRotation().Yaw, 0);
		UpdatedComponent->MoveComponent(FVector::Zero(), StandRotation, true);
	}
	
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void UAlsCharacterMovementComponent_Extend::PhysCustom(float deltaTime, int32 Iterations)
{
	if (CustomMovementMode == CMOVE_FreeClimb)
	{
		PhysClimbing(deltaTime, Iterations);
	}
	else if (CustomMovementMode == CMOVE_Slide)
	{
		PhysSliding(deltaTime, Iterations);
	}
	else if (CustomMovementMode == CMOVE_RopeSwing)
	{
		PhysSwing(deltaTime, Iterations);
	}
	else if (CustomMovementMode == CMOVE_Gliding)
	{
		PhysGliding(deltaTime, Iterations);	
	}
	else
	{
		Super::PhysCustom(deltaTime, Iterations);
	}
}

float UAlsCharacterMovementComponent_Extend::GetMaxSpeed() const
{
	if (IsClimbing())
	{
		
		auto AngleDegree = FVector::DotProduct(CurrentClimbingNormal, -GetGravityDirection());
		return GetMovementSettingsExtendSafe()->ClimbingSettings.MaxClimbingSpeed + AngleDegree * GetMovementSettingsExtendSafe()->ClimbingSettings.SlopeSpeedMultiplier;
	}
	
	if (IsSliding())
	{
		return GetMovementSettingsExtendSafe()->SlidingSettings.MaxSlideSpeed;
	}
	
	if (IsSwimming())
	{
		float MaxSpeed = GetMovementSettingsExtendSafe()->SwimmingSettings.RunSpeed;
		if (MaxAllowedGait == AlsGaitTags::Sprinting)
		{
			MaxSpeed = GetMovementSettingsExtendSafe()->SwimmingSettings.SprintSpeed;
		}
		return MaxSpeed;
	}

	if (IsWalking())
	{
		const FFindFloorResult Hit = CurrentFloor;
		float SlopeAngleDot = 0;
		float FaceSlopeAmount = 0;

		if (Hit.bBlockingHit)
		{
			SlopeAngleDot = FVector::DotProduct(Hit.HitResult.ImpactNormal, Hit.HitResult.ImpactNormal.GetSafeNormal2D());
			FaceSlopeAmount = FVector::DotProduct(FVector(Acceleration.GetSafeNormal2D()),FVector(Hit.HitResult.ImpactNormal.GetSafeNormal2D()));
		}
		const float SlopeAlpha = FaceSlopeAmount > 0 ? SlopeDownwardAlpha : SlopeUpwardAlpha;
		
		return IsCrouching() ? MaxWalkSpeedCrouched : (MaxWalkSpeed + SlopeAlpha * SlopeAngleDot * MaxSpeedSlopeBaseValue * FaceSlopeAmount);
	}

	if (IsFlying())
	{
		float MaxSpeed = GetMovementSettingsExtendSafe()->FlyingSettings.MaxFlySpeed;
		if (MaxAllowedGait == AlsGaitTags::Sprinting)
		{
			MaxSpeed = GetMovementSettingsExtendSafe()->FlyingSettings.FlyFasterMaxSpeed;
		}
		return MaxSpeed;
	}

	if (IsGliding())
	{
		return FMath::Max(GetMovementSettingsExtendSafe()->GlidingSettings.MaxGlideSpeed, FVector2D(Velocity).Length());
	}
	
	return Super::GetMaxSpeed();
}

float UAlsCharacterMovementComponent_Extend::GetMaxAcceleration() const
{
	if (IsClimbing())
	{
		return GetMovementSettingsExtendSafe()->ClimbingSettings.MaxClimbingAcceleration;
	}
	
	if (IsSliding())
	{
		return GetMovementSettingsExtendSafe()->SlidingSettings.MaxSlideAcceleration;
	}

	if (IsGliding())
	{
		return GetMovementSettingsExtendSafe()->GlidingSettings.MaxGlideAcceleration;
	}
		
	return Super::GetMaxAcceleration();
}

float UAlsCharacterMovementComponent_Extend::GetMaxBrakingDeceleration() const
{
	if (IsSliding())
	{
		return GetMovementSettingsExtendSafe()->SlidingSettings.BrakingDecelerationSliding;
	}

	if (IsClimbing())
	{
		return GetMovementSettingsExtendSafe()->ClimbingSettings.BrakingDecelerationClimbing;
	}

	if (IsGliding())
	{
		return GetMovementSettingsExtendSafe()->GlidingSettings.BrakingDecelerationGliding;
	}

	if (IsFlying())
	{
		return GetMovementSettingsExtendSafe()->FlyingSettings.BrakingDecelerationFlying;
	}
	return Super::GetMaxBrakingDeceleration();
}

bool UAlsCharacterMovementComponent_Extend::IsMovingOnGround() const
{
	if (IsSliding())
	{
		return true;
	}
	
	return Super::IsMovingOnGround();
}

void UAlsCharacterMovementComponent_Extend::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	//The Flags parameter contains the compressed input flags that are stored in the saved move.
	//UpdateFromCompressed flags simply copies the flags from the saved move into the movement component.
	//It basically just resets the movement component to the state when the move was made so it can simulate from there.
	bWantsToJumpOutOfWater = (Flags & FAlsSavedMove_Extend::FLAG_JumpOutWater) != 0;
	bWantsToClimb = (Flags & FAlsSavedMove_Extend::FLAG_Climb) != 0;
	bWantsToGlide = (Flags & FAlsSavedMove_Extend::FLAG_Glide) != 0;
}

void UAlsCharacterMovementComponent_Extend::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Update water
	UpdateWaterInfoForSwim();
	
	// Reset jump out of water & Update Swimming
	if (GetGravitySpaceZ(Velocity) < 0.0f && bJumpingOutOfWater == true)
	{
		bJumpingOutOfWater = false;
	}
	
	if (IsSwimming() && WaterBodyComponents.Num() > 0)
	{
		bIsSwimOnSurface = GetImmerseDepth() <= GetMovementSettingsExtendSafe()->SwimmingSettings.SwimOnSurfaceDepth;
		// Update swimming state.
		const FGameplayTag SwimmingStateTag = bIsSwimOnSurface ? AlsSwimmingStateTags::Surface : AlsSwimmingStateTags::Underwater;
		Cast<AAlsCharacter_Extend>(GetOwner())->SetGameplayTagInASC(SwimmingStateTag);
	}
	else
	{
		bIsSwimOnSurface = false;
		if (IsInWater() && !bJumpingOutOfWater && !IsClimbing())
		{
			SetMovementMode(MOVE_Swimming);
			
			//reset wants to jump out of water after entering swimming
			bWantsToJumpOutOfWater = false;
		}
	}

	// Enter glide.
	if (bWantsToGlide && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		SetMovementMode(MOVE_Custom, CMOVE_Gliding);
	}
	
	// Update free climbing
	if (bWantsToClimb && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		SetMovementMode(MOVE_Custom, CMOVE_FreeClimb);
	}
	if (IsClimbing())
	{
		if (bSwimToClimb && (WaterBodyComponents.Num() == 0 || GetGravitySpaceZ(Acceleration) < 0))
		{
			bSwimToClimb = false;
		}
		//Climb check
		bool bAllCollided;
		SweepAndStoreWallHits(CurrentWallHits, VelocityWallHit, bAllCollided, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetForwardVector());
	}
	else if (Acceleration.Length() > 1.0f &&
		!IsCrouching() &&
		!CurrentRootMotion.HasVelocity() &&
		IsMovingOnGround())
	{
		float DefaultStandRadius;
		float DefaultStandHalfHeight;
		GetDefaultScaledCapsule(DefaultStandHalfHeight, DefaultStandRadius);
		if (IsFacingSurface(UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetUpVector(), UpdatedComponent->GetForwardVector(), DefaultStandHalfHeight, DefaultStandRadius))
		{
			float AccelHorDegree;
			bool bCanStartClimbing = CanStartClimbing(AccelHorDegree, CurrentWallHits, VelocityWallHit, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetForwardVector());
			StartClimbingTimer(AccelHorDegree, DeltaSeconds, bCanStartClimbing);
		}
	}

	// Update can climb down(Only local player.)
	if (CharacterOwner->IsLocallyControlled() && !CharacterOwner->IsBotControlled())
	{
		FVector ClimbDownNull_A;
		FRotator ClimbDownNull_B;
		CheckClimbDownLedge(ClimbDownNull_A, ClimbDownNull_A, ClimbDownNull_B, bCanClimbDownLedge);
	}

	// Check landing when flying
	if (IsFlying() && GetGravitySpaceZ(Velocity) < 0 && GetMovementSettingsExtendSafe()->FlyingSettings.bShouldCheckLand && !HasAnimRootMotion())
	{
		FFindFloorResult FloorResult;
		FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);
	
		if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), FloorResult.HitResult))
		{
			SetMovementMode(MOVE_Falling);
		}
	}

	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UAlsCharacterMovementComponent_Extend::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UAlsCharacterMovementComponent_Extend, bWantsToJumpOutOfWater, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UAlsCharacterMovementComponent_Extend, bWantsToClimb, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UAlsCharacterMovementComponent_Extend, bWantsToGlide, COND_SkipOwner);
	DOREPLIFETIME(UAlsCharacterMovementComponent_Extend, bIsClimbDashing);
	DOREPLIFETIME(UAlsCharacterMovementComponent_Extend, SwingActor);
	DOREPLIFETIME(UAlsCharacterMovementComponent_Extend, OnRopeDistance);
	DOREPLIFETIME(UAlsCharacterMovementComponent_Extend, MoveUpDownSpeed);
}

void UAlsCharacterMovementComponent_Extend::PhysicsVolumeChanged(APhysicsVolume* NewVolume)
{
	if (bPhysicsVolumeAffectMovement)
	{
		return Super::PhysicsVolumeChanged(NewVolume);
	}
}

APhysicsVolume* UAlsCharacterMovementComponent_Extend::GetPhysicsVolume() const
{
	if (bPhysicsVolumeAffectMovement)
	{
		return Super::GetPhysicsVolume();
	}
	return GetWorld()->GetDefaultPhysicsVolume();
}

bool UAlsCharacterMovementComponent_Extend::IsInWater() const
{
	// Check is overlap water.
	if (WaterBodyComponents.Num() == 0)
	{
		return false;
	}

	float HalfHeight;
	float Radius;
	GetDefaultScaledCapsule(HalfHeight, Radius);
	
	const auto Position = GetWaterSurface();
	const auto Bottom = Cast<AAlsCharacter_Extend>(GetCharacterOwner())->GetCapsuleBottom();
	// Is negative number
	const float BottomDepth = Bottom.Z - Position.Z;
	// Walking situation
	if (IsWalking())
	{
		return BottomDepth < -1 * HalfHeight;
	}
	
	// Common situation
	return UpdatedComponent->GetComponentLocation().Z < Position.Z;
}

float UAlsCharacterMovementComponent_Extend::ImmersionDepth() const
{
	float ScaledHalfHeight;
	float ScaledRadius;
	GetDefaultScaledCapsule(ScaledHalfHeight, ScaledRadius);

	const auto AlsCharExtend = Cast<AAlsCharacter_Extend>(GetCharacterOwner());
	if (const auto FoundSettings = AlsCharExtend->GetCapsuleSettings())
	{
		bool bValid;
        const auto Settings = FoundSettings->QueryCapsuleSizeByTag(AlsLocomotionModeTags::Swimming, bValid);
        if (bValid)
        {
        	ScaledHalfHeight = Cast<AAlsCharacter_Extend>(GetCharacterOwner())->GetScaledHaleHeight(Settings.CapsuleHalfHeight);
        }
	}

	float Depth = 0.f;
	if (WaterBodyComponents.Num() > 0)
	{
		Depth = FMath::Clamp(GetImmerseDepth() / (ScaledHalfHeight * 2), 0.f, 1.0f);
	}
	return Depth;
}

#pragma region Slide

bool UAlsCharacterMovementComponent_Extend::CanSlide() const
{
	FCollisionQueryParams SlideQueryParams;
	SlideQueryParams.AddIgnoredActor(GetOwner());
	SlideQueryParams.bTraceComplex = true;

	FFindFloorResult FloorResult;
	FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);
	const bool bValidSurface = FloorResult.IsWalkableFloor();
	const bool bEnoughSpeed = Velocity.SizeSquared() > pow(GetMovementSettingsExtendSafe()->SlidingSettings.MinSlideSpeed, 2);
	
	return bValidSurface && bEnoughSpeed;
}

void UAlsCharacterMovementComponent_Extend::EnterSlide()
{
	Velocity += Velocity.GetSafeNormal2D() * GetMovementSettingsExtendSafe()->SlidingSettings.SlideEnterImpulse;
	SetMaxAllowedGait(AlsGaitTags::Walking);
	Cast<AAlsCharacter_Extend>(CharacterOwner)->K2_EnterSlide();
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, true, nullptr);
}

void UAlsCharacterMovementComponent_Extend::ExitSlide()
{
	SetMaxAllowedGait(AlsGaitTags::Sprinting);
	Cast<AAlsCharacter_Extend>(CharacterOwner)->K2_ExitSlide();
}

void UAlsCharacterMovementComponent_Extend::PhysSliding(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CanSlide())
	{
		SetMovementModeLocked(false);
		SetMovementMode(MOVE_Walking);
		StartNewPhysics(deltaTime, Iterations);
		return;
	}

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Save current values
		UPrimitiveComponent* const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != nullptr) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		// Ensure velocity is horizontal.
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;

		FVector SlopeForce = CurrentFloor.HitResult.Normal;
		Velocity += ProjectToGravityFloor(SlopeForce) * GetMovementSettingsExtendSafe()->SlidingSettings.SlideGravityForce * deltaTime;

		Acceleration = Acceleration.ProjectOnTo(UpdatedComponent->GetRightVector().GetSafeNormal2D()) * GetMovementSettingsExtendSafe()->SlidingSettings.SlideRotationMultiplier;

		// Apply acceleration
		CalcVelocity(timeTick, GroundFriction * GetMovementSettingsExtendSafe()->SlidingSettings.SlideFrictionFactor, false, GetMaxBrakingDeceleration());

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;
		bool bFloorWalkable = CurrentFloor.IsWalkableFloor();

		if (bZeroDelta)
		{
			remainingTime = 0.f;
		}
		else
		{
			// try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsFalling())
			{
				// pawn decided to jump up
				const float DesiredDist = Delta.Size();
				if (DesiredDist > KINDA_SMALL_NUMBER)
				{
					const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size2D();
					remainingTime += timeTick * (1.f - FMath::Min(1.f, ActualDist / DesiredDist));
				}
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
		}

		// Update floor.
		// StepUp might have already done it for us.
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, nullptr);
		}


		// check for ledges here
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// calculate possible alternate movement
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, OldFloor);
			if (!NewDelta.IsZero())
			{
				// first revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				continue;
			}
			else
			{
				bool bMustJump = bZeroDelta || (OldBase == nullptr || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;

				// revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.f;
				break;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart - GetGravityDirection() * MAX_FLOOR_DIST;
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// check if just entered water
			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == nullptr || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;
			}
		}

		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround() && bFloorWalkable)
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
				MaintainHorizontalGroundVelocity();
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
	}


	FHitResult Hit;
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal2D(), -GetGravityDirection()).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, Hit);
}

bool UAlsCharacterMovementComponent_Extend::IsSliding() const
{
	return MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == ECustomMovementMode::CMOVE_Slide;
}
#pragma endregion Slide

void UAlsCharacterMovementComponent_Extend::ToggleGlide()
{
	if (bWantsToGlide)
	{
		bWantsToGlide = false;
	}
	else if (CheckCanGlide())
	{
		bWantsToGlide = true;
	}
}

bool UAlsCharacterMovementComponent_Extend::CheckCanGlide()
{
	if (!Cast<AAlsCharacter_Extend>(CharacterOwner)->IsAllowGliding())
	{
		return false;
	}
	
	if (Super::IsFalling())
	{
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(GetOwner());
		GetWorld()->LineTraceSingleByChannel(HitResult, UpdatedComponent->GetComponentLocation(),
					UpdatedComponent->GetComponentLocation() + GetGravityDirection() * GetMovementSettingsExtendSafe()->GlidingSettings.CanStartGlideHeight, ECC_Pawn, Params);
		
		if (!HitResult.IsValidBlockingHit())
		{
			//Check Water
			FHitResult HitResult_1;
			GetWorld()->LineTraceSingleByObjectType(HitResult_1, UpdatedComponent->GetComponentLocation(),
							UpdatedComponent->GetComponentLocation() + GetGravityDirection() * GetMovementSettingsExtendSafe()->GlidingSettings.CanStartGlideHeight, ECC_WorldStatic, Params);
			if (!Cast<AWaterBody>(HitResult_1.GetActor()))
			{
				return true;
			}
		}
		else if (!IsWalkable(HitResult))
		{
			// If slipping in not walkable slope, we allow player to start glide.
			return true;
		}
	}
	return false;
}

bool UAlsCharacterMovementComponent_Extend::IsGliding() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == CMOVE_Gliding;
}

bool UAlsCharacterMovementComponent_Extend::IsFalling() const
{
	return Super::IsFalling() || IsGliding();
}

FVector UAlsCharacterMovementComponent_Extend::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity,
	float DeltaTime) const
{
	auto NewFallVelocity = Super::NewFallVelocity(InitialVelocity, Gravity, DeltaTime);
	if (IsGliding())
	{
		const auto GlideInterpSpeed = GetMovementSettingsExtendSafe()->GlidingSettings.InterpToTargetGlideSpeed;
		const float MaxGlideDownSpeed = GetMovementSettingsExtendSafe()->GlidingSettings.MaxGlideDownSpeed;
		auto CurrentVelocityZ = GetGravitySpaceZ(InitialVelocity);
		// Only try to clamp velocity when falling speed is overload.
		if (FMath::Abs(CurrentVelocityZ) >= MaxGlideDownSpeed)
		{
			CurrentVelocityZ = FMath::FInterpTo(CurrentVelocityZ, FMath::Sign(CurrentVelocityZ) * MaxGlideDownSpeed, DeltaTime, GlideInterpSpeed);
			SetGravitySpaceZ(NewFallVelocity, CurrentVelocityZ);
			return NewFallVelocity;
		}
	}
	
	return Super::NewFallVelocity(InitialVelocity, Gravity, DeltaTime);
}

FVector UAlsCharacterMovementComponent_Extend::GetAirControl(float DeltaTime, float TickAirControl,
	const FVector& FallAcceleration)
{
	if (IsGliding())
	{
		Super::GetAirControl(DeltaTime, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingAirControl, FallAcceleration);
	}
	
	return Super::GetAirControl(DeltaTime, TickAirControl, FallAcceleration);
}

void UAlsCharacterMovementComponent_Extend::CalcVelocity(float DeltaTime, float Friction, bool bFluid,
	float BrakingDeceleration)
{
	if (IsGliding())
	{
		Super::CalcVelocity(DeltaTime, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingLateralFriction, bFluid, BrakingDeceleration);
	}
	
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UAlsCharacterMovementComponent_Extend::PhysGliding(float deltaTime, int32 Iterations)
{
	PhysFalling(deltaTime, Iterations);
	if (!bWantsToGlide)
	{
		SetMovementMode(MOVE_Falling);
	}
}

void UAlsCharacterMovementComponent_Extend::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	if( CharacterOwner && CharacterOwner->ShouldNotifyLanded(Hit) )
	{
		CharacterOwner->Landed(Hit);
	}
	if( IsFalling() || IsGliding() )
	{
		if (GetGroundMovementMode() == MOVE_NavWalking)
		{
			// verify navmesh projection and current floor
			// otherwise movement will be stuck in infinite loop:
			// navwalking -> (no navmesh) -> falling -> (standing on something) -> navwalking -> ....

			const FVector TestLocation = GetActorFeetLocation();
			FNavLocation NavLocation;

			const bool bHasNavigationData = FindNavFloor(TestLocation, NavLocation);
			if (!bHasNavigationData || NavLocation.NodeRef == INVALID_NAVNODEREF)
			{
				SetGroundMovementMode(MOVE_Walking);
			}
		}

		SetPostLandedPhysics(Hit);
	}

	if (IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent())
	{
		PFAgent->OnLanded();
	}

	StartNewPhysics(remainingTime, Iterations);
}

void FAlsSavedMove_Extend::Clear()
{
	FAlsSavedMove::Clear();

	//Clear variables back to their default values.
	bSavedWantsToJumpOutOfWater = false;
	bSavedWantsToClimb = false;
	bSavedWantsToGlide = false;
}

uint8 FAlsSavedMove_Extend::GetCompressedFlags() const
{
	uint8 Result = FAlsSavedMove::GetCompressedFlags();

	if (bSavedWantsToJumpOutOfWater)
	{
		Result |= FLAG_JumpOutWater;
	}
	
	if (bSavedWantsToClimb)
	{
		Result |= FLAG_Climb;
	}

	if (bSavedWantsToGlide)
	{
		Result |= FLAG_Glide;
	}
	
	return Result;
}

bool FAlsSavedMove_Extend::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	const auto* NewMoveExtend = static_cast<FAlsSavedMove_Extend*>(NewMove.Get());
	
	if (bSavedWantsToJumpOutOfWater != NewMoveExtend->bSavedWantsToJumpOutOfWater)
	{
		return false;
	}

	if (bSavedWantsToClimb != NewMoveExtend->bSavedWantsToClimb)
	{
		return false;
	}

	if (bSavedWantsToGlide != NewMoveExtend->bSavedWantsToGlide)
	{
		return false;
	}
	
	return FAlsSavedMove::CanCombineWith(NewMove, Character, MaxDelta);
}

void FAlsSavedMove_Extend::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	FAlsSavedMove::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	if (UAlsCharacterMovementComponent_Extend* CharMov = Cast<UAlsCharacterMovementComponent_Extend>(Character->GetCharacterMovement()))
	{
		//This is literally just the exact opposite of UpdateFromCompressed flags. We're taking the input
		//from the player and storing it in the saved move.
		bSavedWantsToJumpOutOfWater = CharMov->bWantsToJumpOutOfWater;
		bSavedWantsToClimb = CharMov->bWantsToClimb;
		bSavedWantsToGlide = CharMov->bWantsToGlide;
	}
}

void FAlsSavedMove_Extend::PrepMoveFor(ACharacter* Character)
{
	FAlsSavedMove::PrepMoveFor(Character);

	if (UAlsCharacterMovementComponent_Extend* CharMov = Cast<UAlsCharacterMovementComponent_Extend>(Character->GetCharacterMovement()))
	{
		CharMov->bWantsToJumpOutOfWater = bSavedWantsToJumpOutOfWater;
		CharMov->bWantsToClimb = bSavedWantsToClimb;
		CharMov->bWantsToGlide = bSavedWantsToGlide;
	}
}

FAlsNetworkPredictionData_Extend::FAlsNetworkPredictionData_Extend(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
	
}

FSavedMovePtr FAlsNetworkPredictionData_Extend::AllocateNewMove()
{
	return MakeShared<FAlsSavedMove_Extend>();
}