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
#include "Zombies/BaseZombie.h"
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
	Interval = 0.25f;       // 4x per second
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
	AActor* EnemyForFlee = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	const bool bEnemyVisible = EnemyForFlee != nullptr;
	const FVector MyLoc = Survivor->GetActorLocation();
	const float EnemyDist = EnemyForFlee
		? FVector::Dist(MyLoc, EnemyForFlee->GetActorLocation())
		: MAX_FLT;
	const bool bEnemyClose = bEnemyVisible && EnemyDist < 700.f;

	// Commit to fight: if armed and fleeing isn't opening distance, latch a fight.
	if (bEnemyVisible && bHasWeapon)
	{
		FleeProgressTimer += DeltaSeconds;
		if (FleeProgressLastDist < 0.f) FleeProgressLastDist = EnemyDist;
		if (FleeProgressTimer >= 1.5f)
		{
			const float Gained = EnemyDist - FleeProgressLastDist;
			if (bFleeLatched && Gained < 150.f)
			{
				bFightCommitLatched = true;
				UE_LOG(LogTemp, Warning, TEXT("Service: fleeing isn't gaining distance (%.0f) - committing to fight"), Gained);
			}
			FleeProgressTimer = 0.f;
			FleeProgressLastDist = EnemyDist;
		}
	}
	else
	{
		FleeProgressTimer = 0.f;
		FleeProgressLastDist = -1.f;
	}
	// Release the commit once the threat is gone/far, or critically hurt with a medkit to heal mid-flee.
	if (!bEnemyVisible || EnemyDist > 1300.f || (HealthPct < 0.2f && bHasMedkit))
		bFightCommitLatched = false;

	// Distance to the nearest weapon we can perceive or remember (used to decide whether to re-arm).
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
	const bool bWeaponWithinReach = !bHasWeapon && NearestWeaponDist < 1300.f;

	// Cornered: unarmed, no weapon to grab, no house to fall back to, enemy close.
	const bool bHouseKnown = Survivor->GetKnownHouseCount() > 0;
	const bool bCornered = !bHasWeapon && !bWeaponWithinReach && !bHouseKnown && bEnemyClose;

	// Unarmed with an enemy close and no weapon to grab -> flee toward a refuge to re-arm.
	const bool bUnarmedDanger = !bHasWeapon && !bWeaponWithinReach && bEnemyClose;

	// At critical HP, flee only if it helps: can run (stamina), can heal mid-flee (medkit), or unarmed.
	float StamPctNow = 1.f;
	if (UStaminaComponent* Stam = Survivor->GetStaminaComponent())
		if (Stam->GetMaxStamina() > 0.f) StamPctNow = Stam->GetCurrentStamina() / Stam->GetMaxStamina();
	const bool bCriticalFlee = (HealthPct < 0.25f) && (!bHasWeapon || bHasMedkit || StamPctNow > 0.15f);

	// Don't flee a fight we've committed to.
	const bool bWantFlee = bEnemyVisible && (bCriticalFlee || bCornered || bUnarmedDanger) && !bFightCommitLatched;

	// Latch fleeing on until the enemy is clearly far, to stop flicker.
	if (!bEnemyVisible)            bFleeLatched = false;
	else if (bWantFlee)           bFleeLatched = true;
	else if (EnemyDist > 1200.f)  bFleeLatched = false;
	if (bFightCommitLatched)      bFleeLatched = false;
	if (bWeaponWithinReach)       bFleeLatched = false; // re-arm instead of fleeing

	// Purge zone overrides everything: flee out of the blast radius regardless of enemies.
	const bool bInPurgeZone = (Survivor->GetActivePurgeZoneDanger() != nullptr);
	const bool bShouldFlee = bInPurgeZone || (bEnemyVisible && bFleeLatched);

	// Heal below 70% with a medkit; in combat only if critical (<50%) or unarmed.
	const bool bCanHealNow = !bEnemyVisible || (HealthPct < 0.5f) || !bHasWeapon;
	BB->SetValueAsBool(FName("bNeedsHeal"), HealthPct < 0.7f && bHasMedkit && bCanHealNow);

	// Eat below 40% stamina with food; not in combat unless unarmed.
	UStaminaComponent* Stamina = Survivor->GetStaminaComponent();
	const float StaminaPct = (Stamina && Stamina->GetMaxStamina() > 0)
		? Stamina->GetCurrentStamina() / Stamina->GetMaxStamina()
		: 1.f;
	BB->SetValueAsBool(FName("bNeedsFood"), StaminaPct < 0.4f && bHasFood && (!bEnemyVisible || !bHasWeapon));
	BB->SetValueAsBool(FName("bShouldFlee"), bShouldFlee);

	// Validate TargetEnemy (clear if dead, destroyed, or too far)
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
			if (DistToEnemy > 2000.f)
			{
				BB->ClearValue(FName("TargetEnemy"));
				UE_LOG(LogTemp, Log, TEXT("Service: Enemy too far (%.0f), clearing target"), DistToEnemy);
			}
		}
	}

	// No target but a zombie is perceived: acquire the nearest.
	if (!BB->GetValueAsObject(FName("TargetEnemy")) && Survivor->GetPerceptionComponent())
	{
		TArray<AActor*> PerceivedActors;
		Survivor->GetPerceptionComponent()->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);
		AActor* NearestEnemy = nullptr;
		float NearestEnemyDist = MAX_FLT;
		for (AActor* Actor : PerceivedActors)
		{
			if (!Cast<ABaseZombie>(Actor)) continue;
			const float D = FVector::Dist(Survivor->GetActorLocation(), Actor->GetActorLocation());
			if (D < NearestEnemyDist) { NearestEnemyDist = D; NearestEnemy = Actor; }
		}
		if (NearestEnemy)
		{
			BB->SetValueAsObject(FName("TargetEnemy"), NearestEnemy);
			BB->SetValueAsVector(FName("LastKnownEnemyLocation"), NearestEnemy->GetActorLocation());
		}
	}

	// Keep room for a second weapon by dropping a spare consumable when full.
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

		// Full bag with <2 weapons and 3+ consumables: drop the worst consumable.
		if (bInvFull && WeaponCount < 2 && (MedkitCount + FoodCount) >= 3 && WorstConsumableSlot >= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("STATUS: Only %d weapon(s) - dropping %s(%d) from slot %d to keep room for a weapon"),
				WeaponCount,
				ItemTypeName(InvItems[WorstConsumableSlot]->GetItemType()),
				InvItems[WorstConsumableSlot]->GetValue(),
				WorstConsumableSlot);
			Inventory->RemoveItem(WorstConsumableSlot);
		}
	}

	// Validate TargetItem
	AActor* TargetItem = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
	bool bHasEmptySlot = false;
	int32 ConsumableCount = 0;
	int32 InvWeapons = 0, InvMedkits = 0, InvFood = 0;
	if (Inventory)
	{
		for (ABaseItem* Slot : Inventory->GetInventory())
		{
			if (Slot == nullptr) { bHasEmptySlot = true; continue; }
			const EItemType T = Slot->GetItemType();
			if (IsConsumable(T)) ++ConsumableCount;
			if (IsWeapon(T)) ++InvWeapons;
			else if (T == EItemType::Medkit) ++InvMedkits;
			else if (T == EItemType::Food) ++InvFood;
		}
	}
	// Full bag can still take a weapon or a needed consumable by dropping a spare.
	const bool bCanDropForWeapon = (ConsumableCount >= 2);
	const bool bCanDropWeaponForConsumable = (InvWeapons >= 3);
	auto IsNeededConsumable = [&](EItemType T) -> bool
	{
		return (T == EItemType::Medkit && InvMedkits == 0) || (T == EItemType::Food && InvFood == 0);
	};

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
			const bool bKeepConsumableTarget = TI && IsNeededConsumable(TI->GetItemType()) && bCanDropWeaponForConsumable;
			if (!bKeepWeaponTarget && !bKeepConsumableTarget)
			{
				BB->ClearValue(FName("TargetItem"));
				TargetItem = nullptr;
			}
		}
	}

	// No target item but we have room: re-scan perceived items.
	if (!TargetItem && (bHasEmptySlot || bCanDropForWeapon || bCanDropWeaponForConsumable) && Survivor->GetPerceptionComponent())
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

			// Full bag: only a weapon or a consumable we lack is worth the trip.
			if (!bHasEmptySlot && !IsWeapon(Item->GetItemType()) && !IsNeededConsumable(Item->GetItemType())) continue;

			const float Dist = FVector::Dist(Survivor->GetActorLocation(), Item->GetActorLocation());

			// Closer scores higher; weapons get a bonus; a medkit outranks weapons when HP < 30%.
			float Score = 10000.f / FMath::Max(Dist, 1.f);
			if (IsWeapon(Item->GetItemType()))
			{
				Score += 5000.f;
			}
			if (HealthPct < 0.3f && Item->GetItemType() == EItemType::Medkit)
			{
				Score += 9000.f;
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

	// Self location
	BB->SetValueAsVector(FName("SelfLocation"), Survivor->GetActorLocation());

	// Debug status log 
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
