#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_Explore.generated.h"

class ASurvivorPawn;
class AHouse;
class AAIController;

/** Instance memory is unused — Explore keeps its plan on the task object so the BT's frequent
 *  re-executes don't wipe progress. */
struct FBTExploreMemory
{
	uint8 Unused = 0;
};

/**
 * Explore task: knowledge-driven looting as a persistent state machine. Houses come from AIPerception.
 * Heads to the nearest known house not on cooldown, paths inside (trying corner doorways if stalled),
 * lingers so Pickup grabs items, then moves on. Ventures outward when the local cluster is exhausted.
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

	/** After looting (or giving up on) a house, don't re-target it for this long (items respawn). Kept long
	 *  so we push out to fresh houses/clusters instead of cycling back to rooms we just cleared. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float HouseRevisitCooldown = 75.f;

	/** How far we may wander from the home anchor before steering back. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float WanderLeash = 2500.f;

	/** Don't loot a house this close to a perceived threat (would walk us into the horde). */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float EnemyAvoidRadius = 900.f;

	/** While exploring, if a perceived zombie is this close, step around it instead of walking into it. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float EnemyBumpRadius = 420.f;

	/** Once inside a house, linger this long (with no item sensed) before declaring it looted. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float DwellInsideSeconds = 1.2f;

	/** If a house turned up NOTHING to grab, treat it as picked-clean and don't revisit for this long —
	 *  so once our known clusters are empty we venture out to find a fresh one instead of looping them. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float ExhaustedCooldown = 240.f;

	/** When no house is available for this long, strike out to find a fresh cluster. */
	UPROPERTY(EditAnywhere, Category = "Explore")
	float VentureAfter = 5.f;
	UPROPERTY(EditAnywhere, Category = "Explore")
	float VentureDistance = 4500.f;
	UPROPERTY(EditAnywhere, Category = "Explore")
	float VentureMaxDuration = 12.f;

private:
	/** The whole per-tick decision + movement. Run from both Execute and Tick so progress is made
	 *  regardless of which the BT calls. */
	void RunPlan(UBehaviorTreeComponent& OwnerComp, AAIController* AIC, ASurvivorPawn* Survivor);

	/** Issue a MoveTo only if the goal changed or we're not currently moving — avoids restarting
	 *  path-following every time the BT re-executes us (the cause of the entrance stutter). */
	void IssueMove(AAIController* AIC, const FVector& Goal, float Accept);

	void SetActiveHouse(ASurvivorPawn* Survivor, AHouse* House, const FVector& Origin);
	void ClearActive();
	void ResetState();

	void CooldownHouse(AHouse* House, float Seconds = -1.f);
	bool IsHouseOnCooldown(AHouse* House) const;

	FVector GetHomeAnchor(ASurvivorPawn* Survivor) const;
	bool GetThreatLocation(UBehaviorTreeComponent& OwnerComp, FVector& OutLoc) const;
	AHouse* PickTargetHouse(ASurvivorPawn* Survivor, const FVector& From, bool bThreat, const FVector& ThreatLoc) const;
	bool IsInsideHouse(const FVector& Loc, AHouse* House) const;

	/** Nearest zombie we currently PERCEIVE (via AIPerception); fills OutLoc/OutDist, false if none. */
	bool NearestPerceivedEnemy(ASurvivorPawn* Survivor, FVector& OutLoc, float& OutDist) const;

	// --- Persistent plan state (single survivor, so task-object members are safe; reset per PIE run) ---
	TWeakObjectPtr<UWorld> LastWorld;

	TMap<TWeakObjectPtr<AActor>, float> HouseCooldownUntil;

	TWeakObjectPtr<AActor> ActiveHouse;
	FVector InteriorTarget = FVector::ZeroVector;  // the house centre — where we loot from
	FVector CurrentGoal = FVector::ZeroVector;     // where we're trying to get (interior or a corner door)
	int32 CornerTry = -1;                          // -1 = aiming at interior; 0..3 = trying corner doors
	float StuckTimer = 0.f;
	FVector LastPos = FVector::ZeroVector;
	float DwellTimer = 0.f;
	float InsideTimer = 0.f;       // time spent inside the footprint while trying to reach the centre
	bool bStoppedInside = false;
	bool bSawItemThisVisit = false; // did this house have anything to grab? (else mark it picked-clean)

	FVector LastIssuedGoal = FVector::ZeroVector;   // for idempotent movement
	bool bLastIssuedValid = false;

	// Venture-to-new-cluster
	bool bVenturing = false;
	float VentureStartIdle = -1.f;                  // when we became idle with no available house
	float VentureEndTime = 0.f;
	int32 KnownCountAtVenture = 0;
	FVector VentureTarget = FVector::ZeroVector;

	// Threat-avoid throttle
	bool bAvoiding = false;
	float AvoidReissueTimer = 0.f;

	// Enemy bump-avoidance throttle (step around perceived zombies while exploring)
	bool bBumpAvoiding = false;
	float BumpReissueTimer = 0.f;

	// Look-around scan: step the facing through sectors and dwell on each so the sight sense can sample it.
	bool bScanHolding = false;     // true = holding a sector; false = turning to the next
	float ScanTargetYaw = 0.f;     // world-yaw of the sector being faced
	float ScanHoldUntil = 0.f;     // hold the sector until this time
};
