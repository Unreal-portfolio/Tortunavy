#include "Player/TN_ShellImpactFXComponent.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Audio/TN_ShellImpactSynth.h"
#include "Core/TN_Log.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_TurtleSurface.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "TimerManager.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNShellImpactFX
{
	constexpr int32 NumSounds = static_cast<int32>(ETNShellImpactSound::Count);

	int32 GEnabled = 1;
	FAutoConsoleVariableRef CVarEnabled(
		TEXT("TN.Shell.Impact"),
		GEnabled,
		TEXT("Golpes de la bola del caparazón (sonido y efecto según contra qué choca): 1 = activados (por defecto), 0 = apagados."));

	int32 GDebug = 0;
	FAutoConsoleVariableRef CVarDebug(
		TEXT("TN.Shell.Impact.Debug"),
		GDebug,
		TEXT("1 = escribe en el registro cada golpe de la bola del caparazón: velocidad, fuerza, superficie y si ha sonado."));

	float GVolume = 1.f;
	FAutoConsoleVariableRef CVarVolume(
		TEXT("TN.Shell.Impact.Volume"),
		GVolume,
		TEXT("Volumen de los golpes de la bola del caparazón (1 por defecto, 0 = mudos)."));

	float GMinSpeed = 0.f;
	FAutoConsoleVariableRef CVarMinSpeed(
		TEXT("TN.Shell.Impact.MinSpeed"),
		GMinSpeed,
		TEXT("Velocidad de impacto mínima (cm/s) para que la bola del caparazón suene y se vea; 0 = la del componente (260)."));

	/** Un emisor de partículas: la nube o los trocitos de un timbre. */
	struct FSlot
	{
		TNAmbientFX::FEmitter Emitter;
		bool bMade = false;
		/** Quedan partículas vivas: hay que seguir moviéndolas. */
		bool bAlive = false;
	};

	struct FState
	{
		/** Dos emisores por timbre: [2 * timbre] la nube y [2 * timbre + 1] los trocitos. */
		FSlot Slots[NumSounds * 2];
		TNTurtleSurface::FNameCache NameCache;
		/** Velocidad de la caja al empezar el fotograma (antes de la física de este fotograma) y si estaba en el agua. */
		FVector PrevVelocity = FVector::ZeroVector;
		bool bWasInWater = false;
		/** Volúmenes de agua del mundo (se buscan al engancharse a una caja; si no hay ninguno, se vuelve a mirar cada 2 s). */
		TArray<TWeakObjectPtr<APhysicsVolume>> WaterVolumes;
		double NextWaterRefresh = 0.0;
		/** Último golpe de esta bola. */
		double LastHitTime = -10.0;
		float LastStrength = 0.f;
		TWeakObjectPtr<UTN_ShellImpactSynthComponent> Synth;
		/** Semilla del azar de los tonos y de las partículas de esta bola. */
		uint32 Rng = 0x2F6E2B1u;
	};

	/** Golpes recientes de todas las bolas del mundo (hora de la plataforma): tope de sonidos por segundo. */
	constexpr int32 BudgetRing = 12;
	double GRecentHits[BudgetRing] = {};
	int32 GRecentHead = 0;

	/** true si el mundo aún admite otro golpe (como mucho 8 por segundo; pasado ese número, solo los fuertes, hasta 12). */
	bool BudgetAllows(float InStrength)
	{
		const double Now = FPlatformTime::Seconds();
		int32 Recent = 0;
		for (const double Time : GRecentHits)
		{
			if (Now - Time < 1.0) { ++Recent; }
		}
		if (Recent >= BudgetRing || (Recent >= 8 && InStrength < 0.7f))
		{
			return false;
		}
		GRecentHits[GRecentHead] = Now;
		GRecentHead = (GRecentHead + 1) % BudgetRing;
		return true;
	}

	float HitSmoothStep(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	float NextUnit(uint32& InOutState)
	{
		return TNAmbientFX::Rand01(InOutState);
	}

	const TCHAR* SoundName(ETNShellImpactSound InSound)
	{
		switch (InSound)
		{
		case ETNShellImpactSound::Sand: return TEXT("arena");
		case ETNShellImpactSound::Rock: return TEXT("roca");
		case ETNShellImpactSound::Wood: return TEXT("madera");
		case ETNShellImpactSound::Water: return TEXT("agua");
		case ETNShellImpactSound::Turtle: return TEXT("tortuga");
		case ETNShellImpactSound::Enemy: return TEXT("enemigo");
		default: return TEXT("trasto");
		}
	}

	bool ParseSound(const FString& InName, ETNShellImpactSound& OutSound)
	{
		struct FSoundName
		{
			const TCHAR* Spanish;
			const TCHAR* English;
			ETNShellImpactSound Sound;
		};
		static const FSoundName Names[] = {
			{ TEXT("arena"), TEXT("sand"), ETNShellImpactSound::Sand },
			{ TEXT("roca"), TEXT("rock"), ETNShellImpactSound::Rock },
			{ TEXT("madera"), TEXT("wood"), ETNShellImpactSound::Wood },
			{ TEXT("agua"), TEXT("water"), ETNShellImpactSound::Water },
			{ TEXT("tortuga"), TEXT("turtle"), ETNShellImpactSound::Turtle },
			{ TEXT("enemigo"), TEXT("enemy"), ETNShellImpactSound::Enemy },
			{ TEXT("trasto"), TEXT("junk"), ETNShellImpactSound::Junk },
			{ TEXT("trastos"), TEXT("junk"), ETNShellImpactSound::Junk },
		};
		for (const FSoundName& Candidate : Names)
		{
			if (InName.Equals(Candidate.Spanish, ESearchCase::IgnoreCase) || InName.Equals(Candidate.English, ESearchCase::IgnoreCase))
			{
				OutSound = Candidate.Sound;
				return true;
			}
		}
		return false;
	}

	// ── Partículas ────────────────────────────────────────────────────────────

	/** Descripción del emisor de un timbre: Layer 0 = nube, 1 = trocitos. Solo estallidos (sin goteo). */
	TNAmbientFX::FEmitterDesc MakeSlotDesc(ETNShellImpactSound InSound, int32 InLayer)
	{
		using namespace TNAmbientFX;
		FEmitterDesc D;
		D.Rate = 0.f;
		D.WakeDistance = 7000.f;
		D.Direction = FVector::UpVector;
		if (InLayer == 0)
		{
			// Nube blanda del color de lo que se golpea.
			D.Shape = EShape::Puff;
			D.bSoft = true;
			D.bCloud = true;
			D.Alpha = 0.5f;
			D.MaxParticles = 14;
			D.SpawnRadius = 16.f;
			D.SpawnHeight = 4.f;
			D.Speed = 150.f;
			D.SpeedJitter = 0.45f;
			D.Spread = 1.f;
			D.Gravity = -30.f;
			D.Drag = 2.4f;
			D.LifeMin = 0.4f;
			D.LifeMax = 0.8f;
			D.SizeStart = 12.f;
			D.SizeEnd = 44.f;
			switch (InSound)
			{
			case ETNShellImpactSound::Sand:
				D.Color = FLinearColor(0.92f, 0.84f, 0.66f);
				D.Alpha = 0.55f;
				break;
			case ETNShellImpactSound::Rock:
				D.Color = FLinearColor(0.68f, 0.67f, 0.64f);
				D.Alpha = 0.42f;
				D.SizeEnd = 36.f;
				break;
			case ETNShellImpactSound::Wood:
				D.Color = FLinearColor(0.82f, 0.72f, 0.56f);
				D.Alpha = 0.32f;
				D.SizeEnd = 30.f;
				break;
			case ETNShellImpactSound::Water:
				// Espuma: sube y cae enseguida.
				D.Color = FLinearColor(0.9f, 0.96f, 1.f);
				D.Alpha = 0.55f;
				D.Speed = 120.f;
				D.Gravity = -60.f;
				D.LifeMin = 0.35f;
				D.LifeMax = 0.65f;
				D.SizeStart = 10.f;
				D.SizeEnd = 46.f;
				break;
			case ETNShellImpactSound::Turtle:
				D.Color = FLinearColor(0.96f, 0.94f, 0.86f);
				D.Alpha = 0.38f;
				D.SizeEnd = 34.f;
				break;
			case ETNShellImpactSound::Enemy:
				D.Color = FLinearColor(1.f, 0.86f, 0.5f);
				D.Alpha = 0.4f;
				D.SizeEnd = 36.f;
				break;
			default:
				D.Color = FLinearColor(0.95f, 0.9f, 0.85f);
				D.Alpha = 0.32f;
				D.SizeEnd = 32.f;
				break;
			}
			return D;
		}

		// Trocitos: granos, esquirlas, astillas, gotas, chispas o confeti.
		D.Shape = EShape::Ember;
		D.bSoft = false;
		D.MaxParticles = 24;
		D.SpawnRadius = 12.f;
		D.SpawnHeight = 4.f;
		D.Speed = 300.f;
		D.SpeedJitter = 0.5f;
		D.Spread = 0.9f;
		D.Gravity = -980.f;
		D.Drag = 0.5f;
		D.LifeMin = 0.35f;
		D.LifeMax = 0.6f;
		D.SizeStart = 3.f;
		D.SizeEnd = 2.f;
		switch (InSound)
		{
		case ETNShellImpactSound::Sand:
			D.Color = FLinearColor(0.8f, 0.68f, 0.46f);
			D.Speed = 290.f;
			D.SizeStart = 2.6f;
			D.SizeEnd = 1.8f;
			break;
		case ETNShellImpactSound::Rock:
			D.Color = FLinearColor(0.5f, 0.49f, 0.47f);
			D.Speed = 400.f;
			D.SizeStart = 3.6f;
			D.SizeEnd = 2.4f;
			break;
		case ETNShellImpactSound::Wood:
			// Astillas: palitos que vuelan girando a lo largo de su vuelo.
			D.Shape = EShape::Streak;
			D.Color = FLinearColor(0.58f, 0.38f, 0.18f);
			D.MaxParticles = 16;
			D.Speed = 320.f;
			D.SizeStart = 10.f;
			D.SizeEnd = 8.f;
			D.LifeMin = 0.45f;
			D.LifeMax = 0.75f;
			break;
		case ETNShellImpactSound::Water:
			// Gotas translúcidas estiradas en la dirección en que vuelan.
			D.Shape = EShape::Drop;
			D.bSoft = true;
			D.Color = FLinearColor(0.8f, 0.92f, 1.f);
			D.Alpha = 0.7f;
			D.MaxParticles = 28;
			D.Speed = 400.f;
			D.Spread = 0.7f;
			D.Gravity = -900.f;
			D.Drag = 0.3f;
			D.SizeStart = 6.f;
			D.SizeEnd = 4.f;
			D.LifeMin = 0.4f;
			D.LifeMax = 0.7f;
			break;
		case ETNShellImpactSound::Turtle:
			// Chispas claras de golpe de caparazón.
			D.Color = FLinearColor(1.f, 0.92f, 0.55f);
			D.MaxParticles = 18;
			D.Speed = 380.f;
			D.Spread = 1.4f;
			D.Gravity = -500.f;
			D.SizeStart = 4.5f;
			D.SizeEnd = 2.f;
			D.LifeMin = 0.25f;
			D.LifeMax = 0.45f;
			break;
		case ETNShellImpactSound::Enemy:
			D.Color = FLinearColor(1.f, 0.62f, 0.22f);
			D.MaxParticles = 18;
			D.Speed = 380.f;
			D.Spread = 1.4f;
			D.Gravity = -500.f;
			D.SizeStart = 4.5f;
			D.SizeEnd = 2.f;
			D.LifeMin = 0.25f;
			D.LifeMax = 0.45f;
			break;
		default:
			// Confeti de plástico.
			D.Shape = EShape::Flake;
			D.Color = FLinearColor(1.f, 0.55f, 0.72f);
			D.MaxParticles = 14;
			D.Speed = 280.f;
			D.Gravity = -640.f;
			D.SizeStart = 9.f;
			D.SizeEnd = 7.f;
			D.LifeMin = 0.5f;
			D.LifeMax = 0.8f;
			break;
		}
		return D;
	}

	/** El emisor de ese timbre y capa; si no existe, se crea (la primera vez que hace falta). Null si no se puede. */
	FSlot* EnsureSlot(AActor* Owner, FState& S, ETNShellImpactSound InSound, int32 InLayer)
	{
		FSlot& Slot = S.Slots[static_cast<int32>(InSound) * 2 + InLayer];
		if (!Slot.bMade)
		{
			TNAmbientFX::FEmitter& E = Slot.Emitter;
			E = TNAmbientFX::FEmitter();
			E.Desc = MakeSlotDesc(InSound, InLayer);
			E.Origin = Owner->GetActorLocation();
			E.RateScale = 0.f;
			E.Rng ^= ((static_cast<uint32>(InSound) * 40503u + static_cast<uint32>(InLayer) * 977u + 1u) * 2654435761u) | 1u;
			E.Particles.SetNum(E.Desc.MaxParticles);
			E.Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), E.Desc.MaxParticles);
			E.ISM = TNAmbientFX::MakeISM(Owner, TNAmbientFX::ShapeMesh(E.Desc.Shape, E.Desc.Color, E.Desc.bSoft, E.Desc.Alpha, E.Desc.bCloud),
				E.Desc.MaxParticles, false);
			Slot.bMade = E.ISM.IsValid();
			if (!Slot.bMade)
			{
				return nullptr;
			}
		}
		return &Slot;
	}

	/** Estallido de InCount partículas desde InOrigin hacia InDirection. */
	void BurstSlot(FSlot& Slot, const FVector& InOrigin, const FVector& InDirection, int32 InCount)
	{
		if (InCount <= 0 || !Slot.Emitter.ISM.IsValid())
		{
			return;
		}
		Slot.Emitter.Origin = InOrigin;
		Slot.Emitter.Desc.Direction = InDirection;
		TNAmbientFX::Burst(Slot.Emitter, InCount);
		Slot.bAlive = true;
	}

	bool AnyAlive(const TNAmbientFX::FEmitter& E)
	{
		for (const TNAmbientFX::FParticle& P : E.Particles)
		{
			if (P.bAlive) { return true; }
		}
		return false;
	}

	// ── Contra qué choca ──────────────────────────────────────────────────────

	bool NameHasAny(const FString& InText, std::initializer_list<const TCHAR*> InWords)
	{
		for (const TCHAR* Word : InWords)
		{
			if (InText.Contains(Word, ESearchCase::CaseSensitive)) { return true; }
		}
		return false;
	}

	/** Timbre de un golpe según el actor y el componente con los que choca la caja. */
	ETNShellImpactSound ClassifyContact(AActor* InOther, const UPrimitiveComponent* InOtherComp, const FHitResult& InHit, FState& S, UWorld* InWorld)
	{
		if (!InOther)
		{
			return ETNShellImpactSound::Rock;
		}
		// Otra tortuga (su bola o de pie) y los enemigos de la playa.
		if (InOther->IsA<ATN_ShellBody>() || InOther->IsA<ATortugaCharacter>())
		{
			return ETNShellImpactSound::Turtle;
		}
		if (InOther->IsA<ATN_BeachEnemy>())
		{
			return ETNShellImpactSound::Enemy;
		}

		FString ClassName = InOther->GetClass()->GetName();
		ClassName.ToLowerInline();
		FVector Normal = InHit.ImpactNormal.IsNearlyZero() ? FVector(InHit.Normal) : FVector(InHit.ImpactNormal);
		Normal = Normal.GetSafeNormal();

		// Plataformas tambaleantes: madera.
		if (NameHasAny(ClassName, { TEXT("wobbly") }))
		{
			return ETNShellImpactSound::Wood;
		}
		// El resto de trampas y el decorado (cubos, palas, sombrillas, flotadores...) y los objetos de carrera: plástico y lata.
		if (InOther->IsA<ATN_BeachElement>() || NameHasAny(ClassName, { TEXT("decor"), TEXT("raceitem"), TEXT("race_"), TEXT("frisbee"), TEXT("coconut") }))
		{
			return ETNShellImpactSound::Junk;
		}

		// Lo demás: la misma superficie que los pasos y el polvo.
		float Weights[TNTurtleSurface::Num];
		TNTurtleSurface::Resolve(&InHit, &S.NameCache, Weights);
		switch (TNTurtleSurface::Dominant(Weights))
		{
		case TNTurtleSurface::Rock: return ETNShellImpactSound::Rock;
		case TNTurtleSurface::Wood: return ETNShellImpactSound::Wood;
		case TNTurtleSurface::Water: return ETNShellImpactSound::Water;
		default: return ETNShellImpactSound::Sand;
		}
	}

	// ── Consola ───────────────────────────────────────────────────────────────

	void HandleTestCommand(const TArray<FString>& InArgs, UWorld* InWorld)
	{
		const APawn* Pawn = InWorld ? UGameplayStatics::GetPlayerPawn(InWorld, 0) : nullptr;
		UTN_ShellImpactFXComponent* Comp = Pawn ? Pawn->FindComponentByClass<UTN_ShellImpactFXComponent>() : nullptr;
		if (!Comp)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[ShellImpact] No hay tortuga local con golpes de caparazón (servidor dedicado, sin pantalla o sin tortuga)."));
			return;
		}
		const float Strength = InArgs.Num() >= 2 ? FMath::Clamp(FCString::Atof(*InArgs[1]), 0.f, 1.f) : 0.8f;
		ETNShellImpactSound Sound = ETNShellImpactSound::Sand;
		if (InArgs.Num() >= 1 && ParseSound(InArgs[0], Sound))
		{
			Comp->PlayTestImpact(Sound, Strength);
			return;
		}
		if (InArgs.Num() == 0 || InArgs[0].Equals(TEXT("todos"), ESearchCase::IgnoreCase) || InArgs[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			// Los siete, uno cada 0,9 s.
			for (int32 Index = 0; Index < NumSounds; ++Index)
			{
				const ETNShellImpactSound Which = static_cast<ETNShellImpactSound>(Index);
				FTimerHandle Handle;
				InWorld->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Comp, [Comp, Which, Strength]()
				{
					Comp->PlayTestImpact(Which, Strength);
				}), 0.05f + 0.9f * static_cast<float>(Index), false);
			}
			return;
		}
		UE_LOG(LogTortunabo, Display, TEXT("[ShellImpact] Uso: TN.Shell.Impact.Test <arena|roca|madera|agua|tortuga|enemigo|trasto|todos> [fuerza 0..1]"));
	}

	FAutoConsoleCommandWithWorldAndArgs TestCommand(
		TEXT("TN.Shell.Impact.Test"),
		TEXT("Hace sonar y ver un golpe de caparazón delante de tu tortuga: TN.Shell.Impact.Test <arena|roca|madera|agua|tortuga|enemigo|trasto|todos> [fuerza 0..1] (sin argumentos, los siete uno tras otro)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleTestCommand));
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_ShellImpactFXComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_ShellImpactFXComponent::UTN_ShellImpactFXComponent()
{
	// Antes de la física (el grupo por defecto): la velocidad que se anota es la que la caja lleva a los choques de este fotograma.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	State = MakeShared<TNShellImpactFX::FState>();
}

UTN_ShellImpactFXComponent* UTN_ShellImpactFXComponent::FindOrAddTo(AActor* InOwner)
{
	if (!IsValid(InOwner) || InOwner->IsActorBeingDestroyed()) { return nullptr; }
	if (UTN_ShellImpactFXComponent* Existing = InOwner->FindComponentByClass<UTN_ShellImpactFXComponent>())
	{
		return Existing;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer
		|| (!FApp::CanEverRender() && !FApp::CanEverRenderAudio()))
	{
		return nullptr;
	}
	UTN_ShellImpactFXComponent* Comp = NewObject<UTN_ShellImpactFXComponent>(InOwner, NAME_None, RF_Transient);
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}

void UTN_ShellImpactFXComponent::BindBody(ATN_ShellBody* InBody)
{
	UBoxComponent* Box = InBody ? InBody->GetBox() : nullptr;
	if (!Box || !State.IsValid())
	{
		return;
	}
	// «Simulation Generates Hit Events»: sin esto la física no avisa de los choques (la caja lo trae apagado).
	Box->SetNotifyRigidBodyCollision(true);
	Box->OnComponentHit.AddUniqueDynamic(this, &UTN_ShellImpactFXComponent::HandleShellHit);
	BoundBody = InBody;
	State->PrevVelocity = Box->GetPhysicsLinearVelocity();
	State->bWasInWater = false;
	State->WaterVolumes.Reset();
	State->NextWaterRefresh = 0.0;
	State->LastHitTime = -10.0;
	State->LastStrength = 0.f;
}

void UTN_ShellImpactFXComponent::UnbindBody()
{
	if (ATN_ShellBody* Body = BoundBody.Get())
	{
		if (UBoxComponent* Box = Body->GetBox())
		{
			Box->OnComponentHit.RemoveDynamic(this, &UTN_ShellImpactFXComponent::HandleShellHit);
			Box->SetNotifyRigidBodyCollision(false);
		}
	}
	BoundBody.Reset();
}

void UTN_ShellImpactFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindBody();
	Super::EndPlay(EndPlayReason);
}

void UTN_ShellImpactFXComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	using namespace TNShellImpactFX;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Turtle || !World || !State.IsValid())
	{
		return;
	}
	FState& S = *State;
	const float Dt = FMath::Min(DeltaTime, 0.1f);

	// La caja de la bola: cuando cambia (o desaparece), se apunta a la nueva o se suelta.
	UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
	if (!IsValid(Body) || Body->IsActorBeingDestroyed() || GEnabled == 0)
	{
		Body = nullptr;
	}
	if (Body != BoundBody.Get())
	{
		UnbindBody();
		if (Body)
		{
			BindBody(Body);
		}
	}

	// Velocidad con la que la caja entra en la física de este fotograma, y entrada al agua.
	if (ATN_ShellBody* Bound = BoundBody.Get())
	{
		if (UBoxComponent* Box = Bound->GetBox())
		{
			const FVector Velocity = Box->GetPhysicsLinearVelocity();
			const double NowSeconds = World->GetTimeSeconds();
			if (S.WaterVolumes.Num() == 0 && NowSeconds >= S.NextWaterRefresh)
			{
				// Los volúmenes de agua (el mar, las pozas): los mismos con los que la caja decide que ha caído al agua.
				S.NextWaterRefresh = NowSeconds + 2.0;
				for (TActorIterator<APhysicsVolume> It(World); It; ++It)
				{
					if (It->bWaterVolume) { S.WaterVolumes.Add(*It); }
				}
			}
			bool bInWater = false;
			for (const TWeakObjectPtr<APhysicsVolume>& WaterVolume : S.WaterVolumes)
			{
				if (WaterVolume.IsValid() && WaterVolume->IsOverlapInVolume(*Box)) { bInWater = true; break; }
			}
			if (bInWater && !S.bWasInWater)
			{
				// Cae al agua: chapoteo con la velocidad de entrada (la caja sale enseguida del caparazón y nada).
				const float MinSpeed = GMinSpeed > 0.f ? GMinSpeed : MinImpactSpeed;
				const float Speed = FMath::Max(static_cast<float>(-S.PrevVelocity.Z), 0.5f * static_cast<float>(S.PrevVelocity.Size()));
				if (Speed >= MinSpeed)
				{
					const float Strength = 0.1f + 0.9f * HitSmoothStep((Speed - MinSpeed) / FMath::Max(1.f, FullImpactSpeed - MinSpeed));
					const double Now = World->GetTimeSeconds();
					if (Now - S.LastHitTime >= 0.07 && BudgetAllows(Strength))
					{
						S.LastHitTime = Now;
						S.LastStrength = Strength;
						const FVector Where = Box->GetComponentLocation() - FVector(0.0, 0.0, 15.0);
						EmitImpact(ETNShellImpactSound::Water, Strength, Where, FVector::UpVector);
						if (GDebug != 0)
						{
							UE_LOG(LogTortunabo, Display, TEXT("[ShellImpact] %s: agua a %.0f cm/s, fuerza %.2f"), *GetOwner()->GetName(), Speed, Strength);
						}
					}
				}
			}
			S.bWasInWater = bInWater;
			S.PrevVelocity = Velocity;
		}
	}

	// Solo se mueven los emisores con partículas vivas (con la pantalla partida, hacia la cámara local más cercana).
	FVector View = Turtle->GetActorLocation();
	TNLocalViews::ClosestCamera(World, Turtle->GetActorLocation(), View);
	for (FSlot& Slot : S.Slots)
	{
		if (!Slot.bAlive) { continue; }
		TNAmbientFX::TickEmitter(Slot.Emitter, Dt, View);
		Slot.bAlive = AnyAlive(Slot.Emitter);
	}
}

