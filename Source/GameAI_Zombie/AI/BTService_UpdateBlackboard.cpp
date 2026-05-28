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
	// Need heal: HP < 70% and we have a medkit
	BB->SetValueAsBool(FName("bNeedsHeal"), HealthPct < 0.7f && bHasMedkit);

	// Need food: stamina < 40% and we have food
	UStaminaComponent* Stamina = Survivor->GetStaminaComponent();
	const float StaminaPct = (Stamina && Stamina->GetMaxStamina() > 0)
		? Stamina->GetCurrentStamina() / Stamina->GetMaxStamina()
		: 1.f;
	BB->SetValueAsBool(FName("bNeedsFood"), StaminaPct < 0.4f && bHasFood);

	// --- Flee decision ---
	// Only flee from enemies when we're in real danger — no weapon + low HP, or critically low HP.
	// Purge zones are handled by sidestepping in the Explore/Flee tasks, NOT by triggering bShouldFlee.
	const bool bEnemyVisible = BB->GetValueAsObject(FName("TargetEnemy")) != nullptr;
	const bool bShouldFlee = bEnemyVisible && ((HealthPct < 0.3f && !bHasWeapon) || HealthPct < 0.15f);
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
	if (!TargetItem && bHasEmptySlot && Survivor->GetPerceptionComponent())
	{
		TArray<AActor*> PerceivedActors;
		Survivor->GetPerceptionComponent()->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);

		ABaseItem* ClosestItem = nullptr;
		float ClosestDist = MAX_FLT;
		for (AActor* Actor : PerceivedActors)
		{
			ABaseItem* Item = Cast<ABaseItem>(Actor);
			if (!Item || !IsValid(Item)) continue;
			if (Item->GetItemType() == EItemType::Garbage) continue;

			const float Dist = FVector::Dist(Survivor->GetActorLocation(), Item->GetActorLocation());
			if (Dist < ClosestDist)
			{
				ClosestDist = Dist;
				ClosestItem = Item;
			}
		}

		if (ClosestItem)
		{
			BB->SetValueAsObject(FName("TargetItem"), ClosestItem);
		}
	}

	// --- Self location ---
	BB->SetValueAsVector(FName("SelfLocation"), Survivor->GetActorLocation());
}
