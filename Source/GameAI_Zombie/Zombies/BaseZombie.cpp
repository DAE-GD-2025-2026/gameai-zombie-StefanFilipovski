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

