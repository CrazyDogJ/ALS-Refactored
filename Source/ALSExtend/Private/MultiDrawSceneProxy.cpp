#include "MultiDrawSceneProxy.h"

#include "SkeletalMeshSceneProxy.h"
#include "SkeletalRenderPublic.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialRenderProxy.h"
#include "Rendering/SkeletalMeshRenderData.h"

class FSkeletalMeshSectionIter
{
public:
	FSkeletalMeshSectionIter(const int32 InLODIdx, const FSkeletalMeshObject& InMeshObject, const FSkeletalMeshLODRenderData& InLODData, const FSkeletalMeshSceneProxy::FLODSectionElements& InLODSectionElements)
		: SectionIndex(0)
		, MeshObject(InMeshObject)
		, LODSectionElements(InLODSectionElements)
		, Sections(InLODData.RenderSections)
#if WITH_EDITORONLY_DATA
		, SectionIndexPreview(InMeshObject.SectionIndexPreview)
		, MaterialIndexPreview(InMeshObject.MaterialIndexPreview)
#endif
	{
		while (NotValidPreviewSection())
		{
			SectionIndex++;
		}
	}
	FORCEINLINE FSkeletalMeshSectionIter& operator++()
	{
		do 
		{
		SectionIndex++;
		} while (NotValidPreviewSection());
		return *this;
	}
	FORCEINLINE explicit operator bool() const
	{
		return ((SectionIndex < Sections.Num()) && LODSectionElements.SectionElements.IsValidIndex(GetSectionElementIndex()));
	}
	FORCEINLINE const FSkelMeshRenderSection& GetSection() const
	{
		return Sections[SectionIndex];
	}
	FORCEINLINE const int32 GetSectionElementIndex() const
	{
		return SectionIndex;
	}
	FORCEINLINE const FSkeletalMeshSceneProxy::FSectionElementInfo& GetSectionElementInfo() const
	{
		int32 SectionElementInfoIndex = GetSectionElementIndex();
		return LODSectionElements.SectionElements[SectionElementInfoIndex];
	}
	FORCEINLINE bool NotValidPreviewSection()
	{
#if WITH_EDITORONLY_DATA
		if (MaterialIndexPreview == INDEX_NONE)
		{
			int32 ActualPreviewSectionIdx = SectionIndexPreview;

			return	(SectionIndex < Sections.Num()) &&
				((ActualPreviewSectionIdx >= 0) && (ActualPreviewSectionIdx != SectionIndex));
		}
		else
		{
			int32 ActualPreviewMaterialIdx = MaterialIndexPreview;
			int32 ActualPreviewSectionIdx = INDEX_NONE;
			if (ActualPreviewMaterialIdx != INDEX_NONE && Sections.IsValidIndex(SectionIndex))
			{
				const FSkeletalMeshSceneProxy::FSectionElementInfo& SectionInfo = LODSectionElements.SectionElements[SectionIndex];
				if (SectionInfo.UseMaterialIndex == ActualPreviewMaterialIdx)
				{
					ActualPreviewSectionIdx = SectionIndex;
				}
			}

			return	(SectionIndex < Sections.Num()) &&
				((ActualPreviewMaterialIdx >= 0) && (ActualPreviewSectionIdx != SectionIndex));
		}
#else
		return false;
#endif
	}
private:
	int32 SectionIndex;
	const FSkeletalMeshObject& MeshObject;
	const FSkeletalMeshSceneProxy::FLODSectionElements& LODSectionElements;
	const TArray<FSkelMeshRenderSection>& Sections;
#if WITH_EDITORONLY_DATA
	const int32 SectionIndexPreview;
	const int32 MaterialIndexPreview;
#endif
};

