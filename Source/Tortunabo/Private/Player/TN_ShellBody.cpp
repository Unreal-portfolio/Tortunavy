#include "Player/TN_ShellBody.h"

#include "Components/BoxComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Core/TN_Log.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_ShellDecisions.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachStun.h"

namespace TNShellBodyDetail
{
	/** Parada: por debajo de esta velocidad (cm/s) y este giro (rad/s) durante RestSeconds. */
	constexpr float RestLinearSpeed = 60.f;
	constexpr float RestAngularSpeed = 1.5f;
	constexpr float RestSeconds = 0.35f;
	/** Si sigue rodando o resbalando tanto tiempo, sale igual (no se queda atrapada en el caparazón). */
	constexpr float MaxSecondsBeforeExit = 9.f;
	/** Cada cuánto mira si ha caído al agua (s). */
	constexpr float WaterCheckInterval = 0.1f;

	/**
	 * Giro máximo de la caja (grados/s, unas 2,5 vueltas por segundo). Una caja que rueda no necesita más; lo que pasaba de
	 * ahí era un rebote contra una arista o una corrección de red, y se veía como una bola que se vuelve loca.
	 */
	constexpr float MaxSpinDegrees = 900.f;

	/**
	 * Velocidad máxima (cm/s) con la que se separa de lo que ya solapaba al nacer o al recolocarla (la ponen dentro de algo):
	 * sin tope, la física la escupía a varios metros por segundo.
	 */
	constexpr float MaxInitialDepenetration = 300.f;

	/** Instrumento TN.Shell.Debug: cada cuánto mira si la caja está bajo el terreno (s), entre avisos de una caja (s) y choques guardados. */
	constexpr float DepthInterval = 0.1f;
	constexpr float DebugLogInterval = 0.5f;
	constexpr int32 DebugMaxContacts = 6;
	/** A menos de esto (cm) del filo del acantilado no se mira la hondura: la pared está socavada y ahí se cae al agua de meta. */
	constexpr float CliffSkip = 1000.f;

	TAutoConsoleVariable<int32> CVarShellDebug(TEXT("TN.Shell.Debug"), UE_BUILD_SHIPPING ? 0 : 1,
		TEXT("Instrumento de la bola del caparazón (todas las máquinas): 1 = avisa en el registro («[Caparazón] TN.Shell.Debug») cuando la caja gira cerca de su tope durante 0,4 s (torbellino), cuando su parte de abajo queda más de 25 cm bajo el terreno de la playa o cuando sale empujada a más de 9 m/s en un paso (lo que no es frenar un choque) fuera de un lanzamiento, con sus últimos choques (quién la empuja) y, en los clientes, el desfase con el servidor; 0 = apagado (por defecto en Shipping)."));

	const TCHAR* AnomalyName(TNShellLogic::EShellMotionAnomaly Anomaly)
	{
		switch (Anomaly)
		{
		case TNShellLogic::EShellMotionAnomaly::Spin: return TEXT("torbellino (giro sostenido cerca del tope)");
		case TNShellLogic::EShellMotionAnomaly::Sunk: return TEXT("caja bajo el terreno");
		case TNShellLogic::EShellMotionAnomaly::VelocityJump: return TEXT("salto de velocidad sin lanzamiento");
		default: return TEXT("nada");
		}
	}

	TAutoConsoleVariable<int32> CVarShellPhysicsRep(TEXT("TN.Shell.PhysicsRep"), 1,
		TEXT("Réplica de la física de la bola del caparazón en los clientes (se aplica a las bolas nuevas): 1 = interpolación predictiva (de serie: corrige con velocidad hacia el estado del servidor extrapolado, sin tirones); 0 = la de siempre del motor (fijaba cada paso la velocidad y el 40 % del giro hacia un estado ya viejo: la bola temblaba y rebotaba en los clientes)."));

