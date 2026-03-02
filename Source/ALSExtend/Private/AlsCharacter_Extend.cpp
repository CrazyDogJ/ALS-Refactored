// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsCharacter_Extend.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "AlsAnimationInstance.h"
#include "AlsAnimationInstance_Extend.h"
#include "AlsCharacterMovementComponent_Extend.h"
#include "Utility/CustomMovementMode.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Settings/AlsCharacterSettings.h"
#include "Utility/AlsGameplayTags_Extend.h"
#include "Utility/AlsConstants.h"
#include "Utility/AlsDamageType.h"
#include "Utility/AlsVector.h"

void AAlsCharacter_Extend::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	// Init capsule size.
	InitCapsuleSize();
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

void AAlsCharacter_Extend::BeginPlay()
{
	Super::BeginPlay();
	
	// Runtime settings
	// RuntimeMovementSettings = DuplicateObject(MovementSettings, this);
	// MovementComponent_Extend->MovementSettings = RuntimeMovementSettings;

	RuntimeMovementSettings_Extend = DuplicateObject(MovementSettings_Extend, this);
	MovementComponent_Extend->MovementSettings_Extend = RuntimeMovementSettings_Extend;
}

void AAlsCharacter_Extend::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Runtime settings
	// RuntimeMovementSettings = nullptr;
	// MovementComponent_Extend->MovementSettings = nullptr;

	RuntimeMovementSettings_Extend = nullptr;
	MovementComponent_Extend->MovementSettings_Extend = nullptr;
	
	Super::EndPlay(EndPlayReason);
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

void AAlsCharacter_Extend::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	auto* PredictionData{GetCharacterMovement()->GetPredictionData_Client_Character()};

	if (PredictionData != nullptr && GetLocalRole() <= ROLE_SimulatedProxy &&
		ScaledHalfHeightAdjust > 0.0f && IsPlayingNetworkedRootMotionMontage())
	{
		// The code below essentially undoes the changes that will be made later at the end of the
		// UCharacterMovementComponent::Crouch() function because they literally break network smoothing when crouching
		// while the root motion montage is playing, causing the  mesh to take an incorrect location for a while.
		// TODO Wait for https://github.com/EpicGames/UnrealEngine/pull/10373 to be merged into the engine.

		PredictionData->MeshTranslationOffset.Z += ScaledHalfHeightAdjust;
		PredictionData->OriginalMeshTranslationOffset = PredictionData->MeshTranslationOffset;
	}

	RecalculateBaseEyeHeight();

	const ACharacter* DefaultChar = GetDefault<ACharacter>(GetClass());
	float DefaultHeight;
	float DefaultRadius;
	GetDefaultCapsule(DefaultHeight, DefaultRadius);
	if (GetMesh() && DefaultChar->GetMesh())
	{
		FVector& MeshRelativeLocation = GetMesh()->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = -DefaultHeight + HalfHeightAdjust;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->GetBaseTranslationOffset().Z + HalfHeightAdjust;
	}
	
	K2_OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	SetStance(AlsStanceTags::Crouching);
}

void AAlsCharacter_Extend::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	auto* PredictionData{GetCharacterMovement()->GetPredictionData_Client_Character()};

	if (PredictionData != nullptr && GetLocalRole() <= ROLE_SimulatedProxy &&
		ScaledHalfHeightAdjust > 0.0f && IsPlayingNetworkedRootMotionMontage())
	{
		// Same fix as in AAlsCharacter::OnStartCrouch().

		PredictionData->MeshTranslationOffset.Z -= ScaledHalfHeightAdjust;
		PredictionData->OriginalMeshTranslationOffset = PredictionData->MeshTranslationOffset;
	}

	RecalculateBaseEyeHeight();

	const ACharacter* DefaultChar = GetDefault<ACharacter>(GetClass());
	float DefaultHeight;
	float DefaultRadius;
	GetDefaultCapsule(DefaultHeight, DefaultRadius);
	if (GetMesh() && DefaultChar->GetMesh())
	{
		FVector& MeshRelativeLocation = GetMesh()->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = -DefaultHeight;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->GetBaseTranslationOffset().Z;
	}

	K2_OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	SetStance(AlsStanceTags::Standing);
}

