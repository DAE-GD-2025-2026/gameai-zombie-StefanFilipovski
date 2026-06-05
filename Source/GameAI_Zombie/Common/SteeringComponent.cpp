#include "SteeringComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Perception/AIPerceptionComponent.h"
#include "Zombies/BaseZombie.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

USteeringComponent::USteeringComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USteeringComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerPawn = Cast<APawn>(GetOwner());
	WanderAngle = FMath::FRandRange(0.f, 2.f * PI);
}

float USteeringComponent::GetReferenceSpeed() const
{
	if (OwnerPawn)
	{
		if (UFloatingPawnMovement* Move = OwnerPawn->FindComponentByClass<UFloatingPawnMovement>())
		{
			if (Move->GetMaxSpeed() > 0.f) return Move->GetMaxSpeed();
		}
	}
	return 600.f;
}

// ---------------------------------------------------------------------------
// Public requests
// ---------------------------------------------------------------------------
void USteeringComponent::SeekTo(const FVector& Target)   { Mode = ESteeringMode::Seek;   TargetLocation = Target; }
void USteeringComponent::ArriveAt(const FVector& Target) { Mode = ESteeringMode::Arrive; TargetLocation = Target; }
void USteeringComponent::FleeFrom(const FVector& Threat) { Mode = ESteeringMode::Flee;   TargetLocation = Threat; }
void USteeringComponent::WanderAround()                  { Mode = ESteeringMode::Wander; }
void USteeringComponent::Stop()                          { Mode = ESteeringMode::Idle; }

bool USteeringComponent::HasArrived(float Radius) const
{
	if (!OwnerPawn) return false;
	return FVector::Dist2D(OwnerPawn->GetActorLocation(), TargetLocation) <= Radius;
}

// ---------------------------------------------------------------------------
// Individual steering behaviors (each returns a desired velocity)
// ---------------------------------------------------------------------------
FVector USteeringComponent::Seek(const FVector& Target) const
{
	const FVector Dir = (Target - OwnerPawn->GetActorLocation()).GetSafeNormal2D();
	return Dir * GetReferenceSpeed();
}

FVector USteeringComponent::Arrive(const FVector& Target) const
{
	const FVector ToTarget = (Target - OwnerPawn->GetActorLocation()) * FVector(1, 1, 0);
	const float Dist = ToTarget.Size();
	if (Dist <= ArriveStopRadius) return FVector::ZeroVector;

	// Ramp speed down linearly once inside the slow radius
	const float SpeedFactor = (Dist >= ArriveSlowRadius) ? 1.f : (Dist / ArriveSlowRadius);
	return ToTarget.GetSafeNormal() * GetReferenceSpeed() * SpeedFactor;
}

FVector USteeringComponent::Flee(const FVector& Threat) const
{
	const FVector Dir = (OwnerPawn->GetActorLocation() - Threat).GetSafeNormal2D();
	return Dir * GetReferenceSpeed();
}

FVector USteeringComponent::Wander(float DeltaTime)
{
	// Classic wander: a point on a circle projected ahead of the agent, jittered over time.
	WanderAngle += FMath::FRandRange(-WanderJitter, WanderJitter) * DeltaTime;

	FVector Forward = OwnerPawn->GetVelocity().GetSafeNormal2D();
	if (Forward.IsNearlyZero()) Forward = OwnerPawn->GetActorForwardVector().GetSafeNormal2D();

	const FVector CircleCenter = OwnerPawn->GetActorLocation() + Forward * WanderDistance;
	const FVector Offset(FMath::Cos(WanderAngle) * WanderRadius,
	                     FMath::Sin(WanderAngle) * WanderRadius, 0.f);

	const FVector Target = CircleCenter + Offset;
	return (Target - OwnerPawn->GetActorLocation()).GetSafeNormal2D() * GetReferenceSpeed();
}