	/** La colisión de siempre de la caja: cuerpo físico que choca con todo menos con la cámara (ni la propia ni la de los demás). */
	void SetDefaultCollision(UBoxComponent& Box)
	{
		Box.SetCollisionProfileName(TEXT("PhysicsActor"));
		Box.SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
}

ATN_ShellBody::ATN_ShellBody()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Después de la física: la tortuga se coloca donde la caja ha quedado en este fotograma.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->InitBoxExtent(BoxHalfExtent());
	TNShellBodyDetail::SetDefaultCollision(*Box);
	Box->SetSimulatePhysics(true);
	Box->SetEnableGravity(true);
	Box->SetUseCCD(true);
	Box->SetNotifyRigidBodyCollision(false);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);
	Box->CanCharacterStepUpOn = ECB_No;
	Box->BodyInstance.SetMassOverride(38.f, true);
	// La lineal no se toca: la tormenta calcula sus patadas con ella (ATN_BeachStorm, BallisticLaunch).
	Box->BodyInstance.LinearDamping = BoxLinearDamping;
	Box->BodyInstance.AngularDamping = 1.4f;
	// Estable sobre el terreno de la playa: sus teselas son mallas distintas y la caja tropezaba en cada costura y en cada
	// arista interior (saltitos y vueltas sin motivo). Caro, pero son como mucho ocho bolas.
	Box->BodyInstance.bSmoothEdgeCollisions = true;
	Box->BodyInstance.bOverrideMaxAngularVelocity = true;
	Box->BodyInstance.MaxAngularVelocity = TNShellBodyDetail::MaxSpinDegrees;

	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
	// En los clientes la caja también simula (choca con lo de su máquina) y la réplica la lleva al estado del servidor. La de
	// siempre del motor fijaba en cada paso la velocidad y el 40 % del giro hacia un estado de hace ~50-100 ms: una caja que
	// rueda temblaba, rebotaba contra el suelo y daba tirones en la cámara de su dueño. La interpolación predictiva extrapola
	// el estado del servidor y corrige con velocidad (TN.Shell.PhysicsRep 0 vuelve a la de siempre para comparar).
	SetPhysicsReplicationMode(EPhysicsReplicationMode::PredictiveInterpolation);
	// Relevante justo cuando lo es su tortuga (es su dueña): si no, un cliente podría ver a la tortuga sin su caja.
	bNetUseOwnerRelevancy = true;
}

void ATN_ShellBody::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ShellBody, Turtle);
	DOREPLIFETIME(ATN_ShellBody, bPassThrough);
}

void ATN_ShellBody::SetPassThrough(bool bOn)
{
	if (!HasAuthority() || bPassThrough == bOn)
	{
		return;
	}
	bPassThrough = bOn;
	ApplyPassThrough();
	ForceNetUpdate();
}

void ATN_ShellBody::OnRep_PassThrough()
{
	ApplyPassThrough();
}

void ATN_ShellBody::ApplyPassThrough()
{
	if (!Box)
	{
		return;
	}
	if (bPassThrough)
	{
		// Sigue simulando (la gravedad la lleva por el mismo arco), pero no choca con nada.
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	}
	else
	{
		TNShellBodyDetail::SetDefaultCollision(*Box);
	}
	// Sin el tope del motor a la depenetración no hay nada que limitar mientras cruza; al volver a chocar empieza de cero.
	bPushOutPrimed = false;
	SunkStrikes = 0;
}

void ATN_ShellBody::InitBody(ATortugaCharacter* InTurtle, bool bInExitOnRest)
{
	Turtle = InTurtle;
	bExitOnRest = bInExitOnRest;
	Age = 0.f;
	RestTime = 0.f;
	ForceNetUpdate();
}

void ATN_ShellBody::BeginPlay()
{
	Super::BeginPlay();

	// Material resbaladizo propio: el caparazón patina por las cuestas y rebota un poco. Al cambiar los valores en
	// ejecución hay que refrescar el material de Chaos.
	Slippery = NewObject<UPhysicalMaterial>(this, TEXT("ShellSlippery"));
	Slippery->Friction = 0.25f;
	// Rebota algo menos que antes (0,35): contra el decorado pequeño y los bordes de las teselas botaba sin parar.
	Slippery->Restitution = 0.2f;
	FPhysicsInterface::UpdateMaterial(Slippery->GetPhysicsMaterial(), Slippery);
	Box->SetPhysMaterialOverride(Slippery);
	// Si nace (o la recolocan) metida en algo, sale despacio en vez de salir disparada.
	Box->BodyInstance.SetMaxDepenetrationVelocity(TNShellBodyDetail::MaxInitialDepenetration);
	if (TNShellBodyDetail::CVarShellPhysicsRep.GetValueOnGameThread() == 0)
	{
		SetPhysicsReplicationMode(EPhysicsReplicationMode::Default);
	}

	// En los clientes la caja puede llegar antes o después que la referencia del componente de caparazón: el que
	// llegue segundo engancha a la tortuga.
	if (!HasAuthority() && Turtle)
	{
		if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
		{
			Shell->AdoptBody(this);
		}
	}
}

