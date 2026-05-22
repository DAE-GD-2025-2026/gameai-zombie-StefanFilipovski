#include "BTTask_FleeFromEnemy.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "Survivor/SurvivorPawn.h"

UBTTask_FleeFromEnemy::UBTTask_FleeFromEnemy()
{
	NodeName = "Flee From Enemy";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_FleeFromEnemy::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTFleeMemory* Memory = reinterpret_cast<FBTFleeMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB) return EBTNodeResult::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EBTNodeResult::Failed;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(Pawn);

	// Get enemy location — prefer live actor, fall back to last known
	FVector EnemyLocation;
	AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	if (Enemy)
	{
		EnemyLocation = Enemy->GetActorLocation();
	}
	else
	{
		EnemyLocation = BB->GetValueAsVector(FName("LastKnownEnemyLocation"));
	}

	// Flee direction = away from enemy
	const FVector MyLocation = Pawn->GetActorLocation();
	FVector FleeDirection = (MyLocation - EnemyLocation).GetSafeNormal2D();

	if (FleeDirection.IsNearlyZero())
	{
		FleeDirection = FMath::VRand();
		FleeDirection.Z = 0.f;
		FleeDirection.Normalize();
	}

	FVector FleeTarget = MyLocation + FleeDirection * FleeDistance;

	// Try to project onto navmesh
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys)
	{
		FNavLocation ProjectedLocation;
		if (NavSys->ProjectPointToNavigation(FleeTarget, ProjectedLocation, FVector(500.f, 500.f, 250.f)))
		{
			FleeTarget = ProjectedLocation.Location;
		}
	}

	// Sprint while fleeing
	if (Survivor)
	{
		Survivor->StartRunning();
	}

	const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(FleeTarget, 50.f);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// Can't path there — try opposite direction or just succeed to re-evaluate
		if (Survivor) Survivor->StopRunning();
		return EBTNodeResult::Succeeded;
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_FleeFromEnemy::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTFleeMemory* Memory = reinterpret_cast<FBTFleeMemory*>(NodeMemory);
	Memory->TimeElapsed += DeltaSeconds;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Timeout
	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		AIC->StopMovement();
		ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
		if (Survivor) Survivor->StopRunning();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (AIC->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
		if (Survivor) Survivor->StopRunning();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
