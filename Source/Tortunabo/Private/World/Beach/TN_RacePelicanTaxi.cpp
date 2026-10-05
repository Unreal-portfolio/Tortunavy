#include "World/Beach/TN_RacePelicanTaxi.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/Beach/TN_RaceItemSynth.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TortugaCharacter.h"

namespace TNRacePelicanDetail
{
	/** Fases del plan (FTNPelicanTaxiPlan::Phase). */
	constexpr uint8 PhaseNone = 0;
	constexpr uint8 PhaseFlying = 1;
	constexpr uint8 PhaseReleased = 2;
	constexpr uint8 PhaseAborted = 3;

	// ── Plan (servidor, al usar el objeto) ──
	/**
	 * La deja DesiredDistance por delante (cm, hacia el mar), recortado para que el sitio quede a CliffMargin o más del
	 * filo del acantilado (no se salta la meta). Con menos de MinDistance útiles no sale. El sitio se busca con
	 * TNBeach::FindOpenSandSpot en SpotSearchRadius; si no hay, se acorta un RetryShrink (hasta SpotRetries veces, nunca
	 * por debajo de MinDistance). Un sitio encontrado vale si queda al menos MinAheadFraction de MinDistance por delante.
	 */
	constexpr float DesiredDistance = 12000.f;
	constexpr float MinDistance = 3000.f;
	constexpr float CliffMargin = 5500.f;
	constexpr float SpotSearchRadius = 3000.f;
	constexpr int32 SpotRetries = 5;
	constexpr float RetryShrink = 0.8f;
	constexpr float MinAheadFraction = 0.5f;
	/**
	 * Altura de crucero (centro de la cápsula): la cota más alta del suelo muestreada cada GroundSampleStep por el camino
	 * más CruiseClearance, y al menos MinLiftOverStart sobre donde la coge. Lo alto que hay encima del suelo (castillos,
	 * fortalezas) se mira con una traza desde StructureProbeUp por encima, contando como mucho StructureCap.
	 */
	constexpr float GroundSampleStep = 500.f;
	constexpr float CruiseClearance = 2600.f;
	constexpr float MinLiftOverStart = 1200.f;
	constexpr float StructureProbeUp = 6000.f;
	constexpr float StructureProbeDown = 3000.f;
	constexpr float StructureCap = 4000.f;
	/** Taxis a la vez en el mundo como mucho. */
	constexpr int32 MaxTaxis = 8;

	// ── Línea de tiempo (segundos desde Plan.StartTime) ──
	/**
	 * Aproximación (la tortuga ya sujeta y quieta), subida hasta el crucero acelerando, crucero a CruiseSpeed (su duración
	 * sale de la distancia), descenso hasta ReleaseHeight sobre el sitio y despedida. En la subida la altura se alcanza en
	 * ClimbRiseFraction de su tiempo (sube casi en vertical al principio); en el crucero se mece CruiseBobHeight arriba y
	 * abajo CruiseBobRate veces por segundo.
	 */
	constexpr float ApproachSeconds = 1.f;
	constexpr float ClimbSeconds = 1.3f;
	constexpr float CruiseSpeed = 2200.f;
	constexpr float DescentSeconds = 1.5f;
	constexpr float FarewellSeconds = 2.5f;
	constexpr float ClimbRiseFraction = 0.85f;
	constexpr float CruiseBobHeight = 60.f;
	constexpr float CruiseBobRate = 0.6f;
	/** La suelta a esta altura (cm) sobre el sitio, de pie, sin caída larga y con StormGraceSeconds de gracia de la tormenta. */
	constexpr float ReleaseHeight = 260.f;
	constexpr float StormGraceSeconds = 3.f;
	/**
	 * Servidor: se destruye EndGraceSeconds después de la despedida (o a MaxLifeSeconds pase lo que pase). Despedida y gracia
	 * suman más que la vigilancia de la soltada de ATN_BeachEnemy (3 s): así acaba antes de que el actor desaparezca.
	 */
	constexpr float EndGraceSeconds = 1.f;
	constexpr float MaxLifeSeconds = 40.f;
	/**
	 * Seguro de tiempo de la sujeción (GetMaxHoldSeconds): el de ATN_BeachEnemy (6 s) es para un mordisco o un picado y
	 * este vuelo dura más. Una sola sujeción, continua de principio a fin del vuelo: soltarla y volver a sujetarla en el
	 * mismo fotograma hacía parpadear la pataleta, las correcciones de red y el suavizado.
	 */
	constexpr double HoldSafetySeconds = 20.0;
	// El vuelo más largo (el sitio a DesiredDistance + SpotSearchRadius) tiene que caber de sobra en el seguro.
	static_assert(HoldSafetySeconds > ApproachSeconds + ClimbSeconds + DescentSeconds + (DesiredDistance + SpotSearchRadius) / CruiseSpeed + 2.0,
		"El seguro de la sujeción del pelícano taxi es más corto que su vuelo más largo.");

	// ── Pelícano ──
	/** Escala de la fauna (como el pelícano de la zona de gaviotas) y su envergadura (cm). */
	constexpr float PelicanScale = 24.f;
	constexpr float PelicanSpan = 170.f * PelicanScale;
	/** Del centro de la cápsula de pie a la espalda que va en el pico (fracción de media cápsula); sin clase, 88 cm. */
	constexpr float GripDropFraction = 0.75f;
	constexpr float DefaultHalfHeight = 88.f;
	/** De la espalda (hueso Spine2) a la superficie del caparazón que muerde el pico (cm). */
	constexpr float ShellBack = 35.f;
	/** Aproximación: baja desde ApproachBack por detrás y ApproachUp por encima, en picado y con la cabeza algo baja. */
	constexpr float ApproachBack = 4500.f;
	constexpr float ApproachUp = 3500.f;
	constexpr float ApproachDivePitch = -30.f;
	constexpr float ApproachHeadPitch = -15.f;
	/**
	 * Cabeceo del cuerpo al cogerla y al soltarla (un poco morro abajo) y el del vuelo (de la pendiente del camino, entre
	 * MinCarryPitch y MaxCarryPitch). Cerca del suelo alarga la cabeza hacia abajo (ReachHeadPitch) para que el cuerpo no
	 * toque la arena; en el crucero la lleva a CruiseHeadPitch, sacudiéndola HeadShake grados. Se balancea CarryRoll grados.
	 */
	constexpr float GrabPitch = -10.f;
	constexpr float ReleasePitch = -8.f;
	constexpr float MinCarryPitch = -20.f;
	constexpr float MaxCarryPitch = 22.f;
	constexpr float ReachHeadPitch = -72.f;
	constexpr float CruiseHeadPitch = -40.f;
	constexpr float HeadShake = 3.f;
	constexpr float CarryRoll = 4.f;
	/** Despedida: acelera hacia delante y hacia arriba (cm/s²), se abre hacia un lado y levanta el morro. */
	constexpr float FarewellAccel = 1400.f;
	constexpr float FarewellClimbAccel = 1200.f;
	constexpr float FarewellLift = 300.f;
	constexpr float FarewellVeer = 500.f;
	constexpr float FarewellTurn = 25.f;
	constexpr float FarewellPitch = 20.f;
	constexpr float FarewellBank = 15.f;
	/** Ritmo de las alas respecto al de una gaviota y rapidez con la que el giro que se ve sigue al de la fórmula. */
	constexpr float WingRate = 0.7f;
	constexpr float RotSmoothSpeed = 8.f;

