#include "Audio/TN_AmbientSoundscape.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Audio/TN_AmbienceDataAsset.h"
#include "Audio/TN_AmbientSynthComponent.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "World/Beach/TN_BeachStorm.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

namespace
{
	int32 GTNAmbienceDebug = 0;
	FAutoConsoleVariableRef CVarTNAmbienceDebug(
		TEXT("TN.Ambience.Debug"),
		GTNAmbienceDebug,
		TEXT("1 = muestra en pantalla la mezcla del paisaje sonoro (biomas, capas y contexto de la cámara)."),
		ECVF_Cheat);

	float GTNAmbienceVolume = 1.f;
	FAutoConsoleVariableRef CVarTNAmbienceVolume(
		TEXT("TN.Ambience.Volume"),
		GTNAmbienceVolume,
		TEXT("Multiplicador del volumen del ambiente sintetizado y sus sustituciones (1 = normal, 0 = apagado)."));

	/** Huecos de la mezcla: los 8 biomas en el orden de ETNProcBiome y, detrás, el ambiente sin mapa. */
	constexpr int32 TNAmbSlots = static_cast<int32>(ETNProcBiome::Count) + 1;
	constexpr int32 TNAmbGenericSlot = TNAmbSlots - 1;
	constexpr int32 TNAmbLayers = FTNAmbientMix::NumLayers;
	static_assert(TNAmbSlots == 9, "OverrideMask y LastBiomeW (TN_AmbientSoundscape.h) tienen un hueco por bioma y el genérico");
	static_assert(TNAmbLayers <= 16, "OverrideMask guarda un bit por capa en un uint16");

	constexpr int32 TNAmbWind = static_cast<int32>(ETNAmbientLayer::Wind);
	constexpr int32 TNAmbSurf = static_cast<int32>(ETNAmbientLayer::Surf);
	constexpr int32 TNAmbStream = static_cast<int32>(ETNAmbientLayer::Stream);
	constexpr int32 TNAmbBirds = static_cast<int32>(ETNAmbientLayer::Birds);
	constexpr int32 TNAmbCicadas = static_cast<int32>(ETNAmbientLayer::Cicadas);
	constexpr int32 TNAmbCrickets = static_cast<int32>(ETNAmbientLayer::Crickets);
	constexpr int32 TNAmbFrogs = static_cast<int32>(ETNAmbientLayer::Frogs);
	constexpr int32 TNAmbBells = static_cast<int32>(ETNAmbientLayer::Bells);

	/** Lo que se sabe del sitio de la cámara (0..1 salvo las alturas, en cm). */
	struct FTNAmbienceContext
	{
		float WaterNear = 0.f;
		float SeaNear = 0.f;
		float RiverNear = 0.f;
		float HeightAboveSea = 0.f;
		float HeightAboveGround = 0.f;
		float StormInside = 0.f;
		float StormNear = 0.f;
		float Enclosure = 0.f;
		bool bUnderwater = false;
	};

	float TNAmbienceSaturate(float X)
	{
		return FMath::Clamp(X, 0.f, 1.f);
	}

	/**
	 * Pesos de bioma alrededor de MapPos: el punto (doble peso) y seis más en un anillo de radio Radius, sobre el campo
	 * suave de pesos del layout (el mismo que usa la tormenta). Suman 1; sin datos, todo al genérico.
	 */
	void TNAmbienceBiomeWeights(const TNProcMap::FLayout& Layout, const FVector2D& MapPos, double Radius, float OutW[TNAmbSlots])
	{
		double Sample[TNProcMap::NumBiomes];
		double Acc[TNProcMap::NumBiomes] = {};
		double Total = 0.0;
		for (int32 k = 0; k <= 6; ++k)
		{
			const double Angle = TNProcMap::TwoPi * static_cast<double>(k) / 6.0;
			const FVector2D Point = k == 6 ? MapPos : MapPos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			const double Weight = k == 6 ? 2.0 : 1.0;
			Layout.BiomeWeightsAt(Point, Sample);
			for (int32 b = 0; b < TNProcMap::NumBiomes; ++b)
			{
				Acc[b] += Sample[b] * Weight;
				Total += Sample[b] * Weight;
			}
		}
		for (int32 s = 0; s < TNAmbSlots; ++s) { OutW[s] = 0.f; }
		if (Total <= 1e-6)
		{
			OutW[TNAmbGenericSlot] = 1.f;
			return;
		}
		for (int32 b = 0; b < TNProcMap::NumBiomes; ++b) { OutW[b] = static_cast<float>(Acc[b] / Total); }
	}

