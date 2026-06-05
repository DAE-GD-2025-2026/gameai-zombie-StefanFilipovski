#include "BTTask_Explore.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Village/House/House.h"
#include "Common/SteeringComponent.h"

UBTTask_Explore::UBTTask_Explore()
{
	NodeName = "Explore";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_Explore::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTExploreMemory* Memory = reinterpret_cast<FBTExploreMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;
	Memory->bInitialized = false;
	Memory->bHasPendingHouse = false;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) return EBTNodeResult::Failed;

	APawn* Pawn = AIC->GetPawn();
	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(Pawn);
	if (!Survivor) return EBTNodeResult::Failed;

	Survivor->StopRunning();

	const FVector Origin = Survivor->GetActorLocation();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// Drop stale cooldown entries.
	for (auto It = HouseCooldownUntil.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid()) It.RemoveCurrent();
	}

	// Pick the nearest PERCEIVED house that isn't on cooldown.
	AHouse* BestHouse = nullptr;
	float BestDist = MAX_FLT;
	for (const TWeakObjectPtr<AActor>& Known : Survivor->GetKnownHouses())
	{
		AHouse* House = Cast<AHouse>(Known.Get());
		if (!House) continue;

		const float* Until = HouseCooldownUntil.Find(House);
		if (Until && Now < *Until) continue; // recently visited

		const float Dist = FVector::Dist(Origin, House->GetActorLocation());
		if (Dist < BestDist) { BestDist = Dist; BestHouse = House; }
	}

	USteeringComponent* Steering = Survivor->GetSteeringComponent();

	if (BestHouse)
	{
		// Head to a remembered house via path-following (handles room interiors).
		const FHouseBounds Bounds = BestHouse->GetBounds();
		Memory->TargetHouseLocation = Bounds.Origin;
		Memory->TargetHouseActorLoc = BestHouse->GetActorLocation();
		Memory->bHasPendingHouse = true;
		Memory->bWandering = false;

		if (Steering) Steering->Stop(); // path-following drives now
		AIC->MoveToLocation(Memory->TargetHouseLocation, 50.f);

		UE_LOG(LogTemp, Log, TEXT("Explore: heading to known house (dist %.0f, %d known)"),
			BestDist, Survivor->GetKnownHouses().Num());
	}
	else
	{
		// No house available right now (none known yet, or all on cooldown). Wander to discover/
		// revisit — but stay leashed to the home anchor so we don't drift off across the map.
		Memory->bWandering = true;
		const FVector Home = GetHomeAnchor(Survivor);
		Memory->bReturningHome = (FVector::Dist2D(Origin, Home) > GetEffectiveLeash(Survivor));

		if (Memory->bReturningHome)
		{
			if (Steering) Steering->Stop();
			AIC->MoveToLocation(Home, 200.f); // navmesh routes us back to the cluster
			UE_LOG(LogTemp, Log, TEXT("Explore: too far from cluster (%.0f), heading back"),
				FVector::Dist2D(Origin, Home));
		}
		else
		{
			AIC->StopMovement(); // steering drives now
			if (Steering)
			{
				Steering->SetObstacleAvoidanceEnabled(true);
				Steering->SetSeparationEnabled(false);
				Steering->WanderAround();
			}
			UE_LOG(LogTemp, Log, TEXT("Explore: wandering near cluster to discover/revisit"));
		}
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

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());

	// --- Wander mode: keep roaming; timeout re-evaluates (we may have discovered a house). ---
	if (Memory->bWandering)
	{
		if (Survivor)
		{
			USteeringComponent* Steering = Survivor->GetSteeringComponent();
			const FVector Home = GetHomeAnchor(Survivor);
			const float DistHome = FVector::Dist2D(Survivor->GetActorLocation(), Home);
			const float Leash = GetEffectiveLeash(Survivor);

			// Hysteresis: once we exceed the leash, return until we're well back inside it.
			const bool bWasReturning = Memory->bReturningHome;
			if (Memory->bReturningHome) { if (DistHome < Leash * 0.6f) Memory->bReturningHome = false; }
			else                        { if (DistHome > Leash)        Memory->bReturningHome = true;  }

			if (Memory->bReturningHome)
			{
				if (!bWasReturning) // just crossed the leash — issue one path back home
				{
					if (Steering) Steering->Stop();
					AIC->MoveToLocation(Home, 200.f);
				}
				// else: let path-following carry us home
			}
			else if (Steering)
			{
				Steering->WanderAround();
			}
		}
		if (Memory->TimeElapsed >= TimeoutSeconds)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	// --- House mode (path-following) ---
	if (Survivor)
	{
		// Stuck recovery: if we've barely moved in 2s the navmesh route to this house is blocked
		// (e.g. arrived in a corner short of the target). Put it on cooldown and re-plan instead of
		// jittering — navmesh handles the actual cornering, so no random nudge is needed any more.
		const FVector CurrentPos = Survivor->GetActorLocation();
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
				if (DistMoved < 80.f)
				{
					if (Memory->bHasPendingHouse) CooldownHouseNear(Survivor, Memory->TargetHouseActorLoc);
					AIC->StopMovement();
					UE_LOG(LogTemp, Log, TEXT("Explore: stuck near target (moved %.0f in 2s), re-planning"), DistMoved);
					FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
					return;
				}
				Memory->LastCheckedLocation = CurrentPos;
				Memory->StuckCheckTimer = 0.f;
			}
		}

		// Reached the house: put it on cooldown (remembered as recently checked) and move on.
		if (Memory->bHasPendingHouse)
		{
			const float DistToHouse = FVector::Dist2D(CurrentPos, Memory->TargetHouseActorLoc);
			if (DistToHouse < 350.f)
			{
				CooldownHouseNear(Survivor, Memory->TargetHouseActorLoc);
				Memory->bHasPendingHouse = false;
				UE_LOG(LogTemp, Log, TEXT("Explore: reached house, on cooldown now"));
				AIC->StopMovement();
				FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				return;
			}
		}
	}

	// Gave up (timed out or path stopped/failed) before reaching: cooldown the house anyway so we
	// don't loop on one we can't path to, then re-plan.
	if (Memory->TimeElapsed >= TimeoutSeconds || AIC->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		if (Memory->bHasPendingHouse) CooldownHouseNear(Survivor, Memory->TargetHouseActorLoc);
		AIC->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

FVector UBTTask_Explore::GetHomeAnchor(ASurvivorPawn* Survivor) const
{
	if (!Survivor) return FVector::ZeroVector;

	// Anchor on the centroid of the houses we know — that's the cluster we want to stay near and
	// keep looting. Before we've found any, fall back to the spawn point (the cluster is near it).
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& H : Survivor->GetKnownHouses())
	{
		if (H.IsValid()) { Sum += H->GetActorLocation(); ++Count; }
	}
	return (Count > 0) ? (Sum / Count) : Survivor->GetSpawnLocation();
}

