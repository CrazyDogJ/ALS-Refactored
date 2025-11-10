// Fill out your copyright notice in the Description page of Project Settings.


#include "AlsPlayerState.h"

#include "AbilitySystemComponent.h"

AAlsPlayerState::AAlsPlayerState(const FObjectInitializer& ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

UAbilitySystemComponent* AAlsPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