	/** Agua alrededor (terreno bajo el nivel del mar) en anillos de 15, 40 y 80 m; los cercanos pesan más. */
	float TNAmbienceWaterAround(const ATN_ProcMapGenerator& Gen, const FTransform& MapXf, const FVector& View)
	{
		static const float Radii[3] = { 1500.f, 4000.f, 8000.f };
		static const float RingWeight[3] = { 1.f, 0.7f, 0.4f };
		float Wet = 0.f;
		float Sum = 0.f;
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 k = 0; k < 8; ++k)
			{
				const double Angle = TNProcMap::TwoPi * (static_cast<double>(k) + 0.5 * r) / 8.0;
				const FVector Probe = View + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Radii[r];
				const float GroundZ = Gen.GetTerrainHeightAt(Probe);
				const double MapZ = MapXf.InverseTransformPosition(FVector(Probe.X, Probe.Y, GroundZ)).Z;
				if (MapZ < TNProcMap::SeaLevel - 25.0) { Wet += RingWeight[r]; }
				Sum += RingWeight[r];
			}
		}
		return Sum > 0.f ? Wet / Sum : 0.f;
	}

	/** Cercanía al río (0..1): se oye desde ~30 m de su orilla. */
	float TNAmbienceRiverNear(const TNProcMap::FLayout& Layout, const FVector2D& MapPos)
	{
		const TArray<FVector2D>& River = Layout.River;
		if (River.Num() < 2) { return 0.f; }
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < River.Num(); ++i)
		{
			double T = 0.0;
			const double Dist = TNProcMap::DistPointSegment(MapPos, River[i], River[i + 1], T);
			const double HalfWidth = 0.5 * (Layout.RiverWidth.IsValidIndex(i) ? Layout.RiverWidth[i] : 800.0);
			Best = FMath::Min(Best, Dist - HalfWidth);
		}
		return TNAmbienceSaturate(1.f - static_cast<float>(Best) / 3000.f);
	}

	/**
	 * Cierre (0..1): una traza de 25 m hacia arriba y cuatro en diagonal. Sin techo encima, nada (un desfiladero no
	 * cierra); con techo, más cuantas más diagonales choquen (cueva, torre hueca). La vegetación no tiene colisión.
	 */
	float TNAmbienceEnclosure(const UWorld& World, const FVector& View, const AActor* IgnoreA, const AActor* IgnoreB)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TNAmbienceEnclosure), false);
		if (IgnoreA) { Query.AddIgnoredActor(IgnoreA); }
		if (IgnoreB) { Query.AddIgnoredActor(IgnoreB); }
		static const FVector Dirs[5] = {
			FVector(0.0, 0.0, 1.0), FVector(0.7, 0.0, 0.7), FVector(-0.7, 0.0, 0.7), FVector(0.0, 0.7, 0.7), FVector(0.0, -0.7, 0.7) };
		int32 Hits = 0;
		bool bRoof = false;
		for (int32 k = 0; k < 5; ++k)
		{
			FHitResult Hit;
			if (World.LineTraceSingleByChannel(Hit, View, View + Dirs[k] * 2500.0, ECC_Visibility, Query))
			{
				++Hits;
				if (k == 0) { bRoof = true; }
			}
		}
		return bRoof ? FMath::Clamp(0.2f + 0.2f * static_cast<float>(Hits - 1), 0.f, 1.f) : 0.f;
	}
}

