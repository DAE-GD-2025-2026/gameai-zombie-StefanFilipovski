#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_Explore.generated.h"

struct FBTExploreMemory
{
	float TimeElapsed = 0.f;
};

/**
 * Explore task: picks a random point and walks toward it.
 * Fallback behavior when there's nothing else to do.
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

	UPROPERTY(EditAnywhere, Category = "Explore")
	float ExploreRadius = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Explore")
	float TimeoutSeconds = 5.f;
};
