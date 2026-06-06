#include "BTTask_Explore.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Village/House/House.h"
#include "Common/SteeringComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"

UBTTask_Explore::UBTTask_Explore()
{
	NodeName = "Explore";
	bNotifyTick = true;
}

// ---------------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------------

bool UBTTask_Explore::GetThreatLocation(UBehaviorTreeComponent& OwnerComp, FVector& OutLoc) const
{
	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		if (AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy"))))
		{
			OutLoc = Enemy->GetActorLocation();
			return true;
		}
	}
	return false;
}

FVector UBTTask_Explore::GetHomeAnchor(ASurvivorPawn* Survivor) const
{
	if (!Survivor) return FVector::ZeroVector;
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& H : Survivor->GetKnownHouses())
	{
		if (H.IsValid()) { Sum += H->GetActorLocation(); ++Count; }
	}
	return (Count > 0) ? (Sum / Count) : Survivor->GetSpawnLocation();
}

bool UBTTask_Explore::IsInsideHouse(const FVector& Loc, AHouse* House) const
{
	if (!House) return false;
	const FHouseBounds B = House->GetBounds();
	// Generous: count being anywhere within (or just at the edge of) the footprint as "in the doorway /
	// inside", so we loot from the entrance instead of stalling trying to reach the exact centre.
	return FMath::Abs(Loc.X - B.Origin.X) <= B.Extent.X + 60.f
		&& FMath::Abs(Loc.Y - B.Origin.Y) <= B.Extent.Y + 60.f;
}

bool UBTTask_Explore::IsHouseOnCooldown(AHouse* House) const
{
	if (!House || !GetWorld()) return false;
	const float* Until = HouseCooldownUntil.Find(House);
	return Until && GetWorld()->GetTimeSeconds() < *Until;
}

void UBTTask_Explore::CooldownHouse(AHouse* House)
{
	if (!House || !GetWorld()) return;
	HouseCooldownUntil.Add(House, GetWorld()->GetTimeSeconds() + HouseRevisitCooldown);
}

AHouse* UBTTask_Explore::PickTargetHouse(ASurvivorPawn* Survivor, const FVector& From, bool bThreat, const FVector& ThreatLoc) const
{
	if (!Survivor) return nullptr;
	AHouse* Best = nullptr;
	float BestDist = MAX_FLT;
	for (const TWeakObjectPtr<AActor>& Known : Survivor->GetKnownHouses())
	{
		AHouse* House = Cast<AHouse>(Known.Get());
		if (!House || IsHouseOnCooldown(House)) continue;
		if (bThreat && FVector::Dist2D(House->GetActorLocation(), ThreatLoc) < EnemyAvoidRadius) continue;

		const float Dist = FVector::Dist2D(From, House->GetActorLocation());
		if (Dist < BestDist) { BestDist = Dist; Best = House; }
	}
	return Best;
}

void UBTTask_Explore::IssueMove(AAIController* AIC, const FVector& Goal, float Accept)
{
	const bool bMoving = AIC->GetMoveStatus() == EPathFollowingStatus::Moving;
	if (!bLastIssuedValid || !bMoving || FVector::Dist2D(Goal, LastIssuedGoal) > 60.f)
	{
		AIC->MoveToLocation(Goal, Accept);
		LastIssuedGoal = Goal;
		bLastIssuedValid = true;
	}
}

void UBTTask_Explore::SetActiveHouse(ASurvivorPawn* Survivor, AHouse* House, const FVector& Origin)
{
	ActiveHouse = House;
	const FHouseBounds B = House->GetBounds();
	InteriorTarget = FVector(B.Origin.X, B.Origin.Y, Origin.Z);
	CurrentGoal = InteriorTarget;
	CornerTry = -1;
	StuckTimer = 0.f;
	LastPos = Origin;
	DwellTimer = 0.f;
	InsideTimer = 0.f;
	bStoppedInside = false;
	bLastIssuedValid = false; // force a fresh move toward the new target

	UE_LOG(LogTemp, Log, TEXT("Explore: heading into a %s house (dist %.0f, %d known)"),
		*House->GetHouseTypeString(), FVector::Dist2D(Origin, House->GetActorLocation()),
		Survivor ? Survivor->GetKnownHouseCount() : 0);
}

void UBTTask_Explore::ClearActive()
{
	ActiveHouse = nullptr;
	CornerTry = -1;
	StuckTimer = 0.f;
	DwellTimer = 0.f;
	InsideTimer = 0.f;
	bStoppedInside = false;
	bLastIssuedValid = false;
}

void UBTTask_Explore::ResetState()
{
	HouseCooldownUntil.Empty();
	ClearActive();
	bVenturing = false;
	VentureStartIdle = -1.f;
	bAvoiding = false;
	AvoidReissueTimer = 0.f;
}

// ---------------------------------------------------------------------------------------------------
// BT entry points — both just run the persistent planner. We never finish the task ourselves; the BT
// preempts us (Flee/Fight/Pickup) via decorators when needed and resumes us afterwards.
// ---------------------------------------------------------------------------------------------------

