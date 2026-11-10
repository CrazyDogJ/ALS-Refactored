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

	virtual void BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload) override;
	virtual void BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload) override;

	// 修复：保留对最近挂件的观察入口，动画资产不拥有场景组件。
	UPROPERTY(Transient)
	TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	// 修复：NotifyState 是动画资产共享对象，按播放实例记录弱引用，避免角色之间互删挂件。
private:
	struct FAttachment
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		int32 NotifyInstanceId;
		int32 MontageInstanceId;
		TWeakObjectPtr<UStaticMeshComponent> Component;
	};
	TArray<FAttachment> Attachments;

public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UStaticMesh* StaticMeshAsset;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FTransform RelativeTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FName AttachedSocket;
};
