#include "BTTask_UseConsumable.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/HealthComponent.h"
#include "Common/StaminaComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"

UBTTask_UseConsumable::UBTTask_UseConsumable()
{
	NodeName = "Use Consumable";
}

EBTNodeResult::Type UBTTask_UseConsumable::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) return EBTNodeResult::Failed;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return EBTNodeResult::Failed;

	UHealthComponent* Health = Survivor->GetHealthComponent();
	UStaminaComponent* Stamina = Survivor->GetStaminaComponent();
	UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
	if (!Health || !Stamina || !Inventory) return EBTNodeResult::Failed;

	const float HealthPct = (Health->GetMaxHealth() > 0)
		? static_cast<float>(Health->GetHealth()) / static_cast<float>(Health->GetMaxHealth())
		: 1.f;
	const float StaminaPct = (Stamina->GetMaxStamina() > 0)
		? Stamina->GetCurrentStamina() / Stamina->GetMaxStamina()
		: 1.f;

	const TArray<ABaseItem*>& Items = Inventory->GetInventory();

	// Priority 1: Use Medkit if health below 70%
	if (HealthPct < 0.7f)
	{
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i] && Items[i]->GetItemType() == EItemType::Medkit && Items[i]->GetValue() > 0)
			{
				Inventory->UseItem(i);
				Inventory->RemoveItem(i); // Free the slot so inventory isn't permanently full
				UE_LOG(LogTemp, Log, TEXT("UseConsumable: Used Medkit from slot %d (HP %.0f%%)"), i, HealthPct * 100.f);
				return EBTNodeResult::Succeeded;
			}
		}
	}

	// Priority 2: Use Food if stamina below 40%
	if (StaminaPct < 0.4f)
	{
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i] && Items[i]->GetItemType() == EItemType::Food && Items[i]->GetValue() > 0)
			{
				Inventory->UseItem(i);
				Inventory->RemoveItem(i); // Free the slot so inventory isn't permanently full
				UE_LOG(LogTemp, Log, TEXT("UseConsumable: Used Food from slot %d (Stamina %.0f%%)"), i, StaminaPct * 100.f);
				return EBTNodeResult::Succeeded;
			}
		}
	}

	// Nothing to use
	return EBTNodeResult::Failed;
}
