// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsCharacter_Extend.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "AlsAnimationInstance.h"
#include "AlsAnimationInstance_Extend.h"
#include "AlsCharacterMovementComponent_Extend.h"
#include "SkeletalMeshComponent_Outline.h"
#include "CustomMovementMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Settings/AlsCharacterSettings.h"
#include "AlsGameplayTags_Extend.h"
#include "Utility/AlsVector.h"

void AAlsCharacter_Extend::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}

void AAlsCharacter_Extend::CalcCamera(float DeltaTime, FMinimalViewInfo& ViewInfo)
{
	if (Camera->IsActive())
	{
		Camera->GetViewInfo(ViewInfo);
		return;
	}
	
	Super::CalcCamera(DeltaTime, ViewInfo);
}

void AAlsCharacter_Extend::ApplyDesiredStance()
{
	if (!LocomotionAction.IsValid())
	{
		if (LocomotionMode == AlsLocomotionModeTags::Grounded)
		{
			if (DesiredStance == AlsStanceTags::Standing)
			{
				UnCrouch();
			}
			else if (DesiredStance == AlsStanceTags::Crouching)
			{
				Crouch();
				if (Gait == AlsGaitTags::Sprinting && IsAllowSliding())
				{
					SetGait(AlsGaitTags::Walking);
					MovementComponent_Extend->SetMovementMode(MOVE_Custom, CMOVE_Slide);
				}
			}
		}
		else if (LocomotionMode == AlsLocomotionModeTags::InAir)
		{
			UnCrouch();
		}
	}
	else if (LocomotionAction == AlsLocomotionActionTags::Rolling && Settings->Rolling.bCrouchOnStart)
	{
		Crouch();
	}
}

void AAlsCharacter_Extend::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Physical material support.
	if (CurrentPhysicalMaterial != GetCharacterMovement()->CurrentFloor.HitResult.PhysMaterial)
	{
		const auto PrePM = CurrentPhysicalMaterial;
		CurrentPhysicalMaterial = GetCharacterMovement()->CurrentFloor.HitResult.PhysMaterial;
		OnPhysicalMaterialChanged(PrePM.Get());
	}
	
	RefreshSwimmingRotation(DeltaSeconds);
	RefreshGlidingRotation(DeltaSeconds);

	if (MovementComponent_Extend->IsClimbing())
	{
		// Used to fix mantle check;
		LocomotionState.bHasVelocity = false;
		LocomotionState.bHasInput = false;
	}

	if (MovementComponent_Extend->IsGliding())
	{
		if (StartMantlingGliding())
		{
			MovementComponent_Extend->SetMovementMode(MOVE_Falling);
		}
	}
	
	// TODO:RopeSwing
	//if (LocomotionMode == AlsLocomotionModeTags::RopeSwing)
	//{
	//	RefreshGroundedNotMovingAimingRotation(DeltaSeconds);
	//}

	// Look at extend.
	if (LookTargetComponent)
	{
		if (auto AICon = Cast<AAIController>(GetController()))
		{
			AICon->SetFocalPoint(LookTargetComponent->GetSocketLocation(LookTargetSocket) + LookTargetOffset);
		}
		else if (GetController() && Camera)
		{
			auto InterpRotation = UKismetMathLibrary::RInterpTo(GetController()->GetControlRotation(),
				UKismetMathLibrary::FindLookAtRotation(Camera->GetThirdPersonTraceStartLocation(), LookTargetComponent->GetSocketLocation(LookTargetSocket) + LookTargetOffset),
				DeltaSeconds, LookTargetInterpSpeed);
			
			GetController()->SetControlRotation(InterpRotation);
		}
	}
}

bool AAlsCharacter_Extend::CanSprint() const
{
	if (GetMovementComponent()->IsSwimming())
	{
		return true;
	}
	return Super::CanSprint();
}

float AAlsCharacter_Extend::GetDefaultHalfHeight() const
{
	return MovementComponent_Extend->DefaultStandHalfHeight;
}

void AAlsCharacter_Extend::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	//ALS
	auto* PredictionData{GetCharacterMovement()->GetPredictionData_Client_Character()};

	if (PredictionData != nullptr && GetLocalRole() <= ROLE_SimulatedProxy &&
		ScaledHalfHeightAdjust > 0.0f && IsPlayingNetworkedRootMotionMontage())
	{
		// The code below essentially undoes the changes that will be made later at the end of the UCharacterMovementComponent::Crouch()
		// function because they literally break network smoothing when crouching while the root motion montage is playing, causing the
		// mesh to take an incorrect location for a while.

		// TODO Check the need for this fix in future engine versions.

		PredictionData->MeshTranslationOffset.Z += ScaledHalfHeightAdjust;
		PredictionData->OriginalMeshTranslationOffset = PredictionData->MeshTranslationOffset;
	}

	//Engine
	RecalculateBaseEyeHeight();
	
	if (GetMesh())
	{
		FVector& MeshRelativeLocation = GetMesh()->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = -MovementComponent_Extend->DefaultStandHalfHeight + HalfHeightAdjust;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = -MovementComponent_Extend->DefaultStandHalfHeight + HalfHeightAdjust;
	}

	K2_OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	//ALS
	SetStance(AlsStanceTags::Crouching);
}

