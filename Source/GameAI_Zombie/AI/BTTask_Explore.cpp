#include "BTTask_Explore.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "Survivor/SurvivorPawn.h"

UBTTask_Explore::UBTTask_Explore()
{
	NodeName = "Explore";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_Explore::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTExploreMemory* Memory = reinterpret_cast<FBTExploreMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) return EBTNodeResult::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EBTNodeResult::Failed;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(Pawn);
	if (Survivor)
	{
		Survivor->StopRunning();
	}

	const FVector Origin = Pawn->GetActorLocation();
	FVector Target = Origin;

	// Try navmesh first
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys)
	{
		FNavLocation RandomPoint;
		if (NavSys->GetRandomReachablePointInRadius(Origin, ExploreRadius, RandomPoint))
		{
			Target = RandomPoint.Location;
		}
		else
		{
			// Navmesh query failed — use random offset
			Target = Origin + FVector(FMath::RandRange(-ExploreRadius, ExploreRadius),
			                          FMath::RandRange(-ExploreRadius, ExploreRadius), 0.f);
		}
	}
	else
	{
		Target = Origin + FVector(FMath::RandRange(-ExploreRadius, ExploreRadius),
		                          FMath::RandRange(-ExploreRadius, ExploreRadius), 0.f);
	}

	const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(Target, 50.f);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// Movement request failed immediately — don't get stuck
		return EBTNodeResult::Succeeded; // succeed so we try another explore next tick
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_Explore::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTExploreMemory* Memory = reinterpret_cast<FBTExploreMemory*>(NodeMemory);
	Memory->TimeElapsed += DeltaSeconds;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Timeout — don't get stuck
	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		AIC->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (AIC->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
