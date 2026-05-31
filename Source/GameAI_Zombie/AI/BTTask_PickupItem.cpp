#include "BTTask_PickupItem.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Kismet/GameplayStatics.h"

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
		// Inventory full — but if the target is a weapon and we have < 3 weapons,
		// drop worst consumable to make room. Keep at least 1 consumable.
		if (IsWeaponType(TargetItem->GetItemType()) && CountWeapons(Inventory) < 3)
		{
			const int32 DropSlot = FindWorstConsumableSlot(Inventory, 1); // keep at least 1 consumable
			if (DropSlot >= 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("Pickup: Dropping consumable in slot %d to pick up weapon"), DropSlot);
				Inventory->RemoveItem(DropSlot);
				bHasEmptySlot = true;
			}
		}
		if (!bHasEmptySlot)
		{
			BB->ClearValue(FName("TargetItem"));
			return EBTNodeResult::Failed;
		}
	}

	const float PickupRange = Inventory->GetPickupRange();
	const EPathFollowingRequestResult::Type Result = AIC->MoveToActor(TargetItem, PickupRange - 10.f);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		BB->ClearValue(FName("TargetItem"));
		return EBTNodeResult::Failed;
	}

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
		AIC->StopMovement();
		BB->ClearValue(FName("TargetItem"));
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Timeout — give up on this item
	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		AIC->StopMovement();
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

	if (Dist <= PickupRange + 20.f)
	{
		AIC->StopMovement();

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

		// Also grab ALL other nearby items while we're here — no reason to leave and come back
		// Collect nearby items, sorted: weapons first so we prioritize them
		TArray<AActor*> AllItems;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABaseItem::StaticClass(), AllItems);
		const float GrabRadius = PickupRange + 50.f;

		TArray<ABaseItem*> NearbyWeapons;
		TArray<ABaseItem*> NearbyConsumables;

		for (AActor* Actor : AllItems)
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

			// No empty slot — if item is a weapon and we have < 3, drop a consumable (keep at least 1)
			if (EmptySlot < 0 && IsWeaponType(NearbyItem->GetItemType()) && CountWeapons(Inventory) < 3)
			{
				const int32 DropSlot = FindWorstConsumableSlot(Inventory, 1);
				if (DropSlot >= 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("Pickup: Dropping consumable in slot %d for nearby weapon"), DropSlot);
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
