#include "World/ProcMap/TN_PathStorm.h"
#include "Multiplayer/TN_LocalViews.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "World/ProcMap/TN_StormCough.h"
#include "TN_PathStormFX.h"
#include "ProceduralMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "World/Beach/TN_BeachShelterVolume.h"

ATN_PathStorm::ATN_PathStorm()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(4.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	FrontWall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontWall"));
	FrontWall->SetupAttachment(Root);
	FrontWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrontWall->SetCastShadow(false);
	// Muro alto y ancho perpendicular al camino (el camino va por +X local).
	FrontWall->SetRelativeScale3D(FVector(1.f, 180.f, 90.f));
	FrontWall->SetRelativeLocation(FVector(0.f, 0.f, 3000.f));
	FrontWall->SetHiddenInGame(true);
	FrontWall->SetVisibility(false);
	if (Cube.Succeeded()) { FrontWall->SetStaticMesh(Cube.Object); }

	InsidePostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("InsidePostProcess"));
	InsidePostProcess->SetupAttachment(Root);
	InsidePostProcess->bUnbound = true;
	InsidePostProcess->bEnabled = false;
}

void ATN_PathStorm::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_PathStorm, FrontProgress);
	DOREPLIFETIME(ATN_PathStorm, Speed);
	DOREPLIFETIME(ATN_PathStorm, bActive);
	DOREPLIFETIME(ATN_PathStorm, Generator);
}

void ATN_PathStorm::BeginPlay()
{
	Super::BeginPlay();
	static_assert(MaxBiomes == TNProcMap::NumBiomes, "MaxBiomes debe coincidir con los biomas");
}

void ATN_PathStorm::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreFog();
	TNAmbientFX::Registry().Remove(this);
	Super::EndPlay(EndPlayReason);
}

void ATN_PathStorm::StartStorm(ATN_ProcMapGenerator* InGenerator, float InSpeed, float GraceSeconds)
{
	if (!HasAuthority()) { return; }
	Generator = InGenerator;
	Speed = InSpeed;
	GraceRemaining = GraceSeconds;
	// Arranca un poco por detrás de la salida para que el claro inicial quede libre.
	FrontProgress = -3000.f;
	bActive = InGenerator != nullptr && InSpeed > 0.f;
	InsideTime.Reset();
	UE_LOG(LogTortunabo, Log, TEXT("[PathStorm] Tormenta %s · %.0f cm/s · gracia %.0fs"),
		bActive ? TEXT("activa") : TEXT("inactiva"), Speed, GraceSeconds);
}

void ATN_PathStorm::DebugPlaceFront(float Progress, bool bHarmless)
{
	if (!HasAuthority()) { return; }
	if (!Generator)
	{
		for (TActorIterator<ATN_ProcMapGenerator> It(GetWorld()); It; ++It) { Generator = *It; break; }
	}
	if (DefaultSecondsInsideToDie < 0.f) { DefaultSecondsInsideToDie = SecondsInsideToDie; }
	SecondsInsideToDie = bHarmless ? 1.0e7f : DefaultSecondsInsideToDie;
	if (Speed <= 0.f) { Speed = 250.f; }
	bActive = Generator != nullptr;
	GraceRemaining = 0.f;
	FrontProgress = Progress;
	InsideTime.Reset();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[PathStorm] Prueba: frente en %.0f cm%s."), Progress, bHarmless ? TEXT(" (inofensiva)") : TEXT(""));
}

void ATN_PathStorm::StopStorm()
{
	if (!HasAuthority()) { return; }
	bActive = false;
	FrontProgress = -3000.f;
	for (auto& Pair : InsideTime)
	{
		if (APlayerController* PC = Pair.Key.Get())
		{
			if (ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>()) { PS->DeathZoneTimeRemaining = -1.f; }
		}
	}
	InsideTime.Reset();
}