UTN_AmbientSoundscapeComponent::UTN_AmbientSoundscapeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.25f;
}

UTN_AmbientSoundscapeComponent* UTN_AmbientSoundscapeComponent::EnsureOn(AActor* InOwner)
{
	if (!InOwner) { return nullptr; }
	if (UTN_AmbientSoundscapeComponent* Existing = InOwner->FindComponentByClass<UTN_AmbientSoundscapeComponent>())
	{
		return Existing;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || OwnerWorld->GetNetMode() == NM_DedicatedServer) { return nullptr; }
	UTN_AmbientSoundscapeComponent* NewComp = NewObject<UTN_AmbientSoundscapeComponent>(InOwner, NAME_None, RF_Transient);
	NewComp->RegisterComponent();
	InOwner->AddInstanceComponent(NewComp);
	return NewComp;
}

void UTN_AmbientSoundscapeComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		SetComponentTickEnabled(false);
		return;
	}
	SetComponentTickInterval(FMath::Max(0.05f, UpdateInterval));
	SetComponentTickEnabled(true);
}

void UTN_AmbientSoundscapeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAudio();
	Super::EndPlay(EndPlayReason);
}

void UTN_AmbientSoundscapeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsLocalViewer())
	{
		// No es el jugador de esta máquina (o aún no se sabe: en el host el jugador llega a veces tras BeginPlay).
		if (AmbientSynth) { StopAudio(); }
		SetComponentTickInterval(1.f);
		return;
	}
	if (!AmbientSynth)
	{
		SetComponentTickInterval(FMath::Max(0.05f, UpdateInterval));
		StartAudio();
		return;
	}
	if (!AmbientSynth->IsRegistered())
	{
		// Se ha quedado sin mundo (p. ej. tras un viaje entre mapas): se rehace en el siguiente tick.
		StopAudio();
		return;
	}
	if (!AmbientSynth->IsActive())
	{
		// Algo lo ha parado (fin de nivel, dispositivo de audio nuevo): vuelve a sonar con los mismos parámetros.
		AmbientSynth->Start();
	}
	UpdateMix(DeltaTime);
}

bool UTN_AmbientSoundscapeComponent::IsLocalViewer() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return false;
	}
	const AActor* OwnerActor = GetOwner();
	// Con la pantalla partida (#311) suena una sola vez, la del jugador 1 (cada jugador local tiene el suyo en su mando).
	if (const APlayerController* PC = Cast<APlayerController>(OwnerActor)) { return PC->IsLocalController() && UTN_LocalPlaySubsystem::IsPrimaryPlayer(PC); }
	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		return OwnerPawn->IsLocallyControlled() && UTN_LocalPlaySubsystem::IsPrimaryPlayer(Cast<APlayerController>(OwnerPawn->GetController()));
	}
	// En otro actor (colocado a mano en un nivel): suena para quien esté mirando en esta máquina.
	return true;
}

APlayerController* UTN_AmbientSoundscapeComponent::ResolvePlayerController() const
{
	AActor* OwnerActor = GetOwner();
	if (APlayerController* PC = Cast<APlayerController>(OwnerActor)) { return PC; }
	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor)) { return Cast<APlayerController>(OwnerPawn->GetController()); }
	const UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}

bool UTN_AmbientSoundscapeComponent::GetViewLocation(FVector& OutLocation) const
{
	// La cámara (también de espectador tras morir); si no hay, el pawn o el propio dueño.
	const APlayerController* PC = ResolvePlayerController();
	if (PC && PC->PlayerCameraManager)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	if (const APawn* ViewPawn = PC ? static_cast<const APawn*>(PC->GetPawn()) : Cast<APawn>(GetOwner()))
	{
		OutLocation = ViewPawn->GetActorLocation();
		return true;
	}
	if (const AActor* OwnerActor = GetOwner())
	{
		OutLocation = OwnerActor->GetActorLocation();
		return true;
	}
	return false;
}