void AAlsCharacter_Extend::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	//Als
	auto* PredictionData{GetCharacterMovement()->GetPredictionData_Client_Character()};

	if (PredictionData != nullptr && GetLocalRole() <= ROLE_SimulatedProxy &&
		ScaledHalfHeightAdjust > 0.0f && IsPlayingNetworkedRootMotionMontage())
	{
		// Same fix as in AAlsCharacter::OnStartCrouch().

		PredictionData->MeshTranslationOffset.Z -= ScaledHalfHeightAdjust;
		PredictionData->OriginalMeshTranslationOffset = PredictionData->MeshTranslationOffset;
	}

	//Engine
	RecalculateBaseEyeHeight();

	if (GetMesh())
	{
		FVector& MeshRelativeLocation = GetMesh()->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = -MovementComponent_Extend->DefaultStandHalfHeight;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = -MovementComponent_Extend->DefaultStandHalfHeight;
	}

	K2_OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	//ALS
	SetStance(AlsStanceTags::Standing);

	//My changed
	if (MovementComponent_Extend->IsSwimming())
	{
		GetCapsuleComponent()->SetCapsuleSize(RuntimeMovementSettings_Extend->SwimmingSettings.SwimCapsuleRadius, RuntimeMovementSettings_Extend->SwimmingSettings.SwimCapsuleHalfHeight);
	}
}

void AAlsCharacter_Extend::GetDefaultCapsule(float& OutCapsuleScaleZ, float& OutCapsuleHalfHeight,
	float& OutCapsuleRadius)
{
	auto Movement = Cast<UAlsCharacterMovementComponent_Extend>(GetCharacterMovement());
	OutCapsuleScaleZ = 1.0f;
	OutCapsuleRadius = Movement->DefaultStandRadius;
	OutCapsuleHalfHeight = Movement->DefaultStandHalfHeight;
}

void AAlsCharacter_Extend::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (APlayerState* PS = GetPlayerState<APlayerState>())
	{
		AbilitySystemComponent = Cast<UAbilitySystemComponent>(PS->GetComponentByClass(UAbilitySystemComponent::StaticClass()));
		if (AbilitySystemComponent.IsValid())
		{
			AbilitySystemComponent->InitAbilityActorInfo(PS, this);
			GameplayTagBlueprintPropertyMap.Initialize(this, AbilitySystemComponent.Get());
		}
	}

	// If we are controlled remotely, set animation timing to be driven by client's network updates. So timing and events remain in sync.
	if (GetMesh() && IsReplicatingMovement() && (GetRemoteRole() == ROLE_AutonomousProxy && GetNetConnection() != nullptr))
	{
		// Listen server fix
		if (HasAuthority() && GetNetMode() == NM_ListenServer)
		{
			GetMesh()->bOnlyAllowAutonomousTickPose = false;
		}
		else
		{
			GetMesh()->bOnlyAllowAutonomousTickPose = true;
		}
	}
}

void AAlsCharacter_Extend::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (APlayerState* PS = GetPlayerState<APlayerState>())
	{
		AbilitySystemComponent = Cast<UAbilitySystemComponent>(PS->GetComponentByClass(UAbilitySystemComponent::StaticClass()));
		if (AbilitySystemComponent.IsValid())
		{
			AbilitySystemComponent->InitAbilityActorInfo(PS, this);
			GameplayTagBlueprintPropertyMap.Initialize(this, AbilitySystemComponent.Get());
		}
	}
	
	K2_OnPlayerStateRep();
}

FGenericTeamId AAlsCharacter_Extend::GetGenericTeamId() const
{
	if (GetController())
	{
		if (const IGenericTeamAgentInterface* TeamAgent = Cast<const IGenericTeamAgentInterface>(GetController()))
		{
			return TeamAgent->GetGenericTeamId();
		}
	}
	
	return IGenericTeamAgentInterface::GetGenericTeamId();
}

ETeamAttitude::Type AAlsCharacter_Extend::GetTeamAttitudeTowards(const AActor& Other) const
{
	if (GetController())
	{
		if (const IGenericTeamAgentInterface* TeamAgent = Cast<const IGenericTeamAgentInterface>(GetController()))
		{
			return TeamAgent->GetTeamAttitudeTowards(Other);
		}
	}
	
	return IGenericTeamAgentInterface::GetTeamAttitudeTowards(Other);
}

