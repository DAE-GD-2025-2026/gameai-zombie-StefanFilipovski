#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdateBlackboard.generated.h"

/**
 * BT Service that periodically updates blackboard keys with
 * the survivor's current health, weapon status, and position.
 */
UCLASS()
class GAMEAI_ZOMBIE_API UBTService_UpdateBlackboard : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateBlackboard();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
