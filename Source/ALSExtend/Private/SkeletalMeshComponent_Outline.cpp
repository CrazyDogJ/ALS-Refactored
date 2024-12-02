// Fill out your copyright notice in the Description page of Project Settings.


#include "SkeletalMeshComponent_Outline.h"

#include "MaterialDomain.h"
#include "SkeletalRenderPublic.h"
#include "MultiDrawSceneProxy.h"
#include "Rendering/SkeletalMeshRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkinnedMeshComp, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogMaterialParameter, Warning, All);

void USkeletalMeshComponent_Outline::SetDrawMaterials(TArray<UMaterialInterface*> Materials)
{
	DrawMaterial = Materials;
	// Precache PSOs again
	PrecachePSOs();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* USkeletalMeshComponent_Outline::CreateSceneProxy()
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	ERHIFeatureLevel::Type SceneFeatureLevel = GetWorld()->GetFeatureLevel();
	FMultiDrawSceneProxy* Result = nullptr;
	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

	if (CheckPSOPrecachingAndBoostPriority() && GetPSOPrecacheProxyCreationStrategy() == EPSOPrecacheProxyCreationStrategy::DelayUntilPSOPrecached)
	{
		UE_LOG(LogSkinnedMeshComp, Verbose, TEXT("Skipping CreateSceneProxy for USkinnedMeshComponent %s (USkinnedMeshComponent PSOs are still compiling)"), *GetFullName());
		return nullptr;
	}

	// Only create a scene proxy for rendering if properly initialized
	if (SkelMeshRenderData &&
		SkelMeshRenderData->LODRenderData.IsValidIndex(GetPredictedLODLevel()) &&
		!bHideSkin &&
		MeshObject)
	{
		// Only create a scene proxy if the bone count being used is supported, or if we don't have a skeleton (this is the case with destructibles)
		int32 MinLODIndex = ComputeMinLOD();
		int32 MaxBonesPerChunk = SkelMeshRenderData->GetMaxBonesPerSection(MinLODIndex);
		int32 MaxSupportedNumBones = MeshObject->IsCPUSkinned() ? MAX_int32 : FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
		if (MaxBonesPerChunk <= MaxSupportedNumBones)
		{
			Result = ::new FMultiDrawSceneProxy(this, SkelMeshRenderData);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	SendRenderDebugPhysics(Result);
#endif

	return Result;
}

void USkeletalMeshComponent_Outline::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
	bool bGetDebugMaterials) const
{
	Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
	
	for (UMaterialInterface* Material : DrawMaterial)
		OutMaterials.Add(Material);
}

void USkeletalMeshComponent_Outline::CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams,
	FMaterialInterfacePSOPrecacheParamsList& OutParams)
{
	if (GetSkinnedAsset() == nullptr ||
		GetSkinnedAsset()->GetResourceForRendering() == nullptr)
	{
		return;
	}

	ERHIFeatureLevel::Type FeatureLevel = GetWorld() ? GetWorld()->GetFeatureLevel() : GMaxRHIFeatureLevel;
	int32 MinLODIndex = ComputeMinLOD();
	bool bCPUSkin = bRenderStatic || ShouldCPUSkin();

	FPSOPrecacheVertexFactoryDataPerMaterialIndexList VFsPerMaterials = GetSkinnedAsset()->GetVertexFactoryTypesPerMaterialIndex(this, MinLODIndex, bCPUSkin, FeatureLevel);
	bool bAnySectionCastsShadows = GetSkinnedAsset()->GetResourceForRendering()->AnyRenderSectionCastsShadows(MinLODIndex);

	FPSOPrecacheParams PrecachePSOParams = BasePrecachePSOParams;
	PrecachePSOParams.bCastShadow = PrecachePSOParams.bCastShadow && bAnySectionCastsShadows;

	// Skinned assets shouldn't need dynamic indirect lighting but MDCs for LumenCardCapture can still be setup and created (but not actually used) causing PSO precache misses
	//PrecachePSOParams.bAffectDynamicIndirectLighting = false;

	for (FPSOPrecacheVertexFactoryDataPerMaterialIndex& VFsPerMaterial : VFsPerMaterials)
	{
		UMaterialInterface* MaterialInterface = GetMaterial(VFsPerMaterial.MaterialIndex);
		if (MaterialInterface == nullptr)
		{
			MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		FMaterialInterfacePSOPrecacheParams& ComponentParams = OutParams[OutParams.AddDefaulted()];
		ComponentParams.MaterialInterface = MaterialInterface;
		ComponentParams.VertexFactoryDataList = VFsPerMaterial.VertexFactoryDataList;
		ComponentParams.PSOPrecacheParams = PrecachePSOParams;
	}

	for (auto DrawMaterialInterface : DrawMaterial)
	{
		if (DrawMaterialInterface && VFsPerMaterials.Num() != 0)
		{
			// Overlay is rendered with the same set of VFs
			FMaterialInterfacePSOPrecacheParams& ComponentParams = OutParams[OutParams.AddDefaulted()];

			ComponentParams.MaterialInterface = DrawMaterialInterface;
			ComponentParams.VertexFactoryDataList = VFsPerMaterials[0].VertexFactoryDataList;
			ComponentParams.PSOPrecacheParams = PrecachePSOParams;
			ComponentParams.PSOPrecacheParams.bCastShadow = false;
		}
	}
	
	UMaterialInterface* OverlayMaterialInterface = GetOverlayMaterial();
	if (OverlayMaterialInterface && VFsPerMaterials.Num() != 0)
	{
		// Overlay is rendered with the same set of VFs
		FMaterialInterfacePSOPrecacheParams& ComponentParams = OutParams[OutParams.AddDefaulted()];

		ComponentParams.MaterialInterface = OverlayMaterialInterface;
		ComponentParams.VertexFactoryDataList = VFsPerMaterials[0].VertexFactoryDataList;
		ComponentParams.PSOPrecacheParams = PrecachePSOParams;
		ComponentParams.PSOPrecacheParams.bCastShadow = false;
	}
}

FMaterialRelevance USkeletalMeshComponent_Outline::GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	auto result = Super::GetMaterialRelevance(InFeatureLevel);

	for (auto Interface : DrawMaterial)
	{
		UMaterialInterface const* OverlayMaterialInterface = Interface;
		if (OverlayMaterialInterface != nullptr)
		{
			result |= OverlayMaterialInterface->GetRelevance_Concurrent(InFeatureLevel);
		}
	}
	
	return result;
}

