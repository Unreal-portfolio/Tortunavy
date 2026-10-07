#include "World/Beach/TN_BeachQuadLane.h"
#include "World/TN_HazardEffects.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemySynth.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachQuad
{
#if !UE_BUILD_SHIPPING
	/** Registro de la posición del quad en cada fotograma (desfase entre anfitrión y cliente; Docs/Comandos_Prueba.md). */
	TAutoConsoleVariable<int32> CVarQuadTrace(TEXT("TN.Beach.Quad.Trace"), 0,
		TEXT("1: registra en cada fotograma de una pasada la hora del reloj de trampa, la del servidor sin suavizar y la X del quad."));
#endif

	/** Aviso antes de salir (s): temblor creciente, motor y humo entre las palmeras. */
	constexpr float WarnTime = 3.5f;
	/** Tiempo entre pasadas (s, sin contar el aviso) y hasta la primera. */
	constexpr float IntervalMin = 12.f;
	constexpr float IntervalMax = 20.f;
	constexpr float FirstMin = 5.f;
	constexpr float FirstMax = 14.f;
	/**
	 * Atropello (el derribo, en UTN_CombatTuning): espera antes de poder volver a golpear a la misma y lanzamiento del
	 * ragdoll (cm/s: en el sentido del quad, hacia fuera de la rueda y hacia arriba) dando vueltas (grados/s). Moderado
	 * para que el ragdoll no atraviese la arena al caer.
	 */
	constexpr float HitCooldown = 1.2f;
	constexpr float LaunchForward = 950.f;
	constexpr float LaunchSide = 380.f;
	constexpr float LaunchUp = 750.f;
	constexpr float LaunchSpin = 420.f;
	/**
	 * Rodadas en la arena: paso a lo largo (cm; la malla del terreno va cada 3 m), puntos a lo ancho de cada una, largo de cada
	 * banda clara u oscura (cm, los tacos del neumático) y lo que se levantan sobre la arena (cm).
	 */
	constexpr double RutStep = 125.0;
	constexpr int32 RutAcross = 5;
	constexpr double RutBand = 250.0;
	constexpr double RutLift = 5.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// FTNQuadPass
// ─────────────────────────────────────────────────────────────────────────────

float FTNQuadPass::TravelSeconds() const
{
	return 2.f * (HalfLength + QuadHalfLen + PalmMargin) / Speed;
}

bool FTNQuadPass::QuadXAt(double Now, float& OutX) const
{
	if (PassTime < 0.0)
	{
		return false;
	}
	// La resta en double: la hora del servidor crece sin parar y en float perdería milésimas (y centímetros del quad).
	const double T = Now - PassTime;
	if (T < 0.0 || T > static_cast<double>(TravelSeconds()))
	{
		return false;
	}
	const double Sign = Dir >= 0 ? 1.0 : -1.0;
	const double StartX = -Sign * (static_cast<double>(HalfLength) + QuadHalfLen + PalmMargin);
	OutX = static_cast<float>(StartX + Sign * Speed * T);
	return true;
}

double FTNQuadPass::HitEvalTime(double Now, bool bLocallyControlled, float PingMs)
{
	if (bLocallyControlled)
	{
		return Now;
	}
	return Now - FMath::Clamp(static_cast<double>(PingMs) * 0.001, 0.0, MaxLagCompensation);
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachQuadLane
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachQuadLane::ATN_BeachQuadLane()
{
	bUsesMover = false;
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);
}

void ATN_BeachQuadLane::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachQuadLane, PassTime);
	DOREPLIFETIME(ATN_BeachQuadLane, PassDir);
}

void ATN_BeachQuadLane::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.7f, 1.4f);
	HalfLength = 0.5f * (Spec.Extent > 100.f ? Spec.Extent : static_cast<float>(TNBeach::CourseWidth));
	QuadHalfLen = 0.5f * static_cast<float>(TNBeachMeshes::QuadLength * TNBeach::Scale) * SizeK;
	BuildQuad();
}

void ATN_BeachQuadLane::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SchedulePass(ServerNow(this), ServerRng.FRandRange(TNBeachQuad::FirstMin, TNBeachQuad::FirstMax));
	}
}

