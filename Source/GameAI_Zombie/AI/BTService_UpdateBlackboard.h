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

private:
	/** Timer for periodic debug status log */
	float DebugLogTimer = 0.f;

	/** Hysteresis for the flee decision: once we start fleeing we stay committed until the
	 *  enemy is clearly far/gone, instead of flickering when a chaser hovers near the trigger range. */
	bool bFleeLatched = false;
};