#if WITH_EDITOR
void FSkeletalMeshObject::UpdateMinDesiredLODLevel(const FSceneView* View, const FBoxSphereBounds& Bounds, int32 FrameNumber)
{
	// Thumbnail rendering doesn't contribute to MinDesiredLODLevel calculation
	if (View->Family && (View->Family->bThumbnailRendering || !View->Family->GetIsInFocus()))
	{
		return;
	}

	static const auto* SkeletalMeshLODRadiusScale = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SkeletalMeshLODRadiusScale"));
	float LODScale = FMath::Clamp(SkeletalMeshLODRadiusScale->GetValueOnRenderThread(), 0.25f, 1.0f);

	const float ScreenRadiusSquared = ComputeBoundsScreenRadiusSquared(Bounds.Origin, Bounds.SphereRadius, *View) * LODScale * LODScale;

	checkf( SkeletalMeshLODInfo.Num() == SkeletalMeshRenderData->LODRenderData.Num(), TEXT("Mismatched LOD arrays. SkeletalMeshLODInfo.Num() = %d, SkeletalMeshRenderData->LODRenderData.Num() = %d"), SkeletalMeshLODInfo.Num(), SkeletalMeshRenderData->LODRenderData.Num());

	// Need the current LOD
	const int32 CurrentLODLevel = GetLOD();
	const float HysteresisOffset = 0.f;

	int32 NewLODLevel = 0;

	// Look for a lower LOD if the EngineShowFlags is enabled
	if( View->Family && 1==View->Family->EngineShowFlags.LOD )
	{
		// Iterate from worst to best LOD
		for(int32 LODLevel = SkeletalMeshRenderData->LODRenderData.Num()-1; LODLevel > 0; LODLevel--)
		{
			// Get ScreenSize for this LOD
			float ScreenSize = SkeletalMeshLODInfo[LODLevel].ScreenSize.GetValue();

			// If we are considering shifting to a better (lower) LOD, bias with hysteresis.
			if(LODLevel  <= CurrentLODLevel)
			{
				ScreenSize += SkeletalMeshLODInfo[LODLevel].LODHysteresis;
			}

			// If have passed this boundary, use this LOD
			if(FMath::Square(ScreenSize * 0.5f) > ScreenRadiusSquared)
			{
				NewLODLevel = LODLevel;
				break;
			}
		}
	}

	if (!LastFrameNumber)
	{
		// We don't have last frame value on the first call to FSkeletalMeshObject::UpdateMinDesiredLODLevel so
		// just reuse current frame value. Otherwise, MinDesiredLODLevel may get stale value in the update code below
		WorkingMinDesiredLODLevel = NewLODLevel;
	}

	// Different path for first-time vs subsequent-times in this function (ie splitscreen)
	if(FrameNumber != LastFrameNumber)
	{
		// Copy last frames value to the version that will be read by game thread
		MaxDistanceFactor = WorkingMaxDistanceFactor;
		MinDesiredLODLevel = WorkingMinDesiredLODLevel;
		LastFrameNumber = FrameNumber;

		WorkingMaxDistanceFactor = ScreenRadiusSquared;
		WorkingMinDesiredLODLevel = NewLODLevel;
	}
	else
	{
		WorkingMaxDistanceFactor = FMath::Max(WorkingMaxDistanceFactor, ScreenRadiusSquared);
		WorkingMinDesiredLODLevel = FMath::Min(WorkingMinDesiredLODLevel, NewLODLevel);
	}
}
#endif

void FMultiDrawSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	if (!MeshObject || !bRenderStatic)
	{
		return;
	}

	if (!HasViewDependentDPG())
	{
		ESceneDepthPriorityGroup PrimitiveDPG = GetStaticDepthPriorityGroup();
		bool bUseSelectedMaterial = false;

		int32 NumLODs = SkeletalMeshRenderData->LODRenderData.Num();
		int32 ClampedMinLOD = 0; // TODO: MinLOD, Bias?

		for (int32 LODIndex = ClampedMinLOD; LODIndex < NumLODs; ++LODIndex)
		{
			const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];
			
			if (LODSections.Num() > 0 && LODData.GetNumVertices() > 0)
			{
				float ScreenSize;

				auto SkeletalMeshLODInfo = MultiDrawMeshComponent->GetSkinnedAsset()->GetLODInfoArray();
				if (SkeletalMeshLODInfo.IsValidIndex(LODIndex))
				{
					ScreenSize = SkeletalMeshLODInfo[LODIndex].ScreenSize.GetValue();
				}
				else
				{
					ScreenSize = 0.f;
				}
				
				const FLODSectionElements& LODSection = LODSections[LODIndex];
				check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());

				for (FSkeletalMeshSectionIter Iter(LODIndex, *MeshObject, LODData, LODSection); Iter; ++Iter)
				{
					const FSkelMeshRenderSection& Section = Iter.GetSection();
					const int32 SectionIndex = Iter.GetSectionElementIndex();
					const FSectionElementInfo& SectionElementInfo = Iter.GetSectionElementInfo();
					const FVertexFactory* VertexFactory = MeshObject->GetSkinVertexFactory(nullptr, LODIndex, SectionIndex);
				
					// If hidden skip the draw
					check(MeshObject->LODInfo.IsValidIndex(LODIndex));

					if (MeshObject->LODInfo[LODIndex].HiddenMaterials.IsValidIndex(SectionElementInfo.UseMaterialIndex) &&
						MeshObject->LODInfo[LODIndex].HiddenMaterials[SectionElementInfo.UseMaterialIndex])
					{
						continue;
					}
					
					if (!VertexFactory)
					{
						// hide this part
						continue;
					}

				#if WITH_EDITOR
					if (GIsEditor)
					{
						bUseSelectedMaterial = (MeshObject->SelectedEditorSection == SectionIndex);
						PDI->SetHitProxy(SectionElementInfo.HitProxy);
					}
				#endif // WITH_EDITOR
								
					FMeshBatch MeshElement;
					FMeshBatchElement& BatchElement = MeshElement.Elements[0];
					MeshElement.DepthPriorityGroup = PrimitiveDPG;
					MeshElement.VertexFactory = MeshObject->GetSkinVertexFactory(nullptr, LODIndex, SectionIndex);
					MeshElement.MaterialRenderProxy = SectionElementInfo.Material->GetRenderProxy();
					MeshElement.ReverseCulling = IsLocalToWorldDeterminantNegative();
					MeshElement.CastShadow = SectionElementInfo.bEnableShadowCasting;
				#if RHI_RAYTRACING
					MeshElement.CastRayTracedShadow = MeshElement.CastShadow && bCastDynamicShadow;
				#endif
					MeshElement.Type = PT_TriangleList;
					MeshElement.LODIndex = LODIndex;
					MeshElement.SegmentIndex = SectionIndex;
						
					BatchElement.FirstIndex = Section.BaseIndex;
					BatchElement.MinVertexIndex = Section.BaseVertexIndex;
					BatchElement.MaxVertexIndex = LODData.GetNumVertices() - 1;
					BatchElement.NumPrimitives = Section.NumTriangles;
					BatchElement.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
													
					PDI->DrawMesh(MeshElement, ScreenSize);

					//Draw multi first
					if (MultiDrawMeshComponent)
					{
						if (MultiDrawMeshComponent->DrawMaterial.IsValidIndex(SectionIndex))
						{
							if (MultiDrawMeshComponent->DrawMaterial[SectionIndex] != nullptr)
							{
								FMeshBatch MeshBatch(MeshElement);
								MeshBatch.bOverlayMaterial = true;
								MeshBatch.CastShadow = false;
								MeshBatch.bSelectable = false;
								MeshBatch.MaterialRenderProxy = MultiDrawMeshComponent->DrawMaterial[SectionIndex]->GetRenderProxy();
								// make sure overlay is always rendered on top of base mesh
								MeshBatch.MeshIdInPrimitive += LODData.RenderSections.Num();
								// Reuse mesh ScreenSize as cull distance for an overlay. Overlay does not need to compute LOD so we can avoid adding new members into MeshBatch or MeshRelevance
								float OverlayMeshScreenSize = OverlayMaterialMaxDrawDistance;
								PDI->DrawMesh(MeshBatch, OverlayMeshScreenSize);
							}
						}
					}

					//Draw overlay
					if (OverlayMaterial != nullptr)
					{
						FMeshBatch OverlayMeshBatch(MeshElement);
						OverlayMeshBatch.bOverlayMaterial = true;
						OverlayMeshBatch.CastShadow = false;
						OverlayMeshBatch.bSelectable = false;
						OverlayMeshBatch.MaterialRenderProxy = OverlayMaterial->GetRenderProxy();
						// make sure overlay is always rendered on top of base mesh
						OverlayMeshBatch.MeshIdInPrimitive += LODData.RenderSections.Num();
						// Reuse mesh ScreenSize as cull distance for an overlay. Overlay does not need to compute LOD so we can avoid adding new members into MeshBatch or MeshRelevance
						float OverlayMeshScreenSize = OverlayMaterialMaxDrawDistance;
						PDI->DrawMesh(OverlayMeshBatch, OverlayMeshScreenSize);
					}
				}
			}
		}
	}
}