void USkeletalMeshComponent_Outline::SetScalarParameterValueOnMaterials_Custom(const FName ParameterName,
	const float ParameterValue)
{
	if (!bEnableMaterialParameterCaching)
	{
		TArray<UMaterialInterface*> MaterialInterfaces = GetMaterials();
		MaterialInterfaces.Append(DrawMaterial);

		for (int32 MaterialIndex = 0; MaterialIndex < MaterialInterfaces.Num(); ++MaterialIndex)
		{
			UMaterialInterface* MaterialInterface = MaterialInterfaces[MaterialIndex];
			if (MaterialInterface)
			{
				UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(MaterialInterface);
				if (!DynamicMaterial)
				{
					DynamicMaterial = CreateAndSetMaterialInstanceDynamic(MaterialIndex);
				}
				DynamicMaterial->SetScalarParameterValue(ParameterName, ParameterValue);
			}
		}
	}
	else
	{
		if (bCachedMaterialParameterIndicesAreDirty)
		{
			CacheMaterialParameterNameIndices();
		}

		// Look up material index array according to ParameterName
		if (FMaterialParameterCache* ParameterCache = MaterialParameterCache.Find(ParameterName))
		{
			const TArray<int32>& MaterialIndices = ParameterCache->ScalarParameterMaterialIndices;
			// Loop over all the material indices and update set the parameter value on the corresponding materials		
			for (int32 MaterialIndex : MaterialIndices)
			{
				UMaterialInterface* MaterialInterface = GetMaterial(MaterialIndex);
				if (DrawMaterial.IsValidIndex(MaterialIndex))
				{
					UMaterialInterface* MaterialInterface_OutLine = DrawMaterial[MaterialIndex];
					if (MaterialInterface_OutLine)
					{
						UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(MaterialInterface_OutLine);
						if (!DynamicMaterial)
						{
							DynamicMaterial = CreateAndSetMaterialInstanceDynamic(MaterialIndex);
						}
						DynamicMaterial->SetScalarParameterValue(ParameterName, ParameterValue);
					}
				}
				
				if (MaterialInterface)
				{
					UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(MaterialInterface);
					if (!DynamicMaterial)
					{
						DynamicMaterial = CreateAndSetMaterialInstanceDynamic(MaterialIndex);
					}
					DynamicMaterial->SetScalarParameterValue(ParameterName, ParameterValue);
				}
			}
		}
		else
		{
			UE_LOG(LogMaterialParameter, Log, TEXT("%s material parameter hasn't found on the component %s"), *ParameterName.ToString(), *GetPathName());
		}
	}
}