FGameplayTag AAlsCharacter_Extend::CalculateActualGait(const FGameplayTag& MaxAllowedGait) const
{
	if (LocomotionMode == AlsLocomotionModeTags::Swimming)
	{
		if (LocomotionState.Speed < RuntimeMovementSettings_Extend->SwimmingSettings.RunSpeed + 10.0f)
		{
			return AlsGaitTags::Running;
		}
		if (MovementComponent_Extend->GetCurrentAcceleration().Length() > 0 && DesiredGait == AlsGaitTags::Sprinting)
		{
			return AlsGaitTags::Sprinting;
		}
	}
	
	if (LocomotionMode == AlsLocomotionModeTags::Flying)
	{
		if (LocomotionState.Speed < MovementComponent_Extend->MaxFlySpeed + 10.0f)
		{
			return AlsGaitTags::Running;
		}
		if (MovementComponent_Extend->GetCurrentAcceleration().Length() > 0 && LocomotionState.Speed <= GetMovementComponent()->GetMaxSpeed())
		{
			return AlsGaitTags::Sprinting;
		}
	}
	
	return Super::CalculateActualGait(MaxAllowedGait);
}

void AAlsCharacter_Extend::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (GetCharacterMovement()->MovementMode == MOVE_Falling)
	{
		LocomotionState.LastJumpYawAngle = UE_REAL_TO_FLOAT(UAlsVector::DirectionToAngleXY(LocomotionState.Velocity));
	}
	if (GetCharacterMovement()->MovementMode == MOVE_Custom && GetCharacterMovement()->CustomMovementMode == ECustomMovementMode::CMOVE_Slide)
	{
		SetLocomotionMode(AlsLocomotionModeTags::Sliding);
	}
	if (GetCharacterMovement()->MovementMode == MOVE_Custom && GetCharacterMovement()->CustomMovementMode == ECustomMovementMode::CMOVE_FreeClimb)
	{
		SetLocomotionMode(AlsLocomotionModeTags::FreeClimbing);
	}
	if (GetCharacterMovement()->MovementMode == MOVE_Custom && GetCharacterMovement()->CustomMovementMode == ECustomMovementMode::CMOVE_RopeSwing)
	{
		SetLocomotionMode(AlsLocomotionModeTags::RopeSwing);
	}
	if (GetCharacterMovement()->MovementMode == MOVE_Custom && GetCharacterMovement()->CustomMovementMode == ECustomMovementMode::CMOVE_Gliding)
	{
		SetLocomotionMode(AlsLocomotionModeTags::Gliding);
	}
	if (GetCharacterMovement()->MovementMode == MOVE_Flying)
	{
		SetLocomotionMode(AlsLocomotionModeTags::Flying);
	}
	if (GetCharacterMovement()->MovementMode == MOVE_Swimming)
	{
		SetLocomotionMode(AlsLocomotionModeTags::Swimming);
	}
}

void AAlsCharacter_Extend::NotifyLocomotionActionChanged(const FGameplayTag& PreviousLocomotionAction)
{
	Super::NotifyLocomotionActionChanged(PreviousLocomotionAction);

	if (LocomotionAction == AlsLocomotionActionTags::AttackCombo)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	}
	if (PreviousLocomotionAction == AlsLocomotionActionTags::AttackCombo)
	{
		FFindFloorResult Result;
		GetCharacterMovement()->FindFloor(GetCapsuleComponent()->GetComponentLocation(), Result, false);
		if (Result.IsWalkableFloor())
		{
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		else
		{
			GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		}
	}
}

bool AAlsCharacter_Extend::IsRagdollingAllowedToStop() const
{
	return LocomotionAction == AlsLocomotionActionTags::Ragdolling && (RagdollingState.bGrounded || MovementComponent_Extend->GetWaterInfoForSwim().WaterBodyIdx >= 0);
}

bool AAlsCharacter_Extend::IsRollingAllowedToStart(const UAnimMontage* Montage) const
{
	if (IsAllowRolling())
	{
		return Super::IsRollingAllowedToStart(Montage);
	}
	return false;
}

