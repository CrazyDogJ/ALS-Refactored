// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "PhysicsEngine/BoxElem.h"
#include "Units/RigUnit.h"
#include "UObject/Object.h"
#include "AlsRigUnit_FootBoxTrace.generated.h"

USTRUCT(DisplayName = "Foot Box Trace", Meta = (Category = "ALS", NodeColor = "0 0.36 1.0"))
struct ALSEXTEND_API FAlsRigUnit_FootBoxTrace : public FRigUnitMutable
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient, Meta= (Input))
	FName CurrentFootName;

	UPROPERTY(Transient, Meta = (Input))
	FVector Location{ForceInit};

	UPROPERTY(Transient, Meta = (Input))
	FQuat Rotation{ForceInit};

	UPROPERTY(Meta = (Input))
	TEnumAsByte<ECollisionChannel> TraceChannel{ECC_Visibility};
	
	UPROPERTY(Meta = (Input, ClampMin = 0, ForceUnits = "cm"))
	float TraceDistanceUpward{50.0f};

	UPROPERTY(Meta = (Input, ClampMin = 0, ForceUnits = "cm"))
	float TraceDistanceDownward{45.0f};
	
	UPROPERTY(Transient)
	bool bInitialized{false};

	UPROPERTY(Transient)
	FKBoxElem FootBox;

	UPROPERTY(Meta = (Input))
	bool bEnabled{true};

	UPROPERTY(meta = (Input, DetailsOnly))
	bool bDrawDebug{false};
	
	UPROPERTY(Transient)
	bool bFootBoxValid{false};

	UPROPERTY(Transient, Meta = (Output))
	FHitResult FootBoxHitResult{ForceInit};
public:
	virtual void Initialize() override;
	RIGVM_METHOD()
	// ReSharper disable once CppFunctionIsNotImplemented
	virtual void Execute() override;
};
