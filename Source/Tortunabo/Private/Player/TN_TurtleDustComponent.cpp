#include "Player/TN_TurtleDustComponent.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_TurtleSurface.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

namespace TNTurtleDust
{
	/** Un emisor de un color: la nube de polvo o los trocitos de una superficie. */
	struct FSlot
	{
		/** Superficie (índice de TNTurtleSurface) y si son trocitos (granos, terrones, astillas, gotas) o nube. */
		int32 Kind = 0;
		bool bBits = false;
		/** Superficie, tipo y color cuantizado con el que se hizo su malla. */
		uint32 Key = 0;
		TNAmbientFX::FEmitter Emitter;
		double LastUsed = 0.0;
		/** Quedan partículas vivas: hay que seguir moviéndolas. */
		bool bAlive = false;
	};

	/** Emisores por tortuga como mucho (se reciclan los que llevan más tiempo sin usarse y sin partículas vivas). */
	constexpr int32 MaxSlots = 8;

	struct FState
	{
		TArray<FSlot> Slots;
		TNTurtleSurface::FNameCache NameCache;
		TWeakObjectPtr<ATN_ProcMapGenerator> Generator;
		double NextGeneratorLookup = 0.0;
		/** Última superficie bajo la tripa, qué era y dónde y cuándo se miró. */
		float Surface[TNTurtleSurface::Num] = { 0.f, 0.f, 1.f, 0.f, 0.f };
		TNTurtleSurface::FContact Contact;
		FVector LastProbe = FVector::ZeroVector;
		double LastProbeTime = -10.0;
		/** Fotograma anterior: sobre la tripa en el suelo, cayendo, la caída más rápida y la velocidad. */
		bool bWasBellyGround = false;
		bool bWasFalling = false;
		float FallPeak = 0.f;
		FVector2D PrevVelocity = FVector2D::ZeroVector;
		double LastBumpTime = -10.0;
	};