void AAlsCharacter_Extend::PostInitializeComponents()
{
	// Make sure the mesh and animation blueprint are ticking after the character so they can access the most up-to-date character state.

	GetMesh()->AddTickPrerequisiteActor(this);

	MovementComponent_Extend->OnPhysicsRotation.AddUObject(this, &ThisClass::CharacterMovement_OnPhysicsRotation);

	// Pass current movement settings to the movement component.
	RuntimeMovementSettings = DuplicateObject(MovementSettings, nullptr);
	MovementComponent_Extend->MovementSettings = RuntimeMovementSettings;

	RuntimeMovementSettings_Extend = DuplicateObject(MovementSettings_Extend, nullptr);
	MovementComponent_Extend->MovementSettings_Extend = RuntimeMovementSettings_Extend;

	AnimationInstance = Cast<UAlsAnimationInstance>(GetMesh()->GetAnimInstance());

	ACharacter::PostInitializeComponents();
}

void AAlsCharacter_Extend::RefreshRotationMode()
{
	if (LocomotionMode == AlsLocomotionModeTags::FreeClimbing)
	{
		SetRotationMode(AlsRotationModeTags::ViewDirection);
		return;
	}

	if (LocomotionMode == AlsLocomotionModeTags::Swimming ||
		LocomotionMode == AlsLocomotionModeTags::Flying ||
		LocomotionMode == AlsLocomotionModeTags::Sliding)
	{
		SetRotationMode(AlsRotationModeTags::VelocityDirection);
		return;
	}
	
	Super::RefreshRotationMode();
}

UAbilitySystemComponent* AAlsCharacter_Extend::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent.Get();
}

AAlsCharacter_Extend::AAlsCharacter_Extend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAlsCharacterMovementComponent_Extend>(ACharacter::CharacterMovementComponentName)
		.SetDefaultSubobjectClass<USkeletalMeshComponent_Outline>(MeshComponentName))
{
	Camera = CreateDefaultSubobject<UAlsCameraComponent>(FName{TEXTVIEW("Camera")});
	Camera->SetupAttachment(GetMesh());
	Camera->SetRelativeRotation_Direct({0.0f, 90.0f, 0.0f});
	MovementComponent_Extend = Cast<UAlsCharacterMovementComponent_Extend>(GetCharacterMovement());
	Mesh_Outline = Cast<USkeletalMeshComponent_Outline>(GetMesh());
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(FName{TEXTVIEW("MotionWarping")});
	GetCapsuleComponent()->SetNotifyRigidBodyCollision(true);
	GetCapsuleComponent()->OnComponentHit.AddDynamic(this, &AAlsCharacter_Extend::OnCapsuleHit);
	GetCapsuleComponent()->bReturnMaterialOnMove = true;
	
	GetMesh()->OnComponentHit.AddDynamic(this, &AAlsCharacter_Extend::OnMeshHit);
}

void AAlsCharacter_Extend::FixTargetRotation()
{
	LocomotionState.TargetYawAngle = GetActorRotation().Yaw;
	LocomotionState.VelocityYawAngle = GetActorRotation().Yaw;
	LocomotionState.SmoothTargetYawAngle = GetActorRotation().Yaw;
	LocomotionState.InputYawAngle = GetControlRotation().Yaw;
}

void AAlsCharacter_Extend::TryClimbDownLedge()
{
	if (GetLocalRole() <= ROLE_SimulatedProxy)
	{
		return;
	}

	FVector ForwardLoc;
	FVector DownLoc;
	FRotator ForwardRot;
	bool bCanClimbDown;
	MovementComponent_Extend->CheckClimbDownLedge(ForwardLoc, DownLoc, ForwardRot, bCanClimbDown);

	auto CurrentFloorComp = MovementComponent_Extend->CurrentFloor.HitResult.GetComponent();
	if (!bCanClimbDown)
	{
		return;
	}

	FTransform TransformA = UKismetMathLibrary::MakeRelativeTransform(
		FTransform(GetActorRotation(),
		ForwardLoc, FVector::OneVector),
		CurrentFloorComp->GetComponentTransform());

	FTransform TransformB = UKismetMathLibrary::MakeRelativeTransform(
		FTransform(ForwardRot,
		DownLoc, FVector::OneVector),
		CurrentFloorComp->GetComponentTransform());

	auto ClimbDownParams = FClimbDownParams(CurrentFloorComp, TransformA, TransformB);

	if (GetLocalRole() >= ROLE_Authority)
	{
		MulticastClimbDownLedge(ClimbDownParams);
	}
	else
	{
		GetCharacterMovement()->FlushServerMoves();

		ClimbDownLedgeImplementation(ClimbDownParams);
		ServerClimbDownLedge(ClimbDownParams);
	}
}

void AAlsCharacter_Extend::ServerClimbDownLedge_Implementation(const FClimbDownParams& Params)
{
	MulticastClimbDownLedge(Params);
	ForceNetUpdate();
}

void AAlsCharacter_Extend::MulticastClimbDownLedge_Implementation(const FClimbDownParams& Params)
{
	ClimbDownLedgeImplementation(Params);
}

