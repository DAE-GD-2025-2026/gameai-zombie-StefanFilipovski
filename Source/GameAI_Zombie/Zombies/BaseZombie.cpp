// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseZombie.h"
#include "Perception/AISense_Hearing.h"


// Sets default values
ABaseZombie::ABaseZombie()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void ABaseZombie::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ABaseZombie::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Periodically emit a (silent) noise event so the survivor can HEAR us nearby, even when we're
	// outside its sight cone (e.g. chasing from behind). Throttled to keep it cheap with many zombies.
	NoiseEmitTimer += DeltaTime;
	if (NoiseEmitTimer >= NoiseInterval)
	{
		NoiseEmitTimer = 0.f;
		UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), NoiseLoudness, this, NoiseRange, FName("Zombie"));
	}
}

// Called to bind functionality to input
void ABaseZombie::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

