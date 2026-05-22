#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PickupItem.generated.h"

struct FBTPickupMemory
{
	float TimeElapsed = 0.f;
};

/**
 * Pickup task: moves toward the target item and grabs it into the first
 * empty inventory slot.
 */
UCLASS()
class GAMEAI_ZOMBIE_API UBTTask_PickupItem : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PickupItem();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTPickupMemory); }

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Pickup")
	float TimeoutSeconds = 6.f;
};
