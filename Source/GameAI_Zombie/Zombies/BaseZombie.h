// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "BaseZombie.generated.h"

UCLASS()
class GAMEAI_ZOMBIE_API ABaseZombie : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ABaseZombie();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:
	/** Zombies are "noisy" — they periodically report a silent AIPerception noise event so the
	 *  survivor's Hearing sense can detect them nearby from any direction (not just its sight cone).
	 *  This is a gameplay stimulus only; no actual audio is played. */
	float NoiseEmitTimer{0.f};

	UPROPERTY(EditDefaultsOnly, Category = "Perception")
	float NoiseInterval{0.4f};

	UPROPERTY(EditDefaultsOnly, Category = "Perception")
	float NoiseLoudness{1.f};

	UPROPERTY(EditDefaultsOnly, Category = "Perception")
	float NoiseRange{1300.f};
};
