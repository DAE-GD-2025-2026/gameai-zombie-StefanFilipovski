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

	/** Hysteresis: stay fleeing until the enemy is clearly far/gone, to stop flicker near the trigger range. */
	bool bFleeLatched = false;

	/** Set when an armed flee isn't gaining distance; suppresses flee so we stand and fight instead. */
	bool bFightCommitLatched = false;
	float FleeProgressTimer = 0.f;
	float FleeProgressLastDist = -1.f;
};