void ATN_ShellBody::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ATortugaCharacter* OwnerTurtle = Turtle;
	if (!IsValid(OwnerTurtle))
	{
		// La tortuga se ha ido (jugador que sale): el caparazón no se queda suelto por el mapa.
		if (HasAuthority() && Age > 0.5f)
		{
			bReleased = true;
			Destroy();
		}
		Age += DeltaSeconds;
		return;
	}
	Age += DeltaSeconds;

	UTN_ShellComponent* Shell = OwnerTurtle->GetShellComponent();
	if (!Shell)
	{
		return;
	}
	// Todas las máquinas: la tortuga sigue a la caja (solo si es su caja y la tiene enganchada en esta máquina).
	Shell->FollowBody(this);
	const bool bFreshDepth = SampleTerrainDepth(DeltaSeconds);
	// Antes del instrumento: mide la velocidad que queda. En los clientes no: allí la réplica acerca la caja al servidor.
	// Atravesando no choca con el terreno: no hay nada que la escupa.
	bPushOutLimited = HasAuthority() && !bPassThrough && LimitTerrainPushOut();
	TickDebugWatch(DeltaSeconds);

	if (HasAuthority())
	{
		ServerChecks(DeltaSeconds, bFreshDepth);
		RememberPushOutState();
	}
}

FVector ATN_ShellBody::BoxBottom() const
{
	return FVector(Box->Bounds.Origin.X, Box->Bounds.Origin.Y, Box->Bounds.Origin.Z - Box->Bounds.BoxExtent.Z);
}

float ATN_ShellBody::TerrainDepthUnder(const UObject* WorldContext, const FVector& Bottom)
{
	using namespace TNShellBodyDetail;
	const ATN_BeachRaceGenerator* Gen = ATN_BeachRaceGenerator::Find(WorldContext);
	return (Gen && Gen->GetCliffEdgeDistance(Bottom) < -CliffSkip) ? TNBeach::DepthUnderTerrain(WorldContext, Bottom) : -1000.f;
}

bool ATN_ShellBody::LimitTerrainPushOut()
{
	using namespace TNShellBodyDetail;
	// Recién lanzada (InitBody): el salto de velocidad es el propio lanzamiento, como en el instrumento.
	if (!Box || !Box->IsSimulatingPhysics() || !bPushOutPrimed || Age < TNShellLogic::FShellMotionThresholds().LaunchGraceSeconds)
	{
		return false;
	}
	const FVector Before = Box->GetPhysicsLinearVelocity();
	FVector Velocity = Before;
	TNShellLogic::FTerrainPushOutRules Rules;
	Rules.MaxPushOutSpeed = MaxInitialDepenetration;
	if (!TNShellLogic::LimitTerrainPushOut(PushOutPrevVelocity, PushOutPrevDepth, Velocity, Rules))
	{
		return false;
	}
	Box->SetPhysicsLinearVelocity(Velocity);
	UE_LOG(LogTortunabo, Log, TEXT("[Caparazón] %s de %s: estaba %.0f cm dentro del terreno y la escupía a %.0f cm/s; sale a %.0f cm/s."),
		*GetName(), *GetNameSafe(Turtle.Get()), PushOutPrevDepth, Before.Size(), Velocity.Size());
	return true;
}

void ATN_ShellBody::RememberPushOutState()
{
	if (!Box || !Box->IsSimulatingPhysics())
	{
		bPushOutPrimed = false;
		return;
	}
	PushOutPrevVelocity = Box->GetPhysicsLinearVelocity();
	// Desde el centro y con el semieje más corto (tumbada en su vertical), no con la parte de abajo de sus límites: en una
	// cuesta, la esquina más baja de una caja que rueda queda cuesta abajo del centro y la daría por metida sin estarlo.
	PushOutPrevDepth = TerrainDepthUnder(this, Box->GetComponentLocation() - FVector(0.0, 0.0, BoxHalfExtent().GetMin()));
	bPushOutPrimed = true;
}

