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

UBTTask_FleeFromEnemy::UBTTask_FleeFromEnemy()
{
	NodeName = "Flee From Enemy";
	bNotifyTick = true;
}

// The point we run away from: the live enemy, or its last known location (both from AIPerception).
static FVector GetThreatLocation(UBlackboardComponent* BB, const FVector& Fallback)
{
	if (AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy"))))
	{
		return Enemy->GetActorLocation();
	}
	const FVector LastKnown = BB->GetValueAsVector(FName("LastKnownEnemyLocation"));
	return LastKnown.IsZero() ? Fallback : LastKnown;
}

// Decide which way to run. Normally straight away from the threat, but if we've fled far outside
// our explored region (the centroid of remembered houses), we add a sideways component that curves
// back toward that region. This makes the agent KITE/ORBIT around the horde and stay inside the
// playable map, instead of beelining in a dead-straight line off the level edge.
static FVector ComputeFleeDir(ASurvivorPawn* Survivor, const FVector& MyLoc, const FVector& ThreatLoc)
{
	FVector Away = (MyLoc - ThreatLoc).GetSafeNormal2D();
	if (Away.IsNearlyZero()) Away = Survivor->GetActorForwardVector().GetSafeNormal2D();

	// Centroid of remembered houses = the part of the map we know and where loot respawns.
	FVector Home = FVector::ZeroVector;
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& H : Survivor->GetKnownHouses())
	{
		if (H.IsValid()) { Home += H->GetActorLocation(); ++Count; }
	}
	if (Count > 0) Home /= Count;

	const FVector ToHome = Home - MyLoc;
	if (Count > 0 && ToHome.Size2D() > 2000.f)
	{
		// Turn perpendicular to "away", toward home, so our path arcs back into the map.
		FVector Perp = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal2D();
		if ((Perp | ToHome.GetSafeNormal2D()) < 0.f) Perp = -Perp;
		const FVector Dir = (Away + Perp * 0.7f).GetSafeNormal2D();
		// Only accept the curve if it's still generally away from the threat (never turn into it).
		if (!Dir.IsNearlyZero() && (Dir | Away) > 0.f) return Dir;
	}
	return Away;
}

// Where to flee TO. Prefer a remembered house that lies AWAY from the threat — fleeing toward a
// refuge means that once we shake the enemy we're standing in a house we can loot to re-arm/heal,
// instead of running into empty map until we die. Falls back to a kiting point straight away when
// no safe house fits (e.g. the only houses are back through the horde).
static FVector ComputeFleeTarget(ASurvivorPawn* Survivor, const FVector& MyLoc, const FVector& ThreatLoc, bool& bOutToHouse)
{
	bOutToHouse = false;
	const FVector Away = (MyLoc - ThreatLoc).GetSafeNormal2D();

	AActor* BestHouse = nullptr;
	float BestDist = MAX_FLT;
	for (const TWeakObjectPtr<AActor>& H : Survivor->GetKnownHouses())
	{
		if (!H.IsValid()) continue;
		const FVector HouseLoc = H->GetActorLocation();
		const FVector ToHouse = HouseLoc - MyLoc;
		const float Dist = ToHouse.Size2D();
		if (Dist < 100.f) continue;
		// Must be generally away from the threat (don't route us toward or past the enemy)...
		if (!Away.IsNearlyZero() && (ToHouse.GetSafeNormal2D() | Away) < 0.2f) continue;
		// ...and not sitting right on top of the threat.
		if (FVector::Dist2D(HouseLoc, ThreatLoc) < 700.f) continue;
		if (Dist < BestDist) { BestDist = Dist; BestHouse = H.Get(); }
	}

	if (BestHouse)
	{
		bOutToHouse = true;
		return BestHouse->GetActorLocation();
	}
	return MyLoc + ComputeFleeDir(Survivor, MyLoc, ThreatLoc) * 800.f;
}

