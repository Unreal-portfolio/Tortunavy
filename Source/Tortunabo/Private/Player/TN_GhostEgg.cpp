#include "Player/TN_GhostEgg.h"
#include "Player/TN_SpectatorGhost.h"
#include "TN_GhostInternal.h"
#include "../Lobby/TN_CastleKit.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundAttenuation.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"

namespace TNGhostEggDetail
{
	/** Segundos que tarda en salir del suelo. */
	constexpr float AppearSeconds = 0.35f;
	/** Los tres golpes de la vibración («pum, pum, pum»), en fracción del rato entre que entra el fantasma y la eclosión. */
	constexpr float KnockAt[3] = { 0.15f, 0.5f, 0.85f };
	/** Tapa: las de la salida del mapa procedural (ATN_ProcStartStructure): salto (cm/s), gravedad (cm/s²) y giros (grados/s). */
	constexpr double LidSpeedZ = 520.0;
	constexpr double LidSpeedSide = 340.0;
	constexpr double LidGravity = 1400.0;
	constexpr double LidSpinYaw = 540.0;
	constexpr double LidTumble = 320.0;
	constexpr double LidShrinkSeconds = 0.3;
	/** Altura de la costura de la tapa sobre el suelo al posarse (sus dientes bajan 18 cm). */
	constexpr double LidRestHeight = 20.0;
	/** Tras eclosionar, la base se queda un rato y se hunde en la arena; el huevo desaparece después. */
	constexpr float CupStaySeconds = 1.6f;
	constexpr float CupSinkSeconds = 0.6f;
	constexpr float LifeAfterHatch = 3.f;
	/** Luz cálida de dentro mientras el fantasma está en el huevo (lúmenes). */
	constexpr float GlowLumens = 2200.f;

	/** Segundos que vuela la tapa hasta posarse en el suelo. */
	double LidFlightSeconds()
	{
		const double Drop = FMath::Max(0.0, TNCastleKit::EggSeam - LidRestHeight);
		return (LidSpeedZ + FMath::Sqrt(LidSpeedZ * LidSpeedZ + 2.0 * LidGravity * Drop)) / LidGravity;
	}
}

ATN_GhostEgg::ATN_GhostEgg()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.f);
	SetCanBeDamaged(false);

	EggRoot = CreateDefaultSubobject<USceneComponent>(TEXT("EggRoot"));
	SetRootComponent(EggRoot);
	WobbleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WobbleRoot"));
	WobbleRoot->SetupAttachment(EggRoot);
	WobbleRoot->SetRelativeScale3D(FVector(0.01));

	// Base del huevo (malla procedural generada en ejecución: RF_Transient y su puntero, Transient). Sin colisión: la
	// tortuga aparece dentro, de pie sobre el suelo, y sale de un salto.
	CupMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CupMesh"));
	CupMesh->SetFlags(RF_Transient);
	CupMesh->SetupAttachment(WobbleRoot);
	CupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CupMesh->SetCanEverAffectNavigation(false);

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(WobbleRoot);
	Lid->SetMobility(EComponentMobility::Movable);
	Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lid->SetCanEverAffectNavigation(false);

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(WobbleRoot);
	GlowLight->SetRelativeLocation(FVector(0.0, 0.0, 120.0));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(0.f);
	GlowLight->SetAttenuationRadius(650.f);
	GlowLight->SetLightColor(FLinearColor(1.f, 0.84f, 0.55f));
	GlowLight->SetCastShadows(false);
}

void ATN_GhostEgg::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_GhostEgg, ReviverState);
	DOREPLIFETIME(ATN_GhostEgg, AppearTime);
	DOREPLIFETIME(ATN_GhostEgg, ArriveTime);
	DOREPLIFETIME(ATN_GhostEgg, HatchTime);
	DOREPLIFETIME(ATN_GhostEgg, AccentIndex);
	DOREPLIFETIME(ATN_GhostEgg, bHatched);
	DOREPLIFETIME(ATN_GhostEgg, HatchedPawn);
}

void ATN_GhostEgg::InitEgg(APlayerController* InReviver, float InAppearTime, float InArriveTime, float InHatchTime)
{
	Reviver = InReviver;
	ReviverState = InReviver ? InReviver->PlayerState.Get() : nullptr;
	AppearTime = InAppearTime;
	ArriveTime = InArriveTime;
	HatchTime = InHatchTime;
	const int32 Id = ReviverState ? ReviverState->GetPlayerId() : 0;
	AccentIndex = static_cast<uint8>(((Id % 4) + 4) % 4);
}

FVector ATN_GhostEgg::GetHopVelocity() const
{
	return GetActorForwardVector().GetSafeNormal2D() * HopSpeedForward + FVector(0.0, 0.0, HopSpeedUp);
}

bool ATN_GhostEgg::ShouldPlayWorldSound() const
{
	const UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return false;
	}
	// Quien vuelve a la vida lo oye en su pantalla (la cáscara oscura), no aquí.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* LocalPC = It->Get();
		if (LocalPC && LocalPC->IsLocalController() && ReviverState && LocalPC->PlayerState == ReviverState)
		{
			return false;
		}
	}
	return true;
}

