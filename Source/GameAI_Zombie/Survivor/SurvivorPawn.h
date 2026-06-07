// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Common/HealthComponent.h"
#include "Common/StaminaComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/Pawn.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Damage.h"
#include "SurvivorPawn.generated.h"

class UInventoryComponent;
class USteeringComponent;

UCLASS()
class GAMEAI_ZOMBIE_API ASurvivorPawn : public APawn
{
	GENERATED_BODY()

public:
	ASurvivorPawn();

protected:
	virtual void BeginPlay() override;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UInventoryComponent* InventoryComponent;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UHealthComponent* HealthComponent;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UStaminaComponent* StaminaComponent;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	bool bIsRunning{false};
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UFloatingPawnMovement* FloatingPawnMovement;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float RunningSpeed{600.0f};
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float DefaultSpeed{400.0f};
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	USteeringComponent* SteeringComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UAIPerceptionComponent* PerceptionComp;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UAISenseConfig_Sight* SightConfig;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UAISenseConfig_Damage* DamageConfig;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	UAISenseConfig_Hearing* HearingConfig;
	
	UFUNCTION()
	virtual void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	// Logs how long we survived
	UFUNCTION()
	void OnSurvivorDeath();

	// On hit, target the nearest zombie and turn to fight it.
	UFUNCTION()
	void OnSurvivorDamaged();

	// Game time captured at spawn.
	float SpawnTime{0.f};

	// Spawn location, used as the exploration anchor
	FVector SpawnLocation{FVector::ZeroVector};

	// Ground height captured at spawn; the pawn is pinned to this Z
	float GroundZ{0.f};
	bool bGroundZSet{false};

	// Startup spin scan so the sight cone sweeps around at spawn.
	bool bScanning{false};
	float ScanElapsed{0.f};
	float ScanStartYaw{0.f};
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float ScanDuration{1.5f};

	// Houses seen so far (exploration memory)
	TArray<TWeakObjectPtr<AActor>> KnownHouses;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float HouseSenseRadius{1500.f};
	float HouseQueryTimer{0.f};
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float HouseQueryInterval{0.4f};

	// Game time the last new house was discovered.
	float LastHouseDiscoveryTime{0.f};

	// Locations of weapons we saw but didn't take, to re-arm later.
	TArray<FVector> KnownWeaponLocations;

	// Purge zones we've seen, so we can flee them.
	TArray<TWeakObjectPtr<AActor>> KnownPurgeZones;

	// Zone we're committed to leaving, kept until clear.
	mutable TWeakObjectPtr<AActor> LatchedPurgeZone;

public:
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	TArray<FVector> CalculatePath(const FVector& TargetLocation) const;
	void StartRunning();
	void StopRunning();

	bool IsRunning() const;

	// Public getters so BT tasks/services can access components
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }
	UStaminaComponent* GetStaminaComponent() const { return StaminaComponent; }
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }
	UFloatingPawnMovement* GetMovementComponent() const { return FloatingPawnMovement; }
	UAIPerceptionComponent* GetPerceptionComponent() const { return PerceptionComp; }
	USteeringComponent* GetSteeringComponent() const { return SteeringComponent; }

	// Houses seen so far (exploration memory)
	const TArray<TWeakObjectPtr<AActor>>& GetKnownHouses() const { return KnownHouses; }

	// Spawn point and exploration anchor
	FVector GetSpawnLocation() const { return SpawnLocation; }

	// Remembered weapon locations, with a way to forget one once looted. 
	const TArray<FVector>& GetKnownWeaponLocations() const { return KnownWeaponLocations; }
	void ForgetWeaponNear(const FVector& Loc, float Radius = 400.f);

	//Game time of the last new house discovery.
	float GetLastHouseDiscoveryTime() const { return LastHouseDiscoveryTime; }

	// True while doing a spin scan.
	bool IsScanning() const { return bScanning; }

	// Begin a spin scan to sweep the sight cone around. No-op if already scanning.
	void RequestScan();

	// Nearest perceived purge zone we're standing in, else nullptr.
	class APurgeZone* GetActivePurgeZoneDanger() const;

	// Seconds survived since spawn
	float GetSurvivalTime() const { return GetWorld() ? GetWorld()->GetTimeSeconds() - SpawnTime : 0.f; }
	int32 GetKnownHouseCount() const { return KnownHouses.Num(); }
};
