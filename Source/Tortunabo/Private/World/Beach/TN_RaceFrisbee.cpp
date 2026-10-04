#include "World/Beach/TN_RaceFrisbee.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceItems.h"
#include "TN_BeachEnemyKit.h"
#include "TN_RaceItemArt.h"
#include "../../Lobby/Playground/TN_PlaygroundMeshKit.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

/**
 * Ajustes y piezas del disco volador. Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un
 * espacio anónimo se ven en el resto del bloque.
 */
namespace TNRaceFrisbeeDetail
{
	using FDiscBuffers = TNPlaygroundKit::FBuffers;

	// ── Lanzamiento ──
	/** Tope de discos a la vez en el mundo. */
	constexpr int32 MaxInWorld = 6;
	/** Sale a esta distancia (cm) por delante del centro de la tortuga. */
	constexpr double SpawnForward = 100.0;

	// ── Trayectoria (segundos y centímetros) ──
	constexpr double OutSeconds = 1.25;
	constexpr double TurnSeconds = 0.25;
	constexpr double BackSeconds = 1.35;
	/** Nadie dura más: si no lo han cazado, desaparece. */
	constexpr double MaxSeconds = 6.0;
	/** Ida: hacia delante y de lado (interpolación suave). */
	constexpr double OutDistance = 2600.0;
	constexpr double OutCurve = 700.0;
	/** Vuelta: cuánto se abre hacia el mismo lado a mitad de camino. */
	constexpr double BackBulge = 250.0;
	/** Vuela a esta altura (cm) sobre la arena y, sin generador de la playa, se supone el suelo a esta distancia bajo el origen. */
	constexpr double FlyHeight = 90.0;
	constexpr double GroundGuess = 88.0;

	// ── Golpes ──
	/** Radio (cm) de la cápsula del disco que barre lo que toca. */
	constexpr double HitRadius = 90.0;
	/** En la vuelta, a menos de esto (cm) de quien lo lanzó lo caza. */
	constexpr double CatchRadius = 200.0;
	/** Tortugas: segundos de derribo y empujón (cm/s) en el sentido del disco y hacia arriba. */
	constexpr float KnockSeconds = 1.9f;
	constexpr double KnockPush = 550.0;
	constexpr double KnockLift = 300.0;
	/** Enemigos: segundos de mareo. */
	constexpr float EnemyStunSeconds = 4.f;
	/** Segundos que sigue el actor tras acabar (para que se vea la nubecilla y suene el «clap»). */
	constexpr float FinishLingerSeconds = 0.3f;

	// ── Aspecto ──
	/** La malla del arte mide unos 30 cm: se escala a esto. */
	constexpr double ArtScale = 3.0;
	/** Giro sobre sí mismo (grados/s), alabeo hacia el lado de la curva y bamboleo (grados). */
	constexpr double SpinRate = 1500.0;
	constexpr double BankDegrees = 14.0;
	constexpr double WobbleDegrees = 5.0;
	/** Sombra redonda en la arena (radio, cm). */
	constexpr float ShadowRadius = 80.f;
	/** El zumbido suena cada WhirrPeriod segundos si el oyente está a menos de WhirrRange (cm). */
	constexpr float WhirrPeriod = 0.8f;
	constexpr float WhirrRange = 6000.f;

	/** Claves de las mallas compartidas (el disco de respaldo suma el color). */
	constexpr int32 KeyFallbackDisc = 100;

	/**
	 * Malla compartida por clave: se construye la primera vez (en el paquete transitorio) y la mantienen viva los componentes
	 * que la usan; si el recolector la suelta entre rondas, se vuelve a construir.
	 */
	UStaticMesh* SharedMesh(int32 Key, TFunctionRef<void(FDiscBuffers&)> Build)
	{
		static TMap<int32, TWeakObjectPtr<UStaticMesh>> Cache;
		TWeakObjectPtr<UStaticMesh>& Entry = Cache.FindOrAdd(Key);
		if (UStaticMesh* Found = Entry.Get())
		{
			return Found;
		}
		FDiscBuffers Buffers;
		Build(Buffers);
		UStaticMesh* Mesh = Buffers.IsEmpty() ? nullptr : TNPlaygroundKit::BuildMesh(GetTransientPackage(), Buffers, TNPlaygroundKit::VertexColorMaterial());
		Entry = Mesh;
		return Mesh;
	}