void ATN_BeachQuadLane::BuildQuad()
{
	if (QuadRoot || !bHasScreen)
	{
		return;
	}
	const int32 Pal = ((Spec.Seed % 5) + 5) % 5;
	const TNBeachMeshes::FQuadLook Look = TNBeachMeshes::QuadPalette(Pal);
	UStaticMesh* BodyMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("Beach.Quad.%d.Body"), Pal), [&Look](TNProcMesh::FTNProcMeshBuffers& M) { TNBeachMeshes::BuildQuadBody(M, Look); });
	UStaticMesh* WheelMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("Beach.Quad.%d.Wheel"), Pal), [&Look](TNProcMesh::FTNProcMeshBuffers& M) { TNBeachMeshes::BuildQuadWheel(M, Look); });

	QuadRoot = NewObject<USceneComponent>(this, TEXT("QuadRoot"));
	QuadRoot->SetupAttachment(GetRootComponent());
	QuadRoot->SetAbsolute(true, true, true);
	QuadRoot->RegisterComponent();
	QuadBody = TNBeachKit::AddPart(this, QuadRoot, BodyMesh, FVector::ZeroVector, true, TN_ART("Beach.QuadLane.Quad"));
	const double S = TNBeach::Scale;
	for (int32 i = 0; i < 4; ++i)
	{
		const double X = (i < 2 ? 1.0 : -1.0) * TNBeachMeshes::QuadBaseHalf * S;
		const double Y = (i % 2 == 0 ? -1.0 : 1.0) * TNBeachMeshes::QuadTrackHalf * S;
		Wheels.Add(TNBeachKit::AddPart(this, QuadRoot, WheelMesh, FVector(X, Y, TNBeachMeshes::QuadWheelR * S), true, TN_ART("Beach.QuadLane.Wheel")));
	}
	QuadRoot->SetVisibility(false, true);

	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc SmokeDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.55f, 0.5f, 0.44f), true, 0.6f, 60, 14.f, 600.f, 0.f, 2.5f, 4.5f, 800.f, 2200.f);
	SmokeDesc.Buoyancy = 120.f;
	SmokeDesc.SpawnRadius = 1500.f;
	SmokeDesc.SpawnHeight = 900.f;
	SmokeDesc.Drag = 0.5f;
	TNBeachKit::InitEmitter(Smoke, this, SmokeDesc, static_cast<uint32>(Spec.Seed) + 41u);
	TNAmbientFX::FEmitterDesc LeafDesc = TNBeachKit::MakeDesc(EShape::Leaf, FLinearColor(0.2f, 0.5f, 0.15f), false, 1.f, 50, 10.f, 1400.f, -500.f, 1.5f, 3.f, 260.f, 200.f);
	LeafDesc.SpawnRadius = 1800.f;
	LeafDesc.SpawnHeight = 1500.f;
	LeafDesc.Spread = 0.8f;
	TNBeachKit::InitEmitter(Leaves, this, LeafDesc, static_cast<uint32>(Spec.Seed) + 42u);
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.85f, 0.76f, 0.56f), true, 0.5f, 70, 30.f, 500.f, 0.f, 1.2f, 2.4f, 400.f, 1100.f);
	DustDesc.Buoyancy = 50.f;
	DustDesc.SpawnRadius = 900.f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, static_cast<uint32>(Spec.Seed) + 43u);
	GetVoice(GetRootComponent(), 3000.f, 26000.f);
}