void AAlsCharacter_Extend::OnClimbDownMontageBlendOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!RuntimeMovementSettings_Extend)
	{
		return;
	}
	
	if (Montage != RuntimeMovementSettings_Extend->ClimbingSettings.ClimbDownMontage)
	{
		return;
	}

	MovementComponent_Extend->StopMovementImmediately();
	MovementComponent_Extend->SetMovementMode(MOVE_Falling);
	SetLocomotionAction(FGameplayTag::EmptyTag);
	K2_AutoTryClimb();
}

void AAlsCharacter_Extend::ClimbDownLedgeImplementation(const FClimbDownParams& Params)
{
	UE_LOG(LogTemp, Warning, TEXT("Climb down params : Component : %s, Transform_A : %s, Transform_B : %s"), *Params.Component->GetName(), *Params.Transform_A.ToString(), *Params.Transform_B.ToString())

	if (!RuntimeMovementSettings_Extend)
	{
		return;
	}
	
	MotionWarpingComponent->AddOrUpdateWarpTargetFromComponent(RuntimeMovementSettings_Extend->ClimbingSettings.WarpTarget_A,
													   Params.Component, FName(), true,
													   Params.Transform_A.GetLocation(), Params.Transform_A.GetRotation().Rotator());

	MotionWarpingComponent->AddOrUpdateWarpTargetFromComponent(RuntimeMovementSettings_Extend->ClimbingSettings.WarpTarget_B,
													   Params.Component, FName(), true,
													   Params.Transform_B.GetLocation(), Params.Transform_B.GetRotation().Rotator());

	MovementComponent_Extend->SetMovementMode(MOVE_Flying);
	
	GetMesh()->GetAnimInstance()->Montage_Play(RuntimeMovementSettings_Extend->ClimbingSettings.ClimbDownMontage);
	auto Delegate = GetMesh()->GetAnimInstance()->Montage_GetBlendingOutDelegate(RuntimeMovementSettings_Extend->ClimbingSettings.ClimbDownMontage);
	Delegate->BindUFunction(this, "OnClimbDownMontageBlendOut");
	SetLocomotionAction(AlsLocomotionActionTags::ClimbDownLedge);
}

void AAlsCharacter_Extend::SetLookCompAndSocket(UPrimitiveComponent* InComp, const FName& SocketName, const FVector& TargetOffset)
{
	LookTargetComponent = InComp;
	LookTargetSocket = !InComp ? FName() : SocketName;
	LookTargetOffset = !InComp ? FVector::Zero() : TargetOffset;
}

FVector AAlsCharacter_Extend::GetCapsuleBottom()
{
	return GetActorLocation() - FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

void AAlsCharacter_Extend::SetCurrentOverlayClass(FName InTag, TSubclassOf<UAnimInstance> InAnimClass)
{
	if (InAnimClass != nullptr)
	{
		GetMesh()->LinkAnimGraphByTag(InTag, InAnimClass);
	}
	else if (auto AnimInstance_Extend = Cast<UAlsAnimationInstance_Extend>(GetMesh()->GetAnimInstance()))
	{
		GetMesh()->LinkAnimGraphByTag(InTag, AnimInstance_Extend->DefaultOverlayAnimBP);
	}
}

bool AAlsCharacter_Extend::IsAllowRolling_Implementation() const
{
	return true;
}

FGameplayTag AAlsCharacter_Extend::CalculateMaxAllowedGait() const
{
	if (DesiredGait == AlsGaitTags::Running)
	{
		return IsAllowRunning() ? DesiredGait : AlsGaitTags::Walking;
	}
	
	if (DesiredGait == AlsGaitTags::Sprinting)
	{
		return IsAllowSprinting() ? DesiredGait : AlsGaitTags::Running;
	}
	
	return AlsGaitTags::Walking;
}

bool AAlsCharacter_Extend::IsAllowRunning_Implementation() const
{
	return true;
}

bool AAlsCharacter_Extend::IsAllowSprinting_Implementation() const
{
	if (GetMovementComponent()->IsSwimming())
	{
		return true;
	}
	return Super::CanSprint();
}

bool AAlsCharacter_Extend::IsAllowToRotateInAirByVelocity_Implementation() const
{
	return false;
}

bool AAlsCharacter_Extend::IsAllowGliding_Implementation() const
{
	return true;
}

bool AAlsCharacter_Extend::IsAllowSliding_Implementation() const
{
	return true;
}

void AAlsCharacter_Extend::MulticastJumpOutOfWater_Implementation()
{
	K2_JumpOutOfWater();
}

void AAlsCharacter_Extend::SwimUp()
{
	if (MovementComponent_Extend->IsSwimming() || MovementComponent_Extend->IsFlying())
	{
		FVector Direction;
		Direction.Z = 1.f;
		AddMovementInput(Direction, 1.f);
		if (!MovementComponent_Extend->bIsSwimOnSurface)
		{
			MovementComponent_Extend->bJumpInputUnderWater = true;
		}
		if (!MovementComponent_Extend->bJumpInputUnderWater && MovementComponent_Extend->bIsSwimOnSurface)
		{
			MovementComponent_Extend->bWantsToJumpOutOfWater = true;
		}
	}
}

void AAlsCharacter_Extend::SwimUpStop()
{
	if (MovementComponent_Extend->IsSwimming())
	{
		MovementComponent_Extend->bJumpInputUnderWater = !MovementComponent_Extend->bIsSwimOnSurface;
	}
	else
	{
		MovementComponent_Extend->bJumpInputUnderWater = false;
	}
}

void AAlsCharacter_Extend::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	// Ignore movement input when climb down floor
	if (LocomotionAction == AlsLocomotionActionTags::ClimbDownFloor)
	{
		return;
	}
	
	if (MovementComponent_Extend->IsClimbing())
	{
		const FVector DirectionRight = FVector::CrossProduct(MovementComponent_Extend->GetClimbSurfaceNormal(), -GetActorRightVector()) * WorldDirection.Y;
		const FVector DirectionForward = FVector::CrossProduct(MovementComponent_Extend->GetClimbSurfaceNormal(), GetActorUpVector()) * WorldDirection.X;
		WorldDirection = DirectionRight + DirectionForward;
	}
	else
	{
		const auto DirZ = WorldDirection.Z;
		const auto InputXY = UAlsVector::ClampMagnitude012D(FVector2d(WorldDirection));
		const auto DirXY = UAlsVector::AngleToDirectionXY(ViewState.Rotation.Yaw);
		
		WorldDirection = (DirXY * InputXY.Y) + (UAlsVector::PerpendicularCounterClockwiseXY(DirXY) * InputXY.X);
		WorldDirection.Z = DirZ;
	}
	Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
}