	// ── Efectos ──
	/** Graznido grave del pelícano, un aleteo cada FlapSoundPeriod s y el aterrizaje (como mucho LandTimeout s tras soltarla). */
	constexpr float SquawkPitch = 0.55f;
	constexpr float FlapSoundPeriod = 0.6f;
	constexpr float LandTimeout = 2.5f;
	/** Quien entra a mitad no oye lo que pasó hace más de esto (s). */
	constexpr float LateEventSeconds = 0.4f;
	/** Más lejos de la cámara (cm) no se anima ni suena. */
	constexpr float VisualRange = 60000.f;

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	inline double Smooth01(double X)
	{
		const double C = FMath::Clamp(X, 0.0, 1.0);
		return C * C * (3.0 - 2.0 * C);
	}

	/** Vaivén del crucero: cero al empezar y al acabar (U: segundos de crucero; Duration: lo que dura). */
	inline double CruiseBob(double U, double Duration)
	{
		if (Duration < 0.1)
		{
			return 0.0;
		}
		return CruiseBobHeight * FMath::Sin(PI * U / Duration) * FMath::Sin(2.0 * PI * CruiseBobRate * U);
	}

	/** Media cápsula de pie de la clase de la tortuga (en el panzazo, la de la tortuga es más baja). */
	inline float StandHalfHeight(const ACharacter* Turtle)
	{
		const ACharacter* Defaults = Turtle ? Turtle->GetClass()->GetDefaultObject<ACharacter>() : nullptr;
		const UCapsuleComponent* Capsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
		return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : DefaultHalfHeight;
	}

	/** Servidor: empieza o acaba el vuelo en el componente de efectos de la tortuga (invulnerable y sin objetos). */
	inline void SetVictimRiding(ATortugaCharacter* Victim, bool bRiding)
	{
		if (!IsValid(Victim) || !Victim->HasAuthority())
		{
			return;
		}
		UTN_RaceItemComponent* Comp = bRiding ? UTN_RaceItemComponent::FindOrAddOn(Victim) : UTN_RaceItemComponent::FindOn(Victim);
		if (Comp)
		{
			Comp->SetRiding(bRiding);
		}
	}

	/** Por qué ya no se puede llevar a la tortuga (null si se puede). */
	inline const TCHAR* WhyVictimLost(const ATortugaCharacter* Victim)
	{
		if (!IsValid(Victim) || Victim->IsActorBeingDestroyed())
		{
			return TEXT("ya no está");
		}
		if (Victim->IsDead())
		{
			return TEXT("ha muerto");
		}
		if (Victim->IsInShell())
		{
			return TEXT("se ha metido en el caparazón");
		}
		if (Victim->IsKnockedDown())
		{
			return TEXT("la han derribado");
		}
		if (ATN_BeachSandWorm::IsBeingEaten(Victim))
		{
			return TEXT("se la come un gusano de arena");
		}
		if (const ATN_CoopPlayerState* Coop = Victim->GetPlayerState<ATN_CoopPlayerState>())
		{
			if (Coop->bHasFinishedRun)
			{
				return TEXT("ha llegado a la meta");
			}
			if (!Coop->IsAliveAndPlaying())
			{
				return TEXT("ya no está en juego");
			}
		}
		return nullptr;
	}

	/**
	 * Suelo bajo Where para la altura de crucero: el del generador (o Fallback sin él) y, encima, lo más alto que pare una
	 * traza desde arriba (castillos, fortalezas), como mucho StructureCap más.
	 */
	inline float GroundForCruise(const ATortugaCharacter* Turtle, const ATN_BeachRaceGenerator* Gen, const FVector& Where, float Fallback)
	{
		float Base = Gen ? Gen->GetGroundHeightAt(Where) : Fallback;
		float HitZ = 0.f;
		if (ATN_BeachEnemy::TraceGround(Turtle, FVector(Where.X, Where.Y, Base), HitZ, nullptr, StructureProbeUp, StructureProbeDown))
		{
			Base = FMath::Max(Base, FMath::Min(HitZ, Base + StructureCap));
		}
		return Base;
	}

	/**
	 * Servidor: el plan de vuelo de Turtle (sitio de aterrizaje y altura de crucero). false si no hay sitio por delante;
	 * sin generador (otro mapa de pruebas), hacia +X del mundo, sin acantilado y con el suelo por traza.
	 */
	bool BuildPlan(ATortugaCharacter* Turtle, FTNPelicanTaxiPlan& Out)
	{
		const ATN_BeachRaceGenerator* Gen = ATN_BeachRaceGenerator::Find(Turtle);
		const FVector StartAt = Turtle->GetActorLocation();
		FVector Sea = Gen ? Gen->GetSeaDirection() : FVector::ForwardVector;
		Sea.Z = 0.0;
		if (!Sea.Normalize())
		{
			Sea = FVector::ForwardVector;
		}
		float Distance = DesiredDistance;
		if (Gen)
		{
			// GetCliffEdgeDistance es negativa antes del filo: lo que queda hasta él menos el margen.
			Distance = FMath::Min(Distance, -Gen->GetCliffEdgeDistance(StartAt) - CliffMargin);
		}
		if (Distance < MinDistance)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: %s está demasiado cerca del filo (%.0f m útiles)."), *GetNameSafe(Turtle),
				FMath::Max(0.f, Distance) / 100.f);
			return false;
		}
		const float HalfHeight = StandHalfHeight(Turtle);
		FVector Site = FVector::ZeroVector;
		bool bFound = false;
		for (int32 Try = 0; Try <= SpotRetries && !bFound; ++Try)
		{
			const FVector Desired = StartAt + Sea * Distance;
			if (Gen)
			{
				FTransform Spot;
				if (TNBeach::FindOpenSandSpot(Turtle, Desired, SpotSearchRadius, Spot))
				{
					const FVector At = Spot.GetLocation();
					const double Ahead = FVector::DotProduct(At - StartAt, Sea);
					// El anillo de búsqueda puede ir más allá del punto pedido: el filo, otra vez; y que siga siendo por delante.
					if (Gen->GetCliffEdgeDistance(At) <= -CliffMargin && Ahead >= static_cast<double>(MinDistance * MinAheadFraction))
					{
						Site = At;
						bFound = true;
					}
				}
			}
			else
			{
				float GroundZ = 0.f;
				if (ATN_BeachEnemy::TraceGround(Turtle, Desired, GroundZ, nullptr, 3000.f, 8000.f))
				{
					Site = FVector(Desired.X, Desired.Y, GroundZ + HalfHeight + 5.f);
					bFound = true;
				}
			}
			if (!bFound)
			{
				const float Shorter = FMath::Max(MinDistance, Distance * RetryShrink);
				if (Shorter >= Distance - 1.f)
				{
					break;
				}
				Distance = Shorter;
			}
		}
		if (!bFound)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: no hay arena abierta por delante de %s donde dejarla."), *GetNameSafe(Turtle));
			return false;
		}

		// Altura de crucero: por encima de todo lo que hay por el camino (y de la salida y el sitio).
		const double PathLength = FVector::Dist2D(StartAt, Site);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(static_cast<float>(PathLength) / GroundSampleStep));
		float MaxGround = -1.0e9f;
		for (int32 k = 0; k <= Steps; ++k)
		{
			const FVector Sample = FMath::Lerp(StartAt, Site, static_cast<double>(k) / static_cast<double>(Steps));
			MaxGround = FMath::Max(MaxGround, GroundForCruise(Turtle, Gen, Sample, static_cast<float>(Sample.Z) - HalfHeight));
		}
		const float CruiseZ = FMath::Max(MaxGround + CruiseClearance, static_cast<float>(StartAt.Z) + MinLiftOverStart);

		Out = FTNPelicanTaxiPlan();
		Out.Victim = Turtle;
		Out.Start = StartAt;
		Out.End = Site;
		Out.CruiseZ = CruiseZ;
		Out.CourseYaw = static_cast<float>(Sea.Rotation().Yaw);
		Out.StartTime = static_cast<float>(ATN_BeachEnemy::ServerNow(Turtle));
		Out.EndTime = 0.f;
		Out.Phase = PhaseFlying;
		Out.Serial = 1;
		return true;
	}

	/** Sombra del pelícano: en Ground con radio Radius (0 la esconde), opacidad Opacity y nitidez Sharp (0-1, por tramos). */
	inline void PlacePelicanShadow(UStaticMeshComponent* Comp, int32& Bucket, const FVector& Ground, float Radius, float Opacity, float Sharp)
	{
		if (!Comp)
		{
			return;
		}
		TNBeachKit::PlaceShadow(Comp, Ground, Radius);
		if (Radius <= 1.f)
		{
			return;
		}
		static const float InnerFrac[3] = { 0.35f, 0.6f, 0.85f };
		const int32 Wanted = Sharp < 0.33f ? 0 : (Sharp < 0.66f ? 1 : 2);
		if (Wanted != Bucket)
		{
			Bucket = Wanted;
			Comp->SetStaticMesh(TNBeachKit::ShadowDiscEdge(InnerFrac[Wanted]));
		}
		TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
	}

	/** Un sonido del sintetizador en Where. */
	inline void PlayAt(UTN_RaceItemSynthComponent* Synth, ETNRaceSound Sound, const FVector& Where, float Pitch, float Volume)
	{
		if (Synth)
		{
			Synth->SetWorldLocation(Where);
			Synth->Play(Sound, Pitch, Volume);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Creación y red
// ─────────────────────────────────────────────────────────────────────────────

ATN_RacePelicanTaxi::ATN_RacePelicanTaxi()
{
	// Sin Mover: el vuelo es una fórmula del reloj del servidor, igual en todas las máquinas (ni Rig ni ShownLoc).
	bUsesMover = false;
	NetFrequencyNear = 10.f;
	SetNetUpdateFrequency(NetFrequencyNear);
}

void ATN_RacePelicanTaxi::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RacePelicanTaxi, Plan);
}

