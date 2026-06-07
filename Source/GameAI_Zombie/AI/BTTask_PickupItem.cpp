#include "BTTask_PickupItem.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Perception/AIPerceptionComponent.h"
#include "Common/SteeringComponent.h"

namespace
{
	bool IsWeaponType(EItemType Type) { return Type == EItemType::Pistol || Type == EItemType::Shotgun; }
	bool IsConsumableType(EItemType Type) { return Type == EItemType::Food || Type == EItemType::Medkit; }

	/** Count weapons currently in inventory */
	int32 CountWeapons(UInventoryComponent* Inv)
	{
		int32 Count = 0;
		for (ABaseItem* Item : Inv->GetInventory())
		{
			if (Item && IsWeaponType(Item->GetItemType())) ++Count;
		}
		return Count;
	}

	/** True if we currently hold ZERO of this consumable type (so it's worth making room for). */
	bool IsConsumableNeeded(UInventoryComponent* Inv, EItemType Type)
	{
		if (Type != EItemType::Medkit && Type != EItemType::Food) return false;
		for (ABaseItem* Item : Inv->GetInventory())
			if (Item && Item->GetItemType() == Type) return false; // we already have one
		return true;
	}

	/** Slot of the lowest-ammo weapon we can spare (keeps at least KeepAtLeast weapons), or -1. */
	int32 FindWorstWeaponSlot(UInventoryComponent* Inv, int32 KeepAtLeast)
	{
		const TArray<ABaseItem*>& Items = Inv->GetInventory();
		int32 WeaponCount = 0, WorstSlot = -1, WorstAmmo = INT_MAX;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (!Items[i] || !IsWeaponType(Items[i]->GetItemType())) continue;
			++WeaponCount;
			if (Items[i]->GetValue() < WorstAmmo) { WorstAmmo = Items[i]->GetValue(); WorstSlot = i; }
		}
		return (WeaponCount > KeepAtLeast) ? WorstSlot : -1;
	}

	/** Find the slot index of the least valuable consumable, or -1 if none.
	 *  Will not drop if we'd go below MinConsumables total consumables. */
	int32 FindWorstConsumableSlot(UInventoryComponent* Inv, int32 MinConsumables = 0)
	{
		int32 ConsumableCount = 0;
		int32 WorstSlot = -1;
		int32 WorstScore = INT_MAX;
		const TArray<ABaseItem*>& Items = Inv->GetInventory();

		// First pass: count consumables
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i] && IsConsumableType(Items[i]->GetItemType()))
				++ConsumableCount;
		}

		// Don't drop if we'd go below minimum
		if (ConsumableCount <= MinConsumables) return -1;

		// Second pass: find worst
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (!Items[i]) continue;
			if (!IsConsumableType(Items[i]->GetItemType())) continue;
			int32 Score = Items[i]->GetValue();
			if (Items[i]->GetItemType() == EItemType::Medkit) Score += 100; // protect medkits over food
			if (Score < WorstScore) { WorstScore = Score; WorstSlot = i; }
		}
		return WorstSlot;
	}
}

UBTTask_PickupItem::UBTTask_PickupItem()
{
	NodeName = "Pickup Item";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_PickupItem::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTPickupMemory* Memory = reinterpret_cast<FBTPickupMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB) return EBTNodeResult::Failed;

	ABaseItem* TargetItem = Cast<ABaseItem>(BB->GetValueAsObject(FName("TargetItem")));
	if (!TargetItem || !IsValid(TargetItem))
	{
		BB->ClearValue(FName("TargetItem"));
		return EBTNodeResult::Failed;
	}

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return EBTNodeResult::Failed;

	UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
	if (!Inventory) return EBTNodeResult::Failed;

	// Check for empty slot — if full, drop a consumable for a weapon
	bool bHasEmptySlot = false;
	const TArray<ABaseItem*>& Items = Inventory->GetInventory();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i] == nullptr) { bHasEmptySlot = true; break; }
	}

	if (!bHasEmptySlot)
	{
		// Full: drop a spare consumable for a weapon, or a spare weapon for a medkit/food we lack.
		const EItemType TT = TargetItem->GetItemType();
		int32 DropSlot = -1;
		if (IsWeaponType(TT))
			DropSlot = FindWorstConsumableSlot(Inventory, 1);
		else if (IsConsumableNeeded(Inventory, TT))
			DropSlot = FindWorstWeaponSlot(Inventory, 2);

		if (DropSlot >= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Pickup: Dropping slot %d to make room for item type %d"), DropSlot, static_cast<int>(TT));
			Inventory->RemoveItem(DropSlot);
			bHasEmptySlot = true;
		}
		if (!bHasEmptySlot)
		{
			BB->ClearValue(FName("TargetItem"));
			return EBTNodeResult::Failed;
		}
	}

	// Approach via the navmesh so we route around walls rather than driving into them.
	if (USteeringComponent* Steering = Survivor->GetSteeringComponent()) Steering->Stop();
	AIC->MoveToLocation(TargetItem->GetActorLocation(), FMath::Max(Inventory->GetPickupRange() - 10.f, 40.f));

	return EBTNodeResult::InProgress;
}