void AAlsCharacter_Extend::RefreshSwimmingRotation(float DeltaTime)
{
	if (LocomotionMode != AlsLocomotionModeTags::Swimming &&
		LocomotionMode != AlsLocomotionModeTags::Flying)
	{
		return;
	}

	if (HasAnyRootMotion())
	{
		RefreshTargetYawAngleUsingActorRotation();
		return;
	}

	if (!LocomotionState.bMoving)
	{
		// Not moving.

		ApplyRotationYawSpeedAnimationCurve(DeltaTime);

		float TargetYawAngle;

		if (LocomotionState.bRotationTowardsLastInputDirectionBlocked)
		{
			// Rotate to the last target yaw angle, relative to the movement base or not.

			TargetYawAngle = LocomotionState.TargetYawAngle;

			if (MovementBase.bHasRelativeLocation && !MovementBase.bHasRelativeRotation &&
				Settings->bInheritMovementBaseRotationInVelocityDirectionRotationMode)
			{
				TargetYawAngle = UE_REAL_TO_FLOAT(TargetYawAngle + MovementBase.DeltaRotation.Yaw);
			}
		}
		else
		{
			// Rotate to the last velocity direction. Rotation of the movement
			// base handled in the AAlsCharacter::RefreshLocomotionEarly() function.

			TargetYawAngle = Settings->bRotateTowardsDesiredVelocityInVelocityDirectionRotationMode
								 ? DesiredVelocityYawAngle
								 : LocomotionState.VelocityYawAngle;
		}

		static constexpr auto RotationInterpolationSpeed{12.0f};
		static constexpr auto TargetYawAngleRotationSpeed{800.0f};

		SetRotationExtraSmooth(TargetYawAngle, DeltaTime, RotationInterpolationSpeed, TargetYawAngleRotationSpeed);
		return;
	}

	// Moving.

	if (LocomotionState.bHasInput || !LocomotionState.bRotationTowardsLastInputDirectionBlocked)
	{
		LocomotionState.bRotationTowardsLastInputDirectionBlocked = false;

		const auto TargetYawAngle{
			Settings->bRotateTowardsDesiredVelocityInVelocityDirectionRotationMode
				? DesiredVelocityYawAngle
				: LocomotionState.VelocityYawAngle
		};

		const auto RotationInterpolationSpeed{CalculateGroundedMovingRotationInterpolationSpeed()};

		static constexpr auto TargetYawAngleRotationSpeed{800.0f};

		SetRotationExtraSmooth(TargetYawAngle, DeltaTime, RotationInterpolationSpeed, TargetYawAngleRotationSpeed);
		return;
	}

	RefreshTargetYawAngleUsingActorRotation();
}

