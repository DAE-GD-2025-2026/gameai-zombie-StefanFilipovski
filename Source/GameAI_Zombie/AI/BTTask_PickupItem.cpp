#include "BTTask_PickupItem.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"

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

	// Check for empty slot
	bool bHasEmptySlot = false;
	const TArray<ABaseItem*>& Items = Inventory->GetInventory();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i] == nullptr)
		{
			bHasEmptySlot = true;
			break;
		}
	}
	if (!bHasEmptySlot)
	{
		BB->ClearValue(FName("TargetItem"));
		return EBTNodeResult::Failed;
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

		const TArray<ABaseItem*>& Items = Inventory->GetInventory();
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i] == nullptr)
			{
				if (Inventory->GrabItem(i, TargetItem))
				{
					UE_LOG(LogTemp, Log, TEXT("Pickup: Grabbed item type %d into slot %d"),
						static_cast<int>(TargetItem->GetItemType()), i);
					BB->ClearValue(FName("TargetItem"));
					FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
					return;
				}
			}
		}

		BB->ClearValue(FName("TargetItem"));
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}
