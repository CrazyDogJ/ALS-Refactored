// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AlsCharacterMovementComponent.h"
#include "AlsMovementSettings_Extend.h"
#include "SwingRopeActor.h"
#include "WaterInfoForSwim.h"
#include "WaterBodyComponent.h"
#include "AlsCharacterMovementComponent_Extend.generated.h"

class AAlsCharacter_Extend;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ALSEXTEND_API UAlsCharacterMovementComponent_Extend : public UAlsCharacterMovementComponent
{
	GENERATED_BODY()

	friend AAlsCharacter_Extend;

private:
	//adjust for swimming
	virtual void PhysicsVolumeChanged(class APhysicsVolume* NewVolume) override;
	virtual APhysicsVolume* GetPhysicsVolume() const override;
	virtual bool IsInWater() const override;
	virtual float ImmersionDepth() const override;
	virtual void PhysSwimming(float deltaTime, int32 Iterations) override;
	virtual void StartSwimming(FVector OldLocation, FVector OldVelocity, float timeTick, float remainingTime, int32 Iterations) override;
	
	void SwimAtSurface(float deltaTime);

	// ReSharper disable once CppHidingFunction
	float Swim(const FVector& Delta, FHitResult& Hit);
	// ReSharper disable once CppHidingFunction
	FVector FindWaterLine(const FVector& Start, const FVector& End) const;

	UFUNCTION(BlueprintPure, Category = "Character Movement: Swimming")
	FVector GetWaterSurface() const;

	UFUNCTION(BlueprintPure, Category = "Character Movement: Swimming")
	FWaterInfoForSwim GetWaterInfoForSwim() const;
	
public:
	// Override the properties
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State", Transient)
	TObjectPtr<UAlsMovementSettings_Extend> MovementSettings_Extend;
	
	// Sets default values for this component's properties
	UAlsCharacterMovementComponent_Extend();

	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation) override;
	
	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Walking")
	float DefaultStandHalfHeight = 75.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Walking")
	float DefaultStandRadius = 30.0f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Movement)
	bool bPhysicsVolumeAffectMovement = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float MaxSpeedSlopeBaseValue = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking", meta=(ClampMin=0, ClampMax=1, UIMin=0, UIMax=1))
	float SlopeUpwardAlpha = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking", meta=(ClampMin=0, ClampMax=1, UIMin=0, UIMax=1))
	float SlopeDownwardAlpha = 0.f; 
	
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	
#pragma region Swimming
	/**
	 * Water surface to capsule bottom distance.
	 * Will include wave if Include Wave is true.
	 */
	UFUNCTION(BlueprintPure, Category = "Character Movement: Swimming")
	float GetImmerseDepth() const;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Swimming")
	bool bIsSwimOnSurface = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Character Movement: Swimming")
	uint8 bWantsToJumpOutOfWater : 1;

	void GetWaterSplineKey(FVector Location, TMap<const UWaterBodyComponent*, float>& OutMap, TMap<const UWaterBodyComponent*, float>& OutSegmentMap) const;

	UFUNCTION(BlueprintCallable)
	TArray<UWaterBodyComponent*> GetWaterBodyComponents() const;

	UPROPERTY(Transient)
	bool bJumpInputUnderWater = false;
	
	bool bJumpingOutOfWater = false;
#pragma endregion 