bool ATN_ShellBody::SampleTerrainDepth(float DeltaSeconds)
{
	using namespace TNShellBodyDetail;
	if (!Box || !Box->IsSimulatingPhysics() || (!HasAuthority() && CVarShellDebug.GetValueOnGameThread() <= 0))
	{
		return false;
	}
	DepthTimer -= DeltaSeconds;
	if (DepthTimer > 0.f)
	{
		return false;
	}
	DepthTimer = DepthInterval;
	TerrainDepth = TerrainDepthUnder(this, BoxBottom());
	return true;
}

void ATN_ShellBody::RescueFromUnderTerrain()
{
	UWorld* World = GetWorld();
	if (!Box || !World)
	{
		return;
	}
	const float Depth = TerrainDepth;
	const FVector Center = Box->GetComponentLocation();
	const double BottomOffset = Center.Z - (Box->Bounds.Origin.Z - Box->Bounds.BoxExtent.Z);
	const double SurfaceGuess = Center.Z - BottomOffset + Depth;
	// Desde encima de la superficie de su vertical hacia abajo: lo primero firme que para a la caja (el terreno o lo que haya
	// encima de él), sin la propia bola ni su tortuga.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNShellSunkRescue), false, this);
	Query.AddIgnoredActor(Turtle.Get());
	FHitResult Hit;
	const FVector Start(Center.X, Center.Y, SurfaceGuess + 250.0);
	const FVector End(Center.X, Center.Y, SurfaceGuess - 50.0);
	const bool bGround = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Query) && !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0.5;
	const float Lift = bGround ? static_cast<float>(Hit.ImpactPoint.Z + BottomOffset + 2.0 - Center.Z) : 0.f;
	if (!bGround || !TNShellLogic::IsSunkLiftAllowed(Lift))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Caparazón] %s de %s %.0f cm bajo el terreno sin suelo firme cerca (subida %.0f cm): la deja a la red de seguridad de la carrera."),
			*GetName(), *GetNameSafe(Turtle.Get()), Depth, Lift);
		return;
	}
	FVector Linear = Box->GetPhysicsLinearVelocity();
	FVector Angular = Box->GetPhysicsAngularVelocityInRadians();
	TNShellLogic::SettleRescuedVelocity(Linear, Angular);
	Box->SetWorldLocation(FVector(Center.X, Center.Y, Center.Z + Lift), false, nullptr, ETeleportType::TeleportPhysics);
	Box->SetPhysicsLinearVelocity(Linear);
	Box->SetPhysicsAngularVelocityInRadians(Angular);
	// Encima de la arena (hasta la muestra siguiente): el instrumento no la vuelve a contar como hundida.
	TerrainDepth = -2.f;
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Warning, TEXT("[Caparazón] %s de %s había cruzado el terreno (%.0f cm bajo la arena): recolocada %.0f cm más arriba."),
		*GetName(), *GetNameSafe(Turtle.Get()), Depth, Lift);
}

void ATN_ShellBody::ServerChecks(float DeltaSeconds, bool bFreshDepth)
{
	using namespace TNShellBodyDetail;
	ATortugaCharacter* OwnerTurtle = Turtle;
	UTN_ShellComponent* Shell = OwnerTurtle ? OwnerTurtle->GetShellComponent() : nullptr;
	if (bReleased || !Shell || Shell->GetBody() != this || !Box)
	{
		return;
	}

	// Atravesando (la patada de la tormenta sin arco libre): ni el agua de donde sale ni lo que cruza la paran; vuelve a
	// mirarse al volver a chocar, ya encima de su sitio.
	if (bPassThrough)
	{
		return;
	}

	// Al agua: sale del caparazón y nada.
	WaterCheckTimer -= DeltaSeconds;
	if (WaterCheckTimer <= 0.f)
	{
		WaterCheckTimer = WaterCheckInterval;
		if (IsInWater(*Box))
		{
			Shell->NotifyBodyInWater();
			return;
		}
	}

	// Ha cruzado la malla fina del terreno (dos muestras seguidas): encima de la arena en el acto, sin esperar a la red de
	// seguridad de la carrera (1,6 m y un rescate con aturdimiento).
	if (bFreshDepth && TNShellLogic::ShouldRescueSunkenBody(TerrainDepth, SunkStrikes))
	{
		RescueFromUnderTerrain();
	}

	if (!bExitOnRest)
	{
		return;
	}

	// Lanzada (o caída de altura, o escapada): rueda y rebota con la física y, cuando se para, sale.
	const float Linear = static_cast<float>(Box->GetPhysicsLinearVelocity().Size());
	const float Angular = static_cast<float>(Box->GetPhysicsAngularVelocityInRadians().Size());
	const bool bSlow = Linear < RestLinearSpeed && Angular < RestAngularSpeed;
	RestTime = bSlow ? RestTime + DeltaSeconds : 0.f;
	if ((RestTime >= RestSeconds && Age > 0.3f) || Age > MaxSecondsBeforeExit)
	{
		Shell->NotifyBodyAtRest();
	}
}