bool ATN_RacePelicanTaxi::ServerLaunch(ATortugaCharacter* Turtle)
{
	using namespace TNRacePelicanDetail;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	// Lo mismo que SyncHold pide para sujetarla (y lo que la haría abortar en ServerTick): si no, ni se crea.
	if (!World || !IsValid(Turtle) || !Turtle->HasAuthority() || WhyVictimLost(Turtle) != nullptr || IsTurtleHeld(Turtle))
	{
		return false;
	}
	// Uno por tortuga y unos pocos en el mundo.
	int32 Live = 0;
	for (TActorIterator<ATN_RacePelicanTaxi> It(World); It; ++It)
	{
		const ATN_RacePelicanTaxi* Other = *It;
		if (!IsValid(Other) || Other->IsActorBeingDestroyed())
		{
			continue;
		}
		if (Other->Plan.Phase == PhaseFlying && Other->Plan.Victim == Turtle)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: %s ya va en uno."), *GetNameSafe(Turtle));
			return false;
		}
		++Live;
	}
	if (Live >= MaxTaxis)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: ya hay %d en la playa."), Live);
		return false;
	}

	FTNPelicanTaxiPlan NewPlan;
	if (!BuildPlan(Turtle, NewPlan))
	{
		return false;
	}
	// Si llevaba a otra en brazos, la suelta (no debería: con alguien en brazos no se usan objetos).
	if (UTN_CarryComponent* Carry = Turtle->GetCarryComponent())
	{
		if (Carry->IsCarrying())
		{
			Carry->ForceRelease(false);
		}
	}
	FVector Heading = FVector(NewPlan.End) - FVector(NewPlan.Start);
	Heading.Z = 0.0;
	const float SpawnYaw = Heading.IsNearlyZero() ? NewPlan.CourseYaw : static_cast<float>(Heading.Rotation().Yaw);
	const FTransform SpawnXf(FRotator(0.f, SpawnYaw, 0.f), FVector(NewPlan.Start));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_RacePelicanTaxi* Taxi = World->SpawnActor<ATN_RacePelicanTaxi>(ATN_RacePelicanTaxi::StaticClass(), SpawnXf, Params);
	if (!Taxi)
	{
		return false;
	}
	// El plan va en la primera réplica y su BeginPlay (dentro de FinishSpawning) ya la sujeta.
	Taxi->Plan = NewPlan;
	Taxi->FinishSpawning(SpawnXf);
	Taxi->RefreshFlight();
	Taxi->SyncHold();
	if (Taxi->GetHeldTurtle() != Turtle)
	{
		// No se ha podido sujetar (otro sistema la tiene): no se crea nada y el objeto se queda. Sin plan, EndPlay no la toca.
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: no se puede coger a %s ahora."), *GetNameSafe(Turtle));
		Taxi->Plan.Phase = PhaseNone;
		Taxi->Destroy();
		return false;
	}
	SetVictimRiding(Turtle, true);
	Taxi->ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: se lleva a %s %.0f m por delante (crucero a %.0f m sobre la salida, suelta a los %.1f s)."),
		*GetNameSafe(Turtle), Taxi->Flight.Length / 100.f, (NewPlan.CruiseZ - static_cast<float>(NewPlan.Start.Z)) / 100.f, Taxi->Flight.Release);
	return true;
}

void ATN_RacePelicanTaxi::BeginPlay()
{
	Super::BeginPlay();
	RefreshFlight();
	// Quien entra con el plan ya en marcha (o el servidor, al crearlo) la sujeta ya.
	if (Plan.Phase == TNRacePelicanDetail::PhaseFlying)
	{
		SyncHold();
	}
}

void ATN_RacePelicanTaxi::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	using namespace TNRacePelicanDetail;
	if (HasAuthority() && Plan.Phase == PhaseFlying)
	{
		// Se va a media ruta (no debería): la tortuga deja de ser invulnerable y cae sin caída larga.
		ATortugaCharacter* Victim = Plan.Victim;
		Plan.Phase = PhaseAborted;
		if (EndPlayReason == EEndPlayReason::Destroyed)
		{
			SetVictimRiding(Victim, false);
			if (IsValid(Victim) && !Victim->IsDead() && !Victim->IsInShell() && !Victim->IsKnockedDown())
			{
				Victim->SetFallImmuneUntilLanded();
			}
		}
	}
	// La base la suelta del todo (EndHoldTurtle).
	Super::EndPlay(EndPlayReason);
}

void ATN_RacePelicanTaxi::OnRep_Plan()
{
	RefreshFlight();
	if (HasActorBegunPlay())
	{
		SyncHold();
	}
}

float ATN_RacePelicanTaxi::GetVisualRange() const
{
	return TNRacePelicanDetail::VisualRange;
}

double ATN_RacePelicanTaxi::GetMaxHoldSeconds() const
{
	return TNRacePelicanDetail::HoldSafetySeconds;
}

