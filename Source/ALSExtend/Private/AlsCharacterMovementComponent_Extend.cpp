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

	//swim on surface(Water Plugin)
	if (bIsSwimOnSurface)
	{
		if (UpdatedComponent->GetComponentLocation().Z >= GetWaterSurface().Z - 5.0f)
		{
			Buoyancy = 1;
		}
		else
		{
			Buoyancy = 1 + ImmersionDepth();
		}
	}
	else
	{
		Buoyancy = 1;
	}
	
	float Depth = ImmersionDepth();
	float NetBuoyancy = Buoyancy * Depth;
	float OriginalAccelZ = Acceleration.Z;
	bool bLimitedUpAccel = false;
	float MaxSwimSpeedLocal = GetMaxSpeed();
	
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (Velocity.Z > 0.33f * MaxSwimSpeedLocal) && (NetBuoyancy != 0.f))
	{
		//damp positive Z out of water
		Velocity.Z = FMath::Max<FVector::FReal>(0.33f * MaxSwimSpeedLocal, Velocity.Z * Depth * Depth);
	}
	else if (Depth < 0.65f)
	{
		bLimitedUpAccel = (Acceleration.Z > 0.f);
		Acceleration.Z = FMath::Min<FVector::FReal>(0.1f, Acceleration.Z);
	}

	Iterations++;
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	bJustTeleported = false;
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		const float Friction = 0.5f * GetMovementSettingsExtendSafe()->SwimmingSettings.FluidFriction * Depth;
		CalcVelocity(deltaTime, Friction, true, GetMaxBrakingDeceleration());
		Velocity.Z += GetGravityZ() * deltaTime * (1.f - NetBuoyancy);
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
	const FVector Delta = GetWaterInfoForSwim().WaterVelocity * GetMovementSettingsExtendSafe()->SwimmingSettings.WaterVelocityForceMultiplier * deltaTime;
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

void UAlsCharacterMovementComponent_Extend::SwimAtSurface(float deltaTime)
{
	const auto Position = GetWaterSurface();
	const FVector Delta = deltaTime * GetMovementSettingsExtendSafe()->SwimmingSettings.SwimToSurfaceBuoyancyAdditive * (Position - FVector(0,0,GetMovementSettingsExtendSafe()->SwimmingSettings.SwimOnSurfaceAdditiveDepth) - UpdatedComponent->GetComponentLocation());
	Velocity.Z += FMath::Max(0, Delta.Z);
}

float UAlsCharacterMovementComponent_Extend::Swim(const FVector& Delta, FHitResult& Hit)
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	float airTime = 0.f;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (GetWaterInfoForSwim().WaterBodyIdx < 0) //Use Water Plugin,then left water
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

	if (GetWaterInfoForSwim().WaterBodyIdx >= 0)
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
	return GetMovementSettingsExtendSafe()->SwimmingSettings.bIncludeWave ? GetWaterInfoForSwim().WaterSurfacePosition : GetWaterInfoForSwim().WaterPlaneLocation;
}

