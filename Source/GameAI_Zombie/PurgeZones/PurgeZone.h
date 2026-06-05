// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PurgeZone.generated.h"

class UAIPerceptionStimuliSourceComponent;

// Stub, most of the implementation is in BP
UCLASS()
class GAMEAI_ZOMBIE_API APurgeZone : public AActor
{
	GENERATED_BODY()

public:
	friend class AZombieGameMode;

	APurgeZone();
	virtual void Tick(float DeltaTime) override;

	void SetDiameter(float NewDiameter){Diameter = NewDiameter;}
	void SetTimeTillPurge(float NewDiameter){Diameter = NewDiameter;}

	/** Lethal radius (the zone purges everything inside its Diameter). */
	float GetDiameter() const { return Diameter; }
	float GetRadius() const { return Diameter * 0.5f; }
	/** Seconds until this zone detonates, and how long it's been alive. */
	float GetTimeTillPurge() const { return TimeTillPurge; }
	float GetTimePassed() const { return TimePassed; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PurgeZone")
	float Diameter{100};
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PurgeZone")
	float TimeTillPurge{5};
	UPROPERTY(BlueprintReadOnly, Category="PurgeZone")
	float TimePassed{0};

	/** Lets the survivor's AIPerception (sight) detect the hazard so it can flee the blast radius. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception")
	UAIPerceptionStimuliSourceComponent* StimuliSource;

	virtual void BeginPlay() override;
};