void ATN_RacePelicanTaxi::Tick(float DeltaSeconds)
{
	using namespace TNRacePelicanDetail;
	RefreshFlight();
	if (Plan.Phase != PhaseNone)
	{
		// La pose del fotograma y el actor con el cuerpo del pelícano (distancia a la cámara y sonido que lo siguen).
		PoseTime = Elapsed();
		PoseAt(PoseTime, PoseRoot, PoseRot, PoseHead);
		SetActorLocation(BodyCenter(PoseRoot, PoseRot));
	}
	Super::Tick(DeltaSeconds);
	// Después de la base (ServerTick decide la suelta) y del movimiento de la tortuga: la tortuga, en el pico.
	SyncHold();
}

// ─────────────────────────────────────────────────────────────────────────────
// Vuelo: las mismas cuentas en todas las máquinas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RacePelicanTaxi::RefreshFlight()
{
	using namespace TNRacePelicanDetail;
	FFlightTimes F;
	const FVector From = Plan.Start;
	const FVector To = Plan.End;
	FVector Delta = To - From;
	Delta.Z = 0.0;
	F.Length = static_cast<float>(Delta.Size());
	if (F.Length > 1.f)
	{
		F.Dir = Delta / static_cast<double>(F.Length);
	}
	else
	{
		F.Length = 0.f;
		F.Dir = FRotator(0.f, Plan.CourseYaw, 0.f).Vector();
		F.Dir.Z = 0.0;
		F.Dir = F.Dir.GetSafeNormal();
		if (F.Dir.IsNearlyZero())
		{
			F.Dir = FVector::ForwardVector;
		}
	}
	F.Yaw = static_cast<float>(F.Dir.Rotation().Yaw);
	// Subida y descenso cambian la velocidad con una curva suave: recorren la mitad de lo que se recorrería a velocidad de
	// crucero en ese tiempo. Lo que falta, en crucero; si no llega ni para eso, más despacio y sin crucero.
	const float Ramps = 0.5f * (ClimbSeconds + DescentSeconds);
	F.Speed = CruiseSpeed;
	if (F.Length < F.Speed * Ramps)
	{
		// Exacto (Ramps es una constante positiva): el camino acaba justo encima del sitio aunque la distancia sea mínima.
		F.Speed = F.Length / Ramps;
		F.CruiseSeconds = 0.f;
	}
	else
	{
		F.CruiseSeconds = (F.Length - F.Speed * Ramps) / F.Speed;
	}
	F.ClimbStart = ApproachSeconds;
	F.CruiseStart = F.ClimbStart + ClimbSeconds;
	F.DescentStart = F.CruiseStart + F.CruiseSeconds;
	F.Release = F.DescentStart + DescentSeconds;
	F.GripDrop = StandHalfHeight(Plan.Victim.Get()) * GripDropFraction;
	F.Spine0 = From + FVector(0.0, 0.0, F.GripDrop);
	F.CruiseSpineZ = Plan.CruiseZ + F.GripDrop;
	F.SpineEnd = To + FVector(0.0, 0.0, ReleaseHeight + F.GripDrop);
	Flight = F;
}

float ATN_RacePelicanTaxi::Elapsed() const
{
	return static_cast<float>(ServerNow(this) - static_cast<double>(Plan.StartTime));
}

float ATN_RacePelicanTaxi::FarewellStart() const
{
	using namespace TNRacePelicanDetail;
	if (Plan.Phase == PhaseAborted)
	{
		return FMath::Clamp(Plan.EndTime - Plan.StartTime, 0.f, Flight.Release);
	}
	// Dejada en el sitio, o aún llevándola: la suelta es a su hora (un cliente con el reloj adelantado ya se despide).
	return Flight.Release;
}

FVector ATN_RacePelicanTaxi::SpineAt(float T) const
{
	using namespace TNRacePelicanDetail;
	const FFlightTimes& F = Flight;
	const float Tc = FMath::Clamp(T, 0.f, F.Release);
	const double Z0 = F.Spine0.Z;
	const double Zc = FMath::Max(static_cast<double>(F.CruiseSpineZ), Z0);
	const double Z3 = F.SpineEnd.Z;
	const double V = F.Speed;
	double Along = 0.0;
	double Z = Z0;
	if (Tc >= F.ClimbStart && Tc < F.CruiseStart)
	{
		// Sube casi en vertical al principio y va cogiendo velocidad (la de crucero al acabar la subida).
		const double A = static_cast<double>(Tc - F.ClimbStart) / ClimbSeconds;
		Along = V * ClimbSeconds * (A * A * A - 0.5 * A * A * A * A);
		Z = Z0 + (Zc - Z0) * Smooth01(A / ClimbRiseFraction);
	}
	else if (Tc >= F.CruiseStart && Tc < F.DescentStart)
	{
		const double U = static_cast<double>(Tc - F.CruiseStart);
		Along = V * (0.5 * ClimbSeconds + U);
		Z = Zc + CruiseBob(U, F.CruiseSeconds);
	}
	else if (Tc >= F.DescentStart)
	{
		// Frena hasta pararse encima del sitio mientras baja.
		const double B = FMath::Clamp(static_cast<double>(Tc - F.DescentStart) / DescentSeconds, 0.0, 1.0);
		Along = V * (0.5 * ClimbSeconds + F.CruiseSeconds + DescentSeconds * (B - (B * B * B - 0.5 * B * B * B * B)));
		Z = Zc + (Z3 - Zc) * Smooth01(B);
	}
	return FVector(F.Spine0.X + F.Dir.X * Along, F.Spine0.Y + F.Dir.Y * Along, Z);
}

FVector ATN_RacePelicanTaxi::BeakAt(float T) const
{
	// Mira hacia delante: el caparazón (su espalda) queda detrás del hueso, del lado del pelícano.
	return SpineAt(T) - Flight.Dir * TNRacePelicanDetail::ShellBack;
}

float ATN_RacePelicanTaxi::PathPitchAt(float T) const
{
	using namespace TNRacePelicanDetail;
	const FVector D = SpineAt(T + 0.05f) - SpineAt(T - 0.05f);
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(D.Z), FMath::Max(1.f, static_cast<float>(D.Size2D()))));
	return FMath::Clamp(0.6f * Deg, MinCarryPitch, MaxCarryPitch);
}

float ATN_RacePelicanTaxi::AlongSpeedFactor(float T) const
{
	using namespace TNRacePelicanDetail;
	const FFlightTimes& F = Flight;
	if (T < F.ClimbStart || T >= F.Release)
	{
		return 0.f;
	}
	if (T < F.CruiseStart)
	{
		return Smooth01((T - F.ClimbStart) / ClimbSeconds);
	}
	if (T < F.DescentStart)
	{
		return 1.f;
	}
	return 1.f - Smooth01((T - F.DescentStart) / DescentSeconds);
}

FRotator ATN_RacePelicanTaxi::CarryRotation(float T) const
{
	using namespace TNRacePelicanDetail;
	const FFlightTimes& F = Flight;
	const float Slope = PathPitchAt(T);
	float PitchDeg = Slope;
	if (T < F.CruiseStart)
	{
		// Del agarre (morro un poco abajo) al de la subida en su primer tercio.
		const float A = FMath::Clamp((T - F.ClimbStart) / ClimbSeconds, 0.f, 1.f);
		PitchDeg = FMath::Lerp(GrabPitch, Slope, Smooth01(A / 0.35f));
	}
	else if (T >= F.DescentStart)
	{
		const float B = FMath::Clamp((T - F.DescentStart) / DescentSeconds, 0.f, 1.f);
		PitchDeg = FMath::Lerp(Slope, ReleasePitch, Smooth01(B));
	}
	return FRotator(PitchDeg, F.Yaw, CarryRoll * FMath::Sin(T * 1.7f));
}

