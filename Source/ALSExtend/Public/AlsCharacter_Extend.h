// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AlsCharacter.h"
#include "AlsCameraComponent.h"
#include "AlsCharacterMovementComponent_Extend.h"
#include "GameplayEffectTypes.h"
#include "GenericTeamAgentInterface.h"
#include "MotionWarpingComponent.h"
#include "AlsCharacter_Extend.generated.h"

USTRUCT(BlueprintType)
struct FClimbDownParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	UPrimitiveComponent* Component;

	UPROPERTY(BlueprintReadOnly)
	FTransform Transform_A;

	UPROPERTY(BlueprintReadOnly)
	FTransform Transform_B;
};

class UAlsCameraComponent;

/**
 * 
 */
UCLASS(AutoExpandCategories = ("Settings|Als Character Extend", "State|Als Character Extend"))
class ALSEXTEND_API AAlsCharacter_Extend : public AAlsCharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

#pragma region Properties
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings|Als Character")
	TObjectPtr<UAlsMovementSettings_Extend> MovementSettings_Extend;
	
	UPROPERTY(BlueprintReadOnly, Category = "State|Als Character")
	TObjectPtr<UAlsMovementSettings> RuntimeMovementSettings;

	UPROPERTY(BlueprintReadOnly, Category = "State|Als Character")
	TObjectPtr<UAlsMovementSettings_Extend> RuntimeMovementSettings_Extend;
	
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = Movement)
	TObjectPtr<UAlsCameraComponent> Camera;
	
	UPROPERTY(EditDefaultsOnly, Category = "GameplayTags")
	FGameplayTagBlueprintPropertyMap GameplayTagBlueprintPropertyMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	UMotionWarpingComponent* MotionWarpingComponent;
	
	TWeakObjectPtr<class UAbilitySystemComponent> AbilitySystemComponent;
public:
	// Look at start
	UPROPERTY(BlueprintReadOnly, Category = "View")
	UPrimitiveComponent* LookTargetComponent;

	UPROPERTY(BlueprintReadOnly, Category = "View")
	FName LookTargetSocket;

	UPROPERTY(BlueprintReadOnly, Category = "View")
	FVector LookTargetOffset = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View")
	float LookTargetInterpSpeed = 10.0f;
	// Look at end

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement)
	UAlsCharacterMovementComponent_Extend* MovementComponent_Extend;

	UPROPERTY(EditDefaultsOnly)
	float RagdollHitThreshold = 10.0f;
#pragma endregion

#pragma region Functions
protected:
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& ViewInfo) override;
	virtual void ApplyDesiredStance() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanSprint() const override;
	virtual float GetDefaultHalfHeight() const override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	virtual FGameplayTag CalculateActualGait(const FGameplayTag& MaxAllowedGait) const override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	virtual void NotifyLocomotionActionChanged(const FGameplayTag& PreviousLocomotionAction) override;
	virtual bool IsRagdollingAllowedToStop() const override;
	virtual bool IsRollingAllowedToStart(const UAnimMontage* Montage) const override;
	virtual void PostInitializeComponents() override;
	virtual void RefreshRotationMode() override;
	virtual class UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;
	virtual void OnJumped_Implementation() override;
	virtual bool RefreshCustomInAirRotation(float DeltaTime) override;
	virtual void RefreshGait() override;
	virtual void OnGaitChanged_Implementation(const FGameplayTag& PreviousGait) override;
	virtual void RefreshVelocityYawAngle() override;
	void RefreshSwimmingRotation(float DeltaTime);
	void RefreshGlidingRotation(float DeltaTime);
public:
	explicit AAlsCharacter_Extend(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable)
	void FixTargetRotation();
	
	// Climb down ledge
	UFUNCTION(BlueprintCallable)
	void TryClimbDownLedge();

	UFUNCTION(Server, Reliable)
	void ServerClimbDownLedge(const FClimbDownParams& Params);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastClimbDownLedge(const FClimbDownParams& Params);

	UFUNCTION()
	void OnClimbDownMontageBlendOut(UAnimMontage* Montage, bool bInterrupted);
	
	void ClimbDownLedgeImplementation(const FClimbDownParams& Params);
	// Climb down ledge
	
	UFUNCTION(BlueprintCallable, Category = "View")
	void SetLookCompAndSocket(UPrimitiveComponent* InComp, const FName& SocketName, const FVector& TargetOffset);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	FVector GetCapsuleBottom();
	
	/**
	* If input is nullptr, set to default overlay class.
	*/
	UFUNCTION(BlueprintCallable)
	void SetCurrentOverlayClass(FName InTag = "Overlay", TSubclassOf<UAnimInstance> InAnimClass = nullptr);
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Enter Slide")
	void K2_EnterSlide();

	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Exit Slide")
	void K2_ExitSlide();

	UFUNCTION(BlueprintImplementableEvent)
	void OnPhysicalMaterialChanged(UPhysicalMaterial* PreviousPhysicalMaterial);

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<UPhysicalMaterial> CurrentPhysicalMaterial;

	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowRolling() const;
	
	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowSliding() const;
	
	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowGliding() const;

	virtual FGameplayTag CalculateMaxAllowedGait() const override;
	
	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowSprinting() const;

	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowRunning() const;
	
	/**
		 * If not EAlsInAirRotationMode::RotateToVelocityOnJump, this bool will control air rotation toward velocity.
		 * @return Allow to rotate in air by velocity angle or not
		 */
	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowToRotateInAirByVelocity() const;
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Enter Climb Dash")
	void K2_EnterClimbDash();

	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Auto Try Climb")
	void K2_AutoTryClimb();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Exit Climb Dash")
	void K2_ExitClimbDash();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Exit Climb and Enter Walk")
	void K2_ClimbToWalk();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastJumpOutOfWater();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Jump Out Of Water")
	void K2_JumpOutOfWater();
	
	UFUNCTION(Category = "Character Movement: Swimming", BlueprintCallable, DisplayName = "Swim Up or Fly Up")
	void SwimUp();

	UFUNCTION(BlueprintCallable)
	void SetViewState(FAlsViewState InViewState);
	
	UFUNCTION(Category = "Character Movement: Swimming", BlueprintCallable)
	void SwimUpStop();

	UFUNCTION(Category = "Character Movement: Swimming", BlueprintCallable, DisplayName = "Swim Down or Fly Down")
	void SwimDown();

	//Swing start;
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void TryStartSwing();

	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ExitSwing(bool bWantsToJump);

	UFUNCTION(BlueprintCallable)
	void SwingMoveUpDown(float UpDown);
	//Swing end;
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Player State Rep")
	void K2_OnPlayerStateRep();
	
	UFUNCTION(Category = "Character Movement: Swimming", BlueprintCallable)
	void AlsSetSkeletalMeshAsset(USkeletalMesh* SkeletalMeshAsset);

	UFUNCTION(Category = "Character Movement: Walking", BlueprintCallable, NetMulticast, Reliable)
	void TurnInPlaceImmediately();

	UFUNCTION(BlueprintCallable, Category = "Character Movement: Walking")
	void SetDefaultStandHalfHeight(float InValue);

	UFUNCTION(BlueprintCallable, Category = "Character Movement: Walking")
	void SetDefaultStandRadius(float InValue);

	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	
	UFUNCTION(BlueprintImplementableEvent)
	void OnRagdollDamaged(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	bool StartMantlingSwimming();

	bool StartMantlingFreeClimb();

	bool StartMantlingGliding();
	#pragma endregion 
};
