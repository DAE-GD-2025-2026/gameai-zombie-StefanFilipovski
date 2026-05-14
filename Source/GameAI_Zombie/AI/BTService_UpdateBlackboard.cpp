#include "BTService_UpdateBlackboard.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/HealthComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"

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

	// --- Flee decision ---
	const float HealthPct = BB->GetValueAsFloat(FName("HealthPercent"));
	const bool bEnemyVisible = BB->GetValueAsObject(FName("TargetEnemy")) != nullptr;
	// Flee if health is critical and we have no weapon, or if health is very low regardless
	const bool bShouldFlee = bEnemyVisible && (HealthPct < 0.3f && !bHasWeapon);
	BB->SetValueAsBool(FName("bShouldFlee"), bShouldFlee);

	// --- Self location ---
	BB->SetValueAsVector(FName("SelfLocation"), Survivor->GetActorLocation());
}
