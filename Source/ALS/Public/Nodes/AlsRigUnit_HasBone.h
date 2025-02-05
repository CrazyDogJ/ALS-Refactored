#pragma once

#include "Units/RigUnit.h"
#include "AlsRigUnit_HasBone.generated.h"

USTRUCT(DisplayName = "Has Bone", Meta = (Category = "ALS", NodeColor = "0.05 0.25 0.05"))
struct ALS_API FAlsRigUnit_HasBone : public FRigUnit
{
	GENERATED_BODY()

public:
	UPROPERTY(Meta = (Input, ExpandByDefault))
	FRigElementKey CheckBoneItem;

	UPROPERTY(Transient, Meta = (Output))
	bool bHasBone{false};

public:
	RIGVM_METHOD()
	virtual void Execute() override;
};
