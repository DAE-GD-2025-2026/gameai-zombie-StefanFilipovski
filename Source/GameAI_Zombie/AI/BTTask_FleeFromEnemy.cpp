#include "BTTask_FleeFromEnemy.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/SteeringComponent.h"
#include "Common/HealthComponent.h"
#include "Common/StaminaComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "PurgeZones/PurgeZone.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Zombies/BaseZombie.h"

UBTTask_FleeFromEnemy::UBTTask_FleeFromEnemy()
{
	NodeName = "Flee From Enemy";
	bNotifyTick = true;
}

// The point to run from: the live enemy, or its last known location.
static FVector GetThreatLocation(UBlackboardComponent* BB, const FVector& Fallback)
{
	if (AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy"))))
	{
		return Enemy->GetActorLocation();
	}
	const FVector LastKnown = BB->GetValueAsVector(FName("LastKnownEnemyLocation"));
	return LastKnown.IsZero() ? Fallback : LastKnown;
}

// Project a point onto the navmesh
static bool ProjectFleePoint(UWorld* World, const FVector& In, FVector& Out)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) { Out = In; return true; }
	FNavLocation Loc;
	if (Nav->ProjectPointToNavigation(In, Loc, FVector(400.f, 400.f, 600.f)))
	{
		Out = Loc.Location;
		return FVector::Dist2D(In, Out) < 350.f;
	}
	return false;
}

// Flee target
static FVector ComputeFleeTarget(ASurvivorPawn* Survivor, const FVector& MyLoc, const FVector& ThreatLoc, bool& bOutToHouse)
{
	bOutToHouse = false;
	UWorld* World = Survivor->GetWorld();
	FVector Away = (MyLoc - ThreatLoc).GetSafeNormal2D();
	if (Away.IsNearlyZero()) Away = Survivor->GetActorForwardVector().GetSafeNormal2D();

	// Prefer a remembered house that lies away from the threat AND is reachable on the navmesh.
	AActor* BestHouse = nullptr;
	float BestHouseDist = MAX_FLT;
	for (const TWeakObjectPtr<AActor>& H : Survivor->GetKnownHouses())
	{
		if (!H.IsValid()) continue;
		const FVector HouseLoc = H->GetActorLocation();
		const FVector ToHouse = HouseLoc - MyLoc;
		const float Dist = ToHouse.Size2D();
		if (Dist < 100.f) continue;
		if (!Away.IsNearlyZero() && (ToHouse.GetSafeNormal2D() | Away) < 0.2f) continue; // must be away from threat
		if (FVector::Dist2D(HouseLoc, ThreatLoc) < 700.f) continue;                       // and not on the threat
		if (Dist < BestHouseDist) { BestHouseDist = Dist; BestHouse = H.Get(); }
	}
	if (BestHouse)
	{
		FVector NavPt;
		if (ProjectFleePoint(World, BestHouse->GetActorLocation(), NavPt))
		{
			bOutToHouse = true;
			return NavPt;
		}
	}

	// Center of known houses = the explored region.
	FVector Home = FVector::ZeroVector;
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& H : Survivor->GetKnownHouses())
		if (H.IsValid()) { Home += H->GetActorLocation(); ++Count; }
	const bool bHaveHome = Count > 0;
	if (bHaveHome) Home /= Count;
	const FVector ToHomeDir = bHaveHome ? (Home - MyLoc).GetSafeNormal2D() : FVector::ZeroVector;

	// Fan directions out from straight-away.
	static const float Angles[] = { 0.f, 25.f, -25.f, 50.f, -50.f, 80.f, -80.f, 110.f, -110.f };
	FVector BestTarget = MyLoc + Away * 300.f;
	float BestScore = -MAX_FLT;
	for (float Ang : Angles)
	{
		const FVector Dir = Away.RotateAngleAxis(Ang, FVector::UpVector);
		FVector NavPt;
		if (!ProjectFleePoint(World, MyLoc + Dir * 900.f, NavPt)) continue;
		float Score = FVector::Dist2D(NavPt, ThreatLoc);
		if (bHaveHome) Score += (Dir | ToHomeDir) * 400.f;
		if (Score > BestScore) { BestScore = Score; BestTarget = NavPt; }
	}
	return BestTarget;
}

