#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FleeFromEnemy.generated.h"

struct FBTFleeMemory
{
	float TimeElapsed = 0.f;
	float StuckCheckTimer = 0.f;
	FVector LastCheckedLocation = FVector::ZeroVector;
	bool bInitialized = false;

	// When wedged, commit to an open escape for a short while instead of snapping back to the refuge.
	float CommitUntil = 0.f;                         // honour CommitTarget while TimeElapsed < this
	FVector CommitTarget = FVector::ZeroVector;
	int32 StuckCount = 0;                            // consecutive stuck detections (escalates the angle)
};

/**
 * Flee task: moves the survivor away from the currently perceived enemy.
 * Picks a point in the opposite direction and sprints toward it.
 */
UCLASS()
class GAMEAI_ZOMBIE_API UBTTask_FleeFromEnemy : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FleeFromEnemy();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTFleeMemory); }

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Flee")
	float FleeDistance = 800.f;

	UPROPERTY(EditAnywhere, Category = "Flee")
	float TimeoutSeconds = 4.f;
};
