#include "Nodes/AlsRigUnit_HasBone.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AlsRigUnit_HasBone)

FAlsRigUnit_HasBone_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (const auto OwnerSkeletalMesh = Cast<USkeletalMeshComponent>(ExecuteContext.GetOwningComponent()))
	{
		bHasBone = OwnerSkeletalMesh->DoesSocketExist(CheckBoneItem.Name);
	}
	else
	{
		bHasBone = false;
	}
}
