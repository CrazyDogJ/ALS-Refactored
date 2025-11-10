// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "UObject/ObjectMacros.h"

UENUM(BlueprintType)
enum ECustomMovementMode : uint8
{
	CMOVE_Mantle		UMETA(DisplayName = "Mantle"),
	CMOVE_Slide         UMETA(DisplayName = "Slide"),
	CMOVE_FreeClimb		UMETA(DisplayName = "FreeClimb"),
	CMOVE_RopeSwing		UMETA(DisplayName = "RopeSwing"),
	CMOVE_Gliding		UMETA(DisplayName = "Gliding"),
	CMOVE_MAX			UMETA(Hidden),
};
