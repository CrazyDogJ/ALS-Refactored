// Fill out your copyright notice in the Description page of Project Settings.


#include "SwingRopeActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "UObject/Package.h"

ASwingRopeActor::ASwingRopeActor()
{
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
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

	ConstructRopeByInstances();
	UpdateSplineByInstances();
}

void ASwingRopeActor::BeginPlay()
{
	Super::BeginPlay();

	ConstructRopeByInstances();
}

void ASwingRopeActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateSplineByInstances();
	
	//UpdateSpline();
	//
	//const int SplineMeshCount = NumOfCapsules + SplineMeshAdditionalCount;
	//const int PerLength = SplineComponent->GetSplineLength() / SplineMeshCount;
	//
	//for (int index = 0; index < SplineMeshComponents.Num(); index = index + 1)
	//{
	//	const FVector StartPos = SplineComponent->GetLocationAtDistanceAlongSpline(PerLength * index, ESplineCoordinateSpace::World);
	//	const FVector StartTangent = SplineComponent->GetTangentAtDistanceAlongSpline(PerLength * index, ESplineCoordinateSpace::World);
	//	const FVector EndPos = SplineComponent->GetLocationAtDistanceAlongSpline(PerLength * (index + 1), ESplineCoordinateSpace::World);
	//	const FVector EndTangent = SplineComponent->GetTangentAtDistanceAlongSpline(PerLength * (index + 1), ESplineCoordinateSpace::World);
	//	SplineMeshComponents[index]->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent);
	//}
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

void ASwingRopeActor::ConstructRopeByComps()
{
	CapsuleComponents.Empty();
	PCComponents.Empty();
	SplineMeshComponents.Empty();
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
	//for (int32 index = 0; index < SplineComponent->GetNumberOfSplinePoints(); index = index + 1)
	//{
	//	SplineComponent->GetSplinePointAt(index, ESplineCoordinateSpace::World).Type = ESplinePointType::Linear;
	//}

	const int SplineMeshCount = NumOfCapsules + SplineMeshAdditionalCount;
	const int PerLength = SplineComponent->GetSplineLength() / SplineMeshCount;
	
	for (int index = 0; index < SplineMeshCount; index = index + 1)
	{
		const auto Component = Cast<USplineMeshComponent>(AddComponentByClass(USplineMeshComponent::StaticClass(), false, FTransform(), false));
		const FVector StartPos = SplineComponent->GetLocationAtDistanceAlongSpline(PerLength * index, ESplineCoordinateSpace::World);
		const FVector StartTangent = SplineComponent->GetTangentAtDistanceAlongSpline(PerLength * index, ESplineCoordinateSpace::World);
		const FVector EndPos = SplineComponent->GetLocationAtDistanceAlongSpline(PerLength * (index + 1), ESplineCoordinateSpace::World);
		const FVector EndTangent = SplineComponent->GetTangentAtDistanceAlongSpline(PerLength * (index + 1), ESplineCoordinateSpace::World);
		Component->SetStaticMesh(SplineMesh);
		Component->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent);

		SplineMeshComponents.Add(Component);
	}
}

void ASwingRopeActor::OnRep_PlayersOnRope()
{
	SetIgnore();
}

// ---- 创建一个带胶囊的 UBodySetup ----
UBodySetup* CreateCapsuleBodySetup(float Radius, float HalfHeight)
{
	// 新建 UBodySetup（Transient，避免编辑器保存）
	UBodySetup* BodySetup = NewObject<UBodySetup>(GetTransientPackageAsObject(), NAME_None, RF_Transient);
	BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex; // 按需设置

	// 清空默认（保险）
	BodySetup->AggGeom.EmptyElements();

	// 添加一个 Sphyl（胶囊）
	FKSphylElem Sphyl;
	Sphyl.Radius = Radius;
	// FKSphylElem.Length 是胶囊中心轴线长度（通常 = 全高度 - 2*Radius）
	Sphyl.Length = FMath::Max(0.0f, HalfHeight * 2.0f - 2.0f * Radius);
	// 你可能需要设置 Sphyl.Center 或 Rotation 如果不是原点朝向
	BodySetup->AggGeom.SphylElems.Add(Sphyl);

	// 准备物理数据（不同版本函数名可能不同）
	// UE5: CreatePhysicsMeshes 在很多版本可用
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();

	return BodySetup;
}