float ATN_RacePelicanTaxi::CarryHeadPitch(float T) const
{
	using namespace TNRacePelicanDetail;
	const FFlightTimes& F = Flight;
	float BasePitch = CruiseHeadPitch;
	float Envelope = 1.f;
	if (T < F.CruiseStart)
	{
		// Con la cabeza estirada hacia abajo mientras la tortuga está cerca del suelo; luego, la del crucero.
		const float S = Smooth01(FMath::Clamp((T - F.ClimbStart) / ClimbSeconds, 0.f, 1.f));
		BasePitch = FMath::Lerp(ReachHeadPitch, CruiseHeadPitch, S);
		Envelope = S;
	}
	else if (T >= F.DescentStart)
	{
		const float S = Smooth01(FMath::Clamp((T - F.DescentStart) / DescentSeconds, 0.f, 1.f));
		BasePitch = FMath::Lerp(CruiseHeadPitch, ReachHeadPitch, S);
		Envelope = 1.f - S;
	}
	return BasePitch + HeadShake * Envelope * FMath::Sin(T * 6.f);
}

FVector ATN_RacePelicanTaxi::GripOffset(float HeadPitch) const
{
	const TNBeachMeshes::FBirdGeom G = TNBeachMeshes::BirdGeom(true);
	return G.BodyPivot + G.HeadPivot + FRotator(HeadPitch, 0.f, 0.f).RotateVector(G.Grip);
}

FVector ATN_RacePelicanTaxi::RootForGrip(const FVector& Grip, const FRotator& Rot, float HeadPitch) const
{
	return Grip - Rot.RotateVector(GripOffset(HeadPitch) * TNRacePelicanDetail::PelicanScale);
}

FVector ATN_RacePelicanTaxi::BodyCenter(const FVector& Root, const FRotator& Rot) const
{
	return Root + Rot.RotateVector(TNBeachMeshes::BirdGeom(true).BodyPivot * TNRacePelicanDetail::PelicanScale);
}

void ATN_RacePelicanTaxi::HeldPose(float T, FVector& OutRoot, FRotator& OutRot, float& OutHeadPitch) const
{
	using namespace TNRacePelicanDetail;
	const FFlightTimes& F = Flight;
	if (T >= F.ClimbStart)
	{
		// Con la tortuga en el pico: la raíz sale de dónde tiene que estar el pico.
		OutRot = CarryRotation(T);
		OutHeadPitch = CarryHeadPitch(T);
		OutRoot = RootForGrip(BeakAt(T), OutRot, OutHeadPitch);
		return;
	}
	// Aproximación: baja en picado desde atrás y arriba y frena con el pico en su caparazón (la pose del agarre).
	const FRotator GrabRot = CarryRotation(F.ClimbStart);
	const float GrabHead = CarryHeadPitch(F.ClimbStart);
	const FVector Strike = RootForGrip(BeakAt(F.ClimbStart), GrabRot, GrabHead);
	const FVector From = Strike - F.Dir * ApproachBack + FVector(0.0, 0.0, ApproachUp);
	const double A = FMath::Clamp(static_cast<double>(T) / ApproachSeconds, 0.0, 1.0);
	// En planta frena al llegar; en altura baja antes (entra casi rasante los últimos metros).
	const double Flat = 1.0 - (1.0 - A) * (1.0 - A);
	const double Drop = 1.0 - (1.0 - A) * (1.0 - A) * (1.0 - A);
	OutRoot = FVector(FMath::Lerp(From.X, Strike.X, Flat), FMath::Lerp(From.Y, Strike.Y, Flat), FMath::Lerp(From.Z, Strike.Z, Drop));
	const float S = Smooth01(static_cast<float>(A));
	OutRot = FRotator(FMath::Lerp(ApproachDivePitch, static_cast<float>(GrabRot.Pitch), S), static_cast<float>(GrabRot.Yaw),
		static_cast<float>(GrabRot.Roll) * S);
	OutHeadPitch = FMath::Lerp(ApproachHeadPitch, GrabHead, S);
}

void ATN_RacePelicanTaxi::PoseAt(float T, FVector& OutRoot, FRotator& OutRot, float& OutHeadPitch) const
{
	using namespace TNRacePelicanDetail;
	const float EndT = FarewellStart();
	if (T < EndT)
	{
		HeldPose(T, OutRoot, OutRot, OutHeadPitch);
		return;
	}
	// Despedida: desde donde estaba al soltarla (o al perderla), con la velocidad que llevaba, acelera hacia delante y hacia
	// arriba abriéndose a un lado.
	FVector RootEnd;
	FRotator RotEnd;
	float HeadEnd = 0.f;
	HeldPose(EndT, RootEnd, RotEnd, HeadEnd);
	FVector RootBefore;
	FRotator RotBefore;
	float HeadBefore = 0.f;
	HeldPose(EndT - 0.05f, RootBefore, RotBefore, HeadBefore);
	FVector Vel0 = ((RootEnd - RootBefore) / 0.05).GetClampedToMaxSize(4000.0);
	if (Vel0.Z < 0.0)
	{
		// Bajaba: deja de bajar enseguida.
		Vel0.Z *= 0.25;
	}
	const double S = FMath::Max(0.0, static_cast<double>(T - EndT));
	const FVector Side(-Flight.Dir.Y, Flight.Dir.X, 0.0);
	OutRoot = RootEnd + Vel0 * S + Flight.Dir * (0.5 * FarewellAccel * S * S)
		+ FVector(0.0, 0.0, FarewellLift * S + 0.5 * FarewellClimbAccel * S * S) + Side * (0.5 * FarewellVeer * S * S);
	const float K = Smooth01(static_cast<float>(S) / 0.8f);
	const float Turn = FarewellTurn * Smooth01(static_cast<float>(S) / 2.f);
	OutRot = FRotator(FMath::Lerp(static_cast<float>(RotEnd.Pitch), FarewellPitch, K), static_cast<float>(RotEnd.Yaw) + Turn,
		FMath::Lerp(static_cast<float>(RotEnd.Roll), FarewellBank, K));
	OutHeadPitch = FMath::Lerp(HeadEnd, 0.f, Smooth01(static_cast<float>(S) / 0.7f));
}