void UBTTask_PickupItem::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTPickupMemory* Memory = reinterpret_cast<FBTPickupMemory*>(NodeMemory);
	Memory->TimeElapsed += DeltaSeconds;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	ABaseItem* TargetItem = Cast<ABaseItem>(BB->GetValueAsObject(FName("TargetItem")));

	// Item gone or destroyed
	if (!Survivor || !TargetItem || !IsValid(TargetItem))
	{
		if (Survivor && Survivor->GetSteeringComponent()) Survivor->GetSteeringComponent()->Stop();
		BB->ClearValue(FName("TargetItem"));
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Timeout — give up on this item
	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		if (Survivor->GetSteeringComponent()) Survivor->GetSteeringComponent()->Stop();
		BB->ClearValue(FName("TargetItem"));
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
	if (!Inventory)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const float Dist = FVector::Dist(Survivor->GetActorLocation(), TargetItem->GetActorLocation());
	const float PickupRange = Inventory->GetPickupRange();

	USteeringComponent* Steering = Survivor->GetSteeringComponent();

	// Not in range: far → navmesh around walls; close → precise steering (navmesh can stall short of the item).
	if (Dist > PickupRange + 20.f)
	{
		if (Dist > 250.f)
		{
			if (Steering) Steering->Stop();
			if (AIC->GetMoveStatus() != EPathFollowingStatus::Moving)
				AIC->MoveToLocation(TargetItem->GetActorLocation(), 100.f);
		}
		else
		{
			AIC->StopMovement();
			if (Steering)
			{
				Steering->SetObstacleAvoidanceEnabled(false);
				Steering->SetSeparationEnabled(false);
				Steering->ArriveAt(TargetItem->GetActorLocation());
			}
		}
		return;
	}

	// In pickup range — grab.
	{
		AIC->StopMovement();
		if (Steering) Steering->Stop();

		// Grab the target item first
		const TArray<ABaseItem*>& Items = Inventory->GetInventory();
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i] == nullptr)
			{
				if (Inventory->GrabItem(i, TargetItem))
				{
					UE_LOG(LogTemp, Log, TEXT("Pickup: Grabbed item type %d into slot %d"),
						static_cast<int>(TargetItem->GetItemType()), i);
					break;
				}
			}
		}

		// Also grab other nearby items we've perceived (perception only, no world query).
		TArray<AActor*> PerceivedActors;
		if (Survivor->GetPerceptionComponent())
		{
			Survivor->GetPerceptionComponent()->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);
		}
		const float GrabRadius = PickupRange + 50.f;

		TArray<ABaseItem*> NearbyWeapons;
		TArray<ABaseItem*> NearbyConsumables;

		for (AActor* Actor : PerceivedActors)
		{
			ABaseItem* NearbyItem = Cast<ABaseItem>(Actor);
			if (!NearbyItem || !IsValid(NearbyItem)) continue;
			if (NearbyItem == TargetItem) continue;
			if (NearbyItem->GetItemType() == EItemType::Garbage) continue;

			const float ItemDist = FVector::Dist(Survivor->GetActorLocation(), NearbyItem->GetActorLocation());
			if (ItemDist > GrabRadius) continue;

			if (IsWeaponType(NearbyItem->GetItemType()))
				NearbyWeapons.Add(NearbyItem);
			else
				NearbyConsumables.Add(NearbyItem);
		}

		// Grab weapons first (more important), then consumables
		TArray<ABaseItem*> GrabOrder;
		GrabOrder.Append(NearbyWeapons);
		GrabOrder.Append(NearbyConsumables);

		for (ABaseItem* NearbyItem : GrabOrder)
		{
			// Find an empty slot
			int32 EmptySlot = -1;
			const TArray<ABaseItem*>& CurrentItems = Inventory->GetInventory();
			for (int32 i = 0; i < CurrentItems.Num(); ++i)
			{
				if (CurrentItems[i] == nullptr) { EmptySlot = i; break; }
			}

			// No empty slot: drop a spare consumable for a weapon, or a spare weapon for a needed medkit/food.
			if (EmptySlot < 0)
			{
				const EItemType NT = NearbyItem->GetItemType();
				int32 DropSlot = -1;
				if (IsWeaponType(NT))
					DropSlot = FindWorstConsumableSlot(Inventory, 1);
				else if (IsConsumableNeeded(Inventory, NT))
					DropSlot = FindWorstWeaponSlot(Inventory, 2);
				if (DropSlot >= 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("Pickup: Dropping slot %d for nearby item type %d"), DropSlot, static_cast<int>(NT));
					Inventory->RemoveItem(DropSlot);
					EmptySlot = DropSlot;
				}
			}

			if (EmptySlot >= 0)
			{
				if (Inventory->GrabItem(EmptySlot, NearbyItem))
				{
					UE_LOG(LogTemp, Log, TEXT("Pickup: Grabbed nearby item type %d into slot %d"),
						static_cast<int>(NearbyItem->GetItemType()), EmptySlot);
				}
			}
		}

		BB->ClearValue(FName("TargetItem"));
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
