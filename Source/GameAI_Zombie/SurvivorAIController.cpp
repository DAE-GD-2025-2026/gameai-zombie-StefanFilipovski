#include "SurvivorAIController.h"
#include "Components/GameFrameworkComponentManager.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

ASurvivorAIController::ASurvivorAIController()
{
	 //the BT handles 
	PrimaryActorTick.bCanEverTick = false;
}

void ASurvivorAIController::BeginPlay()
{
	Super::BeginPlay();

	// Register with the component manager
	if (UGameFrameworkComponentManager* ComponentManager =
		GetGameInstance()->GetSubsystem<UGameFrameworkComponentManager>())
	{
		ComponentManager->AddReceiver(this);
	}

	// Start the Behavior Tree
	if (BehaviorTreeAsset)
	{
		// UseBlackboard creates (or reuses) a BB component from the BT's linked BB asset
		UBlackboardComponent* BB = nullptr;
		UseBlackboard(BehaviorTreeAsset->BlackboardAsset, BB);
		RunBehaviorTree(BehaviorTreeAsset);

		UE_LOG(LogTemp, Log, TEXT("SurvivorAI: Behavior Tree started successfully."));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SurvivorAI: No BehaviorTreeAsset assigned!"));
	}
}

UBlackboardComponent* ASurvivorAIController::GetBB() const
{
	return Blackboard;
}