EBTNodeResult::Type UBTTask_Explore::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) return EBTNodeResult::Failed;
	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return EBTNodeResult::Failed;

	// New PIE run? (Task objects can persist between runs.) Wipe stale plan/cooldowns.
	if (LastWorld.Get() != GetWorld())
	{
		ResetState();
		LastWorld = GetWorld();
	}

	RunPlan(OwnerComp, AIC, Survivor);
	return EBTNodeResult::InProgress;
}

void UBTTask_Explore::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/, float /*DeltaSeconds*/)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }
	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	RunPlan(OwnerComp, AIC, Survivor);
}

// ---------------------------------------------------------------------------------------------------
// The planner
// ---------------------------------------------------------------------------------------------------

void UBTTask_Explore::RunPlan(UBehaviorTreeComponent& OwnerComp, AAIController* AIC, ASurvivorPawn* Survivor)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float Dt = World->GetDeltaSeconds();
	const float Now = World->GetTimeSeconds();
	const FVector Origin = Survivor->GetActorLocation();

	FVector ThreatLoc;
	const bool bThreat = GetThreatLocation(OwnerComp, ThreatLoc);

	// Sprint when an enemy is near so pursuing our goal (a house/weapon) stays EVASIVE — we keep
	// distance instead of letting the zombie chew on us while we walk. Otherwise move at normal pace.
	if (bThreat && FVector::Dist2D(Origin, ThreatLoc) < 900.f) Survivor->StartRunning();
	else Survivor->StopRunning();

	// 1) Looting: walk into the ROOM (near the centre) before looting — not just touch the doorway.
	// For a long house (e.g. Rectangle) the entrance corner is far from the centre, and the walls block
	// line-of-sight to the loot from the door; stopping at the door = "looting" an empty-looking house.
	if (AHouse* Active = Cast<AHouse>(ActiveHouse.Get()))
	{
		const float DistCenter = FVector::Dist2D(Origin, InteriorTarget);
		const bool bInFootprint = IsInsideHouse(Origin, Active);
		if (bInFootprint) InsideTimer += Dt;

		// Loot once we're at the room centre, OR we've been inside a while but can't reach the exact
		// centre (blocked) — the latter is a safety valve so we never stall inside.
		if (DistCenter < 250.f || (bInFootprint && InsideTimer > 5.f))
		{
			if (!bStoppedInside) { AIC->StopMovement(); bStoppedInside = true; }

			// Sweep our view around the room so we perceive (and Pickup grabs) EVERY item, not just the
			// ones in our current facing cone. Otherwise two items side-by-side get split: we grab one,
			// face away from the other, and leave it behind until we luck into facing it later.
			FRotator R = Survivor->GetActorRotation();
			R.Yaw += 220.f * Dt;
			Survivor->SetActorRotation(R);

			// Keep clearing while anything is still grabbable (Pickup preempts us to take it). Only count
			// toward "looted" once a full sweep turns up nothing left to grab.
			UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
			const bool bItemToGrab = BB && BB->GetValueAsObject(FName("TargetItem")) != nullptr;
			if (bItemToGrab) DwellTimer = 0.f;
			else             DwellTimer += Dt;

			if (DwellTimer >= DwellInsideSeconds)
			{
				CooldownHouse(Active);
				UE_LOG(LogTemp, Log, TEXT("Explore: looted a %s house — moving on"), *Active->GetHouseTypeString());
				ClearActive();
			}
			return;
		}

		// Inside the building but not yet at the centre — head deeper in (don't loot at the door).
		if (bInFootprint)
		{
			CurrentGoal = InteriorTarget;
			IssueMove(AIC, CurrentGoal, 60.f);
			return;
		}
	}

	// 2) Re-arm shortcut: weaponless + a remembered weapon spot closer than the nearest house → grab it.
	bool bHasWeapon = false;
	if (UInventoryComponent* Inv = Survivor->GetInventoryComponent())
	{
		for (ABaseItem* It : Inv->GetInventory())
		{
			if (It && (It->GetItemType() == EItemType::Pistol || It->GetItemType() == EItemType::Shotgun) && It->GetValue() > 0)
			{ bHasWeapon = true; break; }
		}
	}
	if (!bHasWeapon && !bThreat)
	{
		FVector BestW = FVector::ZeroVector; float BestWDist = MAX_FLT; bool bHaveW = false;
		for (const FVector& W : Survivor->GetKnownWeaponLocations())
		{
			const float D = FVector::Dist2D(Origin, W);
			if (D < BestWDist) { BestWDist = D; BestW = W; bHaveW = true; }
		}
		AHouse* NearestHouse = PickTargetHouse(Survivor, Origin, bThreat, ThreatLoc);
		const float HouseDist = NearestHouse ? FVector::Dist2D(Origin, NearestHouse->GetActorLocation()) : MAX_FLT;
		if (bHaveW && BestWDist < HouseDist)
		{
			ClearActive();
			IssueMove(AIC, BestW, 80.f);
			if (BestWDist < 150.f) Survivor->ForgetWeaponNear(BestW); // grabbed (Pickup) or stale
			return;
		}
	}

	// 3) Choose / continue toward a house.
	AHouse* Target = nullptr;
	if (AHouse* A = Cast<AHouse>(ActiveHouse.Get()))
	{
		const bool bBlocked = bThreat && FVector::Dist2D(A->GetActorLocation(), ThreatLoc) < EnemyAvoidRadius;
		if (!IsHouseOnCooldown(A) && !bBlocked) Target = A; // keep our current target if still valid
	}
	if (!Target) Target = PickTargetHouse(Survivor, Origin, bThreat, ThreatLoc);

	// While venturing, stay committed until we discover a NEW house (or arrive / time out), so a home
	// house coming off cooldown doesn't keep yanking us back to the same exhausted cluster.
	if (bVenturing)
	{
		const bool bArrived = FVector::Dist2D(Origin, VentureTarget) < 300.f;
		const bool bFoundNew = Survivor->GetKnownHouseCount() > KnownCountAtVenture;
		if (bFoundNew || bArrived || Now > VentureEndTime)
		{
			bVenturing = false;
			VentureStartIdle = -1.f;
		}
		else
		{
			IssueMove(AIC, VentureTarget, 150.f);
			return;
		}
	}

	if (Target)
	{
		VentureStartIdle = -1.f; // we have a job to do
		if (Target != ActiveHouse.Get()) SetActiveHouse(Survivor, Target, Origin);

		// Stuck / blocked → try the corner doorways in turn (every house type's openings are at corners).
		StuckTimer += Dt;
		const bool bStopped = AIC->GetMoveStatus() != EPathFollowingStatus::Moving;
		if (StuckTimer >= 3.f || bStopped)
		{
			if (FVector::Dist2D(Origin, LastPos) < 80.f)
			{
				FVector Corners[4];
				Target->GetCornerOpenings(Corners, 90.f);
				++CornerTry;
				if (CornerTry <= 3)
				{
					CurrentGoal = Corners[CornerTry];
					CurrentGoal.Z = Origin.Z;
					bLastIssuedValid = false;
					UE_LOG(LogTemp, Log, TEXT("Explore: trying corner doorway %d/4"), CornerTry + 1);
				}
				else
				{
					CooldownHouse(Target);
					UE_LOG(LogTemp, Log, TEXT("Explore: couldn't get inside — moving on"));
					ClearActive();
					return;
				}
			}
			LastPos = Origin;
			StuckTimer = 0.f;
		}

		IssueMove(AIC, CurrentGoal, 60.f);
		return;
	}

	// 4) No house available → avoid threats, or wander/venture to find a new cluster.
	USteeringComponent* Steering = Survivor->GetSteeringComponent();

	if (bThreat && Survivor->GetKnownHouseCount() > 0)
	{
		AvoidReissueTimer += Dt;
		if (!bAvoiding || AvoidReissueTimer >= 0.75f)
		{
			FVector Away = (Origin - ThreatLoc).GetSafeNormal2D();
			if (Away.IsNearlyZero()) Away = Survivor->GetActorForwardVector().GetSafeNormal2D();
			if (Steering) Steering->Stop();
			AIC->MoveToLocation(Origin + Away * 1600.f, 100.f);
			bLastIssuedValid = false;
			bAvoiding = true;
			AvoidReissueTimer = 0.f;
		}
		return;
	}
	bAvoiding = false;

	// Idle (everything known is on cooldown / nothing nearby) for a while → venture out to find more.
	if (VentureStartIdle < 0.f) VentureStartIdle = Now;
	if (Now - VentureStartIdle > VentureAfter)
	{
		bVenturing = true;
		KnownCountAtVenture = Survivor->GetKnownHouseCount();
		VentureEndTime = Now + VentureMaxDuration;
		const float Ang = FMath::DegreesToRadians(FMath::RandRange(0.f, 360.f));
		VentureTarget = Origin + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * VentureDistance;
		if (Steering) Steering->Stop();
		bLastIssuedValid = false;
		IssueMove(AIC, VentureTarget, 150.f);
		UE_LOG(LogTemp, Log, TEXT("Explore: local cluster exhausted — venturing out to find a new one"));
		return;
	}

	// Otherwise wander near the cluster (steering), leashed to the home anchor.
	const FVector Home = GetHomeAnchor(Survivor);
	if (FVector::Dist2D(Origin, Home) > WanderLeash)
	{
		IssueMove(AIC, Home, 200.f);
	}
	else if (Steering)
	{
		if (AIC->GetMoveStatus() == EPathFollowingStatus::Moving) { AIC->StopMovement(); bLastIssuedValid = false; }
		Steering->SetObstacleAvoidanceEnabled(true);
		Steering->SetSeparationEnabled(false);
		Steering->WanderAround();
	}
}