void ATN_PathStorm::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bActive)
	{
		if (HasAuthority())
		{
			if (GraceRemaining > 0.f) { GraceRemaining -= DeltaTime; }
			else { FrontProgress += Speed * DeltaTime; }

			CheckAccumulator += DeltaTime;
			if (CheckAccumulator >= 0.2f)
			{
				ServerCheckPlayers(CheckAccumulator);
				CheckAccumulator = 0.f;
			}
		}
		else if (Speed > 0.f)
		{
			// Extrapolación suave entre actualizaciones de red.
			FrontProgress += Speed * DeltaTime * 0.9f;
		}
	}
	UpdateVisual(DeltaTime);
	TickCough(DeltaTime);
}

bool ATN_PathStorm::IsLocationInside(const FVector& WorldLocation) const
{
	if (!bActive || !Generator || !Generator->IsMapReady()) { return false; }
	return Generator->GetPathProgress(WorldLocation) < FrontProgress - InsideMargin;
}

void ATN_PathStorm::TickCough(float DeltaTime)
{
	// Tos de las tortugas: cada máquina con audio decide quién está dentro con el frente replicado, sin RPC.
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio()) { return; }
	CoughAccumulator += DeltaTime;
	if (CoughAccumulator < 0.1f) { return; }
	const float Step = CoughAccumulator;
	CoughAccumulator = 0.f;

	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		if (!IsValid(Turtle) || Turtle->IsActorBeingDestroyed()) { continue; }
		// Las mismas que cuenta el servidor en ServerCheckPlayers (su estado de jugador llega a todas las máquinas).
		const ATN_CoopPlayerState* PS = Turtle->GetPlayerState<ATN_CoopPlayerState>();
		if (!PS) { continue; }
		if (!PS->bIsAlive || Turtle->IsDead())
		{
			CoughInsideTime.Remove(Turtle);
			if (UTN_StormCoughComponent* DeadCough = Turtle->FindComponentByClass<UTN_StormCoughComponent>()) { DeadCough->Hush(); }
			continue;
		}
		UTN_StormCoughComponent* Cough = UTN_StormCoughComponent::FindOrAddTo(Turtle);
		if (!Cough) { continue; }
		const bool bInside = !PS->bHasFinishedRun && IsLocationInside(Turtle->GetActorLocation()) && !ATN_BeachShelterVolume::IsSheltered(Turtle);
		float& Seconds = CoughInsideTime.FindOrAdd(Turtle);
		Seconds = bInside ? Seconds + Step : 0.f;
		Cough->SetStormExposure(bInside, Seconds / FMath::Max(0.1f, SecondsInsideToDie));
	}
	for (auto It = CoughInsideTime.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); }
	}
}

void ATN_PathStorm::UpdateVisual(float DeltaTime)
{
	const bool bRunning = Generator && Generator->IsMapReady() && bActive;
	FVector Dir = GetActorForwardVector();
	FVector FrontLoc = GetActorLocation();
	bool bLocalInside = false;
	if (bRunning)
	{
		FrontLoc = Generator->GetPathLocationAtProgress(FMath::Max(0.f, FrontProgress), Dir);
		SetActorLocationAndRotation(FrontLoc, FRotator(0.f, Dir.Rotation().Yaw, 0.f));
		// Efecto de dentro solo para los jugadores locales (con la pantalla partida, si alguno está dentro).
		TArray<APlayerController*> LocalControllers;
		TNLocalViews::GetLocalControllers(GetWorld(), LocalControllers);
		for (const APlayerController* PC : LocalControllers)
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				bLocalInside = bLocalInside || IsLocationInside(Pawn->GetActorLocation());
			}
		}
	}
	TickFX(DeltaTime, bRunning && FrontProgress > 0.f, bLocalInside, FrontLoc, Dir);
}

