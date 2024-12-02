// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotifyState_AttachStaticMesh.h"

void UAnimNotifyState_AttachStaticMesh::NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp)
	{
		return;
	}
	if (!MeshComp->GetOwner())
	{
		return;
	}
	//if (StaticMeshComponent)
	//{
	//	StaticMeshComponent->DestroyComponent();
	//	StaticMeshComponent = nullptr;
	//}

	auto comp = MeshComp->GetOwner()->AddComponentByClass(UStaticMeshComponent::StaticClass(), false,
															  RelativeTransform, false);
	
	if (comp)
	{
		StaticMeshComponent = Cast<UStaticMeshComponent>(comp);
		StaticMeshComponent->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetIncludingScale, AttachedSocket);
		StaticMeshComponent->SetStaticMesh(StaticMeshAsset);
		StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StaticMeshComponent->SetRelativeTransform(RelativeTransform);
	}
}

void UAnimNotifyState_AttachStaticMesh::NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	
	if (StaticMeshComponent)
	{
		StaticMeshComponent->DestroyComponent();
		StaticMeshComponent = nullptr;
	}
}
