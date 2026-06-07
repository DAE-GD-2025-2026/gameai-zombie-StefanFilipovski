


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
		// Took a hit and still alive - notify listeners.
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