void UTN_AmbientSoundscapeComponent::StartAudio()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || AmbientSynth) { return; }

	UTN_AmbientSynthComponent* NewSynth = NewObject<UTN_AmbientSynthComponent>(OwnerActor, NAME_None, RF_Transient);
	NewSynth->SourceKind = ETNAmbientSourceKind::Soundscape;
	NewSynth->Loudness = 1.f;
	NewSynth->SoundClass = SoundClassOverride;
	if (USceneComponent* RootComp = OwnerActor->GetRootComponent())
	{
		NewSynth->SetupAttachment(RootComp);
	}
	NewSynth->RegisterComponent();
	NewSynth->SetSmoothingSeconds(LevelSmoothingSeconds);
	AmbientSynth = NewSynth;

	// Primera mezcla antes de arrancar: las capas entran en fundido desde cero hasta su volumen.
	RebuildOverrides();
	UpdateMix(0.f);
	NewSynth->Start();
}

void UTN_AmbientSoundscapeComponent::StopAudio()
{
	ClearOverrides();
	if (AmbientSynth)
	{
		AmbientSynth->Stop();
		AmbientSynth->DestroyComponent();
		AmbientSynth = nullptr;
	}
}

void UTN_AmbientSoundscapeComponent::ClearOverrides()
{
	for (FTNAmbienceOverrideVoice& Voice : OverrideVoices)
	{
		if (Voice.Component)
		{
			Voice.Component->Stop();
			Voice.Component->DestroyComponent();
		}
	}
	OverrideVoices.Reset();
	for (uint16& Mask : OverrideMask) { Mask = 0; }
}

void UTN_AmbientSoundscapeComponent::SetAmbienceData(UTN_AmbienceDataAsset* InData)
{
	AmbienceData = InData;
	if (AmbientSynth) { RebuildOverrides(); }
}

void UTN_AmbientSoundscapeComponent::RebuildOverrides()
{
	ClearOverrides();
	AActor* OwnerActor = GetOwner();
	if (!AmbienceData || !OwnerActor) { return; }

	// Un componente de audio 2D por sonido, parado hasta que su parte de capa tenga volumen. Se cargan aquí, al
	// arrancar el ambiente del jugador local (pocos sonidos; si fueran muchos, convendría cargarlos en asíncrono).
	auto AddVoice = [this, OwnerActor](int32 Slot, ETNAmbientLayer InLayer, const TSoftObjectPtr<USoundBase>& SoundRef, float Scale)
	{
		const int32 LayerIdx = static_cast<int32>(InLayer);
		if (Slot < 0 || Slot >= TNAmbSlots || LayerIdx < 0 || LayerIdx >= TNAmbLayers || SoundRef.IsNull()) { return; }
		if ((OverrideMask[Slot] & (1u << LayerIdx)) != 0u) { return; }
		USoundBase* Loaded = SoundRef.LoadSynchronous();
		if (!Loaded) { return; }
		UAudioComponent* Comp = NewObject<UAudioComponent>(OwnerActor, NAME_None, RF_Transient);
		Comp->bAutoActivate = false;
		Comp->bAutoDestroy = false;
		Comp->bAllowSpatialization = false;
		Comp->bIsUISound = false;
		Comp->SetSound(Loaded);
		if (SoundClassOverride) { Comp->SoundClassOverride = SoundClassOverride; }
		if (USceneComponent* RootComp = OwnerActor->GetRootComponent())
		{
			Comp->SetupAttachment(RootComp);
		}
		Comp->RegisterComponent();
		FTNAmbienceOverrideVoice& NewVoice = OverrideVoices.AddDefaulted_GetRef();
		NewVoice.Component = Comp;
		NewVoice.BiomeSlot = Slot;
		NewVoice.LayerIndex = LayerIdx;
		NewVoice.VolumeScale = FMath::Max(0.f, Scale);
		OverrideMask[Slot] = static_cast<uint16>(OverrideMask[Slot] | (1u << LayerIdx));
	};
	for (const FTNAmbienceOverride& Entry : AmbienceData->BiomeOverrides)
	{
		AddVoice(static_cast<int32>(Entry.Biome), Entry.Layer, Entry.Sound, Entry.VolumeScale);
	}
	for (const FTNAmbienceGenericOverride& Entry : AmbienceData->GenericOverrides)
	{
		AddVoice(TNAmbGenericSlot, Entry.Layer, Entry.Sound, Entry.VolumeScale);
	}
}

