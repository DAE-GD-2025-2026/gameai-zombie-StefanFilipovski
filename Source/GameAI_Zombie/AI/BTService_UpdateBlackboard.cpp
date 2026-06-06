#include "BTService_UpdateBlackboard.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/HealthComponent.h"
#include "Common/StaminaComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "PurgeZones/PurgeZone.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const TCHAR* ItemTypeName(EItemType Type)
	{
		switch (Type)
		{
		case EItemType::Food:    return TEXT("Food");
		case EItemType::Medkit:  return TEXT("Medkit");
		case EItemType::Shotgun: return TEXT("Shotgun");
		case EItemType::Pistol:  return TEXT("Pistol");
		case EItemType::Garbage: return TEXT("Garbage");
		default:                 return TEXT("Unknown");
		}
	}

	bool IsWeapon(EItemType Type) { return Type == EItemType::Pistol || Type == EItemType::Shotgun; }
	bool IsConsumable(EItemType Type) { return Type == EItemType::Food || Type == EItemType::Medkit; }
}

UBTService_UpdateBlackboard::UBTService_UpdateBlackboard()
{
	NodeName = "Update Survivor Blackboard";
	Interval = 0.25f;       // Update 4x per second — plenty fast, not wasteful
	RandomDeviation = 0.05f;
}