void UTN_ShellImpactFXComponent::HandleShellHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	using namespace TNShellImpactFX;
	ATN_ShellBody* Body = BoundBody.Get();
	UWorld* World = GetWorld();
	if (GEnabled == 0 || !State.IsValid() || !Body || !World || !HitComponent || HitComponent != Body->GetBox())
	{
		return;
	}
	FState& S = *State;
	const double Now = World->GetTimeSeconds();
	// El motor manda varios contactos de un mismo golpe: el primero manda.
	if (Now - S.LastHitTime < 0.07)
	{
		return;
	}

	// Velocidad del impacto: la variación de velocidad que da el motor (impulso normal / masa) o, si es mayor, lo que la bola
	// llevaba contra la superficie al entrar en este fotograma.
	const float Mass = FMath::Max(1.f, HitComponent->GetMass());
	const float ImpulseSpeed = static_cast<float>(NormalImpulse.Size()) / Mass;
	FVector Normal = Hit.ImpactNormal.IsNearlyZero() ? FVector(Hit.Normal) : FVector(Hit.ImpactNormal);
	Normal = Normal.GetSafeNormal();
	const float ApproachSpeed = Normal.IsNearlyZero() ? 0.f : FMath::Abs(static_cast<float>(FVector::DotProduct(S.PrevVelocity, Normal)));
	const float Speed = FMath::Max(ImpulseSpeed, ApproachSpeed);
	const float MinSpeed = GMinSpeed > 0.f ? GMinSpeed : MinImpactSpeed;
	if (Speed < MinSpeed)
	{
		return;
	}
	const float Strength = 0.1f + 0.9f * HitSmoothStep((Speed - MinSpeed) / FMath::Max(1.f, FullImpactSpeed - MinSpeed));

	// Bola contra bola: los dos avisan, pero solo suena uno (el de menor identificador, en esta máquina).
	if (const ATN_ShellBody* OtherBody = Cast<ATN_ShellBody>(OtherActor))
	{
		const UBoxComponent* OtherBox = OtherBody->GetBox();
		if (OtherBox && OtherBox->BodyInstance.bNotifyRigidBodyCollision && OtherBody->GetUniqueID() < Body->GetUniqueID())
		{
			return;
		}
	}

	// Separación mínima: más larga con golpes flojos, salvo que llegue uno claramente más fuerte.
	const float Cooldown = FMath::Lerp(0.42f, FMath::Max(0.05f, MinInterval), Strength);
	if (Now - S.LastHitTime < Cooldown && Strength < S.LastStrength * 1.5f)
	{
		return;
	}
	if (!BudgetAllows(Strength))
	{
		return;
	}
	S.LastHitTime = Now;
	S.LastStrength = Strength;

	const ETNShellImpactSound Sound = ClassifyContact(OtherActor, OtherComp, Hit, S, World);
	FVector Where = FVector(Hit.ImpactPoint);
	if (Where.IsNearlyZero())
	{
		Where = HitComponent->GetComponentLocation();
	}
	if (Normal.IsNearlyZero())
	{
		Normal = FVector::UpVector;
	}
	else if (FVector::DotProduct(Normal, HitComponent->GetComponentLocation() - Where) < 0.0)
	{
		// Hacia fuera de la superficie, del lado de la bola.
		Normal = -Normal;
	}
	EmitImpact(Sound, Strength, Where, Normal);
	if (GDebug != 0)
	{
		UE_LOG(LogTortunabo, Display, TEXT("[ShellImpact] %s: %s a %.0f cm/s (impulso %.0f, aproximación %.0f), fuerza %.2f, contra %s / %s"),
			*GetOwner()->GetName(), SoundName(Sound), Speed, ImpulseSpeed, ApproachSpeed, Strength,
			OtherActor ? *OtherActor->GetName() : TEXT("nada"), OtherComp ? *OtherComp->GetName() : TEXT("-"));
	}
}