void AAlsCharacter_Extend::RefreshGlidingRotation(float DeltaTime)
{
	if (LocomotionMode != AlsLocomotionModeTags::Gliding)
	{
		return;
	}

	SetRotationSmooth(LocomotionState.VelocityYawAngle, DeltaTime, RuntimeMovementSettings_Extend->GlidingSettings.GlideRotationInterpSpeed);
}

void AAlsCharacter_Extend::SetViewState(FAlsViewState InViewState)
{
	if (ViewMode == AlsViewModeTags::FirstPerson)
	{
		return;
	}
	
	ViewState = InViewState;
}

void AAlsCharacter_Extend::SwimDown()
{
	if (MovementComponent_Extend->IsSwimming() || MovementComponent_Extend->IsFlying())
	{
		FVector Direction;
		Direction.Z = -1.f;
		AddMovementInput(Direction, 1.0f);
	}
}

void AAlsCharacter_Extend::TryStartSwing_Implementation()
{
	TArray<AActor*> Actors;
	GetCapsuleComponent()->GetOverlappingActors(Actors, ASwingRopeActor::StaticClass());
	float distance;
	if (auto findActor = Cast<ASwingRopeActor>(UGameplayStatics::FindNearestActor(GetActorLocation(), Actors, distance)))
	{
		MovementComponent_Extend->EnterSwing(findActor);
	}
}

void AAlsCharacter_Extend::ExitSwing_Implementation(bool bWantsToJump)
{
	MovementComponent_Extend->ExitSwing(bWantsToJump);
}

void AAlsCharacter_Extend::SwingMoveUpDown(float UpDown)
{
	if (MovementComponent_Extend->MovementMode == MOVE_Custom && MovementComponent_Extend->CustomMovementMode == CMOVE_RopeSwing)
	{
		AddMovementInput(FVector(0,0,UpDown), 1.0f);
	}
}

void AAlsCharacter_Extend::AlsSetSkeletalMeshAsset(USkeletalMesh* SkeletalMeshAsset)
{
	GetMesh()->SetSkeletalMeshAsset(SkeletalMeshAsset);
	AnimationInstance = Cast<UAlsAnimationInstance>(GetMesh()->GetAnimInstance());
}

void AAlsCharacter_Extend::SetDefaultStandHalfHeight(float InValue)
{
	auto Movement = Cast<UAlsCharacterMovementComponent_Extend>(GetCharacterMovement());
	if (Movement->DefaultStandHalfHeight == InValue && GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == InValue && GetMesh()->GetRelativeLocation().Z == -InValue)
	{
		return;
	}
	
	Movement->DefaultStandHalfHeight = InValue;
	
	if ((Movement->MovementMode == MOVE_Walking && !Movement->IsCrouching()) || IsRunningUserConstructionScript())
	{
		GetCapsuleComponent()->SetCapsuleHalfHeight(Movement->DefaultStandHalfHeight);
		GetMesh()->SetRelativeLocation(FVector(0,0,-Movement->DefaultStandHalfHeight));
		BaseTranslationOffset.Z = -Movement->DefaultStandHalfHeight;
	}
}

void AAlsCharacter_Extend::SetDefaultStandRadius(float InValue)
{
	auto Movement = Cast<UAlsCharacterMovementComponent_Extend>(GetCharacterMovement());
	if (Movement->DefaultStandRadius == InValue && GetCapsuleComponent()->GetUnscaledCapsuleRadius() == InValue)
	{
		return;
	}
	
	Movement->DefaultStandRadius = InValue;

	if (GetCharacterMovement()->MovementMode == MOVE_Walking || IsRunningUserConstructionScript())
	{
		GetCapsuleComponent()->SetCapsuleRadius(Movement->DefaultStandRadius);
	}
}

void AAlsCharacter_Extend::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                     UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	FVector PreCollisionVelocity = HitComponent->GetPhysicsLinearVelocity(Hit.MyBoneName);
	
	if (PreCollisionVelocity.Length() >= RagdollHitThreshold && OtherComponent != HitComponent)
	{
		OnRagdollDamaged(HitComponent, OtherActor, OtherComponent, NormalImpulse, Hit);
	}
}

void AAlsCharacter_Extend::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!GetCharacterMovement())
	{
		return;
	}
	
	if ((GetCharacterMovement()->IsFalling() || GetCharacterMovement()->IsSwimming()) && !GetCharacterMovement()->CurrentRootMotion.HasVelocity())
	{
		auto comp = MovementComponent_Extend;
		float AccelHorDegree;
		bool bCanStartClimbing = comp->CanStartClimbing(AccelHorDegree, comp->CurrentWallHits, comp->VelocityWallHit, comp->UpdatedComponent->GetComponentLocation(), comp->UpdatedComponent->GetForwardVector());
		if (AccelHorDegree <= RuntimeMovementSettings_Extend->ClimbingSettings.MinHorizontalDegreesToStartClimbing && bCanStartClimbing)
		{
			K2_AutoTryClimb();
			comp->TryEnterClimbTime = 0.0f;
			comp->TryEnterClimbAlpha = 0.0f;
		}
	}
}

