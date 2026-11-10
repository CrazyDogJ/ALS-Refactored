// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#include "SwingRopeActor.generated.h"

class USplineMeshComponent;

UCLASS(Blueprintable)
class ALSEXTEND_API ASwingRopeActor : public AActor
{
	GENERATED_BODY()

public:

	//functions
	ASwingRopeActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;
	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaTime) override;
	void UpdateSpline();
	void SetIgnore();
	void ConstructRopeByComps();

	UFUNCTION()
	void OnRep_PlayersOnRope();
	
	//properties
	UPROPERTY()
	TArray<USplineMeshComponent*> SplineMeshComponents;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int SplineMeshAdditionalCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UStaticMesh* SplineMesh;
	
	UPROPERTY(BlueprintReadWrite)
	TArray<UCapsuleComponent*> CapsuleComponents;

	UPROPERTY(BlueprintReadWrite)
	TArray<UPhysicsConstraintComponent*> PCComponents;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	USplineComponent* SplineComponent;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 NumOfCapsules = 3;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float CapsuleHalfHeight = 12.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float CapsuleRadius = 4.0f;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PlayersOnRope)
	TArray<ACharacter*> PlayersOnRope;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FCollisionResponseContainer CapsuleCollisionResponse;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bDrawDebug = false;

	//chaos body instance
	UPROPERTY()
	USkeletalMeshComponent* SkeletalMeshComponent;
	
	TArray<FBodyInstance*> RopeBodies;
	TArray<FConstraintInstance*> RopeConstraints;

	void ConstructRopeByInstances();
	void UpdateSplineByInstances();

private:
	// 修复：刚体只弱引用 BodySetup，由绳索持有至物理实例释放。
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UBodySetup> RopeBodySetup;

	// 修复：区分运行时生成的组件与蓝图提供的外部组件。
	TArray<TWeakObjectPtr<UActorComponent>> GeneratedComponents;
	bool bRestoreRegisteredRope = false;
	void ReleaseRopePhysics();
};