// A point just outside the purge zone's blast radius, directly away from its centre.
static FVector PurgeEscapeTarget(ASurvivorPawn* Survivor, const FVector& MyLoc, APurgeZone* Zone)
{
	const FVector Centre = Zone->GetActorLocation();
	FVector Out = (MyLoc - Centre).GetSafeNormal2D();
	if (Out.IsNearlyZero()) Out = Survivor->GetActorForwardVector().GetSafeNormal2D();
	// Just step clear of the blast radius.
	return Centre + Out * (Zone->GetRadius() + 150.f);
}

EBTNodeResult::Type UBTTask_FleeFromEnemy::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTFleeMemory* Memory = reinterpret_cast<FBTFleeMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;
	Memory->bInitialized = false;
	Memory->CommitUntil = 0.f;
	Memory->CommitTarget = FVector::ZeroVector;
	Memory->StuckCount = 0;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB) return EBTNodeResult::Failed;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return EBTNodeResult::Failed;

	// Sprint, and stop steering so it doesn't fight path-following.
	Survivor->StartRunning();
	if (Survivor->GetSteeringComponent()) Survivor->GetSteeringComponent()->Stop();

	const FVector MyLoc = Survivor->GetActorLocation();

	//  sprint out of the blast radius.
	if (APurgeZone* Zone = Survivor->GetActivePurgeZoneDanger())
	{
		AIC->MoveToLocation(PurgeEscapeTarget(Survivor, MyLoc, Zone), 50.f);
		UE_LOG(LogTemp, Warning, TEXT("Flee: START - ESCAPING PURGE ZONE (radius %.0f)"), Zone->GetRadius());
		return EBTNodeResult::InProgress;
	}

	const FVector ThreatLoc = GetThreatLocation(BB, MyLoc);
	bool bToHouse = false;
	const FVector FleeTarget = ComputeFleeTarget(Survivor, MyLoc, ThreatLoc, bToHouse);

	UE_LOG(LogTemp, Log, TEXT("Flee: START (threat %.0f units away)%s"),
		FVector::Dist(MyLoc, ThreatLoc), bToHouse ? TEXT(" - toward a safe house") : TEXT(""));

	AIC->MoveToLocation(FleeTarget, 50.f);
	return EBTNodeResult::InProgress;
}