void UTN_ShellImpactFXComponent::EmitImpact(ETNShellImpactSound InSound, float InStrength, const FVector& InWhere, const FVector& InNormal)
{
	using namespace TNShellImpactFX;
	AActor* Turtle = GetOwner();
	UWorld* World = GetWorld();
	if (!Turtle || !World || !State.IsValid())
	{
		return;
	}
	FState& S = *State;

	// Sonido.
	const float Volume = GVolume * SoundLoudness;
	if (Volume > 0.f && FApp::CanEverRenderAudio())
	{
		UTN_ShellImpactSynthComponent* Synth = S.Synth.Get();
		if (!Synth)
		{
			Synth = UTN_ShellImpactSynthComponent::AttachTo(Turtle);
			S.Synth = Synth;
		}
		if (Synth)
		{
			// Más fuerte = más grave, y un poco de azar para que dos golpes seguidos no suenen idénticos.
			const float Pitch = (1.f - 0.14f * InStrength) * (1.f + 0.07f * (NextUnit(S.Rng) * 2.f - 1.f));
			const float Gain = (0.1f + 0.9f * FMath::Pow(InStrength, 1.1f)) * Volume;
			Synth->Play(InSound, InStrength, Pitch, Gain);
		}
	}

	// Efecto visual, cerca de la cámara local.
	if (FXAmount > 0.f && FApp::CanEverRender())
	{
		FVector View = InWhere;
		TNLocalViews::ClosestCamera(World, InWhere, View);
		if (FVector::DistSquared(View, InWhere) > FMath::Square(static_cast<double>(MaxViewDistance)))
		{
			return;
		}
		const float Amount = FMath::Max(0.f, FXAmount);
		const FVector Up = FVector::UpVector;
		const FVector Away = (InNormal + Up * 0.35).GetSafeNormal();
		const FVector Point = InWhere + InNormal * 4.0;
		if (FSlot* Puff = EnsureSlot(Turtle, S, InSound, 0))
		{
			BurstSlot(*Puff, Point, (InNormal + Up * 0.6).GetSafeNormal(), FMath::RoundToInt((3.f + 8.f * InStrength) * Amount));
		}
		if (FSlot* Bits = EnsureSlot(Turtle, S, InSound, 1))
		{
			BurstSlot(*Bits, Point, Away, FMath::RoundToInt((4.f + 14.f * InStrength) * Amount));
		}
	}
}