void UTN_AmbientSoundscapeComponent::UpdateMix(float DeltaTime)
{
	UWorld* World = GetWorld();
	FVector View = FVector::ZeroVector;
	if (!World || !AmbientSynth || !GetViewLocation(View)) { return; }

	// Generador y tormenta: se buscan cada 2 s mientras falten (el mapa puede llegar después que el jugador).
	LookupTimer -= DeltaTime;
	if ((!Generator.IsValid() || !Storm.IsValid()) && LookupTimer <= 0.f)
	{
		LookupTimer = 2.f;
		if (!Generator.IsValid())
		{
			for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It) { Generator = *It; break; }
		}
		if (!Storm.IsValid())
		{
			for (TActorIterator<ATN_PathStorm> It(World); It; ++It) { Storm = *It; break; }
		}
		// La de la carrera en la playa (no hay generador ahí): se busca sola, la crea el GameMode en cada ronda.
		if (!BeachStorm.IsValid() && !Generator.IsValid())
		{
			BeachStorm = ATN_BeachStorm::FindStorm(World);
		}
	}
	const ATN_ProcMapGenerator* Gen = Generator.Get();
	const bool bMap = Gen && Gen->IsMapReady() && Gen->GetLayout().bValid;

	float BiomeW[TNAmbSlots] = {};
	FTNAmbienceContext Ctx;
	if (bMap)
	{
		const TNProcMap::FLayout& Layout = Gen->GetLayout();
		const FTransform MapXf = Gen->GetActorTransform();
		const FVector MapView = MapXf.InverseTransformPosition(View);
		const FVector2D MapPos(MapView.X, MapView.Y);
		const double MapScale = FMath::Max(0.01, static_cast<double>(MapXf.GetScale3D().X));
		TNAmbienceBiomeWeights(Layout, MapPos, static_cast<double>(BlendRadius) / MapScale, BiomeW);

		const float GroundWorldZ = Gen->GetTerrainHeightAt(View);
		const double GroundMapZ = MapXf.InverseTransformPosition(FVector(View.X, View.Y, GroundWorldZ)).Z;
		Ctx.HeightAboveSea = static_cast<float>(MapView.Z - TNProcMap::SeaLevel);
		Ctx.HeightAboveGround = static_cast<float>(MapView.Z - GroundMapZ);
		Ctx.bUnderwater = MapView.Z < TNProcMap::SeaLevel - 40.0 && GroundMapZ < MapView.Z;
		Ctx.WaterNear = TNAmbienceWaterAround(*Gen, MapXf, View);
		// Mar abierto al norte de la costa: rompientes grandes desde ~120 m antes de la orilla.
		Ctx.SeaNear = TNAmbienceSaturate(1.f - static_cast<float>(Layout.CoastY(MapPos.X) - MapPos.Y) / 12000.f);
		Ctx.RiverNear = TNAmbienceRiverNear(Layout, MapPos);

		// Tormenta: dentro si la cámara va por detrás del frente (el mismo criterio que la cuenta atrás, sin su margen
		// de juego); cerca, en los 70 m por delante del frente.
		if (const ATN_PathStorm* StormActor = Storm.Get())
		{
			if (StormActor->IsStormActive())
			{
				const float Progress = Gen->GetPathProgress(View);
				const float Front = StormActor->GetFrontProgress();
				Ctx.StormInside = Progress < Front - 400.f ? 1.f : 0.f;
				Ctx.StormNear = TNAmbienceSaturate(1.f - (Progress - Front) / 7000.f);
			}
		}
	}
	else if (bPlayWithoutGenerator)
	{
		BiomeW[TNAmbGenericSlot] = 1.f;
	}
	if (!bMap)
	{
		// Sin mapa procedural, la tormenta de bañistas de la playa suena como la del camino (mismo viento, silbido y truenos;
		// es su único ruido): dentro si la cámara va por detrás del frente (con el mismo margen de 4 m) y cerca en los 70 m
		// por delante. El frente y su eje salen del propio actor, con el reloj del servidor, en cualquier máquina.
		if (const ATN_BeachStorm* BeachStormActor = BeachStorm.Get())
		{
			if (BeachStormActor->IsStormActive())
			{
				const float Ahead = static_cast<float>(BeachStormActor->GetActorTransform().InverseTransformPositionNoScale(View).X)
					- BeachStormActor->GetFrontDistance();
				Ctx.StormInside = Ahead < -400.f ? 1.f : 0.f;
				Ctx.StormNear = TNAmbienceSaturate(1.f - Ahead / 7000.f);
			}
		}
	}

	if (bDetectEnclosure)
	{
		const APlayerController* PC = ResolvePlayerController();
		Ctx.Enclosure = TNAmbienceEnclosure(*World, View, GetOwner(), PC ? PC->GetPawn() : nullptr);
	}
	if (Ctx.bUnderwater) { Ctx.Enclosure = 1.f; }

	// Preajustes mezclados con los pesos de bioma, y lo que aporta cada bioma a cada capa (para las sustituciones).
	FTNAmbientMix Mix = FTNAmbientMix::Zero();
	float Contribution[TNAmbSlots][TNAmbLayers] = {};
	float WeightSum = 0.f;
	for (int32 s = 0; s < TNAmbSlots; ++s)
	{
		if (BiomeW[s] <= 1e-4f) { continue; }
		FTNAmbientMix SlotMix;
		if (s == TNAmbGenericSlot)
		{
			UTN_AmbientSynthComponent::GetGenericMix(SlotMix);
		}
		else
		{
			UTN_AmbientSynthComponent::GetBiomeMix(static_cast<ETNProcBiome>(s), SlotMix);
		}
		Mix.AddWeighted(SlotMix, BiomeW[s]);
		WeightSum += BiomeW[s];
		for (int32 l = 0; l < TNAmbLayers; ++l) { Contribution[s][l] = BiomeW[s] * SlotMix.Layers[l]; }
	}
	if (WeightSum > 1e-4f) { Mix.Scale(1.f / WeightSum); }

	// Contexto de la cámara sobre la mezcla.
	const float Night = TNAmbienceSaturate(NightAmount);
	const float Exposure = bMap
		? TNAmbienceSaturate((Ctx.HeightAboveSea - 1500.f) / 5000.f) * 0.7f + TNAmbienceSaturate((Ctx.HeightAboveGround - 500.f) / 2500.f) * 0.6f
		: 0.f;
	const float StormIn = Ctx.StormInside;
	const float StormNear = Ctx.StormNear;
	const float Enc = Ctx.Enclosure;
	const float HighAbove = bMap ? 1.f - 0.6f * TNAmbienceSaturate((Ctx.HeightAboveSea - 800.f) / 6000.f) : 1.f;
	const float Fauna = (1.f - 0.85f * StormIn) * (1.f - 0.35f * StormNear) * (1.f - 0.85f * Enc);

	FTNAmbientMix Final = Mix;
	if (WeightSum > 1e-4f)
	{
		float* Lv = Final.Layers;
		// Viento: más en alto (cimas, puentes, acantilados) y con la tormenta; menos bajo techo.
		Lv[TNAmbWind] = FMath::Min(1.6f, (Mix.Layers[TNAmbWind] * (1.f + 0.9f * Exposure) + 0.25f * Exposure)
			* (1.f + 1.1f * StormIn + 0.4f * StormNear) * (1.f - 0.7f * Enc));
		Final.WindGust = TNAmbienceSaturate(Mix.WindGust + 0.25f * Exposure + 0.35f * StormIn);
		Final.WindBright = TNAmbienceSaturate(Mix.WindBright + 0.25f * Exposure + 0.35f * StormIn);
		Final.WindWhistle = TNAmbienceSaturate(Mix.WindWhistle + 0.2f * Exposure * (1.f - Mix.WindWhistle) + 0.15f * StormIn);
		// Oleaje: el del bioma si hay agua alrededor y el del mar abierto junto a la costa; más flojo desde lo alto.
		Lv[TNAmbSurf] = FMath::Min(1.2f, (Mix.Layers[TNAmbSurf] * TNAmbienceSaturate(Ctx.WaterNear * 1.8f) + 0.9f * Ctx.SeaNear)
			* HighAbove * (1.f - 0.5f * Enc));
		Final.SurfSize = FMath::Lerp(Mix.SurfSize, 1.f, Ctx.SeaNear);
		// Agua corriente: la del bioma (borboteo del manglar) y el río si está cerca.
		Lv[TNAmbStream] = FMath::Min(1.f, Mix.Layers[TNAmbStream] + 0.85f * Ctx.RiverNear);
		// Fauna: callada en la tormenta y bajo techo; de noche, menos aves y cigarras y más grillos y ranas.
		Lv[TNAmbBirds] = Mix.Layers[TNAmbBirds] * Fauna * (1.f - 0.7f * Night);
		Final.BirdRate = Mix.BirdRate * (1.f - 0.6f * StormIn) * (1.f - 0.6f * Night);
		Lv[TNAmbCicadas] = Mix.Layers[TNAmbCicadas] * Fauna * (1.f - 0.85f * Night);
		Lv[TNAmbCrickets] = (Mix.Layers[TNAmbCrickets] * (1.f + 1.5f * Night) + 0.1f * Night) * (1.f - 0.6f * StormIn) * (1.f - 0.6f * Enc);
		Lv[TNAmbFrogs] = Mix.Layers[TNAmbFrogs] * (1.f + 0.8f * Night) * (1.f - 0.5f * StormIn) * (1.f - 0.6f * Enc);
		Lv[TNAmbBells] = Mix.Layers[TNAmbBells] * (1.f - 0.7f * StormIn) * (1.f - 0.6f * Enc);
		Final.Storm = TNAmbienceSaturate(0.8f * StormIn + 0.3f * StormNear);
		Final.Enclosure = Enc;
	}

	// Reparto de cada capa entre biomas: la parte de un bioma con sonido de sustitución suena con él y no se sintetiza.
	float Shares[TNAmbSlots][TNAmbLayers] = {};
	FTNAmbientMix ToSynth = Final;
	for (int32 l = 0; l < TNAmbLayers; ++l)
	{
		float LayerTotal = 0.f;
		for (int32 s = 0; s < TNAmbSlots; ++s) { LayerTotal += Contribution[s][l]; }
		float Replaced = 0.f;
		for (int32 s = 0; s < TNAmbSlots; ++s)
		{
			if ((OverrideMask[s] & (1u << l)) == 0u) { continue; }
			Shares[s][l] = LayerTotal > 1e-4f ? Contribution[s][l] / LayerTotal : BiomeW[s];
			Replaced += Shares[s][l];
		}
		ToSynth.Layers[l] = Final.Layers[l] * FMath::Max(0.f, 1.f - Replaced);
	}

	const float Master = MasterVolume * FMath::Max(0.f, GTNAmbienceVolume);
	AmbientSynth->SetMasterGain(Master);
	AmbientSynth->ApplyMix(ToSynth);

	for (FTNAmbienceOverrideVoice& Voice : OverrideVoices)
	{
		UAudioComponent* Comp = Voice.Component;
		if (!Comp) { continue; }
		const float Target = Final.Layers[Voice.LayerIndex] * Shares[Voice.BiomeSlot][Voice.LayerIndex] * Voice.VolumeScale * Master;
		if (Target > 0.002f)
		{
			Voice.SilentFor = 0.f;
			if (!Voice.bPlaying || !Comp->IsPlaying())
			{
				// Primera vez, o un sonido sin bucle que ha terminado: vuelve a empezar en fundido.
				Comp->FadeIn(0.8f, Target);
				Voice.bPlaying = true;
			}
			else
			{
				Comp->AdjustVolume(UpdateInterval + 0.1f, Target);
			}
		}
		else if (Voice.bPlaying)
		{
			Voice.SilentFor += DeltaTime;
			Comp->AdjustVolume(UpdateInterval + 0.1f, 0.f);
			if (Voice.SilentFor > 2.f)
			{
				Comp->Stop();
				Voice.bPlaying = false;
			}
		}
	}

	// Estado para depurar.
	LastMix = Final;
	for (int32 s = 0; s < TNAmbSlots; ++s) { LastBiomeW[s] = BiomeW[s]; }
	LastWaterNear = Ctx.WaterNear;
	LastSeaNear = Ctx.SeaNear;
	LastRiverNear = Ctx.RiverNear;
	LastHeight = Ctx.HeightAboveSea;
	LastStorm = Final.Storm;
	LastEnclosure = Enc;
	bLastHasMap = bMap;
	if (GTNAmbienceDebug != 0 && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) + 0x7A3B10000ull, FMath::Max(0.3f, UpdateInterval + 0.1f),
			FColor::Cyan, GetDebugString());
	}
}