void ATN_ShellBody::TickDebugWatch(float DeltaSeconds)
{
	using namespace TNShellBodyDetail;
	const UWorld* World = GetWorld();
	if (CVarShellDebug.GetValueOnGameThread() <= 0 || !Box || !World || DeltaSeconds <= 0.f || !Box->IsSimulatingPhysics())
	{
		return;
	}
	if (!bDebugBound)
	{
		bDebugBound = true;
		Box->OnComponentHit.AddUniqueDynamic(this, &ATN_ShellBody::HandleDebugHit);
	}

	const FVector Velocity = Box->GetPhysicsLinearVelocity();
	const FVector Spin = Box->GetPhysicsAngularVelocityInRadians();
	// Sin los avisos de choque de la física no hay «quién la empuja» (el sonido de los golpes también los enciende y los apaga
	// al soltar la caja).
	if (!Box->BodyInstance.bNotifyRigidBodyCollision)
	{
		Box->SetNotifyRigidBodyCollision(true);
	}

	TNShellLogic::FShellMotionSample Sample;
	Sample.DeltaSeconds = DeltaSeconds;
	Sample.AngularSpeed = static_cast<float>(Spin.Size());
	// Solo cuenta la velocidad con la que sale empujada: frenar (aterrizar, chocar) o el tope a la depenetración (ya lo registra) no.
	Sample.VelocityChange = bPushOutLimited ? 0.f : TNShellLogic::UnexplainedVelocityChange(DebugPrevVelocity, Velocity);
	Sample.BottomDepthUnderTerrain = TerrainDepth;
	Sample.AgeSeconds = Age;
	const FVector PrevVelocity = DebugPrevVelocity;
	DebugPrevVelocity = Velocity;
	const TNShellLogic::EShellMotionAnomaly Anomaly = TNShellLogic::ClassifyShellMotion(Sample, DebugSpinSeconds);
	const float Now = World->GetTimeSeconds();
	if (Anomaly == TNShellLogic::EShellMotionAnomaly::None || Now < DebugNextLogTime)
	{
		return;
	}
	DebugNextLogTime = Now + DebugLogInterval;

	// En los clientes, cuánto se ha ido la caja de la última posición que mandó el servidor (la réplica de física la corrige).
	const double ServerGap = HasAuthority() ? 0.0 : FVector::Dist(Box->GetComponentLocation(), GetReplicatedMovement().Location);
	const FVector At = Box->GetComponentLocation();
	UE_LOG(LogTortunabo, Warning,
		TEXT("[Caparazón] TN.Shell.Debug %s de %s en %s: %s · caja en (%.1f, %.1f, %.1f) m, su parte de abajo %.0f cm bajo el terreno · v %.0f cm/s (cambio de %.0f en el paso; antes %s, después %s) · giro %.1f rad/s (vertical %.1f) desde hace %.2f s · desfase con el servidor %.0f cm · edad %.2f s · choques: %s"),
		*GetName(), *GetNameSafe(Turtle.Get()), HasAuthority() ? TEXT("el servidor") : TEXT("un cliente"), AnomalyName(Anomaly),
		At.X / 100.0, At.Y / 100.0, At.Z / 100.0, TerrainDepth, Velocity.Size(), Sample.VelocityChange, *PrevVelocity.ToCompactString(),
		*Velocity.ToCompactString(), Sample.AngularSpeed, Spin.Z,
		DebugSpinSeconds, ServerGap, Age, *DescribeDebugContacts(Now));
}

