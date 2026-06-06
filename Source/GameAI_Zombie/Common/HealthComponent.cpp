// Fill out your copyright notice in the Description page of Project Settings.


#include "HealthComponent.h"


// Sets default values for this component's properties
UHealthComponent::UHealthComponent()
{
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	
	Health = MaxHealth;
}


void UHealthComponent::TakeDamage(int Amount)
{
	Health -= Amount;
	if (IsDead())
	{
		HandleDeath();
	}
	else if (Amount > 0)
	{
		// Still alive and actually took a hit — let listeners react (e.g. turn to face the attacker).
		OnDamaged.Broadcast();
	}
}

void UHealthComponent::HealDamage(int Amount)
{
	Health += Amount;
	if (Health > MaxHealth)
	{
		Health = MaxHealth;
	}
}

void UHealthComponent::HandleDeath() const
{
	OnDeath.Broadcast();
}


