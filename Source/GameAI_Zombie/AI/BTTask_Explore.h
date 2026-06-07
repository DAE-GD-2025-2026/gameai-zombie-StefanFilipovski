#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_Explore.generated.h"

class ASurvivorPawn;
class AHouse;
class AAIController;

// Unused: the plan lives on the task object so re-executes don't wipe it.
struct FBTExploreMemory
{
	uint8 Unused = 0;
};

// Explore: head to the nearest known house not on cooldown, loot it, then move on. Houses come from AIPerception.
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
	float HouseRevisitCooldown = 75.f;

	// How far we may wander from the home anchor before steering back
	UPROPERTY(EditAnywhere, Category = "Explore")
	float WanderLeash = 2500.f;

	// Don't loot a house this close to a perceived threat
	UPROPERTY(EditAnywhere, Category = "Explore")
	float EnemyAvoidRadius = 900.f;

	
	UPROPERTY(EditAnywhere, Category = "Explore")
	float EnemyBumpRadius = 420.f;

	// Once inside a house, linger this long
	UPROPERTY(EditAnywhere, Category = "Explore")
	float DwellInsideSeconds = 1.2f;

	// Empty houses get a longer cooldown so we move to fresh clusters.
	UPROPERTY(EditAnywhere, Category = "Explore")
	float ExhaustedCooldown = 240.f;

	// When no house is available for this long, strike out to find a fresh cluster.
	UPROPERTY(EditAnywhere, Category = "Explore")
	float VentureAfter = 5.f;
	UPROPERTY(EditAnywhere, Category = "Explore")
	float VentureDistance = 4500.f;
	UPROPERTY(EditAnywhere, Category = "Explore")
	float VentureMaxDuration = 12.f;

private:
	// Per-tick decision and movement, run from both Execute and Tick.
	void RunPlan(UBehaviorTreeComponent& OwnerComp, AAIController* AIC, ASurvivorPawn* Survivor);

	//Issue MoveTo only when the goal changes, to avoid restarting path-following.
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

	
	bool NearestPerceivedEnemy(ASurvivorPawn* Survivor, FVector& OutLoc, float& OutDist) const;

	// Persistent plan state
	TWeakObjectPtr<UWorld> LastWorld;

	TMap<TWeakObjectPtr<AActor>, float> HouseCooldownUntil;

	TWeakObjectPtr<AActor> ActiveHouse;
	FVector InteriorTarget = FVector::ZeroVector;  // house centre
	FVector CurrentGoal = FVector::ZeroVector;     
	int32 CornerTry = -1;                          
	float StuckTimer = 0.f;
	FVector LastPos = FVector::ZeroVector;
	float DwellTimer = 0.f;
	float InsideTimer = 0.f;       
	bool bStoppedInside = false;
	bool bSawItemThisVisit = false; 

	FVector LastIssuedGoal = FVector::ZeroVector;   
	bool bLastIssuedValid = false;

	// Venture-to-new-cluster
	bool bVenturing = false;
	float VentureStartIdle = -1.f;                  
	float VentureEndTime = 0.f;
	int32 KnownCountAtVenture = 0;
	FVector VentureTarget = FVector::ZeroVector;

	// Threat-avoid throttle
	bool bAvoiding = false;
	float AvoidReissueTimer = 0.f;

	// Enemy bump-avoidance throttle
	bool bBumpAvoiding = false;
	float BumpReissueTimer = 0.f;

	// Look-around scan state.
	bool bScanHolding = false;     
	float ScanTargetYaw = 0.f;     
	float ScanHoldUntil = 0.f;     
};