void ATN_ShellBody::HandleDebugHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	const FHitResult& Hit)
{
	using namespace TNShellBodyDetail;
	const UWorld* World = GetWorld();
	if (!OtherComp || !World || CVarShellDebug.GetValueOnGameThread() <= 0)
	{
		return;
	}
	FDebugContact* Entry = DebugContacts.FindByPredicate([OtherComp](const FDebugContact& Contact) { return Contact.Component.Get() == OtherComp; });
	if (!Entry)
	{
		if (DebugContacts.Num() >= DebugMaxContacts)
		{
			int32 Oldest = 0;
			for (int32 Index = 1; Index < DebugContacts.Num(); ++Index)
			{
				Oldest = DebugContacts[Index].Time < DebugContacts[Oldest].Time ? Index : Oldest;
			}
			DebugContacts.RemoveAtSwap(Oldest);
		}
		Entry = &DebugContacts.AddDefaulted_GetRef();
		Entry->Component = OtherComp;
		Entry->Name = FString::Printf(TEXT("%s.%s"), *GetNameSafe(OtherActor), *OtherComp->GetName());
	}
	Entry->Time = World->GetTimeSeconds();
	Entry->Normal = Hit.ImpactNormal;
	Entry->Impulse = static_cast<float>(NormalImpulse.Size());
	Entry->bOtherSimulating = OtherComp->IsSimulatingPhysics();
	// De un cinemático (enemigo, plataforma) la física da la velocidad con la que lo mueve su objetivo; de lo estático, cero.
	Entry->OtherVelocity = OtherComp->GetPhysicsLinearVelocity();
}

FString ATN_ShellBody::DescribeDebugContacts(float Now) const
{
	if (DebugContacts.IsEmpty())
	{
		return TEXT("ninguno");
	}
	TArray<FDebugContact> Sorted = DebugContacts;
	Sorted.Sort([](const FDebugContact& A, const FDebugContact& B) { return A.Time > B.Time; });
	TArray<FString> Parts;
	for (const FDebugContact& Contact : Sorted)
	{
		Parts.Add(FString::Printf(TEXT("%s hace %.2f s (normal %.2f, %.2f, %.2f · impulso %.0f · el otro %s a %.0f cm/s)"), *Contact.Name, Now - Contact.Time,
			Contact.Normal.X, Contact.Normal.Y, Contact.Normal.Z, Contact.Impulse, Contact.bOtherSimulating ? TEXT("simula") : TEXT("cinemático o fijo"),
			Contact.OtherVelocity.Size()));
	}
	return FString::Join(Parts, TEXT(" | "));
}

void ATN_ShellBody::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destruida sin pasar por el componente (caída fuera del mundo, por ejemplo): la tortuga no puede quedarse sin
	// movimiento. En los clientes, si era la caja enganchada, se suelta igual.
	if (EndPlayReason == EEndPlayReason::Destroyed && !bReleased)
	{
		if (ATortugaCharacter* OwnerTurtle = Turtle)
		{
			if (UTN_ShellComponent* Shell = OwnerTurtle->GetShellComponent())
			{
				Shell->NotifyBodyEnded(this);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

FTransform ATN_ShellBody::MeshWorldTransform(const FTransform& BoxWorld, const FVector& MeshScale)
{
	// Malla tumbada sobre la tripa: su +Z (la cabeza) va a +X de la caja, su +Y (hacia donde mira, la tripa) abajo y su
	// izquierda (+X) a -Y. El centro del tronco (0; 0,25; 27 en unidades de malla) cae en el centro de la caja.
	static const FQuat LyingRotation(FMatrix(
		FPlane(0.0, -1.0, 0.0, 0.0),
		FPlane(0.0, 0.0, -1.0, 0.0),
		FPlane(1.0, 0.0, 0.0, 0.0),
		FPlane(0.0, 0.0, 0.0, 1.0)));
	const FVector Origin(-27.0 * MeshScale.Z, 0.0, 0.25 * MeshScale.Y);
	const FTransform MeshLocal(LyingRotation, Origin, MeshScale);
	return MeshLocal * FTransform(BoxWorld.GetRotation(), BoxWorld.GetLocation());
}

bool ATN_ShellBody::IsInWater(const UPrimitiveComponent& Component)
{
	const UWorld* World = Component.GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<APhysicsVolume> It(World); It; ++It)
	{
		const APhysicsVolume* Volume = *It;
		if (Volume && Volume->bWaterVolume && Volume->IsOverlapInVolume(Component))
		{
			return true;
		}
	}
	return false;
}
