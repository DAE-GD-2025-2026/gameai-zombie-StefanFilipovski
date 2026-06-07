#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FightEnemy.generated.h"

struct FBTFightMemory
{
	float TimeElapsed = 0.f;
	float FireCooldown = 0.f;  // time until next allowed shot
};

/**
 * Fight task: moves toward the target enemy and uses the equipped weapon.
 * Rotates to face the enemy before shooting.
 */
UCLASS()
class GAMEAI_ZOMBIE_API UBTTask_FightEnemy : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FightEnemy();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTFightMemory); }

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Fight")
	float AttackRange = 400.f;

	UPROPERTY(EditAnywhere, Category = "Fight")
	float TimeoutSeconds = 8.f;

	// Seconds between shots while locked on.
	UPROPERTY(EditAnywhere, Category = "Fight")
	float FireInterval = 0.4f;
};
