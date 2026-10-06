#include "TN_RaceRideFX.h"
#include "TN_BeachEnemyKit.h"
#include "TN_RaceItemArtExtra.h"
#include "World/Beach/TN_RaceItemRules.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Player/TortugaCharacter.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceRideFXDetail
{
	/** La tabla: delante del centro y a ras de los pies. */
	constexpr double BoardForward = 8.0;
	constexpr double BoardLift = 2.0;
	/** La ola: detrás de la tabla, un poco más baja que los pies (que parezca que la tortuga va en su falda). */
	constexpr double WaveBack = 135.0;
	constexpr double WaveSink = 25.0;
	/** El cohete: a la espalda, encima del caparazón, con la punta hacia delante y algo hacia arriba. */
	constexpr double RocketBack = 50.0;
	constexpr double RocketUp = 42.0;
	constexpr double RocketPitch = 12.0;
	constexpr double RocketScale = 0.75;

	/** Curva suave 0-1. */
	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	UStaticMeshComponent* MakePiece(ACharacter* Turtle, TNRaceItemArtExtra::ERidePiece Piece, bool bShadow)
	{
		UStaticMesh* Mesh = TNRaceItemArtExtra::GetRidePiece(Piece);
		USceneComponent* Root = Turtle ? Turtle->GetRootComponent() : nullptr;
		UStaticMeshComponent* Comp = (Mesh && Root) ? TNBeachKit::AddPart(Turtle, Root, Mesh, FVector::ZeroVector, bShadow) : nullptr;
		if (Comp)
		{
			Comp->SetVisibility(false);
		}
		return Comp;
	}
}

void FTNRaceRideFX::EnsurePieces(ACharacter* Turtle)
{
	using namespace TNRaceRideFXDetail;
	if (bPiecesReady || !Turtle)
	{
		return;
	}
	bPiecesReady = true;
	Board = MakePiece(Turtle, TNRaceItemArtExtra::ERidePiece::SurfBoard, true);
	Wave = MakePiece(Turtle, TNRaceItemArtExtra::ERidePiece::Wave, false);
	Rocket = MakePiece(Turtle, TNRaceItemArtExtra::ERidePiece::Rocket, true);
	Flame = MakePiece(Turtle, TNRaceItemArtExtra::ERidePiece::Flame, false);
}

void FTNRaceRideFX::EnsureEmitters(ACharacter* Turtle)
{
	if (bEmittersReady || !Turtle)
	{
		return;
	}
	bEmittersReady = true;
	using TNAmbientFX::EShape;
	const uint32 Seed = static_cast<uint32>(GetTypeHash(Turtle->GetFName())) | 1u;
	// Salpicaduras de la ola: gotas blancas y azules hacia atrás y arriba.
	TNAmbientFX::FEmitterDesc SprayDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.85f, 0.95f, 1.f), true, 0.8f, 70, 90.f, 520.f, -900.f, 0.4f, 0.8f, 9.f, 4.f);
	SprayDesc.SpawnRadius = 120.f;
	SprayDesc.Spread = 0.7f;
	TNBeachKit::InitEmitter(Spray, Turtle, SprayDesc, Seed + 11u);
	// Chispas de la tobera del cohete.
	TNAmbientFX::FEmitterDesc SparkDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(1.f, 0.7f, 0.25f), true, 0.95f, 60, 110.f, 420.f, -300.f, 0.25f, 0.55f, 7.f, 2.f);
	SparkDesc.SpawnRadius = 10.f;
	SparkDesc.Spread = 0.5f;
	TNBeachKit::InitEmitter(Sparks, Turtle, SparkDesc, Seed + 12u);
	// Humo gris que se queda detrás.
	TNAmbientFX::FEmitterDesc SmokeDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.72f, 0.72f, 0.75f), true, 0.45f, 50, 30.f, 120.f, 0.f, 0.8f, 1.4f, 25.f, 90.f);
	SmokeDesc.SpawnRadius = 12.f;
	SmokeDesc.Buoyancy = 60.f;
	SmokeDesc.Spread = 0.4f;
	TNBeachKit::InitEmitter(Smoke, Turtle, SmokeDesc, Seed + 13u);
}

