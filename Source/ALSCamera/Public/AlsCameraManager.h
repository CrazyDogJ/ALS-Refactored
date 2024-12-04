#pragma once

#include "Camera/PlayerCameraManager.h"
#include "AlsCameraManager.generated.h"

UCLASS(Blueprintable)
class ALSCAMERA_API AAlsCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;
};