	inline float DustSmoothStep(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	/** Color con 16 niveles por canal: las mallas de partícula se comparten por color (TNAmbientFX::ShapeMesh). */
	FLinearColor QuantizeColor(const FLinearColor& In)
	{
		const auto Level = [](float V) { return FMath::RoundToFloat(FMath::Clamp(V, 0.f, 1.f) * 15.f) / 15.f; };
		return FLinearColor(Level(In.R), Level(In.G), Level(In.B), 1.f);
	}

	uint32 SlotKey(int32 Kind, bool bBits, const FLinearColor& Quantized)
	{
		const uint32 R = static_cast<uint32>(FMath::RoundToInt(Quantized.R * 15.f));
		const uint32 G = static_cast<uint32>(FMath::RoundToInt(Quantized.G * 15.f));
		const uint32 B = static_cast<uint32>(FMath::RoundToInt(Quantized.B * 15.f));
		return (R << 8) | (G << 4) | B | (static_cast<uint32>(Kind) << 16) | (bBits ? (1u << 20) : 0u);
	}

	/**
	 * Colores del polvo y de los trocitos: arena clara, tierra marrón, polvo gris de roca, serrín claro con astillas y
	 * rocío blanco en el agua. En el terreno del mapa, arena, tierra y roca se tiñen con el camino (o la roca) del bioma.
	 */
	void SurfaceColors(int32 Kind, const TNTurtleSurface::FContact& Contact, FLinearColor& OutDust, FLinearColor& OutBits)
	{
		switch (Kind)
		{
		case TNTurtleSurface::Sand:
			OutDust = FLinearColor(0.92f, 0.84f, 0.66f);
			OutBits = FLinearColor(0.8f, 0.68f, 0.46f);
			break;
		case TNTurtleSurface::Soil:
			OutDust = FLinearColor(0.55f, 0.42f, 0.28f);
			OutBits = FLinearColor(0.34f, 0.24f, 0.14f);
			break;
		case TNTurtleSurface::Rock:
			OutDust = FLinearColor(0.68f, 0.67f, 0.64f);
			OutBits = FLinearColor(0.45f, 0.44f, 0.42f);
			break;
		case TNTurtleSurface::Wood:
			OutDust = FLinearColor(0.82f, 0.72f, 0.56f);
			OutBits = FLinearColor(0.58f, 0.38f, 0.18f);
			break;
		default:
			OutDust = FLinearColor(0.88f, 0.95f, 1.f);
			OutBits = FLinearColor(0.8f, 0.92f, 1.f);
			break;
		}
		if (Contact.bProcTerrain && Contact.Biome >= 0
			&& (Kind == TNTurtleSurface::Sand || Kind == TNTurtleSurface::Soil || Kind == TNTurtleSurface::Rock))
		{
			FLinearColor BiomeGround;
			FLinearColor BiomePath;
			FLinearColor BiomeRock;
			FLinearColor BiomeBed;
			TN_DefaultBiomeColors(TNProcMap::BiomeFromIndex(Contact.Biome), BiomeGround, BiomePath, BiomeRock, BiomeBed);
			const FLinearColor Source = Kind == TNTurtleSurface::Rock ? BiomeRock : BiomePath;
			// El polvo en el aire se ve más claro que el suelo; los trocitos, del color del suelo.
			const FLinearColor Light = Source + (FLinearColor::White - Source) * 0.35f;
			OutDust = OutDust + (Light - OutDust) * 0.6f;
			OutBits = OutBits + (Source - OutBits) * 0.6f;
			OutDust.A = 1.f;
			OutBits.A = 1.f;
		}
	}

	/** Nube de polvo de una superficie. */
	TNAmbientFX::FEmitterDesc DustDesc(int32 Kind, const FLinearColor& Color)
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = TNAmbientFX::EShape::Puff;
		D.bSoft = true;
		D.bCloud = true;
		D.Color = Color;
		D.Alpha = 0.5f;
		D.MaxParticles = 36;
		D.Rate = 50.f;
		D.SpawnRadius = 22.f;
		D.SpawnHeight = 6.f;
		D.Speed = 150.f;
		D.SpeedJitter = 0.45f;
		D.Spread = 0.9f;
		D.Gravity = -30.f;
		D.Drag = 2.4f;
		D.LifeMin = 0.5f;
		D.LifeMax = 0.95f;
		D.SizeStart = 12.f;
		D.SizeEnd = 40.f;
		D.WakeDistance = 6000.f;
		switch (Kind)
		{
		case TNTurtleSurface::Sand:
			D.Alpha = 0.55f;
			D.Rate = 60.f;
			break;
		case TNTurtleSurface::Soil:
			D.Rate = 42.f;
			break;
		case TNTurtleSurface::Rock:
			D.Alpha = 0.45f;
			D.Rate = 34.f;
			D.SizeEnd = 34.f;
			break;
		case TNTurtleSurface::Wood:
			D.Alpha = 0.35f;
			D.Rate = 22.f;
			D.SizeEnd = 30.f;
			break;
		default:
			// Rocío de la estela: sale más deprisa, cae antes y dura poco.
			D.Rate = 48.f;
			D.Speed = 230.f;
			D.Gravity = -380.f;
			D.Drag = 1.2f;
			D.LifeMin = 0.3f;
			D.LifeMax = 0.55f;
			D.SizeStart = 9.f;
			D.SizeEnd = 26.f;
			break;
		}
		return D;
	}