#pragma region FreeClimb
private:
	FVector ClimbDashDirection;
	
	float CurrentClimbDashTime;

	//Enter climb time.
	float TryEnterClimbTime = 0.0f;

	bool bSwimToClimb = false;

	//爬下边缘检测结果
	TArray<FHitResult> CurrentDownHits;

	FHitResult CurrentDownHit;

	//传递给碰撞检测的所需的参数
	FCollisionQueryParams ClimbQueryParams;

	FVector CurrentClimbingNormal;
	FVector CurrentClimbingPosition;

	/**
	 * Set character attachment base component.Like floor or wall.
	 * @param BaseComponent Base component
	 */
	UFUNCTION(BlueprintCallable)
	void SetCharacterBase(UPrimitiveComponent* BaseComponent);

	void PhysClimbing(float deltaTime, int32 Iterations);
	
	FQuat GetClimbingRotation(float deltaTime) const;
	void SingleSweep(FHitResult& Hit, const FVector& Start, const FVector& End, const FCollisionShape Shape) const;
	void SweepAndStoreWallHits(TArray<FHitResult>& Results, FHitResult& Hits_Velocity, bool& bAllSweepCollided, const FVector& CompLocation, const
	                           FVector& CompForwardVector) const;
	bool EyeHeightTrace(const float TraceDistance, const FVector& CompLoc, const FVector& CompUp, const FVector& CompForward, const bool
	                    bUseDefaultCapsule) const;
	bool IsFacingSurface(const float Steepness, const FVector& CompLoc, const FVector& CompUp, const FVector& CompForward, const bool
	                     bUseDefaultCapsule) const;
	void ComputeSurfaceInfo(TArray<FHitResult>& WallHits, FVector& Position, FVector& Normal, const FVector& VelocityHitNormal, const FVector&
	                        Start) const;
	void ComputeClimbingVelocity(float deltaTime);
	void AlignClimbDashDirection();
	bool ShouldStopClimbing() const;
	bool HasReachedEdge() const;
	void StopClimbing(float deltaTime, int32 Iterations, bool bShouldMantle, bool bShouldClimbDownFloor);
	void MoveAlongClimbingSurface(float deltaTime);
	void SnapToClimbingSurface(float deltaTime) const;
	bool ClimbDownToFloor() const;
	void FindFloor_Custom(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const;
	void ComputeFloorDist_Custom(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult) const;
	void UpdateClimbDashState(float deltaTime);
	void StopClimbDashing();
	UAlsMovementSettings_Extend* GetMovementSettingsExtendSafe() const;
	
public:
	UPROPERTY(Category = "Character Movement: Climbing", EditAnywhere, BlueprintReadWrite)
	bool DebugDrawSwitch;

	UPROPERTY(Replicated,BlueprintReadOnly)
	bool bIsClimbDashing = false;
	
	//碰撞检测结果
	UPROPERTY(BlueprintReadOnly)
	TArray<FHitResult> CurrentWallHits;

	UPROPERTY(BlueprintReadOnly)
	FHitResult VelocityWallHit;
	
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Character Movement: Climbing")
	uint8 bWantsToClimb : 1;

	UFUNCTION(Category = "Character Movement: Climbing", BlueprintCallable)
	void EnterClimbing();
	
	UFUNCTION(Category = "Character Movement: Climbing", BlueprintCallable)
	void CancelClimbing();

	//Check climb down ledge
	void CheckClimbDownLedge(FVector& Forward, FVector& Down, FRotator& FaceTo, bool& bInCanClimbDown) const;
	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Climbing")
	bool bCanClimbDownLedge = false;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Climbing")
	FVector ClimbDownWarpingTarget_Forward;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Climbing")
	FVector ClimbDownWarpingTarget_Down;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Climbing")
	FRotator ClimbDownWarpingRotation_Down;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement: Climbing")
	TObjectPtr<UPrimitiveComponent> ClimbDownCachedComponent;

	//Swing start
	UPROPERTY(BlueprintReadOnly, Replicated)
	ASwingRopeActor* SwingActor;

	UPROPERTY(BlueprintReadOnly, Replicated)
	float OnRopeDistance = 0.0f;

	UPROPERTY(BlueprintReadWrite, Replicated, EditAnywhere)
	float MoveUpDownSpeed = 50.0f;
	
	void EnterSwing(ASwingRopeActor* RopeActor);

	void ExitSwing(bool bWantsToJump);
	
	void PhysSwing(float deltaTime, int32 Iterations);

	UFUNCTION(BlueprintPure)
	bool IsSwinging() const;

	UFUNCTION(NetMulticast, Reliable)
	void RemoveIgnoreSwingRope(AActor* RemoveActor);
	//Swing end
	UFUNCTION(BlueprintCallable)
	void CacheClimbDownInfo();

	UFUNCTION(BlueprintCallable)
	void ResetClimbDownInfo();
	
	void TryClimbDashing();
	void StoreClimbDashDirection();

	UFUNCTION(BlueprintPure)
	bool IsClimbDashing() const;

	UFUNCTION(BlueprintPure)
	FVector GetClimbDashDirection() const;
	
	UPROPERTY(Category="Character Movement: Climbing", BlueprintReadOnly)
	float TryEnterClimbAlpha = 0.0f;

	//bool bShouldMantleOnEndClimb = false;
	
	UFUNCTION(BlueprintPure, Category = "Character Movement: Climbing")
	bool IsClimbing() const;
	
	bool CanStartClimbing(float& HorizontalAccelerationDegrees, TArray<FHitResult>& InCurrentWallHits, FHitResult& InVelocityWallHit, const
	                      FVector& CompLoc, const FVector& CompForwardVec) const;

	UFUNCTION(BlueprintCallable, Category = "Character Movement: Climbing")
	bool CheckCanStartClimbing(const FVector CompLoc, const FVector CompForwardVec);

	void StartClimbingTimer(float HorizontalAccelerationDegrees, float DeltaSeconds, bool bCanStartClimbing);

	UFUNCTION(BlueprintPure, Category = "Character Movement: Climbing")
	bool IsClimbMoving() const;

	UFUNCTION(BlueprintPure, Category = "Character Movement: Climbing")
	FVector GetClimbSurfaceNormal() const {return CurrentClimbingNormal;};

	UFUNCTION(BlueprintPure, Category = "Character Movement: Climbing")
	FVector GetCurrentClimbPosition() const {return CurrentClimbingPosition;};
#pragma endregion FreeClimb
#pragma region NetDebug
	UFUNCTION(BlueprintPure, Category = Network)
		int GetNetInRateByte() const
	{
		if (GetWorld()->GetNetDriver())
		{
			return GetWorld()->GetNetDriver()->InBytes;
		}
		return 0;
	}
	UFUNCTION(BlueprintPure, Category = Network)
		int GetNetOutRateByte() const
	{
		if (GetWorld()->GetNetDriver())
		{
			return GetWorld()->GetNetDriver()->OutBytes;
		}
		return 0;
	}
	UFUNCTION(BlueprintPure, Category = Network)
		int GetNetInRatePackets() const
	{
		if (GetWorld()->GetNetDriver())
		{
			return GetWorld()->GetNetDriver()->InPackets;
		}
		return 0;
	}
	UFUNCTION(BlueprintPure, Category = Network)
		int GetNetOutRatePackets() const
	{
		if (GetWorld()->GetNetDriver())
		{
			return GetWorld()->GetNetDriver()->OutPackets;
		}
		return 0;
	}
	UFUNCTION(BlueprintPure, Category = Network)
		int GetNetInLossPackets() const
	{
		if (GetWorld()->GetNetDriver())
		{
			return GetWorld()->GetNetDriver()->InPacketsLost;
		}
		return 0;
	}
	UFUNCTION(BlueprintPure, Category = Network)
		int GetNetOutLossPackets() const
	{
		if (GetWorld()->GetNetDriver())
		{
			return GetWorld()->GetNetDriver()->OutPacketsLost;
		}
		return 0;
	}
#pragma endregion NetDebug
	
protected:
	virtual void BeginPlay() override;
	
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	virtual float GetMaxSpeed() const override;

	virtual float GetMaxAcceleration() const override;

	virtual float GetMaxBrakingDeceleration() const override;
	
	virtual bool IsMovingOnGround() const override;
	
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult) const override;

	virtual void PhysWalking(float deltaTime, int32 Iterations) override;