	/** Corona plana de cuñas de dos colores alternas (así se ve girar un disco simétrico), mirando arriba. */
	void AddWedgeRing(FDiscBuffers& Buffers, double Height, double RadiusIn, double RadiusOut, int32 Wedges, const FLinearColor& ColorA, const FLinearColor& ColorB)
	{
		for (int32 k = 0; k < Wedges; ++k)
		{
			const double A0 = TNPlaygroundKit::KitTwoPi * k / Wedges;
			const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / Wedges;
			const FVector InA(FMath::Cos(A0) * RadiusIn, FMath::Sin(A0) * RadiusIn, Height);
			const FVector InB(FMath::Cos(A1) * RadiusIn, FMath::Sin(A1) * RadiusIn, Height);
			const FVector OutB(FMath::Cos(A1) * RadiusOut, FMath::Sin(A1) * RadiusOut, Height);
			const FVector OutA(FMath::Cos(A0) * RadiusOut, FMath::Sin(A0) * RadiusOut, Height);
			Buffers.AddQuad(InA, InB, OutB, OutA, FVector::UpVector, (k % 2) == 0 ? ColorA : ColorB);
		}
	}

	/** Disco de respaldo (si el arte no da malla): 1,2 m con canto blanco, cuñas de dos colores y una cúpula en el centro. */
	void BuildFallbackDisc(FDiscBuffers& Buffers, int32 IndexA, int32 IndexB)
	{
		constexpr double DiscRadius = 60.0;
		const FLinearColor Outer = TNPlaygroundKit::ToyColor(IndexA);
		const FLinearColor Mid = TNPlaygroundKit::ToyColor(IndexB);
		const FLinearColor Hub = TNPlaygroundKit::Rgb(0xFFFFFF, 0.2f);
		const FLinearColor Rim = TNPlaygroundKit::Rgb(0xF6F2EA, 0.3f);
		const FVector Up = FVector::UpVector;
		// Canto grueso con sus dos tapas.
		TNPlaygroundKit::AddFrustum(Buffers, FVector(0.0, 0.0, -3.0), FVector(0.0, 0.0, 4.0), DiscRadius, DiscRadius * 0.97, 24, Rim, Rim, true, true);
		// Cara de arriba: dos coronas de cuñas y una cúpula.
		AddWedgeRing(Buffers, 4.2, DiscRadius * 0.55, DiscRadius * 0.96, 12, Outer, Mid);
		AddWedgeRing(Buffers, 4.2, DiscRadius * 0.24, DiscRadius * 0.55, 8, Mid, Outer);
		TNPlaygroundKit::AddFrustum(Buffers, FVector(0.0, 0.0, 4.2), FVector(0.0, 0.0, 9.0), DiscRadius * 0.24, DiscRadius * 0.14, 14, Hub, Hub, false, true);
		// Cara de abajo: una corona de color.
		TNPlaygroundKit::AddAnnulus(Buffers, FVector(0.0, 0.0, -3.2), -Up, DiscRadius * 0.3, DiscRadius * 0.94, 24, Mid);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Lanzamiento
// ─────────────────────────────────────────────────────────────────────────────

ATN_RaceFrisbee::ATN_RaceFrisbee()
{
}

void ATN_RaceFrisbee::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Fijos desde que nace: lo que hace falta para calcular la trayectoria en cada máquina.
	DOREPLIFETIME_CONDITION(ATN_RaceFrisbee, Origin, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ATN_RaceFrisbee, Dir, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ATN_RaceFrisbee, Curve, COND_InitialOnly);
}

bool ATN_RaceFrisbee::ServerThrow(ATortugaCharacter* Turtle, const FVector& Direction)
{
	using namespace TNRaceFrisbeeDetail;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	if (!World || !Turtle->HasAuthority())
	{
		return false;
	}
	if (CountOf(World, StaticClass()) >= MaxInWorld)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s no puede lanzar el disco: ya hay demasiados en el mundo."), *GetNameSafe(Turtle));
		return false;
	}
	// Solo cuenta hacia dónde mira en el plano: el disco vuela a ras de la arena.
	FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		Flat = Turtle->GetActorForwardVector().GetSafeNormal2D();
		if (Flat.IsNearlyZero())
		{
			Flat = FVector::ForwardVector;
		}
	}
	const FVector Start = Turtle->GetActorLocation() + Flat * SpawnForward;
	// Con el punto en pantalla, el disco sale hacia él (solo el rumbo: vuela a ras de la arena).
	FVector CrosshairPoint;
	if (Turtle->GetCrosshairPoint(CrosshairPoint))
	{
		const FVector ToPoint = (CrosshairPoint - Start).GetSafeNormal2D();
		if (!ToPoint.IsNearlyZero())
		{
			Flat = ToPoint;
		}
	}
	const FTransform SpawnXf(FRotator(0.0, Flat.Rotation().Yaw, 0.0), Start);
	ATN_RaceFrisbee* Disc = World->SpawnActorDeferred<ATN_RaceFrisbee>(StaticClass(), SpawnXf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Disc)
	{
		return false;
	}
	// Antes de FinishSpawning: el actor se replica con quien lo lanzó y con estos parámetros.
	Disc->SetOwnerTurtle(Turtle);
	Disc->Origin = FVector_NetQuantize10(Start);
	Disc->Dir = FVector_NetQuantizeNormal(Flat);
	Disc->Curve = FMath::RandBool() ? static_cast<int8>(1) : static_cast<int8>(-1);
	Disc->FinishSpawning(SpawnXf);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s lanza el disco."), *GetNameSafe(Turtle));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Trayectoria
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_RaceFrisbee::ComputePosition(double Seconds, int32& OutLeg, double& OutBackAlpha) const
{
	using namespace TNRaceFrisbeeDetail;
	OutBackAlpha = 0.0;
	FVector Fwd = Dir.GetSafeNormal2D();
	if (Fwd.IsNearlyZero())
	{
		Fwd = FVector::ForwardVector;
	}
	const FVector Aside = FVector(-Fwd.Y, Fwd.X, 0.0) * static_cast<double>(Curve);
	const FVector Base(Origin.X, Origin.Y, 0.0);
	const FVector TurnPoint = Base + Fwd * OutDistance + Aside * OutCurve;
	FVector Flat = Base;
	if (Seconds < OutSeconds)
	{
		// Ida: frena hacia el final (llega parada al punto de giro) y se abre de lado con una curva suave.
		OutLeg = 0;
		const double Along = FMath::Clamp(Seconds / OutSeconds, 0.0, 1.0);
		const double Ahead = OutDistance * (1.0 - FMath::Square(1.0 - Along));
		const double Sideways = OutCurve * Along * Along * (3.0 - 2.0 * Along);
		Flat = Base + Fwd * Ahead + Aside * Sideways;
	}
	else if (Seconds < OutSeconds + TurnSeconds)
	{
		// Giro: parado en el punto de vuelta, dando vueltas sobre sí mismo.
		OutLeg = 1;
		Flat = TurnPoint;
	}
	else
	{
		// Vuelta: sale despacio, acelera y se abre hacia el mismo lado; el destino es quien lo lanzó, donde esté ahora.
		OutLeg = 2;
		const double Back = FMath::Clamp((Seconds - OutSeconds - TurnSeconds) / BackSeconds, 0.0, 1.0);
		OutBackAlpha = Back;
		FVector Goal = Base;
		const ATortugaCharacter* Thrower = GetOwnerTurtle();
		if (IsValid(Thrower) && !Thrower->IsDead())
		{
			const FVector ThrowerAt = Thrower->GetActorLocation();
			Goal = FVector(ThrowerAt.X, ThrowerAt.Y, 0.0);
		}
		Flat = TurnPoint + (Goal - TurnPoint) * (Back * Back) + Aside * (BackBulge * FMath::Sin(UE_DOUBLE_PI * Back));
	}
	const float GroundZ = GroundHeightAt(FVector(Flat.X, Flat.Y, Origin.Z), static_cast<float>(Origin.Z - GroundGuess));
	return FVector(Flat.X, Flat.Y, static_cast<double>(GroundZ) + FlyHeight);
}

