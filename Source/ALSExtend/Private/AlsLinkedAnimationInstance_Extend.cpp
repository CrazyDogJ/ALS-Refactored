// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsLinkedAnimationInstance_Extend.h"

#include "Utility/AlsMacros.h"

UAlsLinkedAnimationInstance_Extend::UAlsLinkedAnimationInstance_Extend()
{
	bUseMainInstanceMontageEvaluationData = true;
}

void UAlsLinkedAnimationInstance_Extend::NativeInitializeAnimation()
{
	ParentExtend = Cast<UAlsAnimationInstance_Extend>(GetSkelMeshComponent()->GetAnimInstance());
	CharacterExtend = Cast<AAlsCharacter_Extend>(GetOwningActor());

#if WITH_EDITOR
	const auto* World{GetWorld()};

	if (IsValid(World) && !World->IsGameWorld())
	{
		// Use default objects for editor preview.

		if (!ParentExtend.IsValid())
		{
			ParentExtend = GetMutableDefault<UAlsAnimationInstance_Extend>();
		}

		if (!IsValid(CharacterExtend))
		{
			CharacterExtend = GetMutableDefault<AAlsCharacter_Extend>();
		}
	}
#endif

	Super::NativeInitializeAnimation();
}

void UAlsLinkedAnimationInstance_Extend::NativeBeginPlay()
{
	ALS_ENSURE_MESSAGE(Parent.IsValid(),
					   TEXT("%s (%s) should only be used as a linked animation instance within the %s animation blueprint!"),
					   ALS_GET_TYPE_STRING(UAlsLinkedAnimationInstance_Extend).GetData(), *GetClass()->GetName(),
					   ALS_GET_TYPE_STRING(UAlsAnimationInstance_Extend).GetData());

	Super::NativeBeginPlay();
}

UAlsAnimationInstance_Extend* UAlsLinkedAnimationInstance_Extend::GetParentExtend() const
{
	return ParentExtend.Get();
}