void FMultiDrawSceneProxy::GetDynamicElementsSection(const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, const FSkeletalMeshLODRenderData& LODData,
	const int32 LODIndex, const int32 SectionIndex, bool bSectionSelected,
	const FSectionElementInfo& SectionElementInfo, bool bInSelectable, FMeshElementCollector& Collector) const
{
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];

	//// If hidden skip the draw
	//if (Section.bDisabled || MeshObject->IsMaterialHidden(LODIndex,SectionElementInfo.UseMaterialIndex))
	//{
	//	return;
	//}

#if !WITH_EDITOR
	const bool bIsSelected = false;
#else // #if !WITH_EDITOR
	bool bIsSelected = IsSelected();

	// if the mesh isn't selected but the mesh section is selected in the AnimSetViewer, find the mesh component and make sure that it can be highlighted (ie. are we rendering for the AnimSetViewer or not?)
	if( !bIsSelected && bSectionSelected && bCanHighlightSelectedSections )
	{
		bIsSelected = true;
	}
#endif // #if WITH_EDITOR

	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			FMeshBatch& Mesh = Collector.AllocateMesh();

			CreateBaseMeshBatch(View, LODData, LODIndex, SectionIndex, SectionElementInfo, Mesh);
			
			if(!Mesh.VertexFactory)
			{
				// hide this part
				continue;
			}

			Mesh.bWireframe |= bForceWireframe;
			Mesh.Type = PT_TriangleList;
			Mesh.bSelectable = bInSelectable;

			FMeshBatchElement& BatchElement = Mesh.Elements[0];

		#if WITH_EDITOR
			Mesh.BatchHitProxyId = SectionElementInfo.HitProxy ? SectionElementInfo.HitProxy->Id : FHitProxyId();

			if (bSectionSelected && bCanHighlightSelectedSections)
			{
				Mesh.bUseSelectionOutline = true;
			}
			else
			{
				Mesh.bUseSelectionOutline = !bCanHighlightSelectedSections && bIsSelected;
			}
		#endif

			BatchElement.MinVertexIndex = Section.BaseVertexIndex;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.CastShadow = SectionElementInfo.bEnableShadowCasting;
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.bUseWireframeSelectionColoring = bIsSelected;

			if (ensureMsgf(Mesh.MaterialRenderProxy, TEXT("GetDynamicElementsSection with invalid MaterialRenderProxy. Owner:%s LODIndex:%d UseMaterialIndex:%d"), *GetOwnerName().ToString(), LODIndex, SectionElementInfo.UseMaterialIndex))
			{
				Collector.AddMesh(ViewIndex, Mesh);
			}

			const int32 NumVertices = Section.GetNumVertices();

			if (MultiDrawMeshComponent)
			{
				if (MultiDrawMeshComponent->DrawMaterial.IsValidIndex(SectionIndex))
				{
					if (MultiDrawMeshComponent->DrawMaterial[SectionIndex] != nullptr)
					{
						FMeshBatch& OverlayMeshBatch = Collector.AllocateMesh();
						OverlayMeshBatch = Mesh;
						OverlayMeshBatch.bOverlayMaterial = true;
						OverlayMeshBatch.CastShadow = false;
						OverlayMeshBatch.bSelectable = false;
						OverlayMeshBatch.MaterialRenderProxy = MultiDrawMeshComponent->DrawMaterial[SectionIndex]->GetRenderProxy();
						// make sure overlay is always rendered on top of base mesh
						OverlayMeshBatch.MeshIdInPrimitive += LODData.RenderSections.Num();
						Collector.AddMesh(ViewIndex, OverlayMeshBatch);
					}
				}
			}
			// negative cull distance disables overlay rendering
			if (OverlayMaterial != nullptr && OverlayMaterialMaxDrawDistance >= 0.f)
			{
				const bool bHasOverlayCullDistance = 
					OverlayMaterialMaxDrawDistance > 0.f && 
					OverlayMaterialMaxDrawDistance != FLT_MAX && 
					!ViewFamily.EngineShowFlags.DistanceCulledPrimitives;
				
				bool bAddOverlay = true;
				if (bHasOverlayCullDistance)
				{
					// this is already combined with ViewDistanceScale
					float MaxDrawDistanceScale = GetCachedScalabilityCVars().SkeletalMeshOverlayDistanceScale;
					MaxDrawDistanceScale *= GetCachedScalabilityCVars().CalculateFieldOfViewDistanceScale(View->DesiredFOV);
					float DistanceSquared = (GetBounds().Origin - View->ViewMatrices.GetViewOrigin()).SizeSquared();
					if (DistanceSquared > FMath::Square(OverlayMaterialMaxDrawDistance * MaxDrawDistanceScale))
					{
						// distance culled
						bAddOverlay = false;
					}
				}
				
				if (bAddOverlay)
				{
					FMeshBatch& OverlayMeshBatch = Collector.AllocateMesh();
					OverlayMeshBatch = Mesh;
					OverlayMeshBatch.bOverlayMaterial = true;
					OverlayMeshBatch.CastShadow = false;
					OverlayMeshBatch.bSelectable = false;
					OverlayMeshBatch.MaterialRenderProxy = OverlayMaterial->GetRenderProxy();
					// make sure overlay is always rendered on top of base mesh
					OverlayMeshBatch.MeshIdInPrimitive += LODData.RenderSections.Num();
					Collector.AddMesh(ViewIndex, OverlayMeshBatch);
				}
			}
		}
	}
}

