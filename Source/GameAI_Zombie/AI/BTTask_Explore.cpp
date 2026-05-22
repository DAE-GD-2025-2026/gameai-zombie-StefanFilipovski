#include "BTTask_Explore.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "Survivor/SurvivorPawn.h"
#include "Village/House/House.h"
#include "PurgeZones/PurgeZone.h"
#include "Kismet/GameplayStatics.h"

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

	// --- Avoid purge zones: check if any purge zone is near us ---
	TArray<AActor*> PurgeZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APurgeZone::StaticClass(), PurgeZones);

	FVector PurgeAvoidance = FVector::ZeroVector;
	for (AActor* PZ : PurgeZones)
	{
		if (!PZ) continue;
		const float DistToPurge = FVector::Dist(Origin, PZ->GetActorLocation());
		if (DistToPurge < 1500.f) // purge zone is dangerously close
		{
			// Add repulsion force away from purge zone
			FVector AwayDir = (Origin - PZ->GetActorLocation()).GetSafeNormal2D();
			PurgeAvoidance += AwayDir * (1500.f - DistToPurge);
		}
	}

	if (!PurgeAvoidance.IsNearlyZero())
	{
		// Purge zone nearby — flee from it instead of exploring houses
		PurgeAvoidance.Normalize();
		Target = Origin + PurgeAvoidance * 1200.f;

		if (Survivor) Survivor->StartRunning(); // sprint away from purge

		UE_LOG(LogTemp, Log, TEXT("Explore: FLEEING from purge zone!"));

		const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(Target, 50.f);
		if (Result == EPathFollowingRequestResult::Failed)
		{
			return EBTNodeResult::Succeeded;
		}
		return EBTNodeResult::InProgress;
	}

	// --- Find houses to explore, avoiding recently visited ones ---
	TArray<AActor*> AllHouses;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHouse::StaticClass(), AllHouses);

	// Clean up stale visited entries
	VisitedHouses.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr) { return !Ptr.IsValid(); });

	// Build list of unvisited houses
	TArray<AHouse*> UnvisitedHouses;
	for (AActor* Actor : AllHouses)
	{
		AHouse* House = Cast<AHouse>(Actor);
		if (!House) continue;

		bool bVisited = false;
		for (const TWeakObjectPtr<AActor>& Visited : VisitedHouses)
		{
			if (Visited.Get() == House)
			{
				bVisited = true;
				break;
			}
		}
		if (!bVisited)
		{
			UnvisitedHouses.Add(House);
		}
	}

	// If all visited, clear the list and start fresh
	if (UnvisitedHouses.Num() == 0)
	{
		VisitedHouses.Empty();
		for (AActor* Actor : AllHouses)
		{
			if (AHouse* House = Cast<AHouse>(Actor))
			{
				UnvisitedHouses.Add(House);
			}
		}
	}

	// Pick the nearest unvisited house
	AHouse* BestHouse = nullptr;
	float BestDist = MAX_FLT;
	for (AHouse* House : UnvisitedHouses)
	{
		const float Dist = FVector::Dist(Origin, House->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestHouse = House;
		}
	}

	if (BestHouse)
	{
		FHouseBounds Bounds = BestHouse->GetBounds();
		Target = Bounds.Origin;

		// Mark as visited
		VisitedHouses.Add(BestHouse);

		UE_LOG(LogTemp, Log, TEXT("Explore: Heading toward house at %s (dist %.0f, %d unvisited left)"),
			*Target.ToString(), BestDist, UnvisitedHouses.Num() - 1);
	}
	else
	{
		// No houses at all — random wander
		Target = Origin + FVector(FMath::RandRange(-ExploreRadius, ExploreRadius),
		                          FMath::RandRange(-ExploreRadius, ExploreRadius), 0.f);
	}

	const EPathFollowingRequestResult::Type Result = AIC->MoveToLocation(Target, 50.f);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		return EBTNodeResult::Succeeded;
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