float UBTTask_Explore::GetEffectiveLeash(ASurvivorPawn* Survivor) const
{
	if (!Survivor || !GetWorld()) return WanderLeash;

	// Stay tight while the local cluster keeps yielding new houses; once it goes quiet for a while,
	// widen the search so the agent rolls out to discover a fresh cluster. Discovering a new house
	// resets the timer (in the survivor), which collapses the leash back to base around the new area.
	const float SinceDiscovery = GetWorld()->GetTimeSeconds() - Survivor->GetLastHouseDiscoveryTime();
	if (SinceDiscovery <= ClusterExhaustedAfter) return WanderLeash;
	return FMath::Min(WanderLeash + (SinceDiscovery - ClusterExhaustedAfter) * LeashGrowthPerSec, MaxWanderLeash);
}

void UBTTask_Explore::CooldownHouseNear(ASurvivorPawn* Survivor, const FVector& HouseActorLoc)
{
	if (!Survivor || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	for (const TWeakObjectPtr<AActor>& Known : Survivor->GetKnownHouses())
	{
		if (Known.IsValid() && FVector::Dist(Known->GetActorLocation(), HouseActorLoc) < 50.f)
		{
			HouseCooldownUntil.Add(Known, Now + HouseRevisitCooldown);
			break;
		}
	}
}