void UBTService_UpdateBlackboard::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!BB || !AIC) return;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return;

	// --- Health ---
	UHealthComponent* Health = Survivor->GetHealthComponent();
	if (Health)
	{
		const float HealthPct = (Health->GetMaxHealth() > 0)
			? static_cast<float>(Health->GetHealth()) / static_cast<float>(Health->GetMaxHealth())
			: 0.f;
		BB->SetValueAsFloat(FName("HealthPercent"), HealthPct);
	}

	// --- Weapon status ---
	UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
	bool bHasWeapon = false;
	if (Inventory)
	{
		for (ABaseItem* Item : Inventory->GetInventory())
		{
			if (Item && (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun))
			{
				if (Item->GetValue() > 0) // has ammo
				{
					bHasWeapon = true;
					break;
				}
			}
		}
	}
	BB->SetValueAsBool(FName("bHasWeapon"), bHasWeapon);

	// --- Consumable needs ---
	const float HealthPct = BB->GetValueAsFloat(FName("HealthPercent"));
	bool bHasMedkit = false;
	bool bHasFood = false;
	if (Inventory)
	{
		for (ABaseItem* Item : Inventory->GetInventory())
		{
			if (Item && Item->GetValue() > 0)
			{
				if (Item->GetItemType() == EItemType::Medkit) bHasMedkit = true;
				if (Item->GetItemType() == EItemType::Food) bHasFood = true;
			}
		}
	}
	// --- Flee decision ---
	// Flee when we can't win: no weapon and taking damage (HP < 50%), or critically low HP.
	// With a weapon we prefer to stand and fight (handled by the Fight branch).
	// Flee decision.
	// With NO weapon we can't fight, so be cautious: back away from a nearby zombie even at full
	// HP (don't wait to be chased), and always retreat once hurt. With a weapon we stand and
	// fight until HP is genuinely low.
	AActor* EnemyForFlee = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	const bool bEnemyVisible = EnemyForFlee != nullptr;
	const FVector MyLoc = Survivor->GetActorLocation();
	const float EnemyDist = EnemyForFlee
		? FVector::Dist(MyLoc, EnemyForFlee->GetActorLocation())
		: MAX_FLT;
	const bool bEnemyClose = bEnemyVisible && EnemyDist < 700.f;

	// If we're unarmed but a weapon is within quick reach, ARM UP rather than flee to our death:
	// suppress fleeing so the Pickup→Fight branches grab it and fight back. Fleeing an adjacent zombie
	// with no weapon (and no escape) is how the survivor kept getting cornered and killed.
	float NearestWeaponDist = MAX_FLT;
	if (!bHasWeapon)
	{
		if (UAIPerceptionComponent* Perc = Survivor->GetPerceptionComponent())
		{
			TArray<AActor*> Perceived;
			Perc->GetCurrentlyPerceivedActors(nullptr, Perceived);
			for (AActor* A : Perceived)
			{
				ABaseItem* It = Cast<ABaseItem>(A);
				if (It && IsValid(It) && IsWeapon(It->GetItemType()))
					NearestWeaponDist = FMath::Min(NearestWeaponDist, FVector::Dist(MyLoc, It->GetActorLocation()));
			}
		}
		for (const FVector& W : Survivor->GetKnownWeaponLocations())
			NearestWeaponDist = FMath::Min(NearestWeaponDist, FVector::Dist2D(MyLoc, W));
	}
	const bool bWeaponWithinReach = !bHasWeapon && NearestWeaponDist < 1000.f;

	// Flee is for genuine danger ONLY. We can't truly outrun zombies, so the winning play is to keep
	// pursuing houses/weapons (Explore avoids enemy-adjacent houses and sprints under threat), arm up,
	// and fight back. So flee only when: critically hurt, or genuinely cornered — unarmed with no
	// weapon to grab AND no house to fall back to. Otherwise we stay aggressive and goal-seeking.
	const bool bHouseKnown = Survivor->GetKnownHouseCount() > 0;
	const bool bCornered = !bHasWeapon && !bWeaponWithinReach && !bHouseKnown && bEnemyClose;
	const bool bWantFlee = bEnemyVisible && (HealthPct < 0.25f || bCornered);

	// Hysteresis: latch fleeing ON when we want to flee, and only release it once the enemy is
	// clearly far (or gone). This stops the flee state from flickering when a chaser hovers around
	// the trigger distance, which was making the BT abort/re-enter Flee every frame.
	if (!bEnemyVisible)            bFleeLatched = false;
	else if (bWantFlee)           bFleeLatched = true;
	else if (EnemyDist > 1200.f)  bFleeLatched = false;

	// Lethal purge zone overrides everything — if we're standing in a blast radius, flee out of it
	// (the Flee task targets the way out) regardless of enemies.
	const bool bInPurgeZone = (Survivor->GetActivePurgeZoneDanger() != nullptr);
	const bool bShouldFlee = bInPurgeZone || (bEnemyVisible && bFleeLatched);

	// Need heal: HP < 70% and we have a medkit.
	// During combat: only heal if HP is critical (<50%) or we have no weapon (can't fight anyway).
	// Outside combat: heal freely below 70%.
	const bool bCanHealNow = !bEnemyVisible || (HealthPct < 0.5f) || !bHasWeapon;
	BB->SetValueAsBool(FName("bNeedsHeal"), HealthPct < 0.7f && bHasMedkit && bCanHealNow);

	// Need food: stamina < 40% and we have food — also not during active combat (unless no weapon)
	UStaminaComponent* Stamina = Survivor->GetStaminaComponent();
	const float StaminaPct = (Stamina && Stamina->GetMaxStamina() > 0)
		? Stamina->GetCurrentStamina() / Stamina->GetMaxStamina()
		: 1.f;
	BB->SetValueAsBool(FName("bNeedsFood"), StaminaPct < 0.4f && bHasFood && (!bEnemyVisible || !bHasWeapon));
	BB->SetValueAsBool(FName("bShouldFlee"), bShouldFlee);

	// --- Validate TargetEnemy (clear if dead, destroyed, or too far) ---
	AActor* TargetEnemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	if (TargetEnemy)
	{
		if (!IsValid(TargetEnemy))
		{
			BB->ClearValue(FName("TargetEnemy"));
		}
		else
		{
			// If enemy is beyond lose-sight range + buffer, clear it
			const float DistToEnemy = FVector::Dist(Survivor->GetActorLocation(), TargetEnemy->GetActorLocation());
			if (DistToEnemy > 2000.f) // well beyond lose-sight radius of 1500
			{
				BB->ClearValue(FName("TargetEnemy"));
				UE_LOG(LogTemp, Log, TEXT("Service: Enemy too far (%.0f), clearing target"), DistToEnemy);
			}
		}
	}

	// --- Inventory management: keep room for weapons ---
	// We want to carry up to TWO weapons so emptying one doesn't leave us defenceless. If the bag is
	// full, we hold fewer than two weapons, and we have a comfortable consumable surplus (3+, so we
	// keep at least two after dropping), proactively drop the least useful consumable to free a slot.
	if (Inventory)
	{
		const TArray<ABaseItem*>& InvItems = Inventory->GetInventory();
		bool bInvFull = true;
		int32 WeaponCount = 0;
		int32 MedkitCount = 0;
		int32 FoodCount = 0;
		int32 WorstConsumableSlot = -1;
		int32 WorstConsumableValue = INT_MAX;

		for (int32 i = 0; i < InvItems.Num(); ++i)
		{
			if (InvItems[i] == nullptr) { bInvFull = false; continue; }
			const EItemType Type = InvItems[i]->GetItemType();
			if (IsWeapon(Type)) ++WeaponCount;
			else if (Type == EItemType::Medkit) ++MedkitCount;
			else if (Type == EItemType::Food) ++FoodCount;

			// Track worst consumable (lowest value, prefer dropping food over medkits)
			if (IsConsumable(Type))
			{
				// Score: lower = more droppable. Food is less critical than medkits.
				int32 DropScore = InvItems[i]->GetValue();
				if (Type == EItemType::Medkit) DropScore += 100; // Protect medkits
				if (DropScore < WorstConsumableValue)
				{
					WorstConsumableValue = DropScore;
					WorstConsumableSlot = i;
				}
			}
		}

		// Full bag, fewer than two weapons, 3+ consumables → drop the worst consumable to leave a slot
		// open for a weapon (keeps at least two consumables).
		if (bInvFull && WeaponCount < 2 && (MedkitCount + FoodCount) >= 3 && WorstConsumableSlot >= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("STATUS: Only %d weapon(s) — dropping %s(%d) from slot %d to keep room for a weapon"),
				WeaponCount,
				ItemTypeName(InvItems[WorstConsumableSlot]->GetItemType()),
				InvItems[WorstConsumableSlot]->GetValue(),
				WorstConsumableSlot);
			Inventory->RemoveItem(WorstConsumableSlot);
		}
	}

	// --- Validate TargetItem (clear if destroyed, picked up, or inventory full) ---
	AActor* TargetItem = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
	bool bHasEmptySlot = false;
	int32 ConsumableCount = 0;
	if (Inventory)
	{
		for (ABaseItem* Slot : Inventory->GetInventory())
		{
			if (Slot == nullptr) { bHasEmptySlot = true; continue; }
			if (IsConsumable(Slot->GetItemType())) ++ConsumableCount;
		}
	}
	// Even with a full bag we can still take a WEAPON, because Pickup drops a spare consumable for
	// it (keeping at least one). This stops the agent leaving a second weapon behind in a house.
	const bool bCanDropForWeapon = (ConsumableCount >= 2);

	if (TargetItem)
	{
		if (!IsValid(TargetItem))
		{
			BB->ClearValue(FName("TargetItem"));
			TargetItem = nullptr;
		}
		else if (!bHasEmptySlot)
		{
			ABaseItem* TI = Cast<ABaseItem>(TargetItem);
			const bool bKeepWeaponTarget = TI && IsWeapon(TI->GetItemType()) && bCanDropForWeapon;
			if (!bKeepWeaponTarget)
			{
				BB->ClearValue(FName("TargetItem"));
				TargetItem = nullptr;
			}
		}
	}

	// If we have no target item but have inventory space, re-scan perception for visible items.
	// OnPerceptionUpdated only fires on state changes — items already in sight won't re-trigger.
	// Prioritize weapons over consumables when choosing what to go after.
	if (!TargetItem && (bHasEmptySlot || bCanDropForWeapon) && Survivor->GetPerceptionComponent())
	{
		TArray<AActor*> PerceivedActors;
		Survivor->GetPerceptionComponent()->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);

		ABaseItem* BestItem = nullptr;
		float BestScore = -1.f;
		for (AActor* Actor : PerceivedActors)
		{
			ABaseItem* Item = Cast<ABaseItem>(Actor);
			if (!Item || !IsValid(Item)) continue;
			if (Item->GetItemType() == EItemType::Garbage) continue;
			// With a full bag, only a weapon is worth going for (we'd drop a consumable for it).
			if (!bHasEmptySlot && !IsWeapon(Item->GetItemType())) continue;

			const float Dist = FVector::Dist(Survivor->GetActorLocation(), Item->GetActorLocation());

			// Score: weapons get massive bonus, closer items score higher
			float Score = 10000.f / FMath::Max(Dist, 1.f);
			if (IsWeapon(Item->GetItemType()))
			{
				Score += 5000.f; // Always prefer weapons
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestItem = Item;
			}
		}

		if (BestItem)
		{
			BB->SetValueAsObject(FName("TargetItem"), BestItem);
		}
	}

	// --- Self location ---
	BB->SetValueAsVector(FName("SelfLocation"), Survivor->GetActorLocation());

	// --- Debug status log (every ~2 seconds, not every tick) ---
	DebugLogTimer += Interval;
	if (DebugLogTimer >= 2.0f)
	{
		DebugLogTimer = 0.f;

		FString InvStr;
		if (Inventory)
		{
			const TArray<ABaseItem*>& InvItems = Inventory->GetInventory();
			for (int32 i = 0; i < InvItems.Num(); ++i)
			{
				if (i > 0) InvStr += TEXT(" | ");
				if (InvItems[i])
				{
					InvStr += FString::Printf(TEXT("[%d]%s(%d)"), i,
						ItemTypeName(InvItems[i]->GetItemType()), InvItems[i]->GetValue());
				}
				else
				{
					InvStr += FString::Printf(TEXT("[%d]empty"), i);
				}
			}
		}

		const FString EnemyStr = BB->GetValueAsObject(FName("TargetEnemy"))
			? TEXT("YES") : TEXT("no");
		const FString ItemStr = BB->GetValueAsObject(FName("TargetItem"))
			? TEXT("YES") : TEXT("no");
		const FString FleeStr = bInPurgeZone
			? TEXT("FLEE(PURGE)")
			: (BB->GetValueAsBool(FName("bShouldFlee")) ? TEXT("FLEE") : TEXT("ok"));

		UE_LOG(LogTemp, Warning, TEXT("STATUS [T=%.0fs]: HP=%.0f%% Stam=%.0f%% Weapon=%s Enemy=%s Item=%s %s | Houses=%d | Inv: %s"),
			Survivor->GetSurvivalTime(),
			HealthPct * 100.f, StaminaPct * 100.f,
			bHasWeapon ? TEXT("YES") : TEXT("no"),
			*EnemyStr, *ItemStr, *FleeStr,
			Survivor->GetKnownHouseCount(),
			*InvStr);
	}
}
