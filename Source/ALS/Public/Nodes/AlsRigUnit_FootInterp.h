#pragma once

#include "Units/RigUnit.h"
#include "Utility/AlsMath.h"
#include "AlsRigUnit_FootInterp.generated.h"

USTRUCT(DisplayName = "Foot Interp", Meta = (Category = "ALS", NodeColor = "0 0.36 1.0"))
struct ALS_API FAlsRigUnit_FootInterp : public FRigUnitMutable
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient, Meta = (Input))
	FVector Location{ForceInit};

	UPROPERTY(Transient, Meta = (Input))
	FQuat Rotation{ForceInit};
	
	UPROPERTY(Meta = (Input))
	bool bReset{false};

	UPROPERTY(Meta = (Input))
	bool bEnabled{true};

	UPROPERTY(Transient)
	bool bInitialized{false};

	UPROPERTY(Transient)
	float OffsetLocationZ{0.0f};
	
	UPROPERTY(Transient)
	FQuat OffsetRotation{ForceInit};
	
	UPROPERTY(Transient)
	FAlsSpringFloatState OffsetSpringState;
	
	UPROPERTY(Transient, Meta = (Input))
	float OffsetTargetLocationZ{0.0f};

	UPROPERTY(Transient, Meta = (Input))
	FQuat OffsetTargetRotation{ForceInit};

	UPROPERTY(Transient, Meta = (Output))
	FVector ResultLocation{ForceInit};

	UPROPERTY(Transient, Meta = (Output))
	FQuat ResultRotation{ForceInit};

public:
	virtual void Initialize() override;

	RIGVM_METHOD()
	// ReSharper disable once CppFunctionIsNotImplemented
	virtual void Execute() override;
};