void ATN_PathStorm::SetupFX()
{
	bFXReady = true;
	using namespace TNStormFX;
	// Velo: la malla se guarda en blanco y se tiñe al color mezclado de los biomas.
	TNProcMesh::FTNProcMeshBuffers M;
	BuildVeil(M, FLinearColor::White, 18000.0, 6500.0, 3, 17u);
	VeilVerts = M.Verts;
	VeilTris = M.Tris;
	VeilNormals = M.Normals;
	VeilUVs = M.UVs;
	VeilBase = M.Colors;
	Veil = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	Veil->SetupAttachment(Root);
	Veil->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Veil->SetCastShadow(false);
	Veil->bUseAsyncCooking = false;
	Veil->RegisterComponent();
	Veil->CreateMeshSection_LinearColor(0, VeilVerts, VeilTris, VeilNormals, VeilUVs, VeilBase, TArray<FProcMeshTangent>(), false);
	UMaterialInterface* VeilMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcStormVeil.M_ProcStormVeil"));
	if (!VeilMat) { VeilMat = TNAmbientFX::MaterialFor(true); }
	if (VeilMat) { Veil->SetMaterial(0, VeilMat); }
	Veil->SetVisibility(false);

	// Lo que arrastra: dos clases por bioma, en el frente y alrededor de la cámara (dentro).
	for (int32 b = 0; b < MaxBiomes; ++b)
	{
		const FLook& L = LookFor(TNProcMap::BiomeFromIndex(b));
		for (const FDebris& D : L.Debris)
		{
			TNAmbientFX::FEmitterDesc E;
			E.Shape = D.Shape;
			E.bSoft = D.bSoft;
			E.bCloud = D.bSoft && D.Shape == EShape::Puff;
			E.Color = D.Color;
			E.Alpha = D.Alpha;
			E.MaxParticles = D.Max;
			E.Rate = D.Rate;
			E.SpawnRadius = 2600.f;
			E.SpawnHeight = 2200.f;
			E.Direction = FVector::ForwardVector;
			E.Speed = D.Speed;
			E.SpeedJitter = 0.4f;
			E.Spread = 0.55f;
			E.Gravity = D.Gravity;
			E.Buoyancy = D.Buoyancy;
			E.Drag = D.Drag;
			E.LifeMin = D.LifeMin;
			E.LifeMax = D.LifeMax;
			E.SizeStart = D.Size;
			E.SizeEnd = D.Shape == EShape::Puff ? D.Size * 1.7f : D.Size * 0.8f;
			E.WakeDistance = 30000.f;
			FrontEmitters.Add(TNAmbientFX::AddEmitter(this, E, GetActorLocation()));
			E.MaxParticles = FMath::Max(20, D.Max * 2 / 3);
			E.SpawnRadius = 1500.f;
			E.SpawnHeight = 900.f;
			if (D.Shape != EShape::Puff) { E.SizeStart *= 1.4f; E.SizeEnd *= 1.4f; }
			E.WakeDistance = 1e7f;
			ViewEmitters.Add(TNAmbientFX::AddEmitter(this, E, GetActorLocation()));
		}
		// Nubes que ruedan en la base del frente (solo en el frente).
		TNAmbientFX::FEmitterDesc C;
		C.Shape = EShape::Puff;
		C.bSoft = true;
		C.bCloud = true;
		C.Color = L.Veil * 0.8f;
		C.Alpha = 0.7f;
		C.MaxParticles = 70;
		C.Rate = 22.f;
		C.SpawnRadius = 3000.f;
		C.SpawnHeight = 900.f;
		C.Direction = FVector::ForwardVector;
		C.Speed = 380.f;
		C.SpeedJitter = 0.5f;
		C.Spread = 0.5f;
		C.Gravity = 0.f;
		C.Buoyancy = 35.f;
		C.Drag = 0.35f;
		C.LifeMin = 3.f;
		C.LifeMax = 5.5f;
		C.SizeStart = 450.f;
		C.SizeEnd = 900.f;
		C.WakeDistance = 30000.f;
		FrontEmitters.Add(TNAmbientFX::AddEmitter(this, C, GetActorLocation()));
	}

	// Niebla del nivel (la primera); si no hay, una propia.
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		if (UExponentialHeightFogComponent* C = It->GetComponent()) { Fog = C; break; }
	}
	if (!Fog.IsValid())
	{
		UExponentialHeightFogComponent* Own = NewObject<UExponentialHeightFogComponent>(this, NAME_None, RF_Transient);
		Own->SetupAttachment(Root);
		Own->SetFogDensity(0.f);
		Own->RegisterComponent();
		Fog = Own;
	}
}