void ATN_RaceFrisbee::UpdatePose(double Seconds, FVector& OutPrev, int32& OutLeg, double& OutBackAlpha)
{
	OutPrev = GetActorLocation();
	const FVector Next = ComputePosition(Seconds, OutLeg, OutBackAlpha);
	const FVector Moved = Next - OutPrev;
	if (Moved.SizeSquared2D() > 4.0)
	{
		HeadingYaw = FMath::RadiansToDegrees(FMath::Atan2(Moved.Y, Moved.X));
	}
	CurrentLeg = OutLeg;
	SetActorLocationAndRotation(Next, FRotator(0.0, HeadingYaw, 0.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceFrisbee::ServerTick(float DeltaSeconds)
{
	using namespace TNRaceFrisbeeDetail;
	const double Seconds = GetAge();
	FVector Prev = FVector::ZeroVector;
	int32 Leg = 0;
	double BackAlpha = 0.0;
	UpdatePose(Seconds, Prev, Leg, BackAlpha);
	if (Leg != LastLeg)
	{
		// Al acabar la ida (aunque un tirón de fotograma se salte el giro) empieza la segunda pasada: cada uno puede volver a
		// llevarse un golpe.
		if (LastLeg == 0)
		{
			HitThisPass.Reset();
		}
		LastLeg = Leg;
	}
	const FVector Cur = GetActorLocation();
	ServerSweepHits(Prev, Cur);

	// Lo caza quien lo lanzó (en la vuelta, a menos de 2 m) o se acaba el tiempo.
	const ATortugaCharacter* Thrower = GetOwnerTurtle();
	if (Leg == 2 && IsValid(Thrower) && !Thrower->IsDead() && FVector::Dist(Cur, Thrower->GetActorLocation()) < CatchRadius)
	{
		ServerFinish(FinishLingerSeconds);
		return;
	}
	if (Seconds >= MaxSeconds || (Leg == 2 && BackAlpha >= 1.0))
	{
		ServerFinish(FinishLingerSeconds);
	}
}

void ATN_RaceFrisbee::ServerSweepHits(const FVector& Prev, const FVector& Cur)
{
	using namespace TNRaceFrisbeeDetail;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Con la carrera parada («¡TIEMPO!», recuento, título del sprint, podio) el disco sigue su camino pero no derriba ni marea
	// a nadie (#72), como la mina, el cangrejo y la gaviota.
	if (!ATN_BeachEnemy::IsRaceLive(this))
	{
		return;
	}
	const ATortugaCharacter* Thrower = GetOwnerTurtle();
	// Los golpes empujan en el sentido en que va el disco (en la vuelta, hacia quien lo lanzó).
	FVector Heading = (Cur - Prev).GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		Heading = Dir.GetSafeNormal2D();
	}

	// Tortugas (nunca la que lo lanzó): derribo con ragdoll.
	TArray<ATortugaCharacter*> Racers;
	ATN_BeachEnemy::GatherTurtles(this, Racers);
	for (ATortugaCharacter* Victim : Racers)
	{
		if (!Victim || Victim == Thrower || Victim->IsDead())
		{
			continue;
		}
		const TWeakObjectPtr<AActor> VictimKey(Victim);
		if (HitThisPass.Contains(VictimKey))
		{
			continue;
		}
		if (!TNRaceItems::CanBeHurt(Victim) || !ATN_BeachEnemy::CanBeHit(Victim))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent();
		if (!Capsule)
		{
			continue;
		}
		// El disco es una esfera que barre de Prev a Cur; la tortuga, su cápsula (un segmento vertical con radio).
		const double CapsuleRadius = static_cast<double>(Capsule->GetScaledCapsuleRadius());
		const double CapsuleHalf = static_cast<double>(Capsule->GetScaledCapsuleHalfHeight());
		const FVector CapsuleCenter = Victim->GetActorLocation();
		const FVector AxisHalf(0.0, 0.0, FMath::Max(0.0, CapsuleHalf - CapsuleRadius));
		FVector OnDisc = Cur;
		FVector OnTurtle = CapsuleCenter;
		FMath::SegmentDistToSegmentSafe(Prev, Cur, CapsuleCenter - AxisHalf, CapsuleCenter + AxisHalf, OnDisc, OnTurtle);
		if (FVector::DistSquared(OnDisc, OnTurtle) > FMath::Square(HitRadius + CapsuleRadius))
		{
			continue;
		}
		HitThisPass.Add(VictimKey);
		TNBeach::KnockDownTurtle(Victim, KnockSeconds, Heading * KnockPush + FVector(0.0, 0.0, KnockLift));
		MulticastHit(FVector_NetQuantize10(OnTurtle));
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] El disco de %s derriba a %s."), *GetNameSafe(Thrower), *GetNameSafe(Victim));
	}

	// Enemigos que se dejan marear: pajaritos y estrellas.
	for (TActorIterator<ATN_BeachEnemy> It(World); It; ++It)
	{
		ATN_BeachEnemy* Enemy = *It;
		if (!IsValid(Enemy) || !Enemy->AcceptsHitStun())
		{
			continue;
		}
		const TWeakObjectPtr<AActor> EnemyKey(Enemy);
		if (HitThisPass.Contains(EnemyKey))
		{
			continue;
		}
		FVector CapsuleA = FVector::ZeroVector;
		FVector CapsuleB = FVector::ZeroVector;
		float BodyRadius = 0.f;
		if (!Enemy->GetHitCapsule(CapsuleA, CapsuleB, BodyRadius))
		{
			continue;
		}
		FVector OnDisc = Cur;
		FVector OnEnemy = CapsuleA;
		FMath::SegmentDistToSegmentSafe(Prev, Cur, CapsuleA, CapsuleB, OnDisc, OnEnemy);
		if (FVector::DistSquared(OnDisc, OnEnemy) > FMath::Square(HitRadius + static_cast<double>(BodyRadius)))
		{
			continue;
		}
		HitThisPass.Add(EnemyKey);
		Enemy->ApplyHitStun(EnemyStunSeconds, GetOwnerTurtle());
		MulticastHit(FVector_NetQuantize10(OnEnemy));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceFrisbee::BuildVisuals()
{
	using namespace TNRaceFrisbeeDetail;
	USceneComponent* RootComp = GetRootComponent();
	if (!RootComp)
	{
		return;
	}

	// La malla del arte (~30 cm) escalada; sin ella, un disco de cuñas de colores de respaldo (colores según dónde salió,
	// iguales en todas las máquinas).
	TNRaceItemArt::FHeldLook Look;
	UStaticMesh* DiscAsset = nullptr;
	double DiscScale = 1.0;
	if (TNRaceItemArt::GetHeldLook(ETNRaceItem::Frisbee, Look) && Look.Mesh)
	{
		DiscAsset = Look.Mesh;
		DiscScale = ArtScale;
	}
	else
	{
		const int32 ColorSeed = FMath::FloorToInt32(Origin.X * 0.1) * 73 + FMath::FloorToInt32(Origin.Y * 0.1) * 151;
		const int32 IndexA = ((ColorSeed % 7) + 7) % 7;
		const int32 IndexB = (IndexA + 3) % 7;
		DiscAsset = SharedMesh(KeyFallbackDisc + IndexA, [IndexA, IndexB](FDiscBuffers& Buffers) { BuildFallbackDisc(Buffers, IndexA, IndexB); });
	}
	DiscMesh = TNBeachKit::AddPart(this, RootComp, DiscAsset, FVector::ZeroVector, true);
	if (DiscMesh)
	{
		DiscMesh->SetRelativeScale3D(FVector(DiscScale));
	}
	ShadowMesh = TNBeachKit::AddShadow(this, 0.42f);
	TNBeachKit::PlaceShadow(ShadowMesh, FVector::ZeroVector, 0.f);
	EnsureFlightFX();
}

void ATN_RaceFrisbee::EnsureFlightFX()
{
	using TNAmbientFX::EShape;
	const uint32 Seed = GetTypeHash(GetFName());
	// Estela: nubecillas blancas azuladas que se quedan atrás.
	TNAmbientFX::FEmitterDesc TrailDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.85f, 0.95f, 1.f), true, 0.32f, 28, 42.f, 40.f, 0.f, 0.35f, 0.55f, 55.f, 18.f);
	TrailDesc.Spread = 0.6f;
	TrailDesc.SpawnRadius = 15.f;
	TrailDesc.Drag = 1.f;
	TNBeachKit::InitEmitter(Trail, this, TrailDesc, Seed + 1u);
	// Destellos dorados.
	TNAmbientFX::FEmitterDesc GlitterDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(1.f, 0.85f, 0.35f), true, 0.95f, 26, 36.f, 140.f, -200.f, 0.35f, 0.65f, 16.f, 4.f);
	GlitterDesc.Spread = 1.2f;
	GlitterDesc.SpawnRadius = 35.f;
	TNBeachKit::InitEmitter(Glitter, this, GlitterDesc, Seed + 2u);
}