void ATN_GhostEgg::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Por si nadie llegara a eclosionarlo (la eclosión le pone su vida de verdad).
		SetLifeSpan(FMath::Max(10.f, HatchTime - ATN_SpectatorGhost::ServerNow(GetWorld()) + 8.f));
	}
	// La tapa salta hacia un lado, el mismo en todas las máquinas.
	LidSide = FMath::Frac(GetActorLocation().X * 0.0137 + GetActorLocation().Y * 0.0071) > 0.5 ? 1.f : -1.f;
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	BuildMeshes();
	if (ShouldPlayWorldSound())
	{
		// El «pum» sintetizado de la pantalla de carga del huevo, aquí en el mundo (con distancia).
		Synth = NewObject<UTN_EggSynthComponent>(this, NAME_None, RF_Transient);
		Synth->bAllowSpatialization = true;
		Synth->bOverrideAttenuation = true;
		FSoundAttenuationSettings& Att = Synth->AttenuationOverrides;
		Att.bAttenuate = true;
		Att.bSpatialize = true;
		Att.AttenuationShape = EAttenuationShape::Sphere;
		Att.AttenuationShapeExtents = FVector(400.f, 0.f, 0.f);
		Att.FalloffDistance = 3500.f;
		Att.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Synth->SetupAttachment(EggRoot);
		Synth->RegisterComponent();
		Synth->KeepAwake();
	}
	if (bHatched)
	{
		StartLocalHatch();
	}
}

void ATN_GhostEgg::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Synth)
	{
		Synth->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_GhostEgg::BuildMeshes()
{
	// La misma base y la misma tapa que los huevos de la pila (lobby, salida del mapa procedural y de la carrera).
	UMaterialInterface* Mat = TNCastleKit::VertexColorMaterial();
	const uint32 Accent = TNCastleKit::EggAccent(AccentIndex);
	TNCastleKit::FBuffers Cup;
	TNCastleKit::BuildEggCup(Cup, FVector::ZeroVector, TNCastleKit::Col(0xFFF3DC), TNCastleKit::Col(Accent));
	TNCastleKit::UploadSection(CupMesh, Cup, false, Mat);
	TNCastleKit::FBuffers LidBuffers;
	TNCastleKit::BuildEggLid(LidBuffers, TNCastleKit::Pal(0xFFF3DC), TNCastleKit::Pal(Accent));
	LidStaticMesh = TNProcRuntimeMesh::MakeStaticMesh(this, LidBuffers, Mat);
	if (Lid)
	{
		Lid->SetStaticMesh(LidStaticMesh);
		Lid->SetRelativeLocationAndRotation(FVector(0.0, 0.0, TNCastleKit::EggSeam), FRotator::ZeroRotator);
	}
	bBuilt = true;
}

void ATN_GhostEgg::Hatch(APawn* Pawn)
{
	if (!HasAuthority() || bHatched)
	{
		return;
	}
	bHatched = true;
	HatchedPawn = Pawn;
	ForceNetUpdate();
	SetLifeSpan(TNGhostEggDetail::LifeAfterHatch);
	StartLocalHatch();
}

void ATN_GhostEgg::OnRep_Hatched()
{
	if (bHatched && HasActorBegunPlay())
	{
		StartLocalHatch();
	}
}

void ATN_GhostEgg::StartLocalHatch()
{
	const UWorld* World = GetWorld();
	if (LocalHatchSeen >= 0.f || !World)
	{
		return;
	}
	LocalHatchSeen = World->GetTimeSeconds();
	if (Synth)
	{
		Synth->PlayCrack(1.f);
		Synth->PlayPop();
	}
}

void ATN_GhostEgg::TryLocalHop()
{
	// Como en la salida con huevos: el servidor lanza a la tortuga y su dueño la lanza también a la vez (sin corrección).
	if (HasAuthority() || bLocalHopDone || LocalHatchSeen < 0.f)
	{
		return;
	}
	const UWorld* World = GetWorld();
	APawn* Pawn = HatchedPawn;
	if (!World || !Pawn || !Pawn->IsLocallyControlled())
	{
		// Si no es la nuestra (o tarda mucho en llegar), no se lanza aquí: ya la mueve su dueño o el servidor.
		if (World && World->GetTimeSeconds() - LocalHatchSeen > 1.f)
		{
			bLocalHopDone = true;
		}
		return;
	}
	bLocalHopDone = true;
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			if (Move->MovementMode == MOVE_None)
			{
				Move->SetMovementMode(MOVE_Falling);
			}
		}
		Character->LaunchCharacter(GetHopVelocity(), true, true);
	}
}

void ATN_GhostEgg::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !bHatchRequested && ATN_SpectatorGhost::ServerNow(GetWorld()) >= HatchTime)
	{
		bHatchRequested = true;
		// La tortuga sale ahora: la crea el sistema del fantasma (RestartPlayerAtTransform) y llama a Hatch.
		TNGhostInternal::CompleteRevive(this);
		if (!bHatched)
		{
			Hatch(nullptr);
		}
		if (IsActorBeingDestroyed())
		{
			return;
		}
	}
	if (bBuilt)
	{
		UpdateLook(DeltaSeconds);
	}
	TryLocalHop();
}

