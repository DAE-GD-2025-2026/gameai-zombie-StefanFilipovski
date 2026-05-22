#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_Explore.generated.h"

struct FBTExploreMemory
{
	float TimeElapsed = 0.f;
};

/**
 * Explore task: cycles through houses (item spawns) and avoids purge zones.
 * Tracks visited houses to prevent ping-ponging.
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
	float ExploreRadius = 3000.f;

	UPROPERTY(EditAnywhere, Category = "Explore")
	float TimeoutSeconds = 6.f;

private:
	/** Houses we've recently visited — skip these to avoid ping-ponging */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> VisitedHouses;

	/** Index for round-robin house selection */
	int32 HouseIndex = 0;
};