bool ATN_BeachQuadLane::BuildRuts()
{
	// Dos rodadas oscuras de arena apisonada por donde van las ruedas, pegadas a la arena: avisan de dónde pasa el quad.
	// La altura sale de una traza contra el suelo. Cada tira se apoya en la arena a lo ancho (RutAcross puntos) y a lo largo
	// cada RutStep: una tira plana de 6 m con tramos de 5 m flotaba en las hondonadas y en las cuestas de lado.
	const FTransform LaneXf = GetActorTransform();
	const double S = TNBeach::Scale;
	const double TrackY = TNBeachMeshes::QuadTrackHalf * S * SizeK;
	const double HalfW = TNBeachMeshes::QuadWheelHalfW * S * SizeK * 0.9;
	const int32 N = FMath::Clamp(FMath::CeilToInt32(2.0 * HalfLength / TNBeachQuad::RutStep), 4, 600);
	constexpr int32 Across = TNBeachQuad::RutAcross;
	const auto LocalYAt = [TrackY, HalfW](int32 Side, int32 k)
	{
		return (Side == 0 ? -1.0 : 1.0) * TrackY + FMath::Lerp(-HalfW, HalfW, static_cast<double>(k) / static_cast<double>(Across - 1));
	};
	const auto LocalXAt = [this, N](int32 j)
	{
		return -static_cast<double>(HalfLength) + 2.0 * static_cast<double>(HalfLength) * static_cast<double>(j) / static_cast<double>(N);
	};
	TArray<double> LocalZ;
	LocalZ.SetNumZeroed(2 * (N + 1) * Across);
	const auto ZIndex = [N](int32 Side, int32 j, int32 k) { return (Side * (N + 1) + j) * Across + k; };
	int32 Hits = 0;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 j = 0; j <= N; ++j)
		{
			for (int32 k = 0; k < Across; ++k)
			{
				const FVector OnGround = LaneXf.TransformPosition(FVector(LocalXAt(j), LocalYAt(Side, k), 0.0));
				float Z = static_cast<float>(OnGround.Z);
				if (TraceGround(this, OnGround, Z))
				{
					++Hits;
				}
				LocalZ[ZIndex(Side, j, k)] = LaneXf.InverseTransformPosition(FVector(OnGround.X, OnGround.Y, Z)).Z + TNBeachQuad::RutLift;
			}
		}
	}
	if (Hits < 2 * (N + 1) * Across * 6 / 10 && RutsTries < 8)
	{
		// El suelo aún no está (o el paso cae fuera): se reintenta.
		return false;
	}
	TNProcMesh::FTNProcMeshBuffers B;
	const FLinearColor Light(0.7f, 0.6f, 0.43f);
	const FLinearColor Dark(0.58f, 0.48f, 0.33f);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 j = 0; j < N; ++j)
		{
			const double X0 = LocalXAt(j);
			const double X1 = LocalXAt(j + 1);
			// Bandas claras y oscuras a lo largo (tacos del neumático).
			const int32 Band = FMath::FloorToInt32((0.5 * (X0 + X1) + static_cast<double>(HalfLength)) / TNBeachQuad::RutBand);
			const FLinearColor& Color = Band % 2 == 0 ? Dark : Light;
			for (int32 k = 0; k + 1 < Across; ++k)
			{
				const double Y0 = LocalYAt(Side, k);
				const double Y1 = LocalYAt(Side, k + 1);
				B.AddQuad(FVector(X0, Y0, LocalZ[ZIndex(Side, j, k)]), FVector(X0, Y1, LocalZ[ZIndex(Side, j, k + 1)]),
					FVector(X1, Y1, LocalZ[ZIndex(Side, j + 1, k + 1)]), FVector(X1, Y0, LocalZ[ZIndex(Side, j + 1, k)]), FVector::UpVector, Color);
			}
		}
	}
	RutsMesh = TNProcRuntimeMesh::MakeStaticMesh(this, B, TNBeachKit::SolidMaterial());
	Ruts = TNBeachKit::AddPart(this, GetRootComponent(), RutsMesh, FVector::ZeroVector, false);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pasadas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachQuadLane::SchedulePass(double Now, float Delay)
{
	PassTime = Now + Delay + TNBeachQuad::WarnTime;
	PassDir = static_cast<int8>(ServerRng.FRand() < 0.5f ? 1 : -1);
	LastHit.Reset();
	ForceNetUpdate();
}

void ATN_BeachQuadLane::DebugPassNow()
{
	if (!HasAuthority())
	{
		return;
	}
	SchedulePass(ServerNow(this), 0.f);
}

FTNQuadPass ATN_BeachQuadLane::CurrentPass() const
{
	FTNQuadPass Pass;
	Pass.PassTime = PassTime;
	Pass.Dir = PassDir;
	Pass.HalfLength = HalfLength;
	Pass.QuadHalfLen = QuadHalfLen;
	return Pass;
}

FVector ATN_BeachQuadLane::WheelLocal(int32 Index, float QuadX) const
{
	const double S = TNBeach::Scale * SizeK;
	const double Qx = (Index < 2 ? 1.0 : -1.0) * TNBeachMeshes::QuadBaseHalf * S;
	const double Qy = (Index % 2 == 0 ? -1.0 : 1.0) * TNBeachMeshes::QuadTrackHalf * S;
	const double Dir = PassDir >= 0 ? 1.0 : -1.0;
	// El quad mira hacia donde va (+X del paso, o -X girado 180°).
	return FVector(QuadX + Dir * Qx, Dir * Qy, 0.0);
}

