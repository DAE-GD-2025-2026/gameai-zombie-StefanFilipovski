#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SurvivorAIController.generated.h"

UCLASS()
class GAMEAI_ZOMBIE_API ASurvivorAIController : public AAIController
{
	GENERATED_BODY()

public:
	ASurvivorAIController();

protected:
	virtual void BeginPlay() override;

	
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	UBehaviorTree* BehaviorTreeAsset;

public:
	
	UBlackboardComponent* GetBB() const;
};
