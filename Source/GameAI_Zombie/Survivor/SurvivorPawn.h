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

	/** Bound to HealthComponent->OnDeath — logs how long we survived. */
	UFUNCTION()
	void OnSurvivorDeath();

	/** Game time (seconds) captured at spawn, used to report survival duration on death. */
	float SpawnTime{0.f};

	/** World location captured at spawn. Exploration anchors here (the initial house cluster is
	 *  always near spawn) so the agent doesn't drift across the whole map. */
	FVector SpawnLocation{FVector::ZeroVector};

	/** Ground height captured at spawn; the pawn is kept at this Z so it can't be pushed
	 *  up onto walls or climb on top of zombies (top-down level is flat). */
	float GroundZ{0.f};
	bool bGroundZSet{false};

	/** Memory of houses we've actually perceived (via sight). Exploration only targets these,
	 *  so the agent discovers rooms through perception rather than a global world query. */
	TArray<TWeakObjectPtr<AActor>> KnownHouses;

	/** Game time the most recent NEW house was discovered. Exploration uses this to widen its search
	 *  radius once the local cluster has gone a while without yielding anything new. */
	float LastHouseDiscoveryTime{0.f};

	/** Purge zones (lethal AoE hazards) we've perceived via sight. The agent flees any it's standing in. */
	TArray<TWeakObjectPtr<AActor>> KnownPurgeZones;

	/** Hysteresis for purge-zone escape: once we commit to leaving a zone we stay committed to that
	 *  zone until we've clearly cleared it, so we don't jitter in/out at the blast-radius boundary. */
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

	/** Houses the survivor has perceived so far (its exploration memory). */
	const TArray<TWeakObjectPtr<AActor>>& GetKnownHouses() const { return KnownHouses; }

	/** Where the survivor spawned — exploration's anchor point. */
	FVector GetSpawnLocation() const { return SpawnLocation; }

	/** Game time when the last new house was discovered (for widening the exploration radius). */
	float GetLastHouseDiscoveryTime() const { return LastHouseDiscoveryTime; }

	/** If the survivor is currently inside (or right at the edge of) a perceived purge zone's blast
	 *  radius, returns that zone (nearest one) so the AI can flee out of it; otherwise nullptr. */
	class APurgeZone* GetActivePurgeZoneDanger() const;

	/** Seconds survived since spawn. */
	float GetSurvivalTime() const { return GetWorld() ? GetWorld()->GetTimeSeconds() - SpawnTime : 0.f; }
	int32 GetKnownHouseCount() const { return KnownHouses.Num(); }
};
