// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotifyState_AttachStaticMesh.h"

#include "Animation/AnimNotifyQueue.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyEndDataContext.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

// 修复：Branching Point 默认不传播放标识。保留蒙太奇实例号，使交错的结束事件只回收自己的挂件。
void UAnimNotifyState_AttachStaticMesh::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
	FAnimNotifyEventReference Ref(Payload.NotifyEvent, Payload.SequenceAsset);
	Ref.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	NotifyBegin(Payload.SkelMeshComponent, Payload.SequenceAsset, Payload.NotifyEvent ? Payload.NotifyEvent->GetDuration() : 0.0f, Ref);
}

void UAnimNotifyState_AttachStaticMesh::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
	FAnimNotifyEventReference Ref(Payload.NotifyEvent, Payload.SequenceAsset);
	Ref.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	if (Payload.bReachedEnd) Ref.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
	NotifyEnd(Payload.SkelMeshComponent, Payload.SequenceAsset, Ref);
}

void UAnimNotifyState_AttachStaticMesh::NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!IsValid(MeshComp) || !IsValid(MeshComp->GetOwner())) return;

	// 修复：回收漏收结束通知的失效记录，再为本次播放创建独立挂件。
	for (int32 Index = Attachments.Num() - 1; Index >= 0; --Index)
	{
		const FAttachment Entry = Attachments[Index];
		if (!Entry.Mesh.IsValid() || !Entry.Component.IsValid())
		{
			Attachments.RemoveAt(Index);
			if (auto* Orphan = Entry.Component.Get()) Orphan->DestroyComponent();
		}
	}
	auto* Component = Cast<UStaticMeshComponent>(MeshComp->GetOwner()->AddComponentByClass(
		UStaticMeshComponent::StaticClass(), false, RelativeTransform, false));
	if (!Component) return;
	if (!Component->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetIncludingScale, AttachedSocket))
	{
		Component->DestroyComponent();
		return;
	}
	Component->SetStaticMesh(StaticMeshAsset);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetRelativeTransform(RelativeTransform);
	StaticMeshComponent = Component;
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	const int32 NotifyId = EventReference.GetNotifyInstanceID();
	Attachments.Add(FAttachment{MeshComp, NotifyId, NotifyId == INDEX_NONE && Context ? Context->MontageInstanceID : INDEX_NONE, Component});
}

void UAnimNotifyState_AttachStaticMesh::NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	
	// 修复：只结束当前网格的这次播放；无实例 ID 的通知按同一网格的进入顺序匹配。
	const int32 Id = EventReference.GetNotifyInstanceID();
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	const int32 MontageId = Id == INDEX_NONE && Context ? Context->MontageInstanceID : INDEX_NONE;
	const int32 Index = Attachments.IndexOfByPredicate([MeshComp, Id, MontageId](const FAttachment& Entry)
	{
		return Entry.Mesh.Get() == MeshComp && Entry.NotifyInstanceId == Id && Entry.MontageInstanceId == MontageId;
	});
	if (Index != INDEX_NONE)
	{
		const auto Component = Attachments[Index].Component;
		Attachments.RemoveAt(Index);
		if (StaticMeshComponent == Component) StaticMeshComponent.Reset();
		if (auto* Attached = Component.Get()) Attached->DestroyComponent();
	}
}