void ATN_BeachQuadLane::ServerTick(float DeltaSeconds)
{
	const double Now = ServerNow(this);
	if (PassTime < 0.0)
	{
		SchedulePass(Now, TNBeachQuad::FirstMin);
		return;
	}
	if (Now - PassTime > TravelSeconds())
	{
		SchedulePass(Now, ServerRng.FRandRange(TNBeachQuad::IntervalMin, TNBeachQuad::IntervalMax));
		return;
	}
	const FTNQuadPass Pass = CurrentPass();
	float QuadX = 0.f;
	if (!Pass.QuadXAt(Now, QuadX))
	{
		if (IsDebugDraw())
		{
			DrawDebugBox(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, 200.0), FVector(HalfLength, GetFootprintRadius(), 200.0),
				GetActorQuat(), FColor::Orange, false, -1.f, 0, 12.f);
		}
		return;
	}
	const FTransform LaneXf = GetActorTransform();
	const float Dir = PassDir >= 0 ? 1.f : -1.f;
	const double S = TNBeach::Scale * SizeK;
	const double HalfW = TNBeachMeshes::QuadWheelHalfW * S + 45.0;
	const double Contact = TNBeachMeshes::QuadWheelR * S * 0.55 + 45.0;
	const double Height = TNBeachMeshes::QuadWheelR * 2.0 * S;
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	const double WorldNow = GetWorld()->GetTimeSeconds();
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!CanBeHit(Turtle))
		{
			continue;
		}
		const FVector L = LaneXf.InverseTransformPosition(Turtle->GetActorLocation());
		if (FMath::Abs(L.Z) > Height + 1500.0)
		{
			continue;
		}
		if (const double* Last = LastHit.Find(Turtle))
		{
			if (WorldNow - *Last < TNBeachQuad::HitCooldown)
			{
				continue;
			}
		}
		// La tortuga de un cliente se juzga contra el quad que ese cliente veía (un ping antes): esquivar en su pantalla
		// es esquivar de verdad.
		const APlayerState* State = Turtle->GetPlayerState();
		const double Eval = FTNQuadPass::HitEvalTime(Now, Turtle->IsLocallyControlled(), State ? State->GetPingInMilliseconds() : 0.f);
		float SeenX = QuadX;
		if (Eval != Now && !Pass.QuadXAt(Eval, SeenX))
		{
			continue;
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector W = WheelLocal(i, SeenX);
			if (FMath::Abs(L.Y - W.Y) > HalfW || FMath::Abs(L.X - W.X) > Contact)
			{
				continue;
			}
			LastHit.Add(Turtle, WorldNow);
			const FVector Travel = LaneXf.TransformVectorNoScale(FVector(Dir, 0.0, 0.0));
			const FVector Out = LaneXf.TransformVectorNoScale(FVector(0.0, L.Y >= W.Y ? 1.0 : -1.0, 0.0));
			// Atropello: sale lanzada en ragdoll por delante de la rueda, dando vueltas de campana.
			const FVector Push = Travel * TNBeachQuad::LaunchForward + Out * TNBeachQuad::LaunchSide + FVector(0.0, 0.0, TNBeachQuad::LaunchUp);
			const FVector Spin = FVector::CrossProduct(FVector::UpVector, Travel) * TNBeachQuad::LaunchSpin;
			KnockDownTurtle(Turtle, UTN_CombatTuning::Get().QuadLaneKnockSeconds, Push, Spin);
			// Quad: muerte instantánea de la hoja (#871), tras salir lanzada.
			TNHazard::Apply(UTN_HazardTuning::Get().Quad, Turtle, this);
			MulticastRunOver(Turtle);
			break;
		}
	}
	if (IsDebugDraw())
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector W = LaneXf.TransformPosition(WheelLocal(i, QuadX) + FVector(0.0, 0.0, Height * 0.5));
			DrawDebugBox(GetWorld(), W, FVector(Contact, HalfW, Height * 0.5), LaneXf.GetRotation(), FColor::Red, false, -1.f, 0, 10.f);
		}
	}
}