void UBTTask_FleeFromEnemy::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTFleeMemory* Memory = reinterpret_cast<FBTFleeMemory*>(NodeMemory);
	Memory->TimeElapsed += DeltaSeconds;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Survivor->StartRunning();

	// Use items while fleeing
	UInventoryComponent* Inv = Survivor->GetInventoryComponent();

	// Heal if hurt.
	if (UHealthComponent* Health = Survivor->GetHealthComponent())
	{
		const float HpPct = (Health->GetMaxHealth() > 0)
			? static_cast<float>(Health->GetHealth()) / static_cast<float>(Health->GetMaxHealth())
			: 1.f;
		if (HpPct < 0.6f && Inv)
		{
			const TArray<ABaseItem*>& Items = Inv->GetInventory();
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				if (Items[i] && Items[i]->GetItemType() == EItemType::Medkit && Items[i]->GetValue() > 0)
				{
					Inv->UseItem(i);
					if (Items[i] && Items[i]->GetValue() <= 0) Inv->RemoveItem(i);
					UE_LOG(LogTemp, Log, TEXT("Flee: used a medkit while escaping"));
					break;
				}
			}
		}
	}

	// Eat food if stamina is low.
	if (UStaminaComponent* Stamina = Survivor->GetStaminaComponent())
	{
		const float StamPct = (Stamina->GetMaxStamina() > 0.f)
			? Stamina->GetCurrentStamina() / Stamina->GetMaxStamina()
			: 1.f;
		if (StamPct < 0.3f && Inv)
		{
			const TArray<ABaseItem*>& Items = Inv->GetInventory();
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				if (Items[i] && Items[i]->GetItemType() == EItemType::Food && Items[i]->GetValue() > 0)
				{
					Inv->UseItem(i);
					if (Items[i] && Items[i]->GetValue() <= 0) Inv->RemoveItem(i);
					UE_LOG(LogTemp, Log, TEXT("Flee: ate food while escaping (stamina low)"));
					break;
				}
			}
		}
	}

	const FVector MyLoc = Survivor->GetActorLocation();

	// Purge zone overrides: keep sprinting out of the blast radius.
	if (APurgeZone* Zone = Survivor->GetActivePurgeZoneDanger())
	{
		AIC->MoveToLocation(PurgeEscapeTarget(Survivor, MyLoc, Zone), 50.f);
		return;
	}

	// Escaped only when no perceived zombie is still close.
	{
		float NearestZombie = MAX_FLT;
		if (UAIPerceptionComponent* Perc = Survivor->GetPerceptionComponent())
		{
			TArray<AActor*> Perceived;
			Perc->GetCurrentlyPerceivedActors(nullptr, Perceived);
			for (AActor* A : Perceived)
				if (Cast<ABaseZombie>(A))
					NearestZombie = FMath::Min(NearestZombie, FVector::Dist(MyLoc, A->GetActorLocation()));
		}
		if (NearestZombie > 1200.f)
		{
			AIC->StopMovement();
			Survivor->StopRunning();
			BB->ClearValue(FName("TargetEnemy"));
			UE_LOG(LogTemp, Log, TEXT("Flee: escaped - no zombie nearby, stopping"));
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			return;
		}
	}

	// Periodically hand control back so the BT can re-decide.
	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	const FVector ThreatLoc = GetThreatLocation(BB, MyLoc);
	FVector Away = (MyLoc - ThreatLoc).GetSafeNormal2D();
	if (Away.IsNearlyZero()) Away = Survivor->GetActorForwardVector().GetSafeNormal2D();
	bool bToHouse = false;
	const FVector FleeTarget = ComputeFleeTarget(Survivor, MyLoc, ThreatLoc, bToHouse);

	// While committed to a break-out escape, keep heading there instead of snapping back to the refuge.
	if (Memory->TimeElapsed < Memory->CommitUntil)
	{
		AIC->MoveToLocation(Memory->CommitTarget, 50.f);
		return;
	}

	// Stuck? Pick a reachable open escape away from the enemy and commit to it for 2s.
	if (!Memory->bInitialized)
	{
		Memory->LastCheckedLocation = MyLoc;
		Memory->StuckCheckTimer = 0.f;
		Memory->bInitialized = true;
	}
	else
	{
		Memory->StuckCheckTimer += DeltaSeconds;
		if (Memory->StuckCheckTimer >= 1.5f)
		{
			const float Moved = FVector::Dist2D(MyLoc, Memory->LastCheckedLocation);
			Memory->LastCheckedLocation = MyLoc;
			Memory->StuckCheckTimer = 0.f;
			if (Moved < 60.f)
			{
				++Memory->StuckCount;
				// Sweep directions for a reachable navmesh point clear of us.
				static const float Bases[] = { 0.f, 90.f, -90.f, 150.f, -150.f, 45.f, -45.f };
				FVector Escape = FleeTarget;
				for (int32 k = 0; k < UE_ARRAY_COUNT(Bases); ++k)
				{
					const float Ang = Bases[(Memory->StuckCount + k) % UE_ARRAY_COUNT(Bases)];
					const FVector Dir = Away.RotateAngleAxis(Ang, FVector::UpVector);
					FVector Nav;
					if (ProjectFleePoint(Survivor->GetWorld(), MyLoc + Dir * FleeDistance, Nav)
						&& FVector::Dist2D(Nav, MyLoc) > 200.f)
					{
						Escape = Nav;
						break;
					}
				}
				Memory->CommitTarget = Escape;
				Memory->CommitUntil = Memory->TimeElapsed + 2.0f;
				AIC->MoveToLocation(Escape, 50.f);
				UE_LOG(LogTemp, Log, TEXT("Flee: wedged - committing to an open escape for 2s"));
				return;
			}
			Memory->StuckCount = 0;
		}
	}

	// Keep moving to the flee target.
	AIC->MoveToLocation(FleeTarget, 50.f);
}
