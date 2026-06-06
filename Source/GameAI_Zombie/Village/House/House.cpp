// Fill out your copyright notice in the Description page of Project Settings.


#include "House.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"


// Sets default values
AHouse::AHouse()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Register this house as a sight stimulus so survivors can perceive (discover) it.
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
}

// Called when the game starts or when spawned
void AHouse::BeginPlay()
{
	Super::BeginPlay();

	if (StimuliSource)
	{
		StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
		StimuliSource->RegisterWithPerceptionSystem();
	}
}

// Called every frame
void AHouse::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FHouseBounds AHouse::GetBounds() const
{
	FVector Origin, Extent;
	GetActorBounds(true, Origin, Extent);
	return {Origin, Extent};
}

EHouseType AHouse::GetHouseType() const
{
	// Blueprint instances report a class name like "BP_HouseA_C" — match the variant suffix. Check the
	// A/B/C variants BEFORE the generic "House" (they also contain "House").
	const FString N = GetClass() ? GetClass()->GetName() : FString();
	if (N.Contains(TEXT("HouseA"))) return EHouseType::SquareTwoDoor;
	if (N.Contains(TEXT("HouseB"))) return EHouseType::Rectangle;
	if (N.Contains(TEXT("HouseC"))) return EHouseType::OpenCorners;
	if (N.Contains(TEXT("House")))  return EHouseType::Square;
	return EHouseType::Unknown;
}

FString AHouse::GetHouseTypeString() const
{
	switch (GetHouseType())
	{
	case EHouseType::Square:        return TEXT("Square(1 door)");
	case EHouseType::SquareTwoDoor: return TEXT("Square(2 doors)");
	case EHouseType::Rectangle:     return TEXT("Rectangle(1 door)");
	case EHouseType::OpenCorners:   return TEXT("OpenCorners(4 doors)");
	default:                        return TEXT("Unknown");
	}
}

void AHouse::GetCornerOpenings(FVector OutCorners[4], float Inset) const
{
	const FHouseBounds B = GetBounds();
	// Clamp the inset so it can't cross the centre on a small/narrow house.
	const float IX = FMath::Min(Inset, B.Extent.X * 0.5f);
	const float IY = FMath::Min(Inset, B.Extent.Y * 0.5f);
	const float SX = FMath::Max(B.Extent.X - IX, 0.f);
	const float SY = FMath::Max(B.Extent.Y - IY, 0.f);

	OutCorners[0] = B.Origin + FVector(+SX, +SY, 0.f);
	OutCorners[1] = B.Origin + FVector(+SX, -SY, 0.f);
	OutCorners[2] = B.Origin + FVector(-SX, +SY, 0.f);
	OutCorners[3] = B.Origin + FVector(-SX, -SY, 0.f);
}