float ATN_RacePelicanTaxi::GroundBelow(const FVector& Where) const
{
	float Z = static_cast<float>(Plan.End.Z) - Flight.GripDrop / TNRacePelicanDetail::GripDropFraction;
	GroundHeightAt(Where, Z);
	return Z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sujeción (todas las máquinas) y servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RacePelicanTaxi::SyncHold()
{
	using namespace TNRacePelicanDetail;
	ATortugaCharacter* Victim = Plan.Victim;
	const float T = Elapsed();
	// A quién lleva ahora según el plan: en el servidor, mientras no la suelte (ServerTick); en los clientes, además, hasta
	// su hora aunque el cambio de fase aún no haya llegado. De pie, fuera del caparazón. Una vez soltada, no se vuelve a coger.
	ATortugaCharacter* Want = nullptr;
	bool bNormalDrop = Plan.Phase == PhaseReleased;
	if (Plan.Phase == PhaseFlying && !bLocalHoldDone)
	{
		const bool bInTime = HasAuthority() || T < Flight.Release;
		bNormalDrop = !bInTime;
		if (bInTime && IsValid(Victim) && !Victim->IsDead() && !Victim->IsActorBeingDestroyed() && !Victim->IsInShell() && !Victim->IsKnockedDown())
		{
			Want = Victim;
		}
	}
	ATortugaCharacter* Held = GetHeldTurtle();
	if (Held && Held != Want)
	{
		// En un cliente, la suelta en el sitio como el servidor (la cápsula justo encima): sin corrección al caer.
		if (bNormalDrop && Held == Victim && !HasAuthority())
		{
			PutVictimOverSite(Held);
		}
		EndHoldTurtle();
		bLocalHoldDone = true;
		Held = nullptr;
	}
	if (!Want)
	{
		return;
	}
	// Una sola sujeción, continua de principio a fin del vuelo (su seguro de tiempo, GetMaxHoldSeconds, dura más que él).
	if (!Held)
	{
		BeginHoldTurtle(Want);
	}
	if (GetHeldTurtle() != Want)
	{
		// No se deja sujetar (la ha soltado el seguro de tiempo y aún no se puede volver a coger): el servidor lo deja; un
		// cliente lo vuelve a intentar en el siguiente fotograma hasta que le llegue el cambio de fase.
		if (HasAuthority())
		{
			ServerAbort(TEXT("no se ha podido sujetar"));
		}
		return;
	}
	PlaceHeldTurtle(SpineAt(T), Flight.Yaw);
}

void ATN_RacePelicanTaxi::PutVictimOverSite(ATortugaCharacter* Victim) const
{
	if (!IsValid(Victim))
	{
		return;
	}
	Victim->SetActorLocationAndRotation(FVector(Plan.End) + FVector(0.0, 0.0, TNRacePelicanDetail::ReleaseHeight), FRotator(0.f, Flight.Yaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
}

void ATN_RacePelicanTaxi::ServerTick(float DeltaSeconds)
{
	using namespace TNRacePelicanDetail;
	if (bServerEnding)
	{
		return;
	}
	if (Plan.Phase == PhaseNone)
	{
		bServerEnding = true;
		SetLifeSpan(EndGraceSeconds);
		return;
	}
	const float T = Elapsed();
	if (Plan.Phase == PhaseFlying)
	{
		ATortugaCharacter* Victim = Plan.Victim;
		if (const TCHAR* Why = WhyVictimLost(Victim))
		{
			ServerAbort(Why);
		}
		else if (T >= Flight.Release)
		{
			ServerDropOff();
		}
	}
	if (Plan.Phase != PhaseFlying && T >= FarewellStart() + FarewellSeconds)
	{
		bServerEnding = true;
		SetLifeSpan(EndGraceSeconds);
	}
	else if (T > MaxLifeSeconds)
	{
		// Nunca debería pasar (el vuelo acaba mucho antes): fuera sin más.
		ServerAbort(TEXT("ha pasado demasiado tiempo"));
		bServerEnding = true;
		SetLifeSpan(EndGraceSeconds);
	}
}

void ATN_RacePelicanTaxi::ServerDropOff()
{
	using namespace TNRacePelicanDetail;
	if (!HasAuthority() || Plan.Phase != PhaseFlying)
	{
		return;
	}
	ATortugaCharacter* Victim = Plan.Victim;
	Plan.Phase = PhaseReleased;
	Plan.EndTime = Plan.StartTime + Flight.Release;
	++Plan.Serial;
	bLocalHoldDone = true;
	// De pie justo encima del sitio (arena abierta que se ha comprobado al planear) y suelta: cae los últimos 2,6 m.
	if (IsValid(Victim) && GetHeldTurtle() == Victim)
	{
		PutVictimOverSite(Victim);
	}
	if (GetHeldTurtle())
	{
		EndHoldTurtle();
	}
	if (IsValid(Victim))
	{
		Victim->SetFallImmuneUntilLanded();
		TNBeach::GrantStormGrace(Victim, StormGraceSeconds);
		SetVictimRiding(Victim, false);
		Victim->ForceNetUpdate();
	}
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: deja a %s en (%.1f, %.1f, %.1f) m."), *GetNameSafe(Victim), Plan.End.X / 100.0, Plan.End.Y / 100.0,
		Plan.End.Z / 100.0);
}

void ATN_RacePelicanTaxi::ServerAbort(const TCHAR* Reason)
{
	using namespace TNRacePelicanDetail;
	if (!HasAuthority() || Plan.Phase != PhaseFlying)
	{
		return;
	}
	ATortugaCharacter* Victim = Plan.Victim;
	Plan.Phase = PhaseAborted;
	Plan.EndTime = static_cast<float>(ServerNow(this));
	++Plan.Serial;
	bLocalHoldDone = true;
	if (GetHeldTurtle())
	{
		EndHoldTurtle();
	}
	SetVictimRiding(Victim, false);
	// Si queda suelta por el aire, que la caída no cuente como caída larga (no es culpa suya).
	if (IsValid(Victim) && !Victim->IsDead() && !Victim->IsInShell() && !Victim->IsKnockedDown())
	{
		Victim->SetFallImmuneUntilLanded();
	}
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: deja de llevar a %s (%s)."), *GetNameSafe(Victim), Reason ? Reason : TEXT("sin motivo"));
}

void ATN_RacePelicanTaxi::OnHoldAborted(ATortugaCharacter* Turtle)
{
	using namespace TNRacePelicanDetail;
	// Se la quitan del pico (red de seguridad, rescate, gusano): la base la suelta justo después. Nada de volver a cogerla:
	// el pelícano se marcha sin soltarla en ningún sitio.
	if (!HasAuthority() || !Turtle || Plan.Phase != PhaseFlying || Plan.Victim != Turtle)
	{
		return;
	}
	Plan.Phase = PhaseAborted;
	Plan.EndTime = static_cast<float>(ServerNow(this));
	++Plan.Serial;
	bLocalHoldDone = true;
	SetVictimRiding(Turtle, false);
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pelícano taxi: le quitan a %s del pico."), *GetNameSafe(Turtle));
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RacePelicanTaxi::BuildVisuals()
{
	using namespace TNRacePelicanDetail;
	if (bVisualsBuilt || !bHasScreen)
	{
		return;
	}
	bVisualsBuilt = true;

	// El pelícano de la fauna por piezas, con el pico de abajo aparte (las mismas mallas que el de la zona de gaviotas: se
	// comparten por nombre).
	TArray<TNFauna::FTNFaunaPart> Parts;
	TNFauna::FTNFaunaRig FaunaRig;
	TNFauna::FTNFaunaBirdJaw JawData;
	TNBeachMeshes::BuildBirdParts(true, Parts, FaunaRig, JawData);
	PelicanRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	PelicanRoot->SetupAttachment(GetRootComponent());
	PelicanRoot->SetAbsolute(true, true, true);
	PelicanRoot->RegisterComponent();
	UStaticMeshComponent* BodyComp = nullptr;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const TNFauna::FTNFaunaPart& Part = Parts[i];
			const bool bIsBody = Part.Bone == TNFauna::ETNFaunaBone::Body;
			if ((Pass == 0) != bIsBody)
			{
				continue;
			}
			const TNProcMesh::FTNProcMeshBuffers& Buffers = Part.Mesh;
			UStaticMesh* PartMesh = TNBeachKit::CachedMesh(TNBeachMeshes::BirdPartKey(true, i),
				[&Buffers](TNProcMesh::FTNProcMeshBuffers& M) { M = Buffers; });
			USceneComponent* ParentComp = bIsBody ? PelicanRoot.Get() : static_cast<USceneComponent*>(BodyComp);
			UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, ParentComp ? ParentComp : PelicanRoot.Get(), PartMesh, Part.Pivot, false);
			if (bIsBody && !BodyComp)
			{
				BodyComp = Comp;
			}
			const int32 Index = PelicanParts.Add(Comp);
			PartPivots.Add(Part.Pivot);
			switch (Part.Bone)
			{
			case TNFauna::ETNFaunaBone::WingL: WingLIndex = Index; break;
			case TNFauna::ETNFaunaBone::WingR: WingRIndex = Index; break;
			case TNFauna::ETNFaunaBone::LegBL: LegLIndex = Index; break;
			case TNFauna::ETNFaunaBone::LegBR: LegRIndex = Index; break;
			case TNFauna::ETNFaunaBone::Head: HeadIndex = Index; break;
			default: break;
			}
		}
	}
	// Pico de abajo con su bolsa, en su base: se abre para coger, para soltar y para graznar.
	UStaticMeshComponent* HeadComp = PelicanParts.IsValidIndex(HeadIndex) ? PelicanParts[HeadIndex].Get() : nullptr;
	UStaticMesh* JawMesh = TNBeachKit::CachedMesh(TNBeachMeshes::BirdJawKey(true), [&JawData](TNProcMesh::FTNProcMeshBuffers& M) { M = JawData.Mesh; });
	JawPart = HeadComp ? TNBeachKit::AddPart(this, HeadComp, JawMesh, TNBeachMeshes::BirdGeom(true).BeakBase, false) : nullptr;

	ShadowPart = TNBeachKit::AddShadow(this, 0.38f);
	TNBeachKit::PlaceShadow(ShadowPart, FVector::ZeroVector, 0.f);
	ShadowZ = static_cast<float>(Plan.End.Z);

	using TNAmbientFX::EShape;
	const uint32 FxSeed = GetUniqueID();
	// Estela de viento: rayas blancas que pasan de largo mientras vuela deprisa.
	TNAmbientFX::FEmitterDesc TrailDesc = TNBeachKit::MakeDesc(EShape::Streak, FLinearColor(1.f, 1.f, 1.f), true, 0.5f, 60, 45.f, 300.f, 0.f, 0.3f, 0.5f, 220.f, 60.f);
	TrailDesc.SpawnRadius = 260.f;
	TrailDesc.SpawnHeight = 220.f;
	TrailDesc.Spread = 0.15f;
	TNBeachKit::InitEmitter(WindTrail, this, TrailDesc, FxSeed + 1u);
	// Arena que levantan las alas al cogerla (y un poco al aterrizar).
	TNAmbientFX::FEmitterDesc SandDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.86f, 0.76f, 0.56f), true, 0.55f, 30, 0.f, 700.f, -60.f, 1.f, 2.f, 250.f, 600.f);
	SandDesc.Spread = 1.f;
	SandDesc.SpawnRadius = 400.f;
	TNBeachKit::InitEmitter(SandPuff, this, SandDesc, FxSeed + 2u);
	// Plumas al soltarla.
	TNAmbientFX::FEmitterDesc FeatherDesc = TNBeachKit::MakeDesc(EShape::Flake, FLinearColor(0.95f, 0.95f, 0.93f), false, 1.f, 30, 0.f, 500.f, -120.f, 1.5f, 3.f, 90.f, 70.f);
	FeatherDesc.Spread = 1.f;
	FeatherDesc.SpawnRadius = 300.f;
	TNBeachKit::InitEmitter(Feathers, this, FeatherDesc, FxSeed + 3u);

	// Sonido: sigue al actor (que va con el cuerpo del pelícano).
	Sfx = UTN_RaceItemSynthComponent::AttachTo(this, GetActorLocation(), 3000.f, 20000.f);
}

