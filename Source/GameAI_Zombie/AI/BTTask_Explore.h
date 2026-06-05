#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_Explore.generated.h"

class ASurvivorPawn;

struct FBTExploreMemory
{
	float TimeElapsed = 0.f;
	float StuckCheckTimer = 0.f;
	FVector LastCheckedLocation = FVector::ZeroVector;
	FVector TargetHouseLocation = FVector::ZeroVector;   // Bounds.Origin — where we path to
	FVector TargetHouseActorLoc = FVector::ZeroVector;    // House actor location — for arrival/cooldown matching
	bool bInitialized = false;
	bool bHasPendingHouse = false;
	bool bWandering = false;                              // true when roaming to discover new houses
	bool bReturningHome = false;                          // wander sub-state: pathing back to the spawn anchor
};

/**
 * Explore task: perception-driven exploration with memory.
 * The agent heads to houses it has actually PERCEIVED (remembered in the survivor), and
 * WANDERS (steering) to discover new ones when none are known/available. No world queries.
 */
UCLASS()
class GAMEAI_ZOMBIE_API UBTTask_Explore : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_Explore();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTExploreMemory); }

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Explore")
	float TimeoutSeconds = 6.f;

	/** After visiting (or stalling on) a house, don't re-target it for this long (items respawn). */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float HouseRevisitCooldown = 30.f;

	/** Base distance the agent may wander from its home anchor before it's steered back. Keeps
	 *  discovery near the spawn cluster (where houses always are) instead of drifting off. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float WanderLeash = 2500.f;

	/** Once this many seconds pass without discovering a NEW house, the leash starts to grow so the
	 *  agent ventures out to find a fresh cluster instead of looping an exhausted one. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float ClusterExhaustedAfter = 20.f;

	/** How fast the leash grows (units/sec) once the cluster is considered exhausted, and its cap. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float LeashGrowthPerSec = 150.f;
	UPROPERTY(EditAnywhere, Category = "Explore")
	float MaxWanderLeash = 8000.f;

private:
	/** Put the known house nearest HouseActorLoc on cooldown (so we don't immediately re-target it). */
	void CooldownHouseNear(ASurvivorPawn* Survivor, const FVector& HouseActorLoc);

	/** The anchor exploration stays near: centroid of known houses, or spawn if none known yet. */
	FVector GetHomeAnchor(ASurvivorPawn* Survivor) const;

	/** Current wander leash: base, grown over time when the local cluster stops yielding new houses. */
	float GetEffectiveLeash(ASurvivorPawn* Survivor) const;

	/** Per-house game-time (seconds) until the house may be targeted again. */
	TMap<TWeakObjectPtr<AActor>, float> HouseCooldownUntil;
};