UWaterBodyComponent* UAlsCharacterMovementComponent_Extend::GetCurrentWaterBodyComponent() const
{
	if (!CharacterOwner)
	{
		return nullptr;
	}
	if (!CharacterOwner->GetCapsuleComponent())
	{
		return nullptr;
	}
	
	TArray<AActor*> OverlappedActors;
	CharacterOwner->GetCapsuleComponent()->GetOverlappingActors(OverlappedActors);

	TArray<UWaterBodyComponent*> OverlappedWaterBodyComponents;
	for (const auto Actor : OverlappedActors)
	{
		if (const auto WaterBodyActor = Cast<AWaterBody>(Actor))
		{
			OverlappedWaterBodyComponents.Add(WaterBodyActor->GetWaterBodyComponent());
		}
	}

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

FWaterInfoForSwim UAlsCharacterMovementComponent_Extend::GetWaterInfoForSwim() const
{
	float OutWaterDepth = 0;
	FVector OutWaterPlaneLocation = FVector::ZeroVector;
	FVector OutWaterPlaneNormal = FVector::ZeroVector;
	FVector OutWaterSurfacePosition = FVector::ZeroVector;
	FVector OutWaterVelocity = FVector::ZeroVector;
	int32 OutWaterBodyIdx = -1;
	float OutWaterHeight = 0;
	auto CurrentWaterBody = GetCurrentWaterBodyComponent();
	
	if (CharacterOwner && CurrentWaterBody)
	{
		EWaterBodyQueryFlags QueryFlags =
			EWaterBodyQueryFlags::ComputeLocation
			| EWaterBodyQueryFlags::ComputeNormal
			| EWaterBodyQueryFlags::ComputeImmersionDepth
			| EWaterBodyQueryFlags::ComputeVelocity
			| EWaterBodyQueryFlags::IncludeWaves;

		FWaterBodyQueryResult QueryResult = CurrentWaterBody->QueryWaterInfoClosestToWorldLocation(UpdatedComponent->GetComponentLocation(), QueryFlags);
		check(!QueryResult.IsInExclusionVolume());
		OutWaterHeight = UpdatedComponent->GetComponentLocation().Z + QueryResult.GetImmersionDepth();
		if (EnumHasAnyFlags(QueryResult.GetQueryFlags(), EWaterBodyQueryFlags::ComputeDepth))
		{
			OutWaterDepth = QueryResult.GetWaterSurfaceDepth();
		}
		OutWaterPlaneLocation = QueryResult.GetWaterPlaneLocation();
		OutWaterPlaneNormal = QueryResult.GetWaterPlaneNormal();
		OutWaterSurfacePosition = QueryResult.GetWaterSurfaceLocation();
		OutWaterVelocity = QueryResult.GetVelocity();
		OutWaterBodyIdx = CurrentWaterBody->GetWaterBodyIndex();
	}

	return FWaterInfoForSwim(OutWaterDepth, OutWaterPlaneLocation, OutWaterPlaneNormal, OutWaterSurfacePosition, OutWaterVelocity, OutWaterBodyIdx, OutWaterHeight);
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
	bWantsToClimb = false,
	DebugDrawSwitch = false,
	
	CurrentClimbDashTime = 0,
	SwingActor = nullptr;

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
	GetWorld()->SweepSingleByChannel(BaseHit, CurrentClimbingPosition, CurrentClimbingPosition, FQuat::Identity, ECC_GameTraceChannel1, CollisionSphere, ClimbQueryParams);
	if (BaseHit.bBlockingHit)
	{
		SetBase(BaseHit.Component.Get(), BaseHit.BoneName);
	}

	const bool bClimbDownFloor = ClimbDownToFloor();
	if (ShouldStopClimbing() || bClimbDownFloor)
	{
		if (DebugDrawSwitch)
		{
			if (ShouldStopClimbing())
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Should Stop Climbing Function"));
			}
			else
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Climb Down to Floor"));
			}
		}
		StopClimbing(deltaTime, Iterations, bClimbDownFloor);
		return;
	}

	UpdateClimbDashState(deltaTime);
	
	ComputeClimbingVelocity(deltaTime);

	MoveAlongClimbingSurface(deltaTime);

	//Move on ledge
	const float UpSpeed = FVector::DotProduct(Velocity, UpdatedComponent->GetUpVector());
	const bool bIsMovingUp = UpSpeed >= 1.0f && Acceleration.Z > 1.0f;
	
	if (bIsMovingUp && HasReachedEdge() && !HasAnimRootMotion())
	{
		if (auto* Character = Cast<AAlsCharacter_Extend>(GetOwner()))
		{
			if (Character->StartMantlingFreeClimb())
			{
				if (DebugDrawSwitch) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Try mantle"));
				StopClimbing(deltaTime, Iterations, false);
			}
		}
	}
	
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	SnapToClimbingSurface(deltaTime);
}

bool UAlsCharacterMovementComponent_Extend::ShouldStopClimbing()
{
	if (GetWaterInfoForSwim().WaterBodyIdx >= 0 &&
		Velocity.Z < 0 &&
		!bSwimToClimb)
	{
		return true;
	}
	
	TArray<float> HitsVerticalDegrees;
	for (auto& Hit : CurrentWallHits)
	{
		const auto Dot = FVector::DotProduct(Hit.ImpactNormal, FVector(0,0,-1));
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
			SetInputBlocked(true);
			Cast<AAlsCharacter_Extend>(CharacterOwner)->K2_ClimbToWalk();
		}
		if (DebugDrawSwitch) DrawDebugLine(GetWorld(), CurrentClimbingPosition, CurrentClimbingPosition + CurrentClimbingNormal * 50, FColor::Red, true, 5);
		return true;
	}
	
	return false;
}

bool UAlsCharacterMovementComponent_Extend::HasReachedEdge() const
{
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float TraceDistance = Capsule->GetUnscaledCapsuleRadius() * 2.5f;

	return !EyeHeightTrace(TraceDistance, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetUpVector(), UpdatedComponent->GetForwardVector(), false);
}