#pragma region Slide
	
public:
	bool CanSlide() const;
	
private:
	void EnterSlide();
	void ExitSlide();
	void PhysSliding(float deltaTime, int32 Iterations);
	UFUNCTION(BlueprintPure, Category = "Character Movement: Sliding")
	bool IsSliding() const;

#pragma endregion Slide

#pragma region Glide
public:
	UFUNCTION(BlueprintCallable, Category = "Character Movement: Gliding")
	void ToggleGlide();

	UFUNCTION(Server, Reliable)
	void ServerToggleGlide();

	void ToggleGlideImplementation();

	// Useful in Gameplay Ability
	UFUNCTION(BlueprintCallable)
	bool CheckCanGlide();
	
	UFUNCTION(BlueprintPure, Category = "Character Movement: Gliding")
	bool IsGliding() const;

	virtual bool IsFalling() const override;
private:
	void PhysGliding(float deltaTime, int32 Iterations);

	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
#pragma endregion 
};

class FAlsSavedMove_Extend :public FAlsSavedMove
{
private:
	typedef FAlsSavedMove Super;
	
public:
	enum CompressedFlags
	{
		FLAG_Climb = 0x20,
		FLAG_JumpOutWater = 0x40,
	};

	uint8 bSavedWantsToClimb : 1;
	uint8 bSavedWantsToJumpOutOfWater : 1;

	///@brief Resets all saved variables.
	virtual void Clear() override;
	
	///@brief Store input commands in the compressed flags.
	virtual uint8 GetCompressedFlags() const override;

	///@brief This is used to check whether two moves can be combined into one.
	///Basically you just check to make sure that the saved variables are the same.
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;

	///@brief Sets up the move before sending it to the server. 
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	///@brief Sets variables on character movement component before making a predictive correction.
	virtual void PrepMoveFor(class ACharacter* Character) override;
};

class FAlsNetworkPredictionData_Extend : public FAlsNetworkPredictionData
{
public:
	FAlsNetworkPredictionData_Extend(const UCharacterMovementComponent& ClientMovement);

	typedef FAlsNetworkPredictionData Super;

	///@brief Allocates a new copy of our custom saved move
	virtual FSavedMovePtr AllocateNewMove() override;
};