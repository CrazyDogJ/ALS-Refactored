#include "OutlineSkeletalMeshActor.h"

#include "SkeletalMeshComponent_Outline.h"

AOutlineSkeletalMeshActor::AOutlineSkeletalMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USkeletalMeshComponent_Outline>("SkeletalMeshComponent0"))
{
}