void AAlsCharacter_Extend::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Ability system
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	InitGameplayTagInASC();
	
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

void AAlsCharacter_Extend::OnPlayerStateChanged(APlayerState* NewPlayerState, APlayerState* OldPlayerState)
{
	Super::OnPlayerStateChanged(NewPlayerState, OldPlayerState);
	K2_OnPlayerStateRep();
}

void AAlsCharacter_Extend::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
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
		if (LocomotionState.Speed < RuntimeMovementSettings_Extend->SwimmingSettings.RunSpeed + 10.0f
			|| MaxAllowedGait != AlsGaitTags::Sprinting)
		{
			return AlsGaitTags::Running;
		}
		
		return AlsGaitTags::Sprinting;
	}
	
	if (LocomotionMode == AlsLocomotionModeTags::Flying)
	{
		if (LocomotionState.Speed < MovementComponent_Extend->MaxFlySpeed + 10.0f
			|| MaxAllowedGait != AlsGaitTags::Sprinting)
		{
			return AlsGaitTags::Running;
		}
		
		return AlsGaitTags::Sprinting;
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

void AAlsCharacter_Extend::NotifyLocomotionModeChanged(const FGameplayTag& PreviousLocomotionMode)
{
	Super::NotifyLocomotionModeChanged(PreviousLocomotionMode);

	SetGameplayTagInASC(LocomotionMode);
	// Remove swimming state tag.
	if (LocomotionMode != AlsLocomotionModeTags::Swimming)
	{
		SetGameplayTagInASC(FGameplayTag::EmptyTag, AlsSwimmingStateTags::SwimmingStateParent);
	}
	else
	{
		// Auto stand up when swimming. Fixing some bug.
		if (DesiredStance == AlsStanceTags::Standing)
		{
			SetStance(AlsStanceTags::Standing);
		}
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

	SetGameplayTagInASC(LocomotionAction, AlsLocomotionActionTags::LocomotionActionParent);
}

void AAlsCharacter_Extend::NotifyRotationModeChanged(const FGameplayTag& PreviousRotationMode)
{
	Super::NotifyRotationModeChanged(PreviousRotationMode);

	SetGameplayTagInASC(RotationMode);
}

void AAlsCharacter_Extend::NotifyStanceChanged(const FGameplayTag& PreviousStance)
{
	Super::NotifyStanceChanged(PreviousStance);

	SetGameplayTagInASC(Stance);
}

void AAlsCharacter_Extend::NotifyGaitChanged(const FGameplayTag& PreviousGait)
{
	Super::NotifyGaitChanged(PreviousGait);

	SetGameplayTagInASC(Gait);
}

bool AAlsCharacter_Extend::IsRagdollingAllowedToStop() const
{
	if (LocomotionAction != AlsLocomotionActionTags::Ragdolling)
	{
		return false;
	}
	
	if (RagdollingState.bInWater)
	{
		return true;
	}
	
	return RagdollingState.bGrounded &&
		RagdollingState.Velocity.Length() <= (GetSettings() ? GetSettings()->Ragdolling.RagdollGetUpVelocityThreshold : 10.0f);
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
	
	ACharacter::PostInitializeComponents();
}

void AAlsCharacter_Extend::RefreshRotationMode()
{
	if (LocomotionMode == AlsLocomotionModeTags::FreeClimbing)
	{
		SetRotationMode(AlsRotationModeTags::ViewDirection);
		return;
	}

	if (LocomotionMode == AlsLocomotionModeTags::Flying ||
		LocomotionMode == AlsLocomotionModeTags::Sliding)
	{
		SetRotationMode(AlsRotationModeTags::VelocityDirection);
		return;
	}
	
	Super::RefreshRotationMode();
}

UAbilitySystemComponent* AAlsCharacter_Extend::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

AAlsCharacter_Extend::AAlsCharacter_Extend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAlsCharacterMovementComponent_Extend>(ACharacter::CharacterMovementComponentName))
{
	// Camera
	Camera = CreateDefaultSubobject<UAlsCameraComponent>(FName{TEXTVIEW("Camera")});
	Camera->SetupAttachment(GetMesh());
	Camera->SetRelativeRotation_Direct({0.0f, 90.0f, 0.0f});
	// Custom movement comp
	MovementComponent_Extend = Cast<UAlsCharacterMovementComponent_Extend>(GetCharacterMovement());
	// Motion warping for movement
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(FName{TEXTVIEW("MotionWarping")});
	// On hit on wall to auto climb.
	GetCapsuleComponent()->SetNotifyRigidBodyCollision(true);
	GetCapsuleComponent()->OnComponentHit.AddDynamic(this, &AAlsCharacter_Extend::OnCapsuleHit);
	// Custom behaviour when walking on specific floor.
	GetCapsuleComponent()->bReturnMaterialOnMove = true;
	// Ragdoll damage
	GetMesh()->OnComponentHit.AddDynamic(this, &AAlsCharacter_Extend::OnMeshHit);
	// Ability system component.
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
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

	auto CurrentFloorComp = BasedMovement.MovementBase;
	if (!bCanClimbDown)
	{
		return;
	}

	FTransform TransformA = UKismetMathLibrary::MakeRelativeTransform(
		FTransform(GetActorRotation(),
		ForwardLoc, FVector::OneVector),
		CurrentFloorComp->GetSocketTransform(BasedMovement.BoneName));

	FTransform TransformB = UKismetMathLibrary::MakeRelativeTransform(
		FTransform(ForwardRot,
		DownLoc, FVector::OneVector),
		CurrentFloorComp->GetSocketTransform(BasedMovement.BoneName));

	auto ClimbDownParams = FClimbDownParams(CurrentFloorComp, BasedMovement.BoneName, TransformA, TransformB);

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
													   Params.Component, Params.SocketName, true,
													   Params.Transform_A.GetLocation(), Params.Transform_A.GetRotation().Rotator());

	MotionWarpingComponent->AddOrUpdateWarpTargetFromComponent(RuntimeMovementSettings_Extend->ClimbingSettings.WarpTarget_B,
													   Params.Component, Params.SocketName, true,
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
	const double CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return GetActorLocation() + GetCapsuleComponent()->GetUpVector() * -CapsuleHalfHeight;
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

void AAlsCharacter_Extend::EnterSlide() const
{
	MovementComponent_Extend->SetMovementMode(MOVE_Custom, CMOVE_Slide);
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

UAnimMontage* AAlsCharacter_Extend::SelectClimbToWalkMontage_Implementation()
{
	return Settings->Ragdolling.GetUpFrontMontage;
}

void AAlsCharacter_Extend::ClimbToWalkGetUp_Implementation()
{
	if (GetMesh()->GetAnimInstance()->Montage_Play(SelectClimbToWalkMontage()) > 0.0f)
	{
		AlsCharacterMovement->SetInputBlocked(true);
		SetLocomotionAction(AlsLocomotionActionTags::GettingUp);
	}
}

void AAlsCharacter_Extend::NativeClimbToWalk()
{
	ClimbToWalkGetUp();
	K2_ClimbToWalk();
}

void AAlsCharacter_Extend::SwimUp()
{
	if (MovementComponent_Extend->IsSwimming() || MovementComponent_Extend->IsFlying())
	{
		const FVector Direction{ GetGravityDirection() * -1 };
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

		if (RotationMode == AlsRotationModeTags::Aiming || ViewMode == AlsViewModeTags::FirstPerson)
		{
			RefreshGroundedAimingRotation(DeltaTime);
			return;
		}
		
		if (RotationMode == AlsRotationModeTags::VelocityDirection)
		{
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

			static constexpr auto RotationInterpolationHalfLife{0.1f};
			static constexpr auto TargetYawAngleRotationSpeed{800.0f};

			SetRotationExtraSmooth(TargetYawAngle, DeltaTime, RotationInterpolationHalfLife, TargetYawAngleRotationSpeed);
			return;
		}

		if (RotationMode == AlsRotationModeTags::ViewDirection)
		{
			if ((!LocomotionState.bHasInput && LocomotionState.bRotationTowardsLastInputDirectionBlocked) ||
			    !Settings->bAutoRotateOnAnyInputWhileNotMovingInViewDirectionRotationMode)
			{
				RefreshTargetYawAngleUsingActorRotation();
				return;
			}

			// Rotate to the last view direction.

			const auto TargetYawAngle{
				LocomotionState.bHasInput ? UE_REAL_TO_FLOAT(ViewState.Rotation.Yaw) : LocomotionState.TargetYawAngle
			};

			const auto RotationInterpolationHalfLife{CalculateGroundedMovingRotationInterpolationHalfLife()};

			static constexpr auto TargetYawAngleRotationSpeed{500.0f};

			SetRotationExtraSmooth(TargetYawAngle, DeltaTime, RotationInterpolationHalfLife, TargetYawAngleRotationSpeed);
			return;
		}

		RefreshTargetYawAngleUsingActorRotation();
		return;
	}

	// Moving.

	if (RotationMode == AlsRotationModeTags::VelocityDirection &&
		(LocomotionState.bHasInput || !LocomotionState.bRotationTowardsLastInputDirectionBlocked))
	{
		LocomotionState.bRotationTowardsLastInputDirectionBlocked = false;

		const auto TargetYawAngle{
			Settings->bRotateTowardsDesiredVelocityInVelocityDirectionRotationMode
				? DesiredVelocityYawAngle
				: LocomotionState.VelocityYawAngle
		};

		const auto RotationInterpolationSpeed{CalculateGroundedMovingRotationInterpolationHalfLife()};

		static constexpr auto TargetYawAngleRotationSpeed{800.0f};

		SetRotationExtraSmooth(TargetYawAngle, DeltaTime, RotationInterpolationSpeed, TargetYawAngleRotationSpeed);
		return;
	}

	if (RotationMode == AlsRotationModeTags::ViewDirection &&
		(LocomotionState.bHasInput || !LocomotionState.bRotationTowardsLastInputDirectionBlocked))
	{
		LocomotionState.bRotationTowardsLastInputDirectionBlocked = false;

		float TargetYawAngle;

		if (Gait == AlsGaitTags::Sprinting)
		{
			TargetYawAngle = LocomotionState.VelocityYawAngle;
		}
		else
		{
			TargetYawAngle = UE_REAL_TO_FLOAT(
				ViewState.Rotation.Yaw + GetMesh()->GetAnimInstance()->GetCurveValue(UAlsConstants::RotationYawOffsetCurveName()));
		}

		const auto RotationInterpolationHalfLife{CalculateGroundedMovingRotationInterpolationHalfLife()};

		static constexpr auto TargetYawAngleRotationSpeed{500.0f};

		SetRotationExtraSmooth(TargetYawAngle, DeltaTime, RotationInterpolationHalfLife, TargetYawAngleRotationSpeed);
		return;
	}
	
	if (RotationMode == AlsRotationModeTags::Aiming)
	{
		RefreshGroundedAimingRotation(DeltaTime);
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

void AAlsCharacter_Extend::UpdateMeshRelativeLocation(float HalfHeight, bool bShouldMoveComp /** = false*/)
{
	//ALS Prediction Data Fix
	auto* PredictionData{GetCharacterMovement()->GetPredictionData_Client_Character()};
	if (PredictionData != nullptr && GetLocalRole() <= ROLE_SimulatedProxy && IsPlayingNetworkedRootMotionMontage())
	{
		PredictionData->MeshTranslationOffset.Z = HalfHeight * -1.0f;
		PredictionData->OriginalMeshTranslationOffset = PredictionData->MeshTranslationOffset;
	}

	FVector& MeshRelativeLocation = GetMesh()->GetRelativeLocation_DirectMutable();
	MeshRelativeLocation.Z = HalfHeight * -1.0f;
	
	//Engine
	RecalculateBaseEyeHeight();

	const auto OldTranslationZ = BaseTranslationOffset.Z;
	BaseTranslationOffset.Z = HalfHeight * -1.0f;

	if (bShouldMoveComp)
	{
		GetCapsuleComponent()->MoveComponent((BaseTranslationOffset.Z - OldTranslationZ) * GetGravityDirection(),
			GetCapsuleComponent()->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}
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
		const FVector Direction{ GetGravityDirection() };
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

void AAlsCharacter_Extend::AlsSetSkeletalMeshAsset(USkeletalMesh* SkeletalMeshAsset, const TSubclassOf<UAnimInstance> AnimInstanceClass)
{
	GetMesh()->SetSkeletalMeshAsset(SkeletalMeshAsset);
	GetMesh()->SetAnimInstanceClass(AnimInstanceClass);
	GetMesh()->SetAnimationMode(EAnimationMode::Type::AnimationBlueprint);
}

void AAlsCharacter_Extend::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                     UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// Only ragdoll will trigger damage. // Other actor is not self
	if (LocomotionAction != AlsLocomotionActionTags::Ragdolling || OtherActor == this)
	{
		return;
	}
	// Get settings
	const auto SettingsExtend = MovementSettings_Extend;
	if (!SettingsExtend)
	{
		return;
	}
	const auto DamageCooldown = SettingsExtend->LandDamageSettings.DamageCooldown;
	const auto RagdollSafeSpeedThreshold = SettingsExtend->LandDamageSettings.RagdollSafeSpeedThreshold;
	// 1.Time
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastRagdollDamageTime < DamageCooldown)
	{
		return;
	}
	// 2.Speed
	const auto RagdollVelocity = RagdollingState.Velocity;
	const auto RagdollSpeed = RagdollVelocity.Length();
	if (RagdollSpeed < RagdollSafeSpeedThreshold)
	{
		return;
	}

	// Used to play sound.
	OnRagdollHit(Hit, NormalImpulse, RagdollingState.Velocity);
	
	const auto HitNormal = NormalImpulse.GetSafeNormal();
	const auto VelocityDir = RagdollVelocity.GetSafeNormal();
	const auto ImpulseFactor = FMath::Abs(VelocityDir.Dot(HitNormal));
	const float Damage = (RagdollSpeed - RagdollSafeSpeedThreshold) * ImpulseFactor * RagdollDamageScale;

	if (Damage <= 0.0f)
	{
		return;
	}
	
	UGameplayStatics::ApplyDamage(this, FMath::Floor(Damage), GetController(), OtherActor, UAlsDamageType_Ragdoll::StaticClass());

	LastRagdollDamageTime = CurrentTime;
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
		bool bCanStartClimbing = comp->CanStartClimbing(AccelHorDegree, comp->CurrentWallHits, comp->VelocityWallHit,
			comp->UpdatedComponent->GetComponentLocation(), comp->UpdatedComponent->GetForwardVector());

		const float MinHorizontalDegreesToStartClimbing = RuntimeMovementSettings_Extend ?
			RuntimeMovementSettings_Extend->ClimbingSettings.MinHorizontalDegreesToStartClimbing : 50.0f;
		if (AccelHorDegree <= MinHorizontalDegreesToStartClimbing && bCanStartClimbing)
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

void AAlsCharacter_Extend::SetGameplayTagInASC(const FGameplayTag& AlsTag, const FGameplayTag& AlsParentTag)
{
	if (!GetAbilitySystemComponent()) return;
	
	const auto ParentTag = AlsParentTag.IsValid() ? AlsParentTag : AlsTag.RequestDirectParent();
	if (!ParentTag.IsValid()) return;
	
	const auto ParentTagChildren = UGameplayTagsManager::Get().RequestGameplayTagChildren(ParentTag);
	const auto FilteredChildren = ParentTagChildren.Filter(GetAbilitySystemComponent()->GetOwnedGameplayTags());
	if (!FilteredChildren.IsEmpty())
	{
		UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags(this, FilteredChildren, true);
	}
	UAbilitySystemBlueprintLibrary::AddLooseGameplayTags(this, FGameplayTagContainer(AlsTag), true);
}

void AAlsCharacter_Extend::InitGameplayTagInASC()
{
	SetGameplayTagInASC(ViewMode);
	SetGameplayTagInASC(LocomotionMode);
	SetGameplayTagInASC(RotationMode);
	SetGameplayTagInASC(Stance);
	SetGameplayTagInASC(Gait);
	SetGameplayTagInASC(LocomotionAction);
}

void AAlsCharacter_Extend::InitCapsuleSize()
{
	if (GetCapsuleSettings())
	{
		bool bValid;
		const auto Found = GetCapsuleSettings()->QueryCapsuleSizeByTag(AlsStanceTags::Standing, bValid);
		if (bValid)
		{
			GetCapsuleComponent()->SetCapsuleSize(Found.CapsuleRadius, Found.CapsuleHalfHeight);
			UpdateMeshRelativeLocation(Found.CapsuleHalfHeight);
		}

		const auto FoundCrouch = GetCapsuleSettings()->QueryCapsuleSizeByTag(AlsStanceTags::Crouching, bValid);
		if (bValid)
		{
			// Here the crouched capsule only worked on half height.
			MovementComponent_Extend->SetCrouchedHalfHeight(FoundCrouch.CapsuleHalfHeight);
		}
	}
}

void AAlsCharacter_Extend::SetCapsuleSizeSettings(UAlsCapsuleSizeSettings* InCapsuleSizeSettings)
{
	CapsuleSizeSettings = InCapsuleSizeSettings;
	// TODO : May cause issue.
	InitCapsuleSize();
}

float AAlsCharacter_Extend::GetScaledRadius(float UnscaledRadius) const
{
	const FVector& ComponentScale = GetCapsuleComponent()->GetComponentTransform().GetScale3D();
	return UnscaledRadius * UE_REAL_TO_FLOAT(ComponentScale.X < ComponentScale.Y ? ComponentScale.X : ComponentScale.Y);
}

float AAlsCharacter_Extend::GetScaledHaleHeight(float UnscaledHalfHeight) const
{
	return UnscaledHalfHeight * UE_REAL_TO_FLOAT(GetCapsuleComponent()->GetComponentTransform().GetScale3D().Z);
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
	// Custom gait refresh logic for free climbing (ASC usage)
	if (LocomotionMode == AlsLocomotionModeTags::FreeClimbing)
	{
		if (MovementComponent_Extend->IsClimbDashing())
		{
			SetGait(AlsGaitTags::Sprinting);
			return;
		}
		
		if (GetCharacterMovement()->GetCurrentAcceleration().Length() > 0.1f)
		{
			SetGait(AlsGaitTags::Running);
		}
		else
		{
			SetGait(AlsGaitTags::Walking);
		}
		return;
	}

	// Custom refresh logic for gliding (ASC usage)
	if (LocomotionMode == AlsLocomotionModeTags::Gliding)
	{
		if (GetCharacterMovement()->GetCurrentAcceleration().Length() > 0.1f)
		{
			SetGait(AlsGaitTags::Running);
		}
		else
		{
			SetGait(AlsGaitTags::Walking);
		}
	}

	// Normal gait updating.
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

void AAlsCharacter_Extend::RefreshLocomotion()
{
	if (LocomotionMode == AlsLocomotionModeTags::Swimming)
	{
		const auto bHadVelocity{LocomotionState.bHasVelocity};

		LocomotionState.Velocity = GetVelocity();
	
		// Determine if the character is moving by getting its speed. The speed equals the length
		// of the horizontal velocity, so it does not take vertical movement into account. If the
		// character is moving, update the last velocity rotation. This value is saved because it might
		// be useful to know the last orientation of a movement even after the character has stopped.
	
		LocomotionState.Speed = UE_REAL_TO_FLOAT(LocomotionState.Velocity.Size());
	
		static constexpr auto HasSpeedThreshold{1.0f};
	
		LocomotionState.bHasVelocity = LocomotionState.Speed >= HasSpeedThreshold;
	
		RefreshVelocityYawAngle();
	
		if (GetLocalRole() >= ROLE_AutonomousProxy)
		{
			auto bSendInitialVelocityYawAngle{LocomotionState.bHasVelocity && !bHadVelocity};
			auto VelocityYawAngleToSend{LocomotionState.VelocityYawAngle};
	
			if (Settings->bRotateTowardsDesiredVelocityInVelocityDirectionRotationMode)
			{
				FVector DesiredVelocity;
				if (AlsCharacterMovement->TryConsumePrePenetrationAdjustmentVelocity(DesiredVelocity) &&
				    DesiredVelocity.Size2D() >= HasSpeedThreshold)
				{
					bSendInitialVelocityYawAngle = !bHasDesiredVelocity;
					bHasDesiredVelocity = true;
	
					SetDesiredVelocityYawAngle(UE_REAL_TO_FLOAT(UAlsVector::DirectionToAngleXY(DesiredVelocity)));
				}
				else
				{
					bSendInitialVelocityYawAngle = LocomotionState.bHasVelocity && !bHasDesiredVelocity;
					bHasDesiredVelocity = LocomotionState.bHasVelocity;
	
					SetDesiredVelocityYawAngle(LocomotionState.VelocityYawAngle);
				}
	
				VelocityYawAngleToSend = DesiredVelocityYawAngle;
			}
	
			// Implicitly send the initial velocity yaw angle from the owning client to other clients,
			// since VelocityYawAngle changes are not always detected on the server for very short moves.
	
			if (bSendInitialVelocityYawAngle &&
			    (GetLocalRole() == ROLE_AutonomousProxy ||
			     GetRemoteRole() == ROLE_SimulatedProxy ||
			     (IsNetMode(NM_ListenServer) && IsLocallyControlled())))
			{
				ServerSetInitialVelocityYawAngle(VelocityYawAngleToSend);
			}
		}
	
		// Character is moving if has speed and current acceleration, or if the speed is greater than the moving speed threshold.
	
		LocomotionState.bMoving = (LocomotionState.bHasInput && LocomotionState.bHasVelocity) ||
		                          LocomotionState.Speed > Settings->MovingSpeedThreshold;
	}
	else
	{
		Super::RefreshLocomotion();
	}
}

void AAlsCharacter_Extend::RefreshVelocityYawAngle()
{
	if (GetCharacterMovement()->MovementMode == MOVE_None)
	{
		//Refresh velocity target yaw immediately
		LocomotionState.VelocityYawAngle = UE_REAL_TO_FLOAT(GetActorRotation().Yaw);
		LocomotionState.TargetYawAngle = UE_REAL_TO_FLOAT(GetActorRotation().Yaw);
		LocomotionState.SmoothTargetYawAngle = UE_REAL_TO_FLOAT(GetActorRotation().Yaw);
	}
	else if (LocomotionMode == AlsLocomotionModeTags::FreeClimbing)
	{
		LocomotionState.VelocityYawAngle = UE_REAL_TO_FLOAT(GetActorRotation().Yaw);
	}
	else if (LocomotionMode == AlsLocomotionModeTags::Swimming)
	{
		// No speed and z is not zero means vertical
		if (UE_REAL_TO_FLOAT(GetVelocity().Size2D()) < 1.0f && LocomotionState.bHasInput)
		{
			const auto ViewUpVector = UKismetMathLibrary::GetUpVector(ViewState.Rotation);
			FVector VelocityDir;
			float Length;
			LocomotionState.Velocity.ToDirectionAndLength(VelocityDir, Length);
			const auto ZViewUpVector = VelocityDir.Dot(GetGravityDirection()) > 0 ? ViewUpVector : -ViewUpVector;
			// Update velocity yaw angle by view up vector.
			LocomotionState.VelocityYawAngle = UE_REAL_TO_FLOAT(UAlsVector::DirectionToAngleXY(ZViewUpVector));
		}
		
		Super::RefreshVelocityYawAngle();
	}
	else
	{
		Super::RefreshVelocityYawAngle();
	}
}

/**
bool AAlsCharacter_Extend::IsMantlingFinalAllowedToStart_Implementation(const FAlsMantlingParameters& Parameters)
{
	if (!MovementSettings_Extend)
	{
		return Super::IsMantlingFinalAllowedToStart_Implementation(Parameters);
	}

	// Get overlapping water body actors;
	TArray<FOverlapResult> OverlapResults;
	bool bValid;
	const auto FoundSettings = GetCapsuleSettings()->QueryCapsuleSizeByTag(AlsLocomotionModeTags::Swimming, bValid);
	if (bValid)
	{
		const float UnscaledRadius = FoundSettings.CapsuleRadius;
		const float UnscaledHalfHeight = FoundSettings.CapsuleHalfHeight;
		const TArray<AActor*> IgnoreActors {this};
		const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes {UEngineTypes::ConvertToObjectType(ECC_WorldStatic)};
		TArray<AActor*> OutActors;
	
		UKismetSystemLibrary::SphereOverlapActors(
			GetWorld(),
			Parameters.TargetRelativeLocation + FVector(0.0f, 0.0f, GetScaledHaleHeight(UnscaledHalfHeight)),
			GetScaledRadius(UnscaledRadius),
			ObjectTypes,
			AWaterBody::StaticClass(),
			IgnoreActors,
			OutActors);
		
		return OutActors.Num() == 0;
	}

	return Super::IsMantlingFinalAllowedToStart_Implementation(Parameters);
}
*/

void AAlsCharacter_Extend::GetDefaultCapsule(float& OutCapsuleHalfHeight, float& OutCapsuleRadius)
{
	// Use settings first index for default capsule size.
	if (const auto CapsuleSettings = GetCapsuleSettings())
	{
		bool bValid;
		const auto FoundSettings = CapsuleSettings->QueryCapsuleSizeByTag(AlsStanceTags::Standing, bValid);
		if (bValid)
		{
			OutCapsuleHalfHeight = FoundSettings.CapsuleHalfHeight;
			OutCapsuleRadius = FoundSettings.CapsuleRadius;
			return;
		}
	}

	// Else we use default class property.
	Super::GetDefaultCapsule(OutCapsuleHalfHeight, OutCapsuleRadius);
}

void AAlsCharacter_Extend::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Get settings
	const auto SettingsExtend = MovementSettings_Extend;
	if (!SettingsExtend)
	{
		return;
	}
	const auto DamageVelocityThreshold = SettingsExtend->LandDamageSettings.LandDamageVelocityThreshold;
	const auto RawDamage = (GetVelocity().Length() - DamageVelocityThreshold) * LandDamageScale;
	const float Damage = FMath::Floor(FMath::Max(RawDamage, 0.0f));

	if (Damage <= 0.0f)
	{
		return;
	}
	// Apply land damage.
	UGameplayStatics::ApplyDamage(this, Damage, GetController(), Hit.GetActor(), UAlsDamageType_Fall::StaticClass());
}

void AAlsCharacter_Extend::NotifyViewModeChanged(const FGameplayTag& PreviousViewMode)
{
	Super::NotifyViewModeChanged(PreviousViewMode);

	SetGameplayTagInASC(ViewMode);
}