void ASwingRopeActor::ConstructRopeByInstances()
{
	// 清理旧数据
	for (auto* Body : RopeBodies)
		Body->TermBody();
	for (auto* Constraint : RopeConstraints)
		Constraint->TermConstraint();
	RopeBodies.Empty();
	RopeConstraints.Empty();

	SplineComponent->ClearSplinePoints();

	// 初始化 BodyInstance
	for (int32 i = 0; i <= NumOfCapsules; i++)
	{
		FBodyInstance* NewBody = new FBodyInstance();
		auto NewCapsuleBodySetup = CreateCapsuleBodySetup(CapsuleHalfHeight, CapsuleRadius);
		NewBody->InitBody(NewCapsuleBodySetup, FTransform::Identity, SkeletalMeshComponent, GetWorld()->GetPhysicsScene());

		// 设置质量、阻尼等
		float Z = (CapsuleHalfHeight * 2.0f * i * -1.0f) - CapsuleHalfHeight;
		FTransform ActorTransform = GetActorTransform();
		auto FinalLocation = ActorTransform.TransformPosition(FVector(0,0,Z));
		NewBody->SetBodyTransform(FTransform(ActorTransform.GetRotation(), FinalLocation), ETeleportType::ResetPhysics);
		NewBody->SetMassOverride(0.5f);
		NewBody->LinearDamping = 2.0f;
		NewBody->AngularDamping = 5.0f;
		NewBody->SetResponseToChannels(FCollisionResponseContainer::GetDefaultResponseContainer());
		NewBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		NewBody->SetInstanceSimulatePhysics(true);

		RopeBodies.Add(NewBody);
	}

	// 初始化约束
	for (int32 i = 0; i < NumOfCapsules; i++)
	{
		FConstraintInstance* Constraint = new FConstraintInstance();
		Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Limited, CapsuleHalfHeight);
		Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		Constraint->ProfileInstance.LinearLimit.bSoftConstraint = true;
		Constraint->ProfileInstance.LinearLimit.Stiffness = 3000.f;
		Constraint->ProfileInstance.LinearLimit.Damping = 10.f;

		// 绑定Body
		Constraint->InitConstraint(RopeBodies[i], RopeBodies[i + 1], 1, this);
		RopeConstraints.Add(Constraint);

		auto Component = RopeBodies[i];
		FVector Location = Component->GetUnrealWorldTransform().GetLocation() + (Component->GetUnrealWorldTransform().GetRotation().GetUpVector() * CapsuleHalfHeight);
		SplineComponent->AddSplineWorldPoint(Location);
	}
}

void ASwingRopeActor::UpdateSplineByInstances()
{
	for (int index = 0; index < RopeBodies.Num(); index = index + 1)
	{
		auto BodyInstance = RopeBodies[index];
		auto Transform = BodyInstance->GetUnrealWorldTransform();
		FVector Location = Transform.GetLocation() + (Transform.GetRotation().GetUpVector() * CapsuleHalfHeight);
		SplineComponent->SetLocationAtSplinePoint(index, Location, ESplineCoordinateSpace::World);
	}

	if (RopeBodies.IsValidIndex(RopeBodies.Num() - 1))
	{
		auto LastBody = RopeBodies[RopeBodies.Num() - 1];
		auto LastTransform = LastBody->GetUnrealWorldTransform();
		FVector Location = LastTransform.GetLocation() - (LastTransform.GetRotation().GetUpVector() * CapsuleHalfHeight);
		SplineComponent->SetLocationAtSplinePoint(CapsuleComponents.Num(), Location, ESplineCoordinateSpace::World);
	}
}

void ASwingRopeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASwingRopeActor, PlayersOnRope)
}