bool ATN_RacePelicanTaxi::PosePelican(float T, float DeltaSeconds, float HeadPitch, bool bNear)
{
	using namespace TNRacePelicanDetail;
	const FFlightTimes& F = Flight;
	const float EndT = FarewellStart();
	const float WingClock = Clock * 2.f * PI * WingRate;
	float Flap = 6.f + 3.f * FMath::Sin(Clock * 1.3f);
	float Sweep = 0.f;
	float Tuck = -70.f;
	float JawTarget = 0.f;
	bool bFlapping = false;
	if (T < F.ClimbStart && T < EndT)
	{
		// Picado con las alas recogidas y, al final, frena con las alas abiertas y el pico abierto.
		const float A = T / ApproachSeconds;
		if (A < 0.6f)
		{
			Sweep = 45.f;
			Flap = 6.f;
			Tuck = -85.f;
		}
		else
		{
			Sweep = -15.f;
			Flap = 30.f * FMath::Sin(WingClock * 4.f);
			Tuck = 25.f;
			bFlapping = true;
		}
		JawTarget = (A > 0.5f && A < 0.96f) ? 38.f : 0.f;
	}
	else if (T < EndT)
	{
		// Con la tortuga en el pico (apretando): aleteo fuerte al subir, rachas con planeos en el crucero y frenada al bajar.
		JawTarget = 9.f + 3.f * FMath::Sin(Clock * 11.f);
		if (T < F.CruiseStart)
		{
			Flap = 50.f * FMath::Sin(WingClock * 3.4f);
			Sweep = -5.f;
			Tuck = -60.f;
			bFlapping = true;
		}
		else if (T < F.DescentStart)
		{
			const float Cycle = FMath::Fmod((T - F.CruiseStart) * 0.35f, 1.f);
			if (Cycle < 0.65f)
			{
				Flap = 26.f * FMath::Sin(WingClock * 2.2f);
				bFlapping = true;
			}
			else
			{
				Flap = 5.f;
				Sweep = 10.f;
			}
			Tuck = -80.f;
		}
		else
		{
			Sweep = -15.f;
			Flap = 32.f * FMath::Sin(WingClock * 3.6f);
			Tuck = 20.f;
			bFlapping = true;
		}
	}
	else
	{
		// La ha soltado: pico abierto un momento y se marcha aleteando fuerte.
		const float Since = T - EndT;
		JawTarget = Since < 0.5f ? 38.f : 0.f;
		Flap = 45.f * FMath::Sin(WingClock * 3.2f);
		Tuck = -75.f;
		bFlapping = Since < 1.8f;
	}
	if (!bNear)
	{
		return bFlapping;
	}
	JawAngle = FMath::FInterpTo(JawAngle, JawTarget, DeltaSeconds, 18.f);
	auto PosePart = [this](int32 PartIndex, const FRotator& PartRot)
	{
		if (PelicanParts.IsValidIndex(PartIndex) && PartPivots.IsValidIndex(PartIndex))
		{
			TNBeachKit::Pose(PelicanParts[PartIndex], PartPivots[PartIndex], PartRot);
		}
	};
	PosePart(WingLIndex, FRotator(0.f, -Sweep, Flap));
	PosePart(WingRIndex, FRotator(0.f, Sweep, -Flap));
	PosePart(LegLIndex, FRotator(Tuck, 0.f, 0.f));
	PosePart(LegRIndex, FRotator(Tuck, 0.f, 0.f));
	// La cabeza, con el mismo cabeceo con el que se ha calculado dónde va el pico.
	PosePart(HeadIndex, FRotator(HeadPitch, 0.f, 0.f));
	if (JawPart)
	{
		TNBeachKit::Pose(JawPart, TNBeachMeshes::BirdGeom(true).BeakBase, FRotator(-JawAngle, 0.f, 0.f));
	}
	return bFlapping;
}

