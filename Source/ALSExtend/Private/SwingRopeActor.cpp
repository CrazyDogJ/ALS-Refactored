// Fill out your copyright notice in the Description page of Project Settings.


#include "SwingRopeActor.h"

#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

ASwingRopeActor::ASwingRopeActor()
{
	SplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(true);
	SetReplicatingMovement(true);
	
	CapsuleCollisionResponse.Visibility = ECR_Ignore;
	CapsuleCollisionResponse.Camera = ECR_Ignore;
	//Can climb;
	CapsuleCollisionResponse.GameTraceChannel1 = ECR_Ignore;
	CapsuleCollisionResponse.Pawn = ECR_Overlap;
}

void ASwingRopeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	CapsuleComponents.Empty();
	PCComponents.Empty();
	SplineComponent->ClearSplinePoints();

	for (int index = 0; index < NumOfCapsules; index = index + 1)
	{
		float Z = (CapsuleHalfHeight * 2.0f * index * -1.0f) - CapsuleHalfHeight;
		FTransform RelativeTransform = FTransform(FVector(0,0,Z));
		auto Component = Cast<UCapsuleComponent>(AddComponentByClass(UCapsuleComponent::StaticClass(), false, RelativeTransform, false));
		Component->SetCapsuleRadius(CapsuleRadius);
		Component->SetCapsuleHalfHeight(CapsuleHalfHeight);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_PhysicsBody);
		Component->SetCollisionResponseToChannels(CapsuleCollisionResponse);
		Component->ScaleByMomentOfInertia(FVector(1.5));
		Component->SetIsReplicated(true);
		Component->SetHiddenInGame(!bDrawDebug);
		Component->SetSimulatePhysics(true);
		CapsuleComponents.Add(Component);
	}
	
	for (int index = 0; index < CapsuleComponents.Num(); index = index + 1)
	{
		auto Component = CapsuleComponents[index];
		FVector Location = Component->GetComponentLocation() + (Component->GetUpVector() * CapsuleHalfHeight);
		SplineComponent->AddSplineWorldPoint(Location);

		auto PCC = Cast<UPhysicsConstraintComponent>(AddComponentByClass(UPhysicsConstraintComponent::StaticClass(), false, FTransform(), false));
		PCC->SetWorldLocation(Location);
		PCC->SetDisableCollision(true);
		if (index == 0)
		{
			PCC->SetConstrainedComponents(Component, FName(), nullptr, FName());
		}
		else
		{
			PCC->SetConstrainedComponents(Component, FName(), CapsuleComponents[index-1], FName());
		}

		PCComponents.Add(PCC);
	}
	auto LastComp = CapsuleComponents[CapsuleComponents.Num() - 1];
	FVector Location = LastComp->GetComponentLocation() - (LastComp->GetUpVector() * CapsuleHalfHeight);
	SplineComponent->AddSplineWorldPoint(Location);

	//set to linear;
	for (int32 index = 0; index < SplineComponent->GetNumberOfSplinePoints(); index = index + 1)
	{
		SplineComponent->GetSplinePointAt(index, ESplineCoordinateSpace::World).Type = ESplinePointType::Linear;
	}
}

void ASwingRopeActor::BeginPlay()
{
	Super::BeginPlay();
	
}

void ASwingRopeActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateSpline();
}

void ASwingRopeActor::UpdateSpline()
{
	for (int index = 0; index < CapsuleComponents.Num(); index = index + 1)
	{
		auto Component = CapsuleComponents[index];
		FVector Location = Component->GetComponentLocation() + (Component->GetUpVector() * CapsuleHalfHeight);
		SplineComponent->SetLocationAtSplinePoint(index, Location, ESplineCoordinateSpace::World);
	}
	
	auto LastComp = CapsuleComponents[CapsuleComponents.Num() - 1];
	FVector Location = LastComp->GetComponentLocation() - (LastComp->GetUpVector() * CapsuleHalfHeight);
	SplineComponent->SetLocationAtSplinePoint(CapsuleComponents.Num(), Location, ESplineCoordinateSpace::World);
}

void ASwingRopeActor::SetIgnore()
{
	for (auto comp : CapsuleComponents)
	{
		comp->ClearMoveIgnoreActors();
		for (auto player : PlayersOnRope)
		{
			player->MoveIgnoreActorAdd(this);
			comp->IgnoreActorWhenMoving(Cast<AActor>(player), true);
		}
	}
}

void ASwingRopeActor::OnRep_PlayersOnRope()
{
	SetIgnore();
}

void ASwingRopeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASwingRopeActor, PlayersOnRope)
}