FString UTN_AmbientSoundscapeComponent::GetDebugString() const
{
	static const TCHAR* SlotNames[TNAmbSlots] = {
		TEXT("selva"), TEXT("playa"), TEXT("desierto"), TEXT("volcán"), TEXT("agua"), TEXT("roca"), TEXT("manglar"), TEXT("humana"), TEXT("genérico") };
	static const TCHAR* LayerNames[TNAmbLayers] = {
		TEXT("viento"), TEXT("oleaje"), TEXT("agua"), TEXT("aves"), TEXT("cigarras"), TEXT("grillos"), TEXT("ranas"), TEXT("lava"), TEXT("campanas") };
	FString Out = bLastHasMap ? TEXT("Ambiente (mapa):") : TEXT("Ambiente (sin mapa):");
	for (int32 s = 0; s < TNAmbSlots; ++s)
	{
		if (LastBiomeW[s] >= 0.01f) { Out += FString::Printf(TEXT(" %s %.0f%%"), SlotNames[s], 100.f * LastBiomeW[s]); }
	}
	Out += TEXT("\n  capas:");
	for (int32 l = 0; l < TNAmbLayers; ++l)
	{
		if (LastMix.Layers[l] >= 0.005f) { Out += FString::Printf(TEXT(" %s %.2f"), LayerNames[l], LastMix.Layers[l]); }
	}
	Out += FString::Printf(TEXT("\n  agua %.2f | mar %.2f | río %.2f | altura %.0f m | tormenta %.2f | cierre %.2f | noche %.2f | sustituciones %d"),
		LastWaterNear, LastSeaNear, LastRiverNear, LastHeight / 100.f, LastStorm, LastEnclosure, NightAmount, OverrideVoices.Num());
	return Out;
}
