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
#include "SkeletalMeshComponent_Outline.h"
#include "AlsCharacter_Extend.generated.h"

USTRUCT()
struct FClimbDownParams
{
	GENERATED_BODY()

	UPROPERTY()
	UPrimitiveComponent* Component;
	FTransform Transform_A;
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
	// Look at
	UPROPERTY(BlueprintReadOnly, Category = "View")
	UPrimitiveComponent* LookTargetComponent;

	UPROPERTY(BlueprintReadOnly, Category = "View")
	FName LookTargetSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement)
	UAlsCharacterMovementComponent_Extend* MovementComponent_Extend;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement)
	USkeletalMeshComponent_Outline* Mesh_Outline;

	UPROPERTY(EditDefaultsOnly)
	float RagdollHitThreshold = 10.0f;
#pragma endregion

#pragma region Functions
protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& ViewInfo) override;
	virtual void ApplyDesiredStance() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanSprint() const override;
	virtual float GetDefaultHalfHeight() const override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void GetDefaultCapsule(float& OutCapsuleScaleZ, float& OutCapsuleHalfHeight, float& OutCapsuleRadius);
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	virtual FGameplayTag CalculateActualGait(const FGameplayTag& MaxAllowedGait) const override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	virtual void NotifyLocomotionActionChanged(const FGameplayTag& PreviousLocomotionAction) override;
	virtual bool IsRagdollingAllowedToStop() const override;
	virtual void PostInitializeComponents() override;
	virtual void RefreshRotationMode() override;
	virtual class UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;
	virtual void OnJumped_Implementation() override;
	virtual bool RefreshCustomInAirRotation(float DeltaTime) override;
	virtual void RefreshGait() override;
	virtual void OnGaitChanged_Implementation(const FGameplayTag& PreviousGait) override;
	void RefreshSwimmingRotation(float DeltaTime);
	void RefreshGlidingRotation(float DeltaTime);
public:
	AAlsCharacter_Extend(const FObjectInitializer& ObjectInitializer);

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
	void SetLookCompAndSocket(UPrimitiveComponent* InComp, const FName& SocketName);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	FVector GetCapsuleBottom();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Enter Slide")
	void K2_EnterSlide();

	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Exit Slide")
	void K2_ExitSlide();

	UFUNCTION(BlueprintNativeEvent)
	bool IsAllowSliding();
	
	UFUNCTION(BlueprintNativeEvent)
	bool IsAllowGliding();

	/**
		 * If not EAlsInAirRotationMode::RotateToVelocityOnJump, this bool will control air rotation toward velocity.
		 * @return Allow to rotate in air by velocity angle or not
		 */
	UFUNCTION(BlueprintNativeEvent, Category = "Als Character")
	bool IsAllowToRotateInAirByVelocity();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Enter Climb Dash")
	void K2_EnterClimbDash();

	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Auto Try Climb")
	void K2_AutoTryClimb();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Exit Climb Dash")
	void K2_ExitClimbDash();
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Exit Climb and Enter Walk")
	void K2_ClimbToWalk();
	
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