void FTNRaceRideFX::Tick(ACharacter* Turtle, float DeltaTime, const FState& State)
{
	if (!Turtle)
	{
		return;
	}
	Clock += DeltaTime;
	// La ola crece del suelo en un cuarto de segundo.
	WaveGrow = State.bSurf ? FMath::Min(1.f, WaveGrow + DeltaTime * 4.f) : 0.f;
	const bool bAnyPiece = State.bSurf || State.bRocket;
	if (bAnyPiece)
	{
		EnsurePieces(Turtle);
		EnsureEmitters(Turtle);
	}
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	if (bPiecesReady)
	{
		TickSurf(Turtle, HalfHeight, State.bSurf);
		TickRocket(Turtle, HalfHeight, State.bRocket);
	}
	if (bEmittersReady)
	{
		FVector View = Turtle->GetActorLocation();
		if (TNBeachKit::LocalCamera(Turtle->GetWorld(), View))
		{
			TNBeachKit::TickEmitterIfBusy(Spray, DeltaTime, View);
			TNBeachKit::TickEmitterIfBusy(Sparks, DeltaTime, View);
			TNBeachKit::TickEmitterIfBusy(Smoke, DeltaTime, View);
		}
	}
	TickFlip(Turtle, State.FlipAge);
}

void FTNRaceRideFX::TickSurf(ACharacter* Turtle, float HalfHeight, bool bOn)
{
	using namespace TNRaceRideFXDetail;
	const FVector Forward = Turtle->GetActorForwardVector().GetSafeNormal2D();
	if (UStaticMeshComponent* Comp = Board.Get())
	{
		Comp->SetVisibility(bOn);
		if (bOn)
		{
			// Se mece un poco con la ola.
			const float Roll = 4.f * FMath::Sin(Clock * 5.f);
			const float Pitch = -3.f + 2.f * FMath::Sin(Clock * 3.3f);
			Comp->SetRelativeLocationAndRotation(FVector(BoardForward, 0.0, -HalfHeight + BoardLift), FRotator(Pitch, 0.f, Roll));
		}
	}
	if (UStaticMeshComponent* Comp = Wave.Get())
	{
		Comp->SetVisibility(bOn);
		if (bOn)
		{
			const float Grow = Smooth01(WaveGrow);
			const float Breathe = 1.f + 0.06f * FMath::Sin(Clock * 6.f);
			Comp->SetRelativeLocation(FVector(-WaveBack, 0.0, -HalfHeight - WaveSink));
			Comp->SetRelativeScale3D(FVector(1.0, 1.0, FMath::Max(0.05f, Grow * Breathe)));
		}
	}
	Spray.RateScale = bOn ? 1.f : 0.f;
	if (bOn)
	{
		Spray.Origin = Turtle->GetActorLocation() - Forward * (WaveBack - 30.0) + FVector(0.0, 0.0, 60.0 - HalfHeight);
		Spray.Desc.Direction = (Forward * 0.4 + FVector(0.0, 0.0, 1.0)).GetSafeNormal();
	}
}

