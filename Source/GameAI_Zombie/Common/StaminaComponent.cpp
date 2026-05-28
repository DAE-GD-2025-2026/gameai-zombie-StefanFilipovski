// Fill out your copyright notice in the Description page of Project Settings.


#include "StaminaComponent.h"

#include "Survivor/SurvivorPawn.h"


// Sets default values for this component's properties
UStaminaComponent::UStaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UStaminaComponent::BeginPlay()
{
	Super::BeginPlay();
	
	SurvivorOwner = Cast<ASurvivorPawn>(GetOwner());
	
	Stamina = MaxStamina;
}

// Called every frame
void UStaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                      FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (SurvivorOwner->IsRunning())
	{
		Stamina -= DeltaTime * RunningStaminaDrainPerSecond;
	}
	else
	{
		Stamina -= DeltaTime * DefaultStaminaDrainPerSecond;
	}

	if (Stamina < 0.0f)
	{
		Stamina = 0.0f;
	}

	// Auto-stop running when stamina is depleted
	if (Stamina <= 0.0f && SurvivorOwner && SurvivorOwner->IsRunning())
	{
		SurvivorOwner->StopRunning();
		UE_LOG(LogTemp, Warning, TEXT("Stamina: Exhausted! Forced stop running."));
	}
}

void UStaminaComponent::AddStamina(float Amount)
{
	Stamina += Amount;

	if (Stamina > MaxStamina)
	{
		Stamina = MaxStamina;
	}
}

