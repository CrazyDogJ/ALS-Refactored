// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#include "SwingRopeActor.generated.h"

UCLASS(Blueprintable)
class ALSEXTEND_API ASwingRopeActor : public AActor
{
	GENERATED_BODY()

public:

	//functions
	ASwingRopeActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	void UpdateSpline();
	void SetIgnore();

	UFUNCTION()
	void OnRep_PlayersOnRope();
	
	//properties
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
};