void ATN_PathStorm::TickFX(float DeltaTime, bool bFrontVisible, bool bLocalInside, const FVector& FrontLoc, const FVector& Dir)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) { return; }
	if (!bFXReady) { SetupFX(); }
	FXTime += DeltaTime;

	// Cámara local (con la pantalla partida, la más cercana al frente).
	FVector View = FrontLoc;
	FVector ViewFwd = Dir;
	FRotator ViewRot;
	if (TNLocalViews::ClosestCamera(World, FrontLoc, View, &ViewRot))
	{
		ViewFwd = ViewRot.Vector();
	}

	// Pesos de bioma suavizados (~1,5 s): al cambiar de bioma, el efecto cambia en degradado.
	if (Generator && Generator->IsMapReady())
	{
		const float K = 1.f - FMath::Exp(-DeltaTime / 1.5f);
		double W[TNProcMap::NumBiomes];
		const FVector MF = Generator->GetActorTransform().InverseTransformPosition(FrontLoc);
		Generator->GetLayout().BiomeWeightsAt(FVector2D(MF.X, MF.Y), W);
		for (int32 b = 0; b < MaxBiomes; ++b) { FrontW[b] += (static_cast<float>(W[b]) - FrontW[b]) * K; }
		const FVector MV = Generator->GetActorTransform().InverseTransformPosition(View);
		Generator->GetLayout().BiomeWeightsAt(FVector2D(MV.X, MV.Y), W);
		for (int32 b = 0; b < MaxBiomes; ++b) { ViewW[b] += (static_cast<float>(W[b]) - ViewW[b]) * K; }
	}
	FrontBlend = FMath::FInterpConstantTo(FrontBlend, bFrontVisible ? 1.f : 0.f, DeltaTime, 0.5f);
	InsideBlend = FMath::FInterpConstantTo(InsideBlend, bLocalInside ? 1.f : 0.f, DeltaTime, 0.7f);

	// Velo: se mece un poco y se tiñe del color mezclado (se rehace el color 5 veces por segundo si cambia).
	if (Veil)
	{
		Veil->SetVisibility(FrontBlend > 0.01f);
		Veil->SetRelativeLocation(FVector(180.f * FMath::Sin(FXTime * 0.55f), 0.f, 120.f * FMath::Sin(FXTime * 0.31f)));
		VeilRefresh -= DeltaTime;
		if (FrontBlend > 0.01f && VeilRefresh <= 0.f)
		{
			VeilRefresh = 0.2f;
			FLinearColor VeilC, FogC, Tint;
			float Density = 0.f, Sat = 0.f;
			TNStormFX::Blend(FrontW, VeilC, FogC, Density, Tint, Sat);
			const bool bColor = !VeilC.Equals(VeilShown, 0.01f);
			if (bColor || FMath::Abs(FrontBlend - VeilShownAlpha) > 0.04f)
			{
				VeilShown = VeilC;
				VeilShownAlpha = FrontBlend;
				TArray<FLinearColor> Colors;
				Colors.SetNum(VeilBase.Num());
				for (int32 i = 0; i < VeilBase.Num(); ++i)
				{
					FLinearColor C = VeilC * VeilBase[i];
					C.A = VeilBase[i].A * FrontBlend;
					Colors[i] = C;
				}
				Veil->UpdateMeshSection_LinearColor(0, VeilVerts, VeilNormals, VeilUVs, Colors, TArray<FProcMeshTangent>());
			}
		}
	}

	// Partículas: en el frente según su bioma y, dentro, alrededor de la cámara según el del jugador.
	if (TNAmbientFX::FOwnerFX* FX = TNAmbientFX::Registry().Find(this))
	{
		const FVector Wind = (Dir + FVector(0.f, 0.f, 0.12f)).GetSafeNormal();
		const FVector Flat = FVector(ViewFwd.X, ViewFwd.Y, 0.f).GetSafeNormal();
		auto Run = [&](int32 Index, const FVector& Origin, float Scale)
		{
			if (!FX->Emitters.IsValidIndex(Index)) { return; }
			TNAmbientFX::FEmitter& E = FX->Emitters[Index];
			E.Origin = Origin;
			E.Desc.Direction = Wind;
			E.RateScale = Scale;
			bool bAlive = Scale > 0.f;
			for (int32 p = 0; p < E.Particles.Num() && !bAlive; ++p) { bAlive = E.Particles[p].bAlive; }
			if (bAlive) { TNAmbientFX::TickEmitter(E, DeltaTime, View); }
		};
		// Por bioma: en el frente sus dos restos y sus nubes (3); alrededor de la cámara, sus dos restos (2).
		for (int32 k = 0; k < FrontEmitters.Num(); ++k)
		{
			Run(FrontEmitters[k], FrontLoc - Dir * 600.f - FVector(0.f, 0.f, 300.f), FrontW[k / 3] * FrontBlend);
		}
		for (int32 k = 0; k < ViewEmitters.Num(); ++k)
		{
			Run(ViewEmitters[k], View + Flat * 700.f - FVector(0.f, 0.f, 350.f), ViewW[k / 2] * InsideBlend);
		}
	}

	ApplyInsideLook();
}

