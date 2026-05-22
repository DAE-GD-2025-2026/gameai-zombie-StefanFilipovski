#include "BTTask_FleeFromEnemy.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "Survivor/SurvivorPawn.h"
#include "PurgeZones/PurgeZone.h"
#include "Kismet/GameplayStatics.h"

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

	const FVector MyLocation = Pawn->GetActorLocation();
	FVector FleeDirection = FVector::ZeroVector;

	// Combine threats: enemies + purge zones
	AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	if (Enemy)
	{
		FleeDirection += (MyLocation - Enemy->GetActorLocation()).GetSafeNormal2D();
	}
	else
	{
		FVector LastKnown = BB->GetValueAsVector(FName("LastKnownEnemyLocation"));
		if (!LastKnown.IsZero())
		{
			FleeDirection += (MyLocation - LastKnown).GetSafeNormal2D() * 0.5f;
		}
	}

	// Also flee from nearby purge zones
	TArray<AActor*> PurgeZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APurgeZone::StaticClass(), PurgeZones);
	for (AActor* PZ : PurgeZones)
	{
		if (!PZ) continue;
		const float DistToPurge = FVector::Dist(MyLocation, PZ->GetActorLocation());
		if (DistToPurge < 1500.f)
		{
			// Stronger repulsion the closer we are
			FVector AwayDir = (MyLocation - PZ->GetActorLocation()).GetSafeNormal2D();
			FleeDirection += AwayDir * (1500.f / FMath::Max(DistToPurge, 100.f));
		}
	}

	FleeDirection = FleeDirection.GetSafeNormal2D();

	if (FleeDirection.IsNearlyZero())
	{
		FleeDirection = FMath::VRand();
		FleeDirection.Z = 0.f;
		FleeDirection.Normalize();
	}

	// Try multiple flee angles — straight away, then angled left/right
	// This prevents running into walls and circling back
	TArray<FVector> CandidateDirections;
	CandidateDirections.Add(FleeDirection);                                              // Straight away
	CandidateDirections.Add(FleeDirection.RotateAngleAxis(45.f, FVector::UpVector));     // 45 deg left
	CandidateDirections.Add(FleeDirection.RotateAngleAxis(-45.f, FVector::UpVector));    // 45 deg right
	CandidateDirections.Add(FleeDirection.RotateAngleAxis(90.f, FVector::UpVector));     // 90 deg left
	CandidateDirections.Add(FleeDirection.RotateAngleAxis(-90.f, FVector::UpVector));    // 90 deg right

	FVector BestTarget = MyLocation + FleeDirection * FleeDistance;
	float BestScore = -1.f;

	for (const FVector& Dir : CandidateDirections)
	{
		FVector Candidate = MyLocation + Dir * FleeDistance;

		// Score: prefer directions that move us away from threats
		// Use FleeDirection dot product — higher score = more aligned with flee direction
		float Score = FVector::DotProduct(Dir, FleeDirection) * FleeDistance;

		// Try to project onto navmesh — bonus if the point is navigable
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			FNavLocation ProjectedLocation;
			if (NavSys->ProjectPointToNavigation(Candidate, ProjectedLocation, FVector(500.f, 500.f, 250.f)))
			{
				Candidate = ProjectedLocation.Location;
				Score += 200.f; // navigable bonus
			}
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	// Sprint while fleeing
	if (Survivor)
	{
		Survivor->StartRunning();
	}

	const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(BestTarget, 50.f);
	if (Result == EPathFollowingRequestResult::Failed)
	{
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
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());

	// Timeout
	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		AIC->StopMovement();
		if (Survivor) Survivor->StopRunning();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	// Check if we're now far enough from the enemy — can stop fleeing early
	if (BB && Survivor)
	{
		AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
		if (Enemy)
		{
			const float DistToEnemy = FVector::Dist(Survivor->GetActorLocation(), Enemy->GetActorLocation());
			if (DistToEnemy > 1200.f) // Beyond sight radius, safe enough
			{
				AIC->StopMovement();
				Survivor->StopRunning();
				// Clear the enemy since we've escaped
				BB->ClearValue(FName("TargetEnemy"));
				UE_LOG(LogTemp, Log, TEXT("Flee: Escaped enemy (dist %.0f), stopping"), DistToEnemy);
				FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				return;
			}
		}
	}

	if (AIC->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		if (Survivor) Survivor->StopRunning();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
