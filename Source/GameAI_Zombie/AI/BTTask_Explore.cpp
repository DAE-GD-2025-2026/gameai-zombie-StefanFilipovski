#include "BTTask_Explore.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Village/House/House.h"
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

	// Purge zones are tiny (diameter ~100) with a multi-second delay.
	// The survivor is always moving, so we naturally dodge them — no special avoidance needed.

	// --- Check if inventory has space — only visit houses if we can carry items ---
	bool bHasInventorySpace = false;
	if (Survivor)
	{
		UInventoryComponent* Inv = Survivor->GetInventoryComponent();
		if (Inv)
		{
			for (ABaseItem* Slot : Inv->GetInventory())
			{
				if (Slot == nullptr) { bHasInventorySpace = true; break; }
			}
		}
	}

	// Always visit houses — even with full inventory, items respawn and we may find weapons
	{
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

		// If ALL houses visited, reset and start fresh
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
			UE_LOG(LogTemp, Log, TEXT("Explore: All %d houses visited, resetting list"), UnvisitedHouses.Num());
		}

		// Pick the nearest unvisited house — go to it even if far
		AHouse* BestHouse = nullptr;
		float BestDist = MAX_FLT;
		for (AHouse* House : UnvisitedHouses)
		{
			const float Dist = FVector::Dist(Origin, House->GetActorLocation());
			if (Dist > 150.f && Dist < BestDist)
			{
				BestDist = Dist;
				BestHouse = House;
			}
		}

		if (BestHouse)
		{
			FHouseBounds Bounds = BestHouse->GetBounds();
			Target = Bounds.Origin;

			// Don't mark visited yet — only mark when we actually arrive.
			// If the BT interrupts us (item pickup, fight), we'll retry this house next time.
			Memory->TargetHouseLocation = Target;
			Memory->TargetHouseActorLoc = BestHouse->GetActorLocation();
			Memory->bHasPendingHouse = true;

			UE_LOG(LogTemp, Log, TEXT("Explore: Heading toward house at %s (dist %.0f, %d unvisited left)"),
				*Target.ToString(), BestDist, UnvisitedHouses.Num());
		}
		else
		{
			// All houses too close (within 150) — random wander
			Target = Origin + FVector(FMath::RandRange(-ExploreRadius, ExploreRadius),
			                          FMath::RandRange(-ExploreRadius, ExploreRadius), 0.f);
			UE_LOG(LogTemp, Log, TEXT("Explore: No valid house, random wander"));
		}
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

	APawn* Pawn = AIC->GetPawn();

	// --- Stuck detection: if we haven't moved much in 2 seconds, abort and pick new target ---
	if (Pawn)
	{
		const FVector CurrentPos = Pawn->GetActorLocation();
		if (!Memory->bInitialized)
		{
			Memory->LastCheckedLocation = CurrentPos;
			Memory->StuckCheckTimer = 0.f;
			Memory->bInitialized = true;
		}
		else
		{
			Memory->StuckCheckTimer += DeltaSeconds;
			if (Memory->StuckCheckTimer >= 2.0f)
			{
				const float DistMoved = FVector::Dist2D(CurrentPos, Memory->LastCheckedLocation);
				if (DistMoved < 80.f) // Barely moved in 2 seconds — we're stuck
				{
					AIC->StopMovement();

					// Instead of aborting (which just picks another far house we can't reach),
					// do a SHORT random move to physically push through doorways and escape rooms.
					const float RandomAngle = FMath::RandRange(0.f, 360.f);
					const float RandomDist = FMath::RandRange(200.f, 500.f);
					FVector EscapeDir = FVector(FMath::Cos(FMath::DegreesToRadians(RandomAngle)),
					                            FMath::Sin(FMath::DegreesToRadians(RandomAngle)), 0.f);
					FVector EscapeTarget = CurrentPos + EscapeDir * RandomDist;

					UE_LOG(LogTemp, Log, TEXT("Explore: STUCK (moved %.0f in 2s), short escape move toward %.0f deg"),
						DistMoved, RandomAngle);

					AIC->MoveToLocation(EscapeTarget, 30.f);

					// Reset stuck check so we re-evaluate after this short move
					Memory->LastCheckedLocation = CurrentPos;
					Memory->StuckCheckTimer = 0.f;
					return;
				}
				// Reset for next check window
				Memory->LastCheckedLocation = CurrentPos;
				Memory->StuckCheckTimer = 0.f;
			}
		}
	}

	// --- Mark house as visited when we actually arrive near it ---
	if (Pawn && Memory->bHasPendingHouse)
	{
		// Check distance against the house's actual actor location (not Bounds.Origin)
		const float DistToHouse = FVector::Dist2D(Pawn->GetActorLocation(), Memory->TargetHouseActorLoc);
		if (DistToHouse < 500.f)
		{
			// We actually reached the house — now mark it visited
			TArray<AActor*> AllHouses;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHouse::StaticClass(), AllHouses);
			for (AActor* Actor : AllHouses)
			{
				AHouse* House = Cast<AHouse>(Actor);
				if (!House) continue;
				if (FVector::Dist(House->GetActorLocation(), Memory->TargetHouseActorLoc) < 50.f)
				{
					VisitedHouses.Add(House);
					UE_LOG(LogTemp, Log, TEXT("Explore: Arrived at house, marking visited"));
					break;
				}
			}
			Memory->bHasPendingHouse = false;
		}
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