void FMultiDrawSceneProxy::CreateBaseMeshBatch(const FSceneView* View, const FSkeletalMeshLODRenderData& LODData,
	const int32 LODIndex, const int32 SectionIndex, const FSectionElementInfo& SectionElementInfo, FMeshBatch& Mesh,
	ESkinVertexFactoryMode VFMode) const
{
	Mesh.VertexFactory = MeshObject->GetSkinVertexFactory(View, LODIndex, SectionIndex, VFMode);
	Mesh.MaterialRenderProxy = SectionElementInfo.Material->GetRenderProxy();
#if RHI_RAYTRACING
	Mesh.SegmentIndex = SectionIndex;
	Mesh.CastRayTracedShadow = SectionElementInfo.bEnableShadowCasting && bCastDynamicShadow;
#endif

	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.FirstIndex = LODData.RenderSections[SectionIndex].BaseIndex;
	BatchElement.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
	BatchElement.MinVertexIndex = LODData.RenderSections[SectionIndex].GetVertexBufferIndex();
	BatchElement.MaxVertexIndex = LODData.RenderSections[SectionIndex].GetVertexBufferIndex() + LODData.RenderSections[SectionIndex].GetNumVertices() - 1;

	FSkinBatchVertexFactoryUserData const* VertexFactoryUserData = MeshObject->GetVertexFactoryUserData(LODIndex, SectionIndex, VFMode);
	BatchElement.VertexFactoryUserData = (void*)VertexFactoryUserData;

	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
	BatchElement.NumPrimitives = LODData.RenderSections[SectionIndex].NumTriangles;
}

void FMultiDrawSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if( !MeshObject )
	{
		return;
	}	
	MeshObject->PreGDMECallback(Collector.GetRHICommandList(), ViewFamily.Scene->GetGPUSkinCache(), ViewFamily.FrameCounter);

	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	const int32 FirstLODIdx = SkeletalMeshRenderData->GetFirstValidLODIdx(FMath::Max(SkeletalMeshRenderData->PendingFirstLODIdx, SkeletalMeshRenderData->CurrentFirstLODIdx));
	if (FirstLODIdx == INDEX_NONE)
	{
#if DO_CHECK
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Skeletal mesh %s has no valid LODs for rendering."), *GetResourceName().ToString());
#endif
	}
	else
	{
		if (UNLIKELY(!Views.IsEmpty() && IStereoRendering::IsStereoEyeView(*Views[0])))
		{
			const FSceneView& View = GetLODView(*Views[0]);
			MeshObject->UpdateMinDesiredLODLevel(&View, GetBounds(), ViewFamily.FrameNumber);
		}
		else
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];
					MeshObject->UpdateMinDesiredLODLevel(View, GetBounds(), ViewFamily.FrameNumber);
				}
			}
		}

		const int32 LODIndex = MeshObject->GetLOD();
		check(LODIndex < SkeletalMeshRenderData->LODRenderData.Num());
		const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];

		if (LODSections.Num() > 0 && LODIndex >= FirstLODIdx)
		{
			check(SkeletalMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0);

			const FLODSectionElements& LODSection = LODSections[LODIndex];

			check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());

			for (FSkeletalMeshSectionIter Iter(LODIndex, *MeshObject, LODData, LODSection); Iter; ++Iter)
			{
				const FSkelMeshRenderSection& Section = Iter.GetSection();
				const int32 SectionIndex = Iter.GetSectionElementIndex();
				const FSectionElementInfo& SectionElementInfo = Iter.GetSectionElementInfo();

				bool bSectionSelected = false;

#if WITH_EDITORONLY_DATA
				// TODO: This is not threadsafe! A render command should be used to propagate SelectedEditorSection to the scene proxy.
				if (MeshObject->SelectedEditorMaterial != INDEX_NONE)
				{
					bSectionSelected = (MeshObject->SelectedEditorMaterial == SectionElementInfo.UseMaterialIndex);
				}
				else
				{
					bSectionSelected = (MeshObject->SelectedEditorSection == SectionIndex);
				}
			
#endif
				// If hidden skip the draw
				check(MeshObject->LODInfo.IsValidIndex(LODIndex));
				// If hidden skip the draw
				if (bool bIsMaterialHidden = MeshObject->LODInfo[LODIndex].HiddenMaterials.
				                                                           IsValidIndex(
					                                                           SectionElementInfo.UseMaterialIndex) &&
					MeshObject->LODInfo[LODIndex].HiddenMaterials[SectionElementInfo.UseMaterialIndex]; bIsMaterialHidden || Section.bDisabled)
				{
					continue;
				}

				GetDynamicElementsSection(Views, ViewFamily, VisibilityMap, LODData, LODIndex, SectionIndex, bSectionSelected, SectionElementInfo, true, Collector);
			}
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if( PhysicsAssetForDebug )
			{
				DebugDrawPhysicsAsset(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}

			if (EngineShowFlags.MassProperties && DebugMassData.Num() > 0)
			{
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				if (MeshObject->GetComponentSpaceTransforms())
				{
					const TArray<FTransform>& ComponentSpaceTransforms = *MeshObject->GetComponentSpaceTransforms();

					for (const FDebugMassData& DebugMass : DebugMassData)
					{
						if (ComponentSpaceTransforms.IsValidIndex(DebugMass.BoneIndex))
						{
							const FTransform BoneToWorld = ComponentSpaceTransforms[DebugMass.BoneIndex] * FTransform(GetLocalToWorld());
							DebugMass.DrawDebugMass(PDI, BoneToWorld);
						}
					}
				}
			}

			if (ViewFamily.EngineShowFlags.SkeletalMeshes)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}

			if (ViewFamily.EngineShowFlags.Bones || bDrawDebugSkeleton)
			{
				DebugDrawSkeleton(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}

#if WITH_EDITOR
			DebugDrawPoseWatchSkeletons(ViewIndex, Collector, ViewFamily.EngineShowFlags);
#endif
		}
	}
#endif
}
