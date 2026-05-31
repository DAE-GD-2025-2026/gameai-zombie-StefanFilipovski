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
	// Only flee from enemies when we're in real danger — no weapon + low HP, or critically low HP.
	// Purge zones are handled by sidestepping in the Explore/Flee tasks, NOT by triggering bShouldFlee.
	const bool bEnemyVisible = BB->GetValueAsObject(FName("TargetEnemy")) != nullptr;
	const bool bShouldFlee = bEnemyVisible && ((HealthPct < 0.3f && !bHasWeapon) || HealthPct < 0.15f);

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

	// --- Inventory management: drop consumables to make room for weapons ---
	// Priority: Weapons > everything else. If inventory is full of consumables and we
	// have no weapons, proactively drop the least useful consumable so we can pick up weapons.
	if (Inventory && !bHasWeapon)
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

		// If inventory is full, no weapons, and we have 2+ consumables, drop the worst one
		// to leave a slot open for picking up a weapon
		if (bInvFull && WeaponCount == 0 && (MedkitCount + FoodCount) >= 2 && WorstConsumableSlot >= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("STATUS: No weapons! Dropping %s(%d) from slot %d to make room"),
				ItemTypeName(InvItems[WorstConsumableSlot]->GetItemType()),
				InvItems[WorstConsumableSlot]->GetValue(),
				WorstConsumableSlot);
			Inventory->RemoveItem(WorstConsumableSlot);
		}
	}

	// --- Validate TargetItem (clear if destroyed, picked up, or inventory full) ---
	AActor* TargetItem = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
	bool bHasEmptySlot = false;
	if (Inventory)
	{
		for (ABaseItem* Slot : Inventory->GetInventory())
		{
			if (Slot == nullptr) { bHasEmptySlot = true; break; }
		}
	}

	if (TargetItem)
	{
		if (!IsValid(TargetItem))
		{
			BB->ClearValue(FName("TargetItem"));
			TargetItem = nullptr;
		}
		else if (!bHasEmptySlot)
		{
			BB->ClearValue(FName("TargetItem"));
			TargetItem = nullptr;
		}
	}

	// If we have no target item but have inventory space, re-scan perception for visible items.
	// OnPerceptionUpdated only fires on state changes — items already in sight won't re-trigger.
	// Prioritize weapons over consumables when choosing what to go after.
	if (!TargetItem && bHasEmptySlot && Survivor->GetPerceptionComponent())
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
		const FString FleeStr = BB->GetValueAsBool(FName("bShouldFlee"))
			? TEXT("FLEE") : TEXT("ok");

		UE_LOG(LogTemp, Warning, TEXT("STATUS: HP=%.0f%% Stam=%.0f%% Weapon=%s Enemy=%s Item=%s %s | Inv: %s"),
			HealthPct * 100.f, StaminaPct * 100.f,
			bHasWeapon ? TEXT("YES") : TEXT("no"),
			*EnemyStr, *ItemStr, *FleeStr,
			*InvStr);
	}
}