void ATN_RacePelicanTaxi::TickEvents(float T, float DeltaSeconds, const FVector& Body, bool bNear, bool bFlapping)
{
	using namespace TNRacePelicanDetail;
	const float EndT = FarewellStart();
	const float HalfHeight = Flight.GripDrop / GripDropFraction;
	if (!bEventsPrimed)
	{
		// Quien lo ve por primera vez a mitad de vuelo no oye lo que ya pasó.
		bEventsPrimed = true;
		bGrabPlayed = T > Flight.ClimbStart + LateEventSeconds;
		bReleasePlayed = T > EndT + LateEventSeconds;
		bLandPlayed = bReleasePlayed;
	}

	// La coge: graznido grave, arena que levantan las alas y un golpe de cámara.
	if (!bGrabPlayed && T >= Flight.ClimbStart && EndT > Flight.ClimbStart)
	{
		bGrabPlayed = true;
		const FVector GrabFeet = FVector(Plan.Start) - FVector(0.0, 0.0, HalfHeight);
		PlayAt(Sfx, ETNRaceSound::Squawk, BeakAt(Flight.ClimbStart), SquawkPitch, 1.2f);
		TNBeachKit::BurstAt(SandPuff, GrabFeet, FVector::UpVector, 14);
		UTN_BeachCameraShake::Kick(this, FVector(Plan.Start), 0.45f, 500.f, 3000.f);
		ShowPop(NSLOCTEXT("TNRace", "PelicanTaxiGrab", "¡TAXI!"), FColor(255, 210, 60), FVector(Plan.Start) + FVector(0.0, 0.0, 320.0), 160.f);
	}

	// La suelta (o la pierde): graznido y plumas.
	if (!bReleasePlayed && T >= EndT)
	{
		bReleasePlayed = true;
		const bool bAborted = Plan.Phase == PhaseAborted;
		const FVector BeakPoint = BeakAt(EndT);
		PlayAt(Sfx, ETNRaceSound::Squawk, BeakPoint, bAborted ? SquawkPitch * 0.85f : SquawkPitch, 1.2f);
		TNBeachKit::BurstAt(Feathers, BeakPoint + FVector(0.0, 0.0, 150.0), FVector::UpVector, bAborted ? 10 : 18);
	}

	// La tortuga toca la arena (solo si la ha dejado en el sitio).
	if (bReleasePlayed && !bLandPlayed && Plan.Phase != PhaseAborted)
	{
		const ATortugaCharacter* Victim = Plan.Victim;
		const float Since = T - EndT;
		bool bDown = Since >= LandTimeout;
		if (!bDown && Since > 0.15f && IsValid(Victim))
		{
			const UCharacterMovementComponent* Move = Victim->GetCharacterMovement();
			bDown = Move && Move->IsMovingOnGround();
		}
		if (bDown)
		{
			bLandPlayed = true;
			const FVector LandFeet = (IsValid(Victim) ? Victim->GetActorLocation() : FVector(Plan.End)) - FVector(0.0, 0.0, HalfHeight);
			PlayAt(Sfx, ETNRaceSound::Land, LandFeet, 1.f, 1.1f);
			TNBeachKit::BurstAt(SandPuff, LandFeet, FVector::UpVector, 8);
		}
	}

	// Aleteos (un golpe de aire cada FlapSoundPeriod mientras bate las alas).
	if (bFlapping && bNear)
	{
		FlapSoundTimer -= DeltaSeconds;
		if (FlapSoundTimer <= 0.f)
		{
			FlapSoundTimer = FlapSoundPeriod;
			PlayAt(Sfx, ETNRaceSound::Flap, Body, 0.85f + 0.1f * FMath::Sin(Clock * 3.1f), 0.9f);
		}
	}
	else
	{
		FlapSoundTimer = FMath::Min(FlapSoundTimer, 0.15f);
	}
}

void ATN_RacePelicanTaxi::VisualTick(float DeltaSeconds)
{
	using namespace TNRacePelicanDetail;
	if (Plan.Phase == PhaseNone)
	{
		return;
	}
	BuildVisuals();
	if (!PelicanRoot)
	{
		return;
	}
	Clock += DeltaSeconds;
	const float Dt = FMath::Max(DeltaSeconds, 1.0e-3f);
	const float T = PoseTime;
	const float EndT = FarewellStart();
	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);

	// Acabada la despedida se esconde (el servidor lo destruye enseguida).
	const bool bGone = T > EndT + FarewellSeconds + 0.3f;
	if (PelicanRoot->IsVisible() == bGone)
	{
		PelicanRoot->SetVisibility(!bGone, true);
	}
	if (bGone)
	{
		TNBeachKit::PlaceShadow(ShadowPart, FVector::ZeroVector, 0.f);
		WindTrail.RateScale = 0.f;
	}
	else
	{
		const bool bNear = ViewDistance < GetVisualRange();
		// Giro que se ve (suavizado); con la tortuga en el pico, la raíz sale del pico: sigue en su caparazón.
		if (!bShownRotValid)
		{
			ShownRot = PoseRot;
			bShownRotValid = true;
		}
		else
		{
			ShownRot = FMath::RInterpTo(ShownRot, PoseRot, Dt, RotSmoothSpeed);
		}
		const bool bCarry = T >= Flight.ClimbStart && T < EndT;
		const FVector ShownRoot = bCarry ? RootForGrip(BeakAt(T), ShownRot, PoseHead) : PoseRoot;
		PelicanRoot->SetWorldTransform(FTransform(ShownRot, ShownRoot, FVector(PelicanScale)));
		const bool bFlapping = PosePelican(T, Dt, PoseHead, bNear);
		const FVector Body = BodyCenter(ShownRoot, ShownRot);

		// Sombra en la arena bajo el cuerpo: cuanto más bajo, más pequeña, nítida y oscura.
		ShadowTimer -= DeltaSeconds;
		if (ShadowTimer <= 0.f)
		{
			ShadowTimer = 0.1f;
			ShadowZ = GroundBelow(Body);
		}
		const float Height = FMath::Max(0.f, static_cast<float>(Body.Z) - ShadowZ);
		const float FreeH = FMath::Clamp(Height / 8000.f, 0.f, 1.f);
		const float ShadowRadius = PelicanSpan * 0.2f * (0.75f + 0.5f * FreeH);
		const float Opacity = FMath::Lerp(0.5f, 0.12f, FreeH);
		PlacePelicanShadow(ShadowPart, ShadowEdge, FVector(Body.X, Body.Y, ShadowZ), bNear ? ShadowRadius : 0.f, Opacity, 1.f - FreeH);

		// Estela de viento alrededor de la tortuga mientras vuela deprisa.
		const float TrailK = (bCarry && bNear) ? AlongSpeedFactor(T) : 0.f;
		WindTrail.RateScale = TrailK;
		if (TrailK > 0.f)
		{
			WindTrail.Origin = SpineAt(T) - FVector(0.0, 0.0, 110.0);
			WindTrail.Desc.Direction = -Flight.Dir;
		}

		TickEvents(T, DeltaSeconds, Body, bNear, bFlapping);
	}

	TNBeachKit::TickEmitterIfBusy(WindTrail, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(SandPuff, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Feathers, DeltaSeconds, View);
}