void ATN_GhostEgg::UpdateLook(float DeltaSeconds)
{
	using namespace TNGhostEggDetail;
	const UWorld* World = GetWorld();
	if (!World || !WobbleRoot)
	{
		return;
	}
	const float Now = ATN_SpectatorGhost::ServerNow(World);
	const float LocalNow = World->GetTimeSeconds();

	// Sale del suelo de un saltito (se pasa un poco y vuelve).
	const float SinceAppear = Now - AppearTime;
	float Scale = 1.f;
	if (SinceAppear < AppearSeconds)
	{
		const float T = FMath::Clamp(SinceAppear / AppearSeconds, 0.f, 1.f);
		Scale = T < 0.7f ? FMath::Lerp(0.01f, 1.12f, T / 0.7f) : FMath::Lerp(1.12f, 1.f, (T - 0.7f) / 0.3f);
	}
	if (!bAppearCued && SinceAppear >= 0.f)
	{
		bAppearCued = true;
		if (Synth)
		{
			Synth->PlayKnock(0.3f);
		}
	}

	float SquashZ = 1.f;
	float SquashXY = 1.f;
	float TiltPitch = 0.f;
	float TiltRoll = 0.f;
	float SinkZ = 0.f;
	float Glow = 0.f;
	if (LocalHatchSeen < 0.f)
	{
		const float Vibrate = FMath::Max(0.1f, HatchTime - ArriveTime);
		const float SinceArrive = Now - ArriveTime;
		if (SinceArrive >= 0.f)
		{
			// El fantasma está dentro: se ilumina y vibra «pum, pum, pum».
			if (!bArrivalCued)
			{
				bArrivalCued = true;
				if (Synth)
				{
					Synth->PlayWhoosh(0.45f);
				}
			}
			while (KnocksPlayed < 3 && SinceArrive >= KnockAt[KnocksPlayed] * Vibrate)
			{
				++KnocksPlayed;
				KnockTilt = 1.f;
				KnockTiltSide = (KnocksPlayed % 2) ? 1.f : -1.f;
				if (Synth)
				{
					Synth->PlayKnock(0.55f + 0.15f * KnocksPlayed);
				}
			}
			KnockTilt = FMath::Max(0.f, KnockTilt - DeltaSeconds * 4.f);
			const float Kick = KnockTilt * KnockTilt;
			const float Progress = FMath::Clamp(SinceArrive / Vibrate, 0.f, 1.f);
			SquashZ = 1.f - 0.13f * Kick;
			SquashXY = 1.f + 0.08f * Kick;
			TiltRoll = 7.f * Kick * KnockTiltSide;
			TiltPitch = 1.6f * Progress * FMath::Sin(LocalNow * 42.f);
			Glow = 0.3f + 0.5f * Kick + 0.2f * Progress;
		}
		else
		{
			// Esperando al fantasma: se mece un poco.
			TiltRoll = 1.5f * FMath::Sin(LocalNow * 3.f);
		}
	}
	else
	{
		// Eclosión: la tapa salta dando vueltas hacia un lado, se posa y se esfuma; la base se queda y se hunde.
		const float Since = LocalNow - LocalHatchSeen;
		const double Flight = LidFlightSeconds();
		const double Shrink = Since > Flight ? 1.0 - (Since - Flight) / LidShrinkSeconds : 1.0;
		if (Lid)
		{
			if (Shrink <= 0.0)
			{
				Lid->SetVisibility(false);
			}
			else
			{
				const double Tf = FMath::Min(static_cast<double>(Since), Flight);
				const FVector Side = FVector(-0.3, LidSide, 0.0).GetSafeNormal();
				const FVector Where = FVector(0.0, 0.0, TNCastleKit::EggSeam) + Side * (LidSpeedSide * Tf)
					+ FVector(0.0, 0.0, LidSpeedZ * Tf - 0.5 * LidGravity * Tf * Tf);
				Lid->SetRelativeLocationAndRotation(Where, FRotator(LidTumble * Tf, LidSpinYaw * Tf, 0.0));
				Lid->SetRelativeScale3D(FVector(Shrink));
			}
		}
		Glow = FMath::Max(0.f, 1.f - Since / 0.4f);
		SquashZ = 1.f - 0.1f * FMath::Max(0.f, 1.f - Since / 0.25f);
		if (Since > CupStaySeconds)
		{
			const float Sink = FMath::Clamp((Since - CupStaySeconds) / CupSinkSeconds, 0.f, 1.f);
			SinkZ = -130.f * Sink;
			Scale *= 1.f - 0.6f * Sink;
		}
	}
	WobbleRoot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, SinkZ), FRotator(TiltPitch, 0.f, TiltRoll));
	WobbleRoot->SetRelativeScale3D(FVector(Scale * SquashXY, Scale * SquashXY, Scale * SquashZ));
	if (GlowLight)
	{
		GlowLight->SetIntensity(GlowLumens * Glow);
	}
}