// A point just outside the purge zone's blast radius, directly away from its centre.
static FVector PurgeEscapeTarget(ASurvivorPawn* Survivor, const FVector& MyLoc, APurgeZone* Zone)
{
	const FVector Centre = Zone->GetActorLocation();
	FVector Out = (MyLoc - Centre).GetSafeNormal2D();
	if (Out.IsNearlyZero()) Out = Survivor->GetActorForwardVector().GetSafeNormal2D();
	// Aim past the latch-release distance (radius+250) so we actually clear the zone and stop fleeing.
	return Centre + Out * (Zone->GetRadius() + 350.f);
}

EBTNodeResult::Type UBTTask_FleeFromEnemy::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTFleeMemory* Memory = reinterpret_cast<FBTFleeMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;
	Memory->bInitialized = false;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB) return EBTNodeResult::Failed;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return EBTNodeResult::Failed;

	// Commit to sprinting and run straight away from the threat. Steering is stopped so it
	// doesn't fight the path-following.
	Survivor->StartRunning();
	if (Survivor->GetSteeringComponent()) Survivor->GetSteeringComponent()->Stop();

	const FVector MyLoc = Survivor->GetActorLocation();

	// Lethal purge zone takes precedence over everything: sprint straight out of the blast radius.
	if (APurgeZone* Zone = Survivor->GetActivePurgeZoneDanger())
	{
		AIC->MoveToLocation(PurgeEscapeTarget(Survivor, MyLoc, Zone), 50.f);
		UE_LOG(LogTemp, Warning, TEXT("Flee: START — ESCAPING PURGE ZONE (radius %.0f)"), Zone->GetRadius());
		return EBTNodeResult::InProgress;
	}

	const FVector ThreatLoc = GetThreatLocation(BB, MyLoc);
	bool bToHouse = false;
	const FVector FleeTarget = ComputeFleeTarget(Survivor, MyLoc, ThreatLoc, bToHouse);

	UE_LOG(LogTemp, Log, TEXT("Flee: START (threat %.0f units away)%s"),
		FVector::Dist(MyLoc, ThreatLoc), bToHouse ? TEXT(" — toward a safe house") : TEXT(""));

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

	// Keep sprinting the whole time (StaminaComponent auto-stops at 0, so this uses all of it).
	Survivor->StartRunning();

	// Consume items WHILE fleeing (instant use, no need to stop). Flee outranks the Heal/Food
	// branches, so without this we'd die mid-escape holding a medkit, or run out of stamina with
	// food in the bag and get caught.
	UInventoryComponent* Inv = Survivor->GetInventoryComponent();

	// Heal if hurt and we have a medkit.
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

	// Refuel stamina with food if low, so we can keep sprinting away.
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

	// Lethal purge zone overrides the enemy-flee logic: keep sprinting out of the blast radius.
	if (APurgeZone* Zone = Survivor->GetActivePurgeZoneDanger())
	{
		AIC->MoveToLocation(PurgeEscapeTarget(Survivor, MyLoc, Zone), 50.f);
		return;
	}

	// Escaped far enough? Stop fleeing.
	if (AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy"))))
	{
		if (FVector::Dist(MyLoc, Enemy->GetActorLocation()) > 1200.f)
		{
			AIC->StopMovement();
			Survivor->StopRunning();
			BB->ClearValue(FName("TargetEnemy"));
			UE_LOG(LogTemp, Log, TEXT("Flee: escaped enemy, stopping"));
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			return;
		}
	}

	// Periodically hand control back so the BT can re-decide (e.g. we found a weapon, or HP changed).
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

	// Stuck against a wall? Sidestep — but always to a direction that's still AWAY from the enemy,
	// never back toward it. Otherwise keep committing straight away.
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
				const float Side = FMath::RandRange(-75.f, 75.f);
				const FVector SideDir = Away.RotateAngleAxis(Side, FVector::UpVector);
				AIC->MoveToLocation(MyLoc + SideDir * FleeDistance, 50.f);
				UE_LOG(LogTemp, Log, TEXT("Flee: stuck, sidestep %.0f deg off-away"), Side);
				return;
			}
		}
	}

	// Commit: keep moving to the flee target (re-issued so it tracks the enemy / a better refuge).
	// FleeTarget is a safe known house when one lies away from the threat — so we flee toward loot
	// and can re-arm there once we shake the enemy — otherwise a kiting point away from the threat.
	AIC->MoveToLocation(FleeTarget, 50.f);
}