void ATN_RaceFrisbee::EnsureImpactFX()
{
	// Los golpes y la nubecilla del final se crean la primera vez que hacen falta.
	if (bImpactFXReady || !bHasScreen)
	{
		return;
	}
	bImpactFXReady = true;
	using TNAmbientFX::EShape;
	const uint32 Seed = GetTypeHash(GetFName());
	TNAmbientFX::FEmitterDesc ImpactDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(1.f, 0.95f, 0.6f), true, 0.95f, 24, 0.f, 900.f, -700.f, 0.3f, 0.6f, 28.f, 6.f);
	ImpactDesc.Spread = 1.4f;
	ImpactDesc.SpawnRadius = 30.f;
	TNBeachKit::InitEmitter(Impact, this, ImpactDesc, Seed + 3u);
	TNAmbientFX::FEmitterDesc PoofDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.95f, 0.95f, 0.98f), true, 0.5f, 16, 0.f, 350.f, 0.f, 0.4f, 0.7f, 70.f, 200.f);
	PoofDesc.Spread = 1.2f;
	PoofDesc.SpawnRadius = 30.f;
	TNBeachKit::InitEmitter(Poof, this, PoofDesc, Seed + 4u);
}

void ATN_RaceFrisbee::VisualTick(float DeltaSeconds)
{
	using namespace TNRaceFrisbeeDetail;
	const double Seconds = GetAge();
	if (!HasAuthority())
	{
		// Los clientes lo colocan con la misma fórmula; el servidor ya lo ha movido en ServerTick.
		FVector Prev = FVector::ZeroVector;
		int32 Leg = 0;
		double BackAlpha = 0.0;
		UpdatePose(Seconds, Prev, Leg, BackAlpha);
	}
	const FVector Where = GetActorLocation();

	// Gira sobre sí mismo con una ligera inclinación hacia el lado de la curva (y un bamboleo).
	if (DiscMesh)
	{
		const double SpinAngle = FMath::Fmod(Seconds * SpinRate, 360.0);
		const double Bank = BankDegrees * (CurrentLeg == 1 ? 0.5 : 1.0) * static_cast<double>(Curve);
		const double Wobble = WobbleDegrees * FMath::Sin(Seconds * 9.0);
		const FQuat SpinQuat(FVector::UpVector, FMath::DegreesToRadians(SpinAngle));
		const FQuat TiltQuat = FRotator(Wobble, 0.0, Bank).Quaternion();
		DiscMesh->SetRelativeRotation(TiltQuat * SpinQuat);
	}
	if (ShadowMesh)
	{
		TNBeachKit::PlaceShadow(ShadowMesh, FVector(Where.X, Where.Y, Where.Z - FlyHeight), ShadowRadius);
	}

	// Estela y destellos: nacen donde está el disco.
	Trail.Origin = Where;
	Trail.RateScale = 1.f;
	Glitter.Origin = Where;
	Glitter.RateScale = 1.f;

	// Zumbido mientras vuela, si hay quien lo oiga.
	WhirrClock -= DeltaSeconds;
	if (WhirrClock <= 0.f)
	{
		WhirrClock = WhirrPeriod;
		if (ATN_BeachEnemy::LocalViewDistance(this, Where) < WhirrRange)
		{
			PlaySfx(ETNRaceSound::Whirr, 1.f, 0.8f);
		}
	}
	TickFX(DeltaSeconds);
}