void UAlsCharacterMovementComponent_Extend::StopClimbing(float deltaTime, int32 Iterations, bool bShouldClimbDownFloor)
{
	bWantsToClimb = false;
	
	if (bShouldClimbDownFloor)
	{
		SetMovementMode(MOVE_Walking);
		StartNewPhysics(deltaTime, Iterations);
		const auto Character = Cast<AAlsCharacter_Extend>(CharacterOwner);
		Character->GetMesh()->GetAnimInstance()->Montage_Play(GetMovementSettingsExtendSafe()->ClimbingSettings.ClimbDownFloorMontage);
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
	if (Velocity.Z < 0 && Acceleration.Z < 0)
	{
		FFindFloorResult FloorResult;
		// TODO : Fix climbing down floor teleport visual bug. maybe we can use root motion montage to lerp to the ground.
		const FVector FindFloorLocation = UpdatedComponent->GetComponentLocation();

		// First simple check
		FindFloor(FindFloorLocation, FloorResult, false);
		if (IsValidLandingSpot(FindFloorLocation, FloorResult.HitResult) && FloorResult.IsWalkableFloor())
		{
			// Enough space check
			float ScaledStandRadius;
			float ScaledStandHalfHeight;
			GetDefaultScaledCapsule(ScaledStandHalfHeight, ScaledStandRadius);
			
			FHitResult EnoughSpaceCheckHitResult;
			FVector End = FindFloorLocation - FVector(0,0,ScaledStandHalfHeight);
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(GetCharacterOwner());
			GetWorld()->SweepSingleByChannel(EnoughSpaceCheckHitResult, FindFloorLocation,
			                                 End, FQuat::Identity, ECC_Visibility,
			                                 FCollisionShape::MakeSphere(ScaledStandRadius), Params);

			if (IsWalkable(EnoughSpaceCheckHitResult))
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

	//0.Check stand still;
	if (!Acceleration.IsNearlyZero() || !Velocity.IsNearlyZero() || !IsMovingOnGround() || CharacterOwner->IsPlayingRootMotion())
	{
		return;
	}

	//1.Const Variables;
	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultScaledCapsule(DefaultHalfHeight, DefaultRadius);
	const FVector CompLoc = UpdatedComponent->GetComponentLocation();
	const FVector CompBottomLoc = CompLoc - (FVector::UpVector * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector CompForward = UpdatedComponent->GetForwardVector();

	//2.Check forward floor standable(and forward not wall);
	const FVector FindFloorLoc = CompLoc + CompForward * DefaultRadius * 2.5;
	FFindFloorResult FloorResult;
	FindFloor(FindFloorLoc, FloorResult, false);
	FHitResult ForwardWallResult;
	GetWorld()->SweepSingleByChannel(ForwardWallResult, CompLoc, FindFloorLoc, FQuat::Identity, ECC_Visibility,
									 FCollisionShape::MakeSphere(DefaultRadius), ClimbQueryParams);
	if (FloorResult.IsWalkableFloor() || ForwardWallResult.bBlockingHit)
	{
		return;
	}
	
	//3.Find ledge wall;
	FHitResult LineWallHit;
	const FVector LineWallStart = CompBottomLoc + (CompForward *
		DefaultRadius * 3) - FVector(0, 0, DefaultHalfHeight * 1.25f);
	const FVector LineWallEnd = CompBottomLoc - FVector(0, 0, DefaultHalfHeight * 1.25f) - (CompForward * DefaultRadius * 2);
	GetWorld()->LineTraceSingleByChannel(LineWallHit, LineWallStart, LineWallEnd, ECollisionChannel::ECC_GameTraceChannel1, ClimbQueryParams);
	if (DebugDrawSwitch)
	{
		DrawDebugLine(GetWorld(), LineWallStart, LineWallEnd, FColor::Blue, false, 0, 0, 1);
	}
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
	GetWorld()->LineTraceSingleByChannel(WaterCheckHit, CheckStartClimbLoc, CheckStartClimbLoc - FVector(0,0,1), ECC_WorldStatic, WaterCheckHitQueryParams);
	if (Cast<AWaterBody>(WaterCheckHit.GetActor()))
	{
		return;
	}
	//check start
	TArray<FHitResult> InClimbWallHits;
	FHitResult InVelocityWallHit;
	bool bAllCollided;
	SweepAndStoreWallHits(InClimbWallHits, InVelocityWallHit, bAllCollided, CheckStartClimbLoc, CheckStartClimbNormal);
	FVector Position;
	FVector Normal;
	ComputeSurfaceInfo(InClimbWallHits, Position, Normal, FVector::Zero(), CheckStartClimbLoc);
	const FVector HorizontalNormal = Normal.GetSafeNormal2D();

	const float HorizontalForwardDot = FVector::DotProduct(CheckStartClimbNormal, -HorizontalNormal);
	const float VerticalDot = FVector::DotProduct(Normal, HorizontalNormal);

	const float HorizontalForwardDegrees = FMath::RadiansToDegrees(FMath::Acos(HorizontalForwardDot));
	const float VerticalDegrees = FMath::RadiansToDegrees(FMath::Acos(VerticalDot));

	const bool bIsCeiling = FMath::IsNearlyZero(VerticalDot);

	if (DebugDrawSwitch)
	{
		DrawDebugLine(GetWorld(), Position, Position+(Normal*100), FColor::Red, false, 0, 0, 1);
	}
	const bool bCanClimb =
		IsFacingSurface(VerticalDot, CheckStartClimbLoc, FVector::UpVector, CheckStartClimbNormal, true) &&
		HorizontalForwardDegrees <= GetMovementSettingsExtendSafe()->ClimbingSettings.MinHorizontalDegreesToStartClimbing && VerticalDegrees <= 90 -
		GetWalkableFloorAngle() && !bIsCeiling;
	if (!bCanClimb)
	{
		return;
	}

	//5.Find edge point;
	FHitResult EdgeHit;
	const FVector EdgeFindStart = FVector(CheckStartClimbLoc.X, CheckStartClimbLoc.Y, CompBottomLoc.Z);
	const FVector EdgeFindEnd = EdgeFindStart + CheckStartClimbNormal * DefaultRadius * 2;
	GetWorld()->SweepSingleByChannel(EdgeHit, EdgeFindStart, EdgeFindEnd, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(),
	                                 FCollisionShape::MakeSphere(DefaultRadius), ClimbQueryParams);
	if (!EdgeHit.bBlockingHit)
	{
		return;
	}

	//6.Check enough space(Forward);
	const FVector CheckSpace_1 = EdgeHit.ImpactPoint + (DefaultRadius * 2) * LineWallHit.ImpactNormal.GetSafeNormal2D();
	FHitResult CheckSpaceHit;
	GetWorld()->SweepSingleByChannel(CheckSpaceHit, CheckSpace_1, CheckSpace_1 + FVector(0, 0, (DefaultHalfHeight - DefaultRadius) * 2),
	                                 FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(DefaultRadius), ClimbQueryParams);
	if (CheckSpaceHit.bBlockingHit)
	{
		return;
	}
	
	//7.Set params;
	Forward = CheckSpace_1;
	Down = CheckStartClimbLoc - FVector(0,0,DefaultHalfHeight);
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
	OnRopeDistance = FMath::Clamp(OnRopeDistance - FMath::Sign(Acceleration.Z) * MoveUpDownSpeed * deltaTime, 0.0f, MaxDistance);
	
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

void UAlsCharacterMovementComponent_Extend::CacheClimbDownInfo()
{
	bool bCanClimbDownNull;
	CheckClimbDownLedge(ClimbDownWarpingTarget_Forward, ClimbDownWarpingTarget_Down, ClimbDownWarpingRotation_Down, bCanClimbDownNull);
	ClimbDownCachedComponent = CurrentFloor.HitResult.GetComponent();
}

void UAlsCharacterMovementComponent_Extend::ResetClimbDownInfo()
{
	ClimbDownCachedComponent = nullptr;
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

	if (DebugDrawSwitch)
	{
		DrawDebugLine(GetWorld(), Position, Position+(Normal*100), FColor::Red, false, 0, 0, 1);
	}
		
	if (IsFacingSurface(VerticalDot, CompLoc, FVector::UpVector, CompForwardVec, true) && HorizontalForwardDegrees <=
		GetMovementSettingsExtendSafe()->ClimbingSettings.MinHorizontalDegreesToStartClimbing && VerticalDegrees <= 90 - GetWalkableFloorAngle() && !bIsCeiling && bAllCollided)
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
	GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_GameTraceChannel1, Shape, ClimbQueryParams);
	if (DebugDrawSwitch)
	{
		UAlsDebugUtility::DrawSweepSphere(GetWorld(), Start, End, Shape.GetSphereRadius(), FColor::Red);
	}
}

void UAlsCharacterMovementComponent_Extend::SweepAndStoreWallHits(TArray<FHitResult>& Results, FHitResult& Hits_Velocity, bool& bAllSweepCollided, const FVector& CompLocation, const FVector& CompForwardVector) const
{
	Results.Empty();
	
	float DefaultHalfHeight;
	float DefaultRadius;
	GetDefaultScaledCapsule(DefaultHalfHeight, DefaultRadius);
	const FCollisionShape CollisionShape = FCollisionShape::MakeSphere(DefaultRadius);

	const FVector ClampedAccelerationDir = UKismetMathLibrary::GetDirectionUnitVector(
		FVector::Zero(), FVector(Acceleration.X, Acceleration.Y, FMath::Max(Acceleration.Z, 0)));

	const FVector ClampedVelocityDir = UKismetMathLibrary::GetDirectionUnitVector(
		FVector::Zero(), FVector(Acceleration.X, Acceleration.Y, FMath::Max(Acceleration.Z, 0)));

	const FVector Dir = Velocity.IsNearlyZero() ? ClampedAccelerationDir : ClampedVelocityDir;
	
	const FVector Start_Forward = CompLocation;
	const FVector Start_Up = Start_Forward + (DefaultHalfHeight - DefaultRadius) * GetCharacterOwner()->GetActorUpVector();
	const FVector Start_Down = Start_Forward - (DefaultHalfHeight - DefaultRadius) * GetCharacterOwner()->GetActorUpVector();
	
	const FVector End_ForwardAmount = CompForwardVector * DefaultRadius * 2;
	
	FHitResult Hits_Up;
	FHitResult Hits_Forward;
	FHitResult Hits_Down;

	SingleSweep(Hits_Up, Start_Up, Start_Up + End_ForwardAmount, CollisionShape);
	SingleSweep(Hits_Forward, Start_Forward, Start_Forward + End_ForwardAmount, CollisionShape);
	SingleSweep(Hits_Down, Start_Down, Start_Down + End_ForwardAmount, CollisionShape);
	SingleSweep(Hits_Velocity, Start_Forward, Start_Forward + Dir * DefaultRadius * 2, CollisionShape);
	
	bAllSweepCollided = Hits_Up.bBlockingHit && Hits_Forward.bBlockingHit;
	
	Results.Add(Hits_Up);
	Results.Add(Hits_Forward);
	Results.Add(Hits_Down);
}

bool UAlsCharacterMovementComponent_Extend::EyeHeightTrace(const float TraceDistance, const FVector& CompLoc, const FVector& CompUp, const FVector& CompForward, const bool bUseDefaultCapsule) const
{
	FHitResult UpperEdgeHit;

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	const float Offset = (bUseDefaultCapsule ? DefaultCharacter->BaseEyeHeight : CharacterOwner->BaseEyeHeight) + GetMovementSettingsExtendSafe()->ClimbingSettings.EyeHeightOffset;
	const FVector Start = CompLoc + (CompUp * Offset);
	const FVector End = Start + (CompForward * TraceDistance);

	if (DebugDrawSwitch)
	{
		DrawDebugLine(GetWorld(), Start, End, FColor::Black, false, 0, 0, 1);
	}
	
	return GetWorld()->LineTraceSingleByChannel(UpperEdgeHit, Start, End, ECC_GameTraceChannel1, ClimbQueryParams);
}

bool UAlsCharacterMovementComponent_Extend::IsFacingSurface(const float Steepness, const FVector& CompLoc, const FVector& CompUp, const FVector& CompForward, const bool bUseDefaultCapsule) const
{
	constexpr float BaseLength = 80;
	const float SteepnessMultiplier = 1 + (1 - Steepness) * 5;

	return EyeHeightTrace(BaseLength * SteepnessMultiplier, CompLoc, CompUp, CompForward, bUseDefaultCapsule);
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
		GetWorld()->SweepSingleByChannel(AssistHit, Start, End, FQuat::Identity,
			ECC_GameTraceChannel1, CollisionSphere, ClimbQueryParams);
		
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
		
		auto AngleDegree = FVector::DotProduct(CurrentClimbingNormal, FVector(0, 0, 1));
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
}

void UAlsCharacterMovementComponent_Extend::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Reset jump out of water & Update Swimming
	if (Velocity.Z < 0.0f && bJumpingOutOfWater == true)
	{
		bJumpingOutOfWater = false;
	}
	if (IsSwimming() && GetWaterInfoForSwim().WaterBodyIdx >= 0)
	{
		bIsSwimOnSurface = GetImmerseDepth() <= GetMovementSettingsExtendSafe()->SwimmingSettings.SwimOnSurfaceDepth;
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
	
	// Update free climbing
	if (bWantsToClimb && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		SetMovementMode(MOVE_Custom, CMOVE_FreeClimb);
	}
	if (IsClimbing())
	{
		if (bSwimToClimb && (GetWaterInfoForSwim().WaterBodyIdx < 0 || Acceleration.Z < 0))
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
		if (EyeHeightTrace(DefaultStandRadius * 5, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetUpVector(), UpdatedComponent->GetForwardVector(), true))
		{
			float AccelHorDegree;
			bool bCanStartClimbing = CanStartClimbing(AccelHorDegree, CurrentWallHits, VelocityWallHit, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetForwardVector());
			StartClimbingTimer(AccelHorDegree,DeltaSeconds,bCanStartClimbing);
		}
	}

	// Update can climb down
	FVector ClimbDownNull_A;
	FRotator ClimbDownNull_B;
	CheckClimbDownLedge(ClimbDownNull_A, ClimbDownNull_A, ClimbDownNull_B, bCanClimbDownLedge);

	// Check landing when flying
	if (IsFlying() && Velocity.Z < 0 && GetMovementSettingsExtendSafe()->FlyingSettings.bShouldCheckLand && !HasAnimRootMotion())
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
	if (GetWaterInfoForSwim().WaterBodyIdx < 0)
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
	if (GetWaterInfoForSwim().WaterBodyIdx >= 0)
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
		SlopeForce.Z = 0.f;
		Velocity += SlopeForce * GetMovementSettingsExtendSafe()->SlidingSettings.SlideGravityForce * deltaTime;

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
				// see if it is OK to jump
				// @todo collision : only thing that can be problem is that old base has world collision on
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
				Hit.TraceEnd = Hit.TraceStart + FVector(0.f, 0.f, MAX_FLOOR_DIST);
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
				// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
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
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal2D(), FVector::UpVector).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, Hit);
}

bool UAlsCharacterMovementComponent_Extend::IsSliding() const
{
	return MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == ECustomMovementMode::CMOVE_Slide;
}
#pragma endregion Slide

void UAlsCharacterMovementComponent_Extend::ToggleGlide()
{
	if (CharacterOwner->GetLocalRole() >= ROLE_Authority)
	{
		ToggleGlideImplementation();
	}
	else
	{
		FlushServerMoves();

		ToggleGlideImplementation();
		ServerToggleGlide();
	}
}

void UAlsCharacterMovementComponent_Extend::ServerToggleGlide_Implementation()
{
	ToggleGlideImplementation();
}

void UAlsCharacterMovementComponent_Extend::ToggleGlideImplementation()
{
	if (Super::IsFalling())
	{
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(GetOwner());
		GetWorld()->LineTraceSingleByChannel(HitResult, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentLocation() - FVector(0,0, GetMovementSettingsExtendSafe()->GlidingSettings.CanStartGlideHeight), ECC_Pawn, Params);
		if (Cast<AAlsCharacter_Extend>(CharacterOwner)->IsAllowGliding() && !HitResult.IsValidBlockingHit())
		{
			//Check Water
			FHitResult HitResult_1;
			GetWorld()->LineTraceSingleByObjectType(HitResult_1, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentLocation() - FVector(0,0, GetMovementSettingsExtendSafe()->GlidingSettings.CanStartGlideHeight), ECC_WorldStatic, Params);
			if (!Cast<AWaterBody>(HitResult_1.GetActor()))
			{
				SetMovementMode(MOVE_Custom, CMOVE_Gliding);
			}
		}
	}
	else if (IsGliding())
	{
		SetMovementMode(MOVE_Falling);
	}
}

bool UAlsCharacterMovementComponent_Extend::CheckCanGlide()
{
	if (Super::IsFalling())
	{
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(GetOwner());
		GetWorld()->LineTraceSingleByChannel(HitResult, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentLocation() - FVector(0,0, GetMovementSettingsExtendSafe()->GlidingSettings.CanStartGlideHeight), ECC_Pawn, Params);
		if (Cast<AAlsCharacter_Extend>(CharacterOwner)->IsAllowGliding() && !HitResult.IsValidBlockingHit())
		{
			//Check Water
			FHitResult HitResult_1;
			GetWorld()->LineTraceSingleByObjectType(HitResult_1, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentLocation() - FVector(0,0, GetMovementSettingsExtendSafe()->GlidingSettings.CanStartGlideHeight), ECC_WorldStatic, Params);
			if (!Cast<AWaterBody>(HitResult_1.GetActor()))
			{
				return true;
			}
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

void UAlsCharacterMovementComponent_Extend::PhysGliding(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// GetFallingLateralAcceleration();
	const FVector GravityRelativeAcceleration = RotateWorldToGravity(Acceleration);
	FVector FallAcceleration = RotateGravityToWorld(FVector(GravityRelativeAcceleration.X, GravityRelativeAcceleration.Y, 0.f));

	// bound acceleration, falling object has minimal ability to impact acceleration
	if (!HasAnimRootMotion() && GravityRelativeAcceleration.SizeSquared2D() > 0.f)
	{
		FallAcceleration = GetAirControl(deltaTime, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingAirControl, FallAcceleration);
		FallAcceleration = FallAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
	}
	// GetFallingLateralAcceleration();
	
	const FVector GravityRelativeFallAcceleration = RotateWorldToGravity(FallAcceleration);
	FallAcceleration = RotateGravityToWorld(FVector(GravityRelativeFallAcceleration.X, GravityRelativeFallAcceleration.Y, 0));
	const bool bHasLimitedAirControl = ShouldLimitAirControl(deltaTime, FallAcceleration);

	float remainingTime = deltaTime;
	while( (remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) )
	{
		Iterations++;
		float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;
		
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
		bJustTeleported = false;

		const FVector OldVelocityWithRootMotion = Velocity;

		RestorePreAdditiveRootMotionVelocity();

		const FVector OldVelocity = Velocity;

		// Apply input
		const float MaxDecel = GetMaxBrakingDeceleration();
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			// Compute Velocity
			{
				// Acceleration = FallAcceleration for CalcVelocity(), but we restore it after using it.
				TGuardValue<FVector> RestoreAcceleration(Acceleration, FallAcceleration);
				if (HasCustomGravity())
				{
					Velocity = FVector::VectorPlaneProject(Velocity, RotateGravityToWorld(FVector::UpVector));
					const FVector GravityRelativeOffset = OldVelocity - Velocity;
					CalcVelocity(timeTick, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingLateralFriction, false, MaxDecel);
					Velocity += GravityRelativeOffset;
				}
				else
				{
					Velocity.Z = 0.f;
					CalcVelocity(timeTick, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingLateralFriction, false, MaxDecel);
					Velocity.Z = OldVelocity.Z;
				}
			}
		}

		// TODO: Glide changed here
		const float ProjectionLength = FVector::DotProduct(Velocity, GetGravityDirection()) / GetGravityDirection().Size();
		const float DeltaFromGlide = GetMovementSettingsExtendSafe()->GlidingSettings.MaxGlideDownSpeed - ProjectionLength;
		const FVector Gravity = GetGravityDirection() * DeltaFromGlide * GetMovementSettingsExtendSafe()->GlidingSettings.InterpToTargetGlideSpeed * timeTick;
		float GravityTime = timeTick;

		// Apply gravity
		Velocity = NewFallVelocity(Velocity, Gravity, GravityTime);

		//UE_LOG(LogCharacterMovement, Log, TEXT("dt=(%.6f) OldLocation=(%s) OldVelocity=(%s) OldVelocityWithRootMotion=(%s) NewVelocity=(%s)"), timeTick, *(UpdatedComponent->GetComponentLocation()).ToString(), *OldVelocity.ToString(), *OldVelocityWithRootMotion.ToString(), *Velocity.ToString());
		ApplyRootMotionToVelocity(timeTick);
		DecayFormerBaseVelocity(timeTick);

		// Compute change in position (using midpoint integration method).
		FVector Adjusted = 0.5f * (OldVelocityWithRootMotion + Velocity) * timeTick;

		// Move
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent( Adjusted, PawnRotation, true, Hit);
		
		if (!HasValidData())
		{
			return;
		}
		
		float LastMoveTimeSlice = timeTick;
		float subTimeTickRemaining = timeTick * (1.f - Hit.Time);
		
		if ( IsSwimming() ) //just entered water
		{
			remainingTime += subTimeTickRemaining;
			StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
			return;
		}
		else if ( Hit.bBlockingHit )
		{
			if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
			{
				remainingTime += subTimeTickRemaining;
				ProcessLanded(Hit, remainingTime, Iterations);
				return;
			}
			else
			{
				// Compute impact deflection based on final velocity, not integration step.
				// This allows us to compute a new velocity from the deflected vector, and ensures the full gravity effect is included in the slide result.
				Adjusted = Velocity * timeTick;

				// See if we can convert a normally invalid landing spot (based on the hit result) to a usable one.
				if (!Hit.bStartPenetrating && ShouldCheckForValidLandingSpot(timeTick, Adjusted, Hit))
				{
					const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
					FFindFloorResult FloorResult;
					FindFloor(PawnLocation, FloorResult, false);
					if (FloorResult.IsWalkableFloor() && IsValidLandingSpot(PawnLocation, FloorResult.HitResult))
					{
						remainingTime += subTimeTickRemaining;
						ProcessLanded(FloorResult.HitResult, remainingTime, Iterations);
						return;
					}
				}

				HandleImpact(Hit, LastMoveTimeSlice, Adjusted);
				
				// If we've changed physics mode, abort.
				if (!HasValidData() || !IsFalling())
				{
					return;
				}

				// Limit air control based on what we hit.
				// We moved to the impact point using air control, but may want to deflect from there based on a limited air control acceleration.
				FVector VelocityNoAirControl = OldVelocity;
				FVector AirControlAccel = Acceleration;
				if (bHasLimitedAirControl)
				{
					// Compute VelocityNoAirControl
					{
						// Find velocity *without* acceleration.
						TGuardValue<FVector> RestoreAcceleration(Acceleration, FVector::ZeroVector);
						TGuardValue<FVector> RestoreVelocity(Velocity, OldVelocity);
						if (HasCustomGravity())
						{
							Velocity = FVector::VectorPlaneProject(Velocity, RotateGravityToWorld(FVector::UpVector));
							const FVector GravityRelativeOffset = OldVelocity - Velocity;
							CalcVelocity(timeTick, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingLateralFriction, false, MaxDecel);
							VelocityNoAirControl = Velocity + GravityRelativeOffset;
						}
						else
						{
							Velocity.Z = 0.f;
							CalcVelocity(timeTick, GetMovementSettingsExtendSafe()->GlidingSettings.GlidingLateralFriction, false, MaxDecel);
							VelocityNoAirControl = FVector(Velocity.X, Velocity.Y, OldVelocity.Z);
						}
						
						VelocityNoAirControl = NewFallVelocity(VelocityNoAirControl, Gravity, GravityTime);
					}

					constexpr bool bCheckLandingSpot = false; // we already checked above.
					AirControlAccel = (Velocity - VelocityNoAirControl) / timeTick;
					const FVector AirControlDeltaV = LimitAirControl(LastMoveTimeSlice, AirControlAccel, Hit, bCheckLandingSpot) * LastMoveTimeSlice;
					Adjusted = (VelocityNoAirControl + AirControlDeltaV) * LastMoveTimeSlice;
				}

				const FVector OldHitNormal = Hit.Normal;
				const FVector OldHitImpactNormal = Hit.ImpactNormal;				
				FVector Delta = ComputeSlideVector(Adjusted, 1.f - Hit.Time, OldHitNormal, Hit);

				// Compute velocity after deflection (only gravity component for RootMotion)
				const UPrimitiveComponent* HitComponent = Hit.GetComponent();
				if (!Velocity.IsNearlyZero() && MovementBaseUtility::IsSimulatedBase(HitComponent))
				{
					const FVector ContactVelocity = MovementBaseUtility::GetMovementBaseVelocity(HitComponent, NAME_None) + MovementBaseUtility::GetMovementBaseTangentialVelocity(HitComponent, NAME_None, Hit.ImpactPoint);
					const FVector NewVelocity = Velocity - Hit.ImpactNormal * FVector::DotProduct(Velocity - ContactVelocity, Hit.ImpactNormal);
					Velocity = HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? FVector(Velocity.X, Velocity.Y, NewVelocity.Z) : NewVelocity;
				}
				else if (subTimeTickRemaining > UE_KINDA_SMALL_NUMBER && !bJustTeleported)
				{
					const FVector NewVelocity = (Delta / subTimeTickRemaining);
					Velocity = HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? FVector(Velocity.X, Velocity.Y, NewVelocity.Z) : NewVelocity;
				}

				if (subTimeTickRemaining > UE_KINDA_SMALL_NUMBER && (Delta | Adjusted) > 0.f)
				{
					// Move in deflected direction.
					SafeMoveUpdatedComponent( Delta, PawnRotation, true, Hit);
					
					if (Hit.bBlockingHit)
					{
						// hit second wall
						LastMoveTimeSlice = subTimeTickRemaining;
						subTimeTickRemaining = subTimeTickRemaining * (1.f - Hit.Time);

						if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
						{
							remainingTime += subTimeTickRemaining;
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}

						HandleImpact(Hit, LastMoveTimeSlice, Delta);

						// If we've changed physics mode, abort.
						if (!HasValidData() || !IsFalling())
						{
							return;
						}

						// Act as if there was no air control on the last move when computing new deflection.
						if (bHasLimitedAirControl && RotateWorldToGravity(Hit.Normal).Z > 0.001f)
						{
							const FVector LastMoveNoAirControl = VelocityNoAirControl * LastMoveTimeSlice;
							Delta = ComputeSlideVector(LastMoveNoAirControl, 1.f, OldHitNormal, Hit);
						}
						
						TwoWallAdjust(Delta, Hit, OldHitNormal);

						// Limit air control, but allow a slide along the second wall.
						if (bHasLimitedAirControl)
						{
							constexpr bool bCheckLandingSpot = false; // we already checked above.
							const FVector AirControlDeltaV = LimitAirControl(subTimeTickRemaining, AirControlAccel, Hit, bCheckLandingSpot) * subTimeTickRemaining;

							// Only allow if not back in to first wall
							if (FVector::DotProduct(AirControlDeltaV, OldHitNormal) > 0.f)
							{
								Delta += (AirControlDeltaV * subTimeTickRemaining);
							}
						}

						// Compute velocity after deflection (only gravity component for RootMotion)
						if (subTimeTickRemaining > UE_KINDA_SMALL_NUMBER && !bJustTeleported)
						{
							const FVector NewVelocity = (Delta / subTimeTickRemaining);
							Velocity = HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? FVector(Velocity.X, Velocity.Y, NewVelocity.Z) : NewVelocity;
						}

						// bDitch=true means that pawn is straddling two slopes, neither of which it can stand on
						bool bDitch = ( (RotateWorldToGravity(OldHitImpactNormal).Z > 0.f) && (RotateWorldToGravity(Hit.ImpactNormal).Z > 0.f) && (FMath::Abs(Delta.Z) <= UE_KINDA_SMALL_NUMBER) && ((Hit.ImpactNormal | OldHitImpactNormal) < 0.f) );
						SafeMoveUpdatedComponent( Delta, PawnRotation, true, Hit);
						if ( Hit.Time == 0.f )
						{
							// if we are stuck then try to side step
							FVector SideDelta = (OldHitNormal + Hit.ImpactNormal).GetSafeNormal2D();
							if ( SideDelta.IsNearlyZero() )
							{
								SideDelta = FVector(OldHitNormal.Y, -OldHitNormal.X, 0).GetSafeNormal();
							}
							SafeMoveUpdatedComponent( SideDelta, PawnRotation, true, Hit);
						}
							
						if ( bDitch || IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit) || Hit.Time == 0.f  )
						{
							remainingTime = 0.f;
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}
						else if (GetPerchRadiusThreshold() > 0.f && Hit.Time == 1.f && RotateWorldToGravity(OldHitImpactNormal).Z >= GetWalkableFloorZ())
						{
							// We might be in a virtual 'ditch' within our perch radius. This is rare.
							const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
							const float ZMovedDist = FMath::Abs(RotateWorldToGravity(PawnLocation - OldLocation).Z);
							const float MovedDist2DSq = FVector::VectorPlaneProject(PawnLocation - OldLocation, RotateGravityToWorld(FVector::UpVector)).Size2D();
							if (ZMovedDist <= 0.2f * timeTick && MovedDist2DSq <= 4.f * timeTick)
							{
								FVector GravityRelativeVelocity = RotateWorldToGravity(Velocity);
								GravityRelativeVelocity.X += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								GravityRelativeVelocity.Y += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								GravityRelativeVelocity.Z = FMath::Max<float>(JumpZVelocity * 0.25f, 1.f);
								Velocity = RotateGravityToWorld(GravityRelativeVelocity);
								Delta = Velocity * timeTick;
								SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
							}
						}
					}
				}
			}
		}
		
		FVector GravityRelativeVelocity = RotateWorldToGravity(Velocity);
		if (GravityRelativeVelocity.SizeSquared2D() <= UE_KINDA_SMALL_NUMBER * 10.f)
		{
			GravityRelativeVelocity.X = 0.f;
			GravityRelativeVelocity.Y = 0.f;
			Velocity = RotateGravityToWorld(GravityRelativeVelocity);
		}

		FFindFloorResult FindFloorResult;
		FVector FindLocation = UpdatedComponent->GetComponentLocation() - FVector(GetMovementSettingsExtendSafe()->GlidingSettings.GlideToFallCheckHeight);
		FindFloor(FindLocation, FindFloorResult,false);
		if (IsValidLandingSpot(FindLocation, FindFloorResult.HitResult))
		{
			SetMovementMode(MOVE_Falling);
		}
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
	return Result;
}

bool FAlsSavedMove_Extend::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (bSavedWantsToJumpOutOfWater != ((FAlsSavedMove_Extend*)&NewMove)->bSavedWantsToJumpOutOfWater)
	{
		return false;
	}

	if (bSavedWantsToClimb != ((FAlsSavedMove_Extend*)&NewMove)->bSavedWantsToClimb)
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
	}
}

void FAlsSavedMove_Extend::PrepMoveFor(ACharacter* Character)
{
	FAlsSavedMove::PrepMoveFor(Character);

	if (UAlsCharacterMovementComponent_Extend* CharMov = Cast<UAlsCharacterMovementComponent_Extend>(Character->GetCharacterMovement()))
	{
		CharMov->bWantsToJumpOutOfWater = bSavedWantsToJumpOutOfWater;
		CharMov->bWantsToClimb = bSavedWantsToClimb;
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