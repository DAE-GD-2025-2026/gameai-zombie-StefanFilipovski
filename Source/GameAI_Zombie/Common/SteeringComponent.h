// Steering behaviors for the survivor's local movement.
// Implements multiple individual behaviors (Seek, Arrive, Flee, Wander,
// Obstacle Avoidance, Separation) and combines them with weighted blending.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SteeringComponent.generated.h"

/** Which primary steering behavior is currently requested by the AI. */
UENUM(BlueprintType)
enum class ESteeringMode : uint8
{
	Idle    UMETA(DisplayName = "Idle"),
	Seek    UMETA(DisplayName = "Seek"),
	Arrive  UMETA(DisplayName = "Arrive"),
	Flee    UMETA(DisplayName = "Flee"),
	Wander  UMETA(DisplayName = "Wander")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMEAI_ZOMBIE_API USteeringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USteeringComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// --- Requests set by Behavior Tree tasks (call each frame while active) ---
	void SeekTo(const FVector& Target);     // full-speed move toward target
	void ArriveAt(const FVector& Target);   // move toward target, slow down on approach
	void FleeFrom(const FVector& Threat);   // full-speed move directly away from threat
	void WanderAround();                    // smooth random exploration
	void Stop();                            // stop issuing movement input

	/** True once within StopRadius of the current Seek/Arrive target. */
	bool HasArrived(float Radius) const;

	/** Always-on blended behaviors can be toggled per situation. */
	void SetObstacleAvoidanceEnabled(bool bEnabled) { bUseObstacleAvoidance = bEnabled; }
	void SetSeparationEnabled(bool bEnabled) { bUseSeparation = bEnabled; }

protected:
	virtual void BeginPlay() override;

	// --- Individual steering behaviors: each returns a desired velocity ---
	FVector Seek(const FVector& Target) const;
	FVector Arrive(const FVector& Target) const;
	FVector Flee(const FVector& Threat) const;
	FVector Wander(float DeltaTime);
	FVector ObstacleAvoidance(const FVector& DesiredDir) const;
	FVector Separation() const;

	/** Reference top speed used to scale steering into 0..1 movement input. */
	float GetReferenceSpeed() const;

	// --- Tunables ---
	UPROPERTY(EditAnywhere, Category = "Steering|Arrive")
	float ArriveSlowRadius = 200.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Arrive")
	float ArriveStopRadius = 60.f;

	// Tuned for broad roaming: a small jitter + a far-projected circle gives long, smooth
	// sweeps that cover ground (so we get line-of-sight on new houses) instead of tight circles.
	UPROPERTY(EditAnywhere, Category = "Steering|Wander")
	float WanderRadius = 90.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Wander")
	float WanderDistance = 450.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Wander")
	float WanderJitter = 15.f;

	UPROPERTY(EditAnywhere, Category = "Steering|Avoidance")
	float AvoidanceDistance = 150.f;     // whisker length
	UPROPERTY(EditAnywhere, Category = "Steering|Avoidance")
	float AvoidanceWeight = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Steering|Separation")
	float SeparationRadius = 220.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Separation")
	float SeparationWeight = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Steering")
	float PrimaryWeight = 1.0f;

	// Off by default: rotating to face travel direction turns the sight cone away from
	// enemies chasing from behind, which hurts perception/survival. The Fight task controls
	// facing itself when aiming.
	UPROPERTY(EditAnywhere, Category = "Steering")
	bool bFaceTravelDirection = false;

	UPROPERTY(EditAnywhere, Category = "Steering|Debug")
	bool bDrawDebug = false;

private:
	ESteeringMode Mode = ESteeringMode::Idle;
	FVector TargetLocation = FVector::ZeroVector;

	bool bUseObstacleAvoidance = true;
	bool bUseSeparation = false;

	// Wander internal state (current point on the wander circle)
	float WanderAngle = 0.f;

	UPROPERTY()
	APawn* OwnerPawn = nullptr;
};