void ATN_RaceFrisbee::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// La base deja de llamar a VisualTick al acabar: las partículas que quedan siguen su curso.
	if (bHasScreen && IsFinished())
	{
		TickFX(DeltaSeconds);
	}
}

void ATN_RaceFrisbee::TickFX(float DeltaSeconds)
{
	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);
	TNBeachKit::TickEmitterIfBusy(Trail, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Glitter, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Impact, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Poof, DeltaSeconds, View);
	BonkText.Tick(DeltaSeconds, GetWorld());
}

void ATN_RaceFrisbee::MulticastHit_Implementation(FVector_NetQuantize10 Where)
{
	if (!bHasScreen || !HasActorBegunPlay())
	{
		return;
	}
	EnsureImpactFX();
	TNBeachKit::BurstAt(Impact, Where, FVector::UpVector, 14);
	TNBeachKit::BurstAt(Poof, Where, FVector::UpVector, 5);
	PlaySfx(ETNRaceSound::Bonk, FMath::FRandRange(0.9f, 1.1f), 1.f);
	BonkText.Show(this, NSLOCTEXT("TNRace", "FrisbeeBonk", "¡BONK!"), FColor(255, 226, 90), Where + FVector(0.0, 0.0, 190.0), 130.f);
	UTN_BeachCameraShake::Kick(this, Where, 0.25f, 300.f, 1600.f);
}

void ATN_RaceFrisbee::OnFinished()
{
	if (!bHasScreen)
	{
		return;
	}
	if (DiscMesh)
	{
		DiscMesh->SetVisibility(false);
	}
	if (ShadowMesh)
	{
		TNBeachKit::PlaceShadow(ShadowMesh, FVector::ZeroVector, 0.f);
	}
	Trail.RateScale = 0.f;
	Glitter.RateScale = 0.f;
	if (!HasActorBegunPlay())
	{
		return;
	}
	// «¡Clap!» y una nubecilla donde acaba (cazado o, si nadie lo caza, al desaparecer).
	EnsureImpactFX();
	const FVector Where = GetActorLocation();
	TNBeachKit::BurstAt(Poof, Where, FVector::UpVector, 6);
	TNBeachKit::BurstAt(Impact, Where, FVector::UpVector, 8);
	PlaySfx(ETNRaceSound::Catch, 1.f, 1.f);
}