	/** Trocitos de una superficie: granos de arena, terrones, arenilla, astillas o gotas. */
	TNAmbientFX::FEmitterDesc BitsDesc(int32 Kind, const FLinearColor& Color)
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = TNAmbientFX::EShape::Ember;
		D.bSoft = false;
		D.Color = Color;
		D.MaxParticles = 32;
		D.Rate = 30.f;
		D.SpawnRadius = 18.f;
		D.SpawnHeight = 4.f;
		D.Speed = 260.f;
		D.SpeedJitter = 0.5f;
		D.Spread = 0.8f;
		D.Gravity = -980.f;
		D.Drag = 0.5f;
		D.LifeMin = 0.35f;
		D.LifeMax = 0.6f;
		D.SizeStart = 3.f;
		D.SizeEnd = 2.f;
		D.WakeDistance = 4000.f;
		switch (Kind)
		{
		case TNTurtleSurface::Sand:
			D.Rate = 45.f;
			D.SizeStart = 2.5f;
			D.SizeEnd = 1.8f;
			break;
		case TNTurtleSurface::Soil:
			D.Rate = 18.f;
			D.SizeStart = 4.5f;
			D.SizeEnd = 3.5f;
			D.Speed = 220.f;
			break;
		case TNTurtleSurface::Rock:
			D.Rate = 26.f;
			D.Speed = 300.f;
			D.SizeEnd = 2.5f;
			break;
		case TNTurtleSurface::Wood:
			// Astillas: palitos que vuelan girando a lo largo de su vuelo.
			D.Shape = TNAmbientFX::EShape::Streak;
			D.Rate = 14.f;
			D.SizeStart = 9.f;
			D.SizeEnd = 7.f;
			D.Speed = 240.f;
			D.Gravity = -900.f;
			D.Drag = 0.6f;
			D.LifeMin = 0.45f;
			D.LifeMax = 0.75f;
			break;
		default:
			// Salpicaduras: gotas translúcidas estiradas en la dirección en que vuelan.
			D.Shape = TNAmbientFX::EShape::Drop;
			D.bSoft = true;
			D.Alpha = 0.7f;
			D.MaxParticles = 40;
			D.Rate = 60.f;
			D.SizeStart = 5.f;
			D.SizeEnd = 3.f;
			D.Speed = 380.f;
			D.Spread = 0.7f;
			D.Drag = 0.3f;
			D.LifeMin = 0.4f;
			D.LifeMax = 0.7f;
			break;
		}
		return D;
	}

	bool AnyAlive(const TNAmbientFX::FEmitter& E)
	{
		for (const TNAmbientFX::FParticle& P : E.Particles)
		{
			if (P.bAlive) { return true; }
		}
		return false;
	}

	void DestroySlot(FSlot& Slot)
	{
		if (UInstancedStaticMeshComponent* ISM = Slot.Emitter.ISM.Get())
		{
			ISM->DestroyComponent();
		}
		Slot.Emitter = TNAmbientFX::FEmitter();
		Slot.bAlive = false;
	}

	/** El emisor de esa superficie, tipo y color; si no existe, se crea (o se recicla uno parado). Null si no hay hueco. */
	FSlot* AcquireSlot(AActor* Owner, FState& S, int32 Kind, bool bBits, const FLinearColor& Color, double Now)
	{
		const FLinearColor Quantized = QuantizeColor(Color);
		const uint32 Key = SlotKey(Kind, bBits, Quantized);
		for (FSlot& Existing : S.Slots)
		{
			if (Existing.Key == Key && Existing.Emitter.ISM.IsValid())
			{
				Existing.LastUsed = Now;
				return &Existing;
			}
		}

		FSlot* Target = nullptr;
		if (S.Slots.Num() < MaxSlots)
		{
			Target = &S.Slots.AddDefaulted_GetRef();
		}
		else
		{
			// Recicla el que lleva más tiempo sin usarse de los que ya no tienen partículas en el aire.
			for (FSlot& Candidate : S.Slots)
			{
				if (!Candidate.bAlive && (!Target || Candidate.LastUsed < Target->LastUsed)) { Target = &Candidate; }
			}
			if (!Target)
			{
				// Todos ocupados: el de la misma superficie y tipo, aunque sea de otro color.
				for (FSlot& Candidate : S.Slots)
				{
					if (Candidate.Kind == Kind && Candidate.bBits == bBits) { Candidate.LastUsed = Now; return &Candidate; }
				}
				return nullptr;
			}
			DestroySlot(*Target);
		}

		Target->Kind = Kind;
		Target->bBits = bBits;
		Target->Key = Key;
		Target->LastUsed = Now;
		Target->bAlive = false;
		TNAmbientFX::FEmitter& E = Target->Emitter;
		E = TNAmbientFX::FEmitter();
		E.Desc = bBits ? BitsDesc(Kind, Quantized) : DustDesc(Kind, Quantized);
		E.Origin = Owner->GetActorLocation();
		E.RateScale = 0.f;
		E.Rng ^= (Key * 2654435761u) | 1u;
		E.Particles.SetNum(E.Desc.MaxParticles);
		E.Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), E.Desc.MaxParticles);
		E.ISM = TNAmbientFX::MakeISM(Owner, TNAmbientFX::ShapeMesh(E.Desc.Shape, E.Desc.Color, E.Desc.bSoft, E.Desc.Alpha, E.Desc.bCloud),
			E.Desc.MaxParticles, false);
		return Target;
	}

	/** Estallido de Count partículas hacia Direction, más abierto y rápido que el goteo del arrastre. */
	void BurstFrom(TNAmbientFX::FEmitter& E, const FVector& Origin, const FVector& Direction, int32 Count, float SpreadK, float SpeedK)
	{
		if (Count <= 0) { return; }
		const FVector SavedOrigin = E.Origin;
		const FVector SavedDirection = E.Desc.Direction;
		const float SavedSpread = E.Desc.Spread;
		const float SavedSpeed = E.Desc.Speed;
		const float SavedRadius = E.Desc.SpawnRadius;
		E.Origin = Origin;
		E.Desc.Direction = Direction;
		E.Desc.Spread = SavedSpread * SpreadK;
		E.Desc.Speed = SavedSpeed * SpeedK;
		E.Desc.SpawnRadius = SavedRadius * 1.5f;
		TNAmbientFX::Burst(E, Count);
		E.Origin = SavedOrigin;
		E.Desc.Direction = SavedDirection;
		E.Desc.Spread = SavedSpread;
		E.Desc.Speed = SavedSpeed;
		E.Desc.SpawnRadius = SavedRadius;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_TurtleDustComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_TurtleDustComponent::UTN_TurtleDustComponent()
{
	// Tras el movimiento: el polvo sale de donde ya está la tortuga en este fotograma.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	State = MakeShared<TNTurtleDust::FState>();
}

UTN_TurtleDustComponent* UTN_TurtleDustComponent::FindOrAddTo(AActor* InOwner)
{
	if (!IsValid(InOwner) || InOwner->IsActorBeingDestroyed()) { return nullptr; }
	if (UTN_TurtleDustComponent* Existing = InOwner->FindComponentByClass<UTN_TurtleDustComponent>())
	{
		return Existing;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender())
	{
		return nullptr;
	}
	UTN_TurtleDustComponent* Comp = NewObject<UTN_TurtleDustComponent>(InOwner, NAME_None, RF_Transient);
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}

void UTN_TurtleDustComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	UWorld* DustWorld = GetWorld();
	if (!Turtle || !DustWorld || !State.IsValid()) { return; }
	TNTurtleDust::FState& S = *State;
	const double Now = DustWorld->GetTimeSeconds();
	const float Dt = FMath::Min(DeltaTime, 0.1f);

	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	const bool bFalling = Move && Move->IsFalling();
	const bool bBellyGround = Turtle->IsBellyOnGround();
	const FVector TurtleVelocity = Turtle->GetVelocity();
	const FVector2D Flat(TurtleVelocity.X, TurtleVelocity.Y);
	const float Speed = static_cast<float>(Flat.Size());
	if (bFalling)
	{
		S.FallPeak = FMath::Max(S.FallPeak, static_cast<float>(-TurtleVelocity.Z));
	}

	// Cerca de la cámara local (lo que ya vuela acaba su vida igualmente).
	FVector View = Turtle->GetActorLocation();
	TNLocalViews::ClosestCamera(DustWorld, Turtle->GetActorLocation(), View);
	const bool bVisible = FVector::DistSquared(View, Turtle->GetActorLocation()) < FMath::Square(static_cast<double>(MaxViewDistance));

	// Golpes: caer de tripa y chocar arrastrándose (la velocidad cambia de golpe; el rebote la da la vuelta).
	const bool bLanded = bBellyGround && !S.bWasBellyGround && S.bWasFalling && S.FallPeak > 120.f;
	const FVector2D Change = Flat - S.PrevVelocity;
	const bool bBump = bBellyGround && S.bWasBellyGround && Change.Size() > 260.0 && S.PrevVelocity.Size() > 180.0
		&& Now - S.LastBumpTime > 0.25;
	if (bBump)
	{
		S.LastBumpTime = Now;
	}

	const float Amount = bVisible ? FMath::Max(0.f, DustAmount) : 0.f;
	const float Intensity = (bBellyGround && Amount > 0.f)
		? TNTurtleDust::DustSmoothStep((Speed - 40.f) / FMath::Max(50.f, FullDustSpeed - 40.f)) : 0.f;
	const bool bEmit = Amount > 0.f && (Intensity > 0.f || bLanded || bBump);

	// Punto de contacto: la base de la cápsula, algo por delante (el pecho es lo que ara el suelo).
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const double HalfHeight = Capsule ? static_cast<double>(Capsule->GetScaledCapsuleHalfHeight()) : 35.0;
	const FVector Forward = Turtle->GetActorForwardVector();
	const FVector Foot = Turtle->GetActorLocation() - FVector(0.0, 0.0, HalfHeight);
	const FVector Contact = Foot + FVector(Forward.X, Forward.Y, 0.0) * 18.0 + FVector(0.0, 0.0, 3.0);

	for (TNTurtleDust::FSlot& Slot : S.Slots)
	{
		Slot.Emitter.RateScale = 0.f;
	}

	if (bEmit)
	{
		// Superficie bajo la tripa (una traza cada 0,1 s o cada 40 cm).
		if (Now - S.LastProbeTime > 0.1 || FVector::DistSquared(Contact, S.LastProbe) > FMath::Square(40.0))
		{
			if (!S.Generator.IsValid() && Now >= S.NextGeneratorLookup)
			{
				S.NextGeneratorLookup = Now + 2.0;
				S.Generator = TNTurtleSurface::FindGenerator(DustWorld);
			}
			TNTurtleSurface::Probe(DustWorld, Turtle, Turtle->GetActorLocation(), Foot, S.Generator.Get(), &S.NameCache, S.Surface, &S.Contact);
			S.LastProbeTime = Now;
			S.LastProbe = Contact;
		}

		// Hacia atrás y arriba de por donde va (parado, hacia atrás de donde mira).
		const FVector Back = Speed > 1.f ? FVector(-Flat.X / Speed, -Flat.Y / Speed, 0.0) : -FVector(Forward.X, Forward.Y, 0.0);
		const FVector TrailDirection = (Back * 0.65 + FVector::UpVector * 0.75).GetSafeNormal();
		const float LandForce = FMath::Clamp(0.5f + (S.FallPeak - 150.f) / 900.f + Speed / 2500.f, 0.4f, 1.4f);
		const FVector BumpAway = Change.IsNearlyZero() ? FVector::ZeroVector : FVector(Change.X, Change.Y, 0.0).GetSafeNormal();

		for (int32 Kind = 0; Kind < TNTurtleSurface::Num; ++Kind)
		{
			const float Weight = S.Surface[Kind];
			if (Weight < 0.2f) { continue; }
			FLinearColor DustColor;
			FLinearColor BitsColor;
			TNTurtleDust::SurfaceColors(Kind, S.Contact, DustColor, BitsColor);
			for (int32 Layer = 0; Layer < 2; ++Layer)
			{
				const bool bBits = Layer == 1;
				TNTurtleDust::FSlot* Slot = TNTurtleDust::AcquireSlot(Turtle, S, Kind, bBits, bBits ? BitsColor : DustColor, Now);
				if (!Slot) { continue; }
				TNAmbientFX::FEmitter& E = Slot->Emitter;
				E.Origin = Contact;
				E.Desc.Direction = TrailDirection;
				E.RateScale = Intensity * Weight * Amount;
				const float WaterBoost = Kind == TNTurtleSurface::Water ? 1.8f : 1.f;
				if (bLanded)
				{
					// Al caer de tripa: una bocanada alrededor (en el agua, un chapuzón).
					const float Base = bBits ? 6.f + 10.f * LandForce : 10.f + 12.f * LandForce;
					TNTurtleDust::BurstFrom(E, Foot + FVector(0.0, 0.0, 4.0), FVector::UpVector,
						FMath::RoundToInt(Base * Weight * Amount * WaterBoost), 2.4f, 1.3f);
				}
				if (bBump && !BumpAway.IsZero())
				{
					// Al chocar: una bocanada pequeña contra el obstáculo, que sale rebotada.
					const float Base = bBits ? 4.f : 6.f;
					TNTurtleDust::BurstFrom(E, Contact - BumpAway * 30.0, (BumpAway + FVector::UpVector).GetSafeNormal(),
						FMath::RoundToInt(Base * Weight * Amount * WaterBoost), 1.6f, 1.1f);
				}
				Slot->bAlive = true;
			}
		}
	}

	// Solo se mueven los emisores con partículas en el aire o naciendo.
	for (TNTurtleDust::FSlot& Slot : S.Slots)
	{
		if (Slot.Emitter.RateScale <= 0.f && !Slot.bAlive) { continue; }
		TNAmbientFX::TickEmitter(Slot.Emitter, Dt, View);
		Slot.bAlive = TNTurtleDust::AnyAlive(Slot.Emitter);
	}

	S.bWasBellyGround = bBellyGround;
	S.bWasFalling = bFalling;
	if (!bFalling)
	{
		S.FallPeak = 0.f;
	}
	S.PrevVelocity = Flat;
}
