// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_AttachStaticMesh.generated.h"

/**
 * 
 */
UCLASS()
class ALSEXTEND_API UAnimNotifyState_AttachStaticMesh : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UStaticMesh* StaticMeshAsset;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FTransform RelativeTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FName AttachedSocket;
};