bool AAlsCharacter_Extend::StartMantlingSwimming()
{
	return LocomotionMode == AlsLocomotionModeTags::Swimming && IsLocallyControlled() &&
		   StartMantling(Settings->Mantling.InAirTrace);
}

bool AAlsCharacter_Extend::StartMantlingFreeClimb()
{
	return LocomotionMode == AlsLocomotionModeTags::FreeClimbing && IsLocallyControlled() &&
		   StartMantling(Settings->Mantling.FreeClimbTrace); 
}

bool AAlsCharacter_Extend::StartMantlingGliding()
{
	return LocomotionMode == AlsLocomotionModeTags::Gliding && IsLocallyControlled() &&
		   StartMantling(Settings->Mantling.InAirTrace); 
}

void AAlsCharacter_Extend::TurnInPlaceImmediately_Implementation()
{
	Cast<UAlsAnimationInstance_Extend>(GetMesh()->GetAnimInstance())->TurnInPlaceImmediately();
}

void AAlsCharacter_Extend::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	LocomotionState.LastJumpYawAngle = UE_REAL_TO_FLOAT(UAlsVector::DirectionToAngleXY(LocomotionState.Velocity));
}

bool AAlsCharacter_Extend::RefreshCustomInAirRotation(float DeltaTime)
{
	static constexpr auto RotationInterpolationSpeed{5.0f};

	switch (Settings->InAirRotationMode)
	{
	case EAlsInAirRotationMode::RotateToVelocityOnJump:
		if (LocomotionState.bMoving)
		{
			if (IsAllowToRotateInAirByVelocity())
			{
				SetRotationSmooth(LocomotionState.VelocityYawAngle, DeltaTime, RotationInterpolationSpeed);
			}
			else
			{
				SetRotationSmooth(LocomotionState.LastJumpYawAngle, DeltaTime, RotationInterpolationSpeed);
			}
		}
		else
		{
			RefreshTargetYawAngleUsingActorRotation();
		}
		break;

	case EAlsInAirRotationMode::KeepRelativeRotation:
		if (IsAllowToRotateInAirByVelocity())
		{
			SetRotationSmooth(LocomotionState.VelocityYawAngle, DeltaTime, RotationInterpolationSpeed);
		}
		else
		{
			SetRotationSmooth(FRotator3f::NormalizeAxis(UE_REAL_TO_FLOAT(
							ViewState.Rotation.Yaw - LocomotionState.ViewRelativeTargetYawAngle)),
						DeltaTime, RotationInterpolationSpeed);
		}
		break;

	default:
		if (IsAllowToRotateInAirByVelocity())
		{
			SetRotationSmooth(LocomotionState.VelocityYawAngle, DeltaTime, RotationInterpolationSpeed);
		}
		else
		{
			RefreshTargetYawAngleUsingActorRotation();
		}
		break;
	}
	
	return true;
}

void AAlsCharacter_Extend::RefreshGait()
{
	if (LocomotionMode == AlsLocomotionModeTags::FreeClimbing)
	{
		SetGait(GetDesiredGait());
		return;
	}
	
	if (LocomotionMode != AlsLocomotionModeTags::Grounded &&
		LocomotionMode != AlsLocomotionModeTags::Flying &&
		LocomotionMode != AlsLocomotionModeTags::Swimming)
	{
		return;
	}
	
	const auto MaxAllowedGait{CalculateMaxAllowedGait()};

	// Update the character max walk speed to the configured speeds based on the currently max allowed gait.

	AlsCharacterMovement->SetMaxAllowedGait(MaxAllowedGait);

	const auto ActualGait{CalculateActualGait(MaxAllowedGait)};

	SetGait(ActualGait);
}

void AAlsCharacter_Extend::OnGaitChanged_Implementation(const FGameplayTag& PreviousGait)
{
	Super::OnGaitChanged_Implementation(PreviousGait);

	if (Gait == AlsGaitTags::Sprinting)
	{
		MovementComponent_Extend->TryClimbDashing();
	}
}

void AAlsCharacter_Extend::RefreshVelocityYawAngle()
{
	if (LocomotionMode == AlsLocomotionModeTags::FreeClimbing)
	{
		LocomotionState.VelocityYawAngle = UE_REAL_TO_FLOAT(GetActorRotation().Yaw);
	}
	else
	{
		Super::RefreshVelocityYawAngle();
	}
}
