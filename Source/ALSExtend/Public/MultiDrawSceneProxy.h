#pragma once

#include "SkeletalMeshComponent_Outline.h"
#include "SkeletalMeshSceneProxy.h"

class FMultiDrawSceneProxy : public FSkeletalMeshSceneProxy
{
public:
	FMultiDrawSceneProxy(USkeletalMeshComponent_Outline* Component, FSkeletalMeshRenderData* InSkelMeshRenderData)
		: FSkeletalMeshSceneProxy(Component, InSkelMeshRenderData)
	, MultiDrawMeshComponent(Component)
	{
	}

	USkeletalMeshComponent_Outline* MultiDrawMeshComponent;
	
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;

	void GetDynamicElementsSection(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap,
									const FSkeletalMeshLODRenderData& LODData, const int32 LODIndex, const int32 SectionIndex, bool bSectionSelected,
								   const FSectionElementInfo& SectionElementInfo, bool bInSelectable, FMeshElementCollector& Collector) const;

	void CreateBaseMeshBatch(const FSceneView* View, const FSkeletalMeshLODRenderData& LODData, const int32 LODIndex, const int32 SectionIndex, const FSectionElementInfo& SectionElementInfo, FMeshBatch& Mesh, ESkinVertexFactoryMode VFMode = ESkinVertexFactoryMode::Default) const;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
};