void UTN_ShellImpactFXComponent::PlayTestImpact(ETNShellImpactSound InSound, float InStrength)
{
	AActor* Turtle = GetOwner();
	if (!Turtle)
	{
		return;
	}
	const float Strength = FMath::Clamp(InStrength, 0.f, 1.f);
	const FVector Forward = Turtle->GetActorForwardVector();
	const FVector Where = Turtle->GetActorLocation() + FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal() * 220.0 - FVector(0.0, 0.0, 70.0);
	EmitImpact(InSound, Strength, Where, FVector::UpVector);
	UE_LOG(LogTortunabo, Display, TEXT("[ShellImpact] Prueba: %s con fuerza %.2f."), TNShellImpactFX::SoundName(InSound), Strength);
}

void UTN_ShellImpactFXComponent::PlayWallSplat(const FVector& InWhere, const FVector& InWallNormal, float InStrength)
{
	using namespace TNShellImpactFX;
	AActor* Turtle = GetOwner();
	UWorld* World = GetWorld();
	if (GEnabled == 0 || !State.IsValid() || !Turtle || !World)
	{
		return;
	}
	FState& S = *State;
	const FVector Normal = InWallNormal.IsNearlyZero() ? FVector::UpVector : InWallNormal.GetSafeNormal();
	const float Strength = FMath::Clamp(InStrength, 0.f, 1.f);

	// Lo que hay en la pared en esta máquina: de un poco fuera hacia dentro, sin la propia tortuga ni su bola.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNShellWallSplat), true, Turtle);
	if (const ATN_ShellBody* Body = BoundBody.Get())
	{
		Query.AddIgnoredActor(Body);
	}
	FHitResult Hit;
	ETNShellImpactSound Sound = ETNShellImpactSound::Rock;
	if (World->LineTraceSingleByChannel(Hit, InWhere + Normal * 30.0, InWhere - Normal * 40.0, ECC_Visibility, Query))
	{
		Sound = ClassifyContact(Hit.GetActor(), Hit.GetComponent(), Hit, S, World);
	}
	S.LastHitTime = World->GetTimeSeconds();
	S.LastStrength = Strength;
	EmitImpact(Sound, Strength, InWhere, Normal);
	if (GDebug != 0)
	{
		UE_LOG(LogTortunabo, Display, TEXT("[ShellImpact] %s: estampado contra la pared (%s), fuerza %.2f."), *Turtle->GetName(),
			SoundName(Sound), Strength);
	}
}