FVector USteeringComponent::ObstacleAvoidance(const FVector& DesiredDir) const
{
	// Deliberately gentle: two angled feeler whiskers that only nudge us sideways away
	// from a wall we're about to scrape. It never reduces forward drive and (with its low
	// weight) never overrides the primary force, so the agent still commits into doorways
	// and rooms. Genuine dead-ends are handled by the task-level stall/re-plan, not here.
	FVector Forward = DesiredDir.GetSafeNormal2D();
	if (Forward.IsNearlyZero()) Forward = OwnerPawn->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero()) return FVector::ZeroVector;

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal2D();
	const FVector Origin = OwnerPawn->GetActorLocation();

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerPawn);

	// Returns clearance (hit distance, or full length if clear).
	auto CastWhisker = [&](const FVector& Dir, float Len) -> float
	{
		FHitResult Hit;
		const FVector End = Origin + Dir * Len;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_WorldStatic, Params))
		{
			if (bDrawDebug) DrawDebugLine(GetWorld(), Origin, Hit.ImpactPoint, FColor::Red, false, 0.f, 0, 2.f);
			return Hit.Distance;
		}
		if (bDrawDebug) DrawDebugLine(GetWorld(), Origin, End, FColor::Green, false, 0.f, 0, 1.f);
		return Len;
	};

	const float Len = AvoidanceDistance;
	const FVector LeftDir  = Forward.RotateAngleAxis(-35.f, FVector::UpVector);
	const FVector RightDir = Forward.RotateAngleAxis( 35.f, FVector::UpVector);

	const float ClearL = CastWhisker(LeftDir,  Len);
	const float ClearR = CastWhisker(RightDir, Len);

	const float Ref = GetReferenceSpeed();
	FVector Avoid = FVector::ZeroVector;

	// Near wall on the left -> nudge right, and vice versa. Lateral only.
	if (ClearL < Len) Avoid +=  Right * Ref * (1.f - ClearL / Len);
	if (ClearR < Len) Avoid += -Right * Ref * (1.f - ClearR / Len);

	return Avoid;
}

FVector USteeringComponent::Separation() const
{
	// Keep distance from nearby zombies — uses ONLY perceived actors (no world omniscience).
	UAIPerceptionComponent* Perception = OwnerPawn->FindComponentByClass<UAIPerceptionComponent>();
	if (!Perception) return FVector::ZeroVector;

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(nullptr, Perceived);

	const FVector MyLoc = OwnerPawn->GetActorLocation();
	FVector Push = FVector::ZeroVector;

	for (AActor* Actor : Perceived)
	{
		if (!Cast<ABaseZombie>(Actor)) continue;
		const FVector Offset = (MyLoc - Actor->GetActorLocation()) * FVector(1, 1, 0);
		const float Dist = Offset.Size();
		if (Dist > KINDA_SMALL_NUMBER && Dist < SeparationRadius)
		{
			// Inverse-distance weighting: closer neighbors push harder.
			Push += Offset.GetSafeNormal() * (1.f - Dist / SeparationRadius);
		}
	}

	return Push * GetReferenceSpeed();
}

// ---------------------------------------------------------------------------
// Blended steering: combine weighted behaviors and drive the pawn
// ---------------------------------------------------------------------------
void USteeringComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerPawn) return;
	if (Mode == ESteeringMode::Idle) return;

	// 1) Primary behavior
	FVector Primary = FVector::ZeroVector;
	switch (Mode)
	{
	case ESteeringMode::Seek:   Primary = Seek(TargetLocation);   break;
	case ESteeringMode::Arrive: Primary = Arrive(TargetLocation); break;
	case ESteeringMode::Flee:   Primary = Flee(TargetLocation);   break;
	case ESteeringMode::Wander: Primary = Wander(DeltaTime);      break;
	default: break;
	}

	// 2) Blend in the always-on behaviors with their weights.
	//    Avoidance steers along the PRIMARY desired direction so it turns us around
	//    walls without cancelling our forward speed.
	FVector Steering = Primary * PrimaryWeight;
	if (bUseObstacleAvoidance) Steering += ObstacleAvoidance(Primary) * AvoidanceWeight;
	if (bUseSeparation)        Steering += Separation() * SeparationWeight;

	if (Steering.IsNearlyZero()) return;

	// 3) Convert the blended desired velocity into a 0..1 movement input
	const float Ref = GetReferenceSpeed();
	const FVector Dir = Steering.GetSafeNormal2D();
	const float Scale = FMath::Clamp(Steering.Size() / Ref, 0.f, 1.f);

	OwnerPawn->AddMovementInput(Dir, Scale);

	// 4) Face the way we're travelling (nice for visualization / GIFs)
	if (bFaceTravelDirection && !Dir.IsNearlyZero())
	{
		const FRotator Current = OwnerPawn->GetActorRotation();
		const FRotator Desired = Dir.Rotation();
		OwnerPawn->SetActorRotation(FMath::RInterpTo(Current, Desired, DeltaTime, 10.f));
	}
}
