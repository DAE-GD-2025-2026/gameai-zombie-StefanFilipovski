#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_UseConsumable.generated.h"

/**
 * Uses the best consumable from inventory:
 *  - Medkit if health < 70%
 *  - Food if stamina < 40%
 * Instant task - fires and finishes immediately.
 */
UCLASS()
class GAMEAI_ZOMBIE_API UBTTask_UseConsumable : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_UseConsumable();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
