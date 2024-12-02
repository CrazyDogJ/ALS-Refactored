// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshComponent_Outline.generated.h"

/**
 * 
 */
UCLASS(editinlinenew,
   meta = (BlueprintSpawnableComponent),
   ClassGroup = Rendering,
   HideCategories = (Physics, Object, Activation, "Components|Activation"),
   meta = (DisplayName = "OutlineSkeletalMesh"))
class ALSEXTEND_API USkeletalMeshComponent_Outline : public USkeletalMeshComponent
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintReadOnly, Category = "MultiDraw Mesh")
	TArray<UMaterialInterface*> DrawMaterial;

	UFUNCTION(BlueprintCallable, Category = "MultiDraw Mesh")
	void SetDrawMaterials(TArray<UMaterialInterface*> Materials);
	
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;

	virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FMaterialInterfacePSOPrecacheParamsList& OutParams) override;

	virtual FMaterialRelevance GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const override;

	/** Set all occurrences of Scalar Material Parameters with ParameterName in the set of materials of the SkeletalMesh to ParameterValue */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Material")
	void SetScalarParameterValueOnMaterials_Custom(const FName ParameterName, const float ParameterValue);
};