void FTNRaceRideFX::TickRocket(ACharacter* Turtle, float HalfHeight, bool bOn)
{
	using namespace TNRaceRideFXDetail;
	const FRotator Mount(RocketPitch, 0.f, 0.f);
	const FVector MountAt(-RocketBack, 0.0, RocketUp);
	if (UStaticMeshComponent* Comp = Rocket.Get())
	{
		Comp->SetVisibility(bOn);
		if (bOn)
		{
			// Tiembla con el empuje.
			const FVector Shake(0.0, 1.2 * FMath::Sin(Clock * 61.f), 1.2 * FMath::Sin(Clock * 47.f));
			Comp->SetRelativeLocationAndRotation(MountAt + Shake, Mount);
			Comp->SetRelativeScale3D(FVector(RocketScale));
		}
	}
	if (UStaticMeshComponent* Comp = Flame.Get())
	{
		Comp->SetVisibility(bOn);
		if (bOn)
		{
			const float Flicker = 0.8f + 0.25f * FMath::Sin(Clock * 53.f) + 0.15f * FMath::Sin(Clock * 31.f);
			Comp->SetRelativeLocationAndRotation(MountAt, Mount);
			Comp->SetRelativeScale3D(FVector(RocketScale * Flicker, RocketScale * (0.9f + 0.1f * Flicker), RocketScale * (0.9f + 0.1f * Flicker)));
		}
	}
	Sparks.RateScale = bOn ? 1.f : 0.f;
	Smoke.RateScale = bOn ? 1.f : 0.f;
	if (bOn)
	{
		const FTransform Root = Turtle->GetActorTransform();
		const FVector Nozzle = Root.TransformPosition(MountAt);
		const FVector Back = -Root.TransformVectorNoScale(Mount.Vector());
		Sparks.Origin = Nozzle;
		Sparks.Desc.Direction = Back;
		Smoke.Origin = Nozzle + Back * 30.0;
		Smoke.Desc.Direction = Back;
	}
}

void FTNRaceRideFX::TickFlip(ACharacter* Turtle, float FlipAge)
{
	USkeletalMeshComponent* Mesh = Turtle ? Turtle->GetMesh() : nullptr;
	const ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Turtle);
	// Otra cosa mueve la malla (el ragdoll, la bola, el panzazo): la voltereta se deja y la malla vuelve a su sitio.
	const bool bBlocked = !Mesh || Mesh->IsSimulatingPhysics()
		|| (Tortuga && (Tortuga->IsInShell() || Tortuga->IsKnockedDown() || Tortuga->IsDiving() || Tortuga->IsBellyOnGround()));
	if (FlipAge < 0.f || bBlocked)
	{
		if (!bBlocked)
		{
			RestoreFlip(Turtle);
		}
		else
		{
			// Sin tocar la malla: la mueve otro sistema.
			bFlipApplied = false;
		}
		return;
	}
	const float Alpha = TNRaceRideFXDetail::Smooth01(FlipAge / TNRaceItemRules::FlipSeconds);
	// Vuelta hacia delante sobre el eje de la derecha de la tortuga, con el centro de la cápsula de pivote.
	const FQuat Flip(FVector::RightVector, 2.0 * PI * static_cast<double>(Alpha));
	const FVector BaseLoc = Turtle->GetBaseTranslationOffset();
	const FQuat BaseRot = Turtle->GetBaseRotationOffset();
	Mesh->SetRelativeLocationAndRotation(Flip.RotateVector(BaseLoc), Flip * BaseRot);
	bFlipApplied = true;
}

void FTNRaceRideFX::RestoreFlip(ACharacter* Turtle)
{
	if (!bFlipApplied)
	{
		return;
	}
	bFlipApplied = false;
	if (USkeletalMeshComponent* Mesh = Turtle ? Turtle->GetMesh() : nullptr)
	{
		Mesh->SetRelativeLocationAndRotation(Turtle->GetBaseTranslationOffset(), Turtle->GetBaseRotationOffset());
	}
}

void FTNRaceRideFX::Stop(ACharacter* Turtle)
{
	RestoreFlip(Turtle);
	for (TWeakObjectPtr<UStaticMeshComponent>* Piece : { &Board, &Wave, &Rocket, &Flame })
	{
		if (UStaticMeshComponent* Comp = Piece->Get())
		{
			Comp->DestroyComponent();
		}
		Piece->Reset();
	}
	bPiecesReady = false;
	Spray.RateScale = 0.f;
	Sparks.RateScale = 0.f;
	Smoke.RateScale = 0.f;
}

bool FTNRaceRideFX::IsBusy() const
{
	const bool bLive = bEmittersReady && (TNBeachKit::AnyAlive(Spray) || TNBeachKit::AnyAlive(Sparks) || TNBeachKit::AnyAlive(Smoke));
	return bFlipApplied || bLive;
}