void ATN_BeachQuadLane::MulticastRunOver_Implementation(ATortugaCharacter* Victim)
{
	if (!bHasScreen || !Victim)
	{
		return;
	}
	const FVector At = Victim->GetActorLocation();
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Slam, 0.75f, 1.3f);
	}
	TNBeachKit::BurstAt(Dust, At, FVector::UpVector, 10);
	UTN_BeachCameraShake::Kick(this, At, 0.9f, 800.f, 4000.f);
	ShowPop(NSLOCTEXT("TNBeach", "QuadRunOver", "¡ATROPELLO!"), FColor(255, 140, 40), At + FVector(0.0, 0.0, 300.0), 160.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachQuadLane::VisualTick(float DeltaSeconds)
{
	if (!Ruts && RutsTries < 9)
	{
		RutsRetry -= DeltaSeconds;
		if (RutsRetry <= 0.f)
		{
			RutsRetry = 2.f;
			++RutsTries;
			BuildRuts();
		}
	}
	if (!QuadRoot)
	{
		return;
	}
	// Reloj de trampa compartido: la hora del servidor suavizada, sin saltos cuando el GameState la corrige.
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	const float Dir = PassDir >= 0 ? 1.f : -1.f;
	const FTransform LaneXf = GetActorTransform();
	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);
	if (!FMath::IsNearlyEqual(ShownPass, PassTime))
	{
		ShownPass = PassTime;
		bCrashIn = false;
		bCrashOut = false;
		GroundTimer = 0.f;
	}
	const float T = static_cast<float>(Now - PassTime);
	const FVector ViewLocal = LaneXf.InverseTransformPosition(View);
	const FVector Nearest = LaneXf.TransformPosition(FVector(FMath::Clamp(ViewLocal.X, -static_cast<double>(HalfLength), static_cast<double>(HalfLength)), 0.0, 0.0));
	const FVector StartEdge = LaneXf.TransformPosition(FVector(-Dir * (HalfLength + 1200.f), 0.0, 800.0));
	const FVector ExitEdge = LaneXf.TransformPosition(FVector(Dir * (HalfLength + 1200.f), 0.0, 800.0));
	const FVector Travel = LaneXf.TransformVectorNoScale(FVector(Dir, 0.0, 0.0));
	float Engine = 0.f;
	float Rpm = 0.f;

	// Aviso: temblor creciente, motor que se acerca, humo y hojas entre las palmeras de salida.
	if (T > -TNBeachQuad::WarnTime && T < 0.f)
	{
		const float W = (T + TNBeachQuad::WarnTime) / TNBeachQuad::WarnTime;
		UTN_BeachCameraShake::Rumble(this, Nearest, 0.12f + 0.45f * W * W, 1500.f, 9000.f);
		Smoke.Origin = StartEdge;
		Smoke.Desc.Direction = (Travel + FVector(0.0, 0.0, 0.35)).GetSafeNormal();
		Smoke.RateScale = 0.4f + 1.4f * W;
		Leaves.Origin = StartEdge + FVector(0.0, 0.0, 1500.0);
		Leaves.Desc.Direction = (Travel + FVector(0.0, 0.0, 0.5)).GetSafeNormal();
		Leaves.RateScale = W;
		Engine = 0.25f + 0.75f * W;
		Rpm = 0.3f + 0.5f * W;
		if (Voice)
		{
			Voice->SetWorldLocation(StartEdge);
		}
	}
	else
	{
		Smoke.RateScale = 0.f;
		Leaves.RateScale = 0.f;
	}

	float QuadX = 0.f;
	const bool bPassing = QuadXAt(Now, QuadX);
#if !UE_BUILD_SHIPPING
	if (bPassing && TNBeachQuad::CVarQuadTrace.GetValueOnGameThread() != 0)
	{
		// Reloj de pared común a los procesos del mismo equipo: permite comparar anfitrión y cliente en el mismo instante.
		UE_LOG(LogTortunabo, Log, TEXT("[Quad] %s %s wall=%.4f clock=%.4f raw=%.4f pass=%.4f x=%.1f"), HasAuthority() ? TEXT("server") : TEXT("client"),
			*GetName(), FPlatformTime::Seconds(), Now, ServerNow(this), PassTime, QuadX);
	}
#endif
	if (bPassing)
	{
		// Suelo bajo cada rueda (diez veces por segundo): el quad se inclina con las dunas. Del generador, sin trazas: entre
		// las palmeras cruza los muros invisibles de los lados y una traza que empieza dentro de uno lo subiría 40 m.
		GroundTimer -= DeltaSeconds;
		if (GroundTimer <= 0.f)
		{
			GroundTimer = 0.1f;
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector W = LaneXf.TransformPosition(WheelLocal(i, QuadX));
				float Z = static_cast<float>(W.Z);
				GroundHeightAt(W, Z);
				WheelGround[i] = Z;
			}
		}
		const double S = TNBeach::Scale * SizeK;
		const float Front = 0.5f * (WheelGround[0] + WheelGround[1]);
		const float Rear = 0.5f * (WheelGround[2] + WheelGround[3]);
		const float Left = 0.5f * (WheelGround[0] + WheelGround[2]);
		const float Right = 0.5f * (WheelGround[1] + WheelGround[3]);
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Front - Rear, static_cast<float>(2.0 * TNBeachMeshes::QuadBaseHalf * S)));
		const float Roll = FMath::RadiansToDegrees(FMath::Atan2(Left - Right, static_cast<float>(2.0 * TNBeachMeshes::QuadTrackHalf * S)));
		const float Bounce = 60.f * SizeK * FMath::Sin(T * 9.f) * FMath::Sin(T * 3.7f);
		FVector Center = LaneXf.TransformPosition(FVector(QuadX, 0.0, 0.0));
		Center.Z = 0.25f * (WheelGround[0] + WheelGround[1] + WheelGround[2] + WheelGround[3]);
		const float Yaw = static_cast<float>(LaneXf.Rotator().Yaw) + (Dir > 0.f ? 0.f : 180.f);
		QuadRoot->SetWorldTransform(FTransform(FRotator(Pitch, Yaw, Roll), Center, FVector(SizeK)));
		if (!QuadRoot->IsVisible())
		{
			QuadRoot->SetVisibility(true, true);
		}
		// Ruedas que giran con lo recorrido y un poco de suspensión.
		WheelSpin = FMath::Fmod(WheelSpin + FTNQuadPass::Speed * DeltaSeconds / static_cast<float>(TNBeachMeshes::QuadWheelR * S) * (180.f / PI), 360.f);
		for (int32 i = 0; i < Wheels.Num(); ++i)
		{
			const double X = (i < 2 ? 1.0 : -1.0) * TNBeachMeshes::QuadBaseHalf * TNBeach::Scale;
			const double Y = (i % 2 == 0 ? -1.0 : 1.0) * TNBeachMeshes::QuadTrackHalf * TNBeach::Scale;
			const double Suspension = 40.0 * FMath::Sin(T * 11.f + i * 1.7f);
			TNBeachKit::Pose(Wheels[i], FVector(X, Y, TNBeachMeshes::QuadWheelR * TNBeach::Scale - Bounce / SizeK + Suspension), FRotator(-WheelSpin, 0.f, 0.f));
		}
		TNBeachKit::Pose(QuadBody, FVector(0.0, 0.0, 0.0), FRotator(0.f, 0.f, 0.f));
		UTN_BeachCameraShake::Rumble(this, Center, 0.85f, 2500.f, 11000.f);
		Engine = 1.f;
		Rpm = 0.85f + 0.1f * FMath::Sin(T * 2.f);
		if (Voice)
		{
			Voice->SetWorldLocation(Center + FVector(0.0, 0.0, 800.0));
		}
		// Arena que levantan las ruedas de atrás.
		Dust.Origin = LaneXf.TransformPosition(FVector(QuadX - Dir * TNBeachMeshes::QuadBaseHalf * S, 0.0, 0.0)) + FVector(0.0, 0.0, 300.0);
		Dust.Desc.Direction = (-Travel + FVector(0.0, 0.0, 0.5)).GetSafeNormal();
		Dust.RateScale = 1.f;
		// Revienta las palmeras al salir y al volver a meterse.
		const float Along = QuadX * Dir;
		if (!bCrashIn && Along > -(HalfLength + QuadHalfLen))
		{
			bCrashIn = true;
			TNBeachKit::BurstAt(Leaves, StartEdge + FVector(0.0, 0.0, 1200.0), (Travel + FVector(0.0, 0.0, 0.6)).GetSafeNormal(), 30);
			if (Voice)
			{
				Voice->Play(ETNBeachSfx::Crunch, 0.8f, 1.3f);
			}
		}
		if (!bCrashOut && Along + QuadHalfLen > HalfLength + 300.f)
		{
			bCrashOut = true;
			TNBeachKit::BurstAt(Leaves, ExitEdge + FVector(0.0, 0.0, 1200.0), (Travel + FVector(0.0, 0.0, 0.6)).GetSafeNormal(), 30);
			TNBeachKit::BurstAt(Smoke, ExitEdge, (Travel + FVector(0.0, 0.0, 0.3)).GetSafeNormal(), 10);
			if (Voice)
			{
				Voice->Play(ETNBeachSfx::Crunch, 0.75f, 1.3f);
			}
		}
	}
	else
	{
		if (QuadRoot->IsVisible())
		{
			QuadRoot->SetVisibility(false, true);
		}
		Dust.RateScale = 0.f;
	}
	if (Voice)
	{
		Voice->SetEngine(Engine, Rpm);
	}
	TNBeachKit::TickEmitterIfBusy(Smoke, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Leaves, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);
}
