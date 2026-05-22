#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FleeFromEnemy.generated.h"

struct FBTFleeMemory
{
	float TimeElapsed = 0.f;
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
