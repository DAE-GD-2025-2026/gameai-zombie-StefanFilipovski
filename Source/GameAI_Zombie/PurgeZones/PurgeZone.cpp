// Fill out your copyright notice in the Description page of Project Settings.


#include "PurgeZone.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"

APurgeZone::APurgeZone()
{
	PrimaryActorTick.bCanEverTick = true;

	// Make the hazard perceivable by the survivor's sight sense (same pattern as houses), so the
	// agent can detect it and flee the blast radius instead of standing in it.
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
}

void APurgeZone::BeginPlay()
{
	Super::BeginPlay();

	if (StimuliSource)
	{
		StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
		StimuliSource->RegisterWithPerceptionSystem();
	}
}

void APurgeZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	TimePassed += DeltaTime;
}