void ATN_PathStorm::ApplyInsideLook()
{
	FLinearColor VeilC, FogC, Tint;
	float Density = 0.f, Sat = 1.f;
	TNStormFX::Blend(ViewW, VeilC, FogC, Density, Tint, Sat);
	const float B = InsideBlend;

	// Niebla: se cierra dentro (casi uniforme en altura) y vuelve exactamente a su estado al salir.
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		if (!bFogCached)
		{
			bFogCached = true;
			FogDensity0 = F->FogDensity;
			FogFalloff0 = F->FogHeightFalloff;
			FogStart0 = F->StartDistance;
			FogOpacity0 = F->FogMaxOpacity;
			FogColor0 = F->FogInscatteringLuminance;
		}
		if (B > 0.001f)
		{
			F->SetFogDensity(FMath::Lerp(FogDensity0, Density, B));
			F->SetFogHeightFalloff(FMath::Lerp(FogFalloff0, 0.002f, B));
			F->SetStartDistance(FMath::Lerp(FogStart0, 0.f, B));
			F->SetFogMaxOpacity(FMath::Lerp(FogOpacity0, 1.f, B));
			F->SetFogInscatteringColor(FMath::Lerp(FogColor0, FogC, B));
			bFogApplied = true;
		}
		else
		{
			RestoreFog();
		}
	}

	// Tinte, saturación y viñeta.
	InsidePostProcess->bEnabled = B > 0.001f;
	InsidePostProcess->BlendWeight = B;
	FPostProcessSettings& S = InsidePostProcess->Settings;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(Tint.R, Tint.G, Tint.B, 1.f);
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.9f;
}

void ATN_PathStorm::RestoreFog()
{
	if (!bFogApplied) { return; }
	bFogApplied = false;
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		F->SetFogDensity(FogDensity0);
		F->SetFogHeightFalloff(FogFalloff0);
		F->SetStartDistance(FogStart0);
		F->SetFogMaxOpacity(FogOpacity0);
		F->SetFogInscatteringColor(FogColor0);
	}
}

void ATN_PathStorm::ServerCheckPlayers(float Interval)
{
	if (!Generator || !Generator->IsMapReady()) { return; }

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) { continue; }
		ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>();
		ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());
		if (!PS || !Turtle || !PS->bIsAlive || PS->bHasFinishedRun || Turtle->IsDead())
		{
			InsideTime.Remove(PC);
			continue;
		}

		// Dentro de un búnker (#689) la tormenta no cuenta: es refugio.
		if (IsLocationInside(Turtle->GetActorLocation()) && !ATN_BeachShelterVolume::IsSheltered(Turtle))
		{
			float& T = InsideTime.FindOrAdd(PC);
			T += Interval;
			PS->DeathZoneTimeRemaining = FMath::Max(0.f, SecondsInsideToDie - T);
			if (T >= SecondsInsideToDie)
			{
				InsideTime.Remove(PC);
				PS->DeathZoneTimeRemaining = -1.f;
				Turtle->RequestKill(this);
			}
		}
		else if (InsideTime.Remove(PC) > 0)
		{
			PS->DeathZoneTimeRemaining = -1.f;
		}
	}
}

void ATN_PathStorm::ForceCheckPlayer(APlayerController* PC)
{
	if (PC) { InsideTime.Remove(PC); }
}
