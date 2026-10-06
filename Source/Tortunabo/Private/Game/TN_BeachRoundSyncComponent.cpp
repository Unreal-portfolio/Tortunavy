#include "Game/TN_BeachRoundSyncComponent.h"
#include "Core/TN_Log.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"

UTN_BeachRoundSyncComponent::UTN_BeachRoundSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Cuatro veces por segundo sobra: montar la ronda lleva segundos.
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);
}

void UTN_BeachRoundSyncComponent::BeginPlay()
{
	Super::BeginPlay();
	// Solo el cliente dueño mira su ronda: el servidor (y el anfitrión, que es el servidor) ya sabe la suya. En un cliente
	// solo existe el PlayerController propio, así que este componente es siempre el del dueño.
	SetComponentTickEnabled(GetNetMode() == NM_Client);
}

UTN_BeachRoundSyncComponent* UTN_BeachRoundSyncComponent::FindOn(const AController* Controller)
{
	return Controller ? Controller->FindComponentByClass<UTN_BeachRoundSyncComponent>() : nullptr;
}

UTN_BeachRoundSyncComponent* UTN_BeachRoundSyncComponent::FindOrAddOn(APlayerController* Controller)
{
	if (UTN_BeachRoundSyncComponent* Existing = FindOn(Controller))
	{
		return Existing;
	}
	if (!Controller || !Controller->HasAuthority())
	{
		return nullptr;
	}
	// Componente dinámico replicado (como el aturdimiento de la tortuga): el servidor lo crea y el cliente dueño recibe el suyo.
	UTN_BeachRoundSyncComponent* Sync = NewObject<UTN_BeachRoundSyncComponent>(Controller, UTN_BeachRoundSyncComponent::StaticClass(), TEXT("BeachRoundSync"));
	if (!Sync)
	{
		return nullptr;
	}
	Controller->AddInstanceComponent(Sync);
	Sync->RegisterComponent();
	return Sync;
}

void UTN_BeachRoundSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetNetMode() != NM_Client)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (!Generator.IsValid())
	{
		Generator = ATN_BeachRaceGenerator::Find(this);
		if (!Generator.IsValid())
		{
			return;
		}
	}
	const ATN_BeachRaceGenerator* Gen = Generator.Get();
	const int32 Round = Gen->GetRoundNumber();
	// Montada en esta máquina y con las trampas y estructuras replicadas ya aquí (#828): si no, chocaría con menos que el servidor.
	if (Round <= 0 || Round == ReportedRound || !Gen->IsRoundReady() || !Gen->HasRoundElements())
	{
		return;
	}
	ReportedRound = Round;
	ServerReportRoundReady(Round);
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] ronda %d montada en este cliente (terreno y decorado local): se avisa al servidor."), Round);
}

void UTN_BeachRoundSyncComponent::ServerReportRoundReady_Implementation(int32 Round)
{
	// Solo vale la ronda que el servidor ya ha montado: una ronda futura dejaría al servidor sin esperar a este cliente.
	const ATN_BeachRaceGenerator* Gen = ATN_BeachRaceGenerator::Find(this);
	if (!Gen || Round <= 0 || Round > Gen->GetRoundNumber())
	{
		return;
	}
	ReadyRound = FMath::Max(ReadyRound, Round);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s tiene montada la ronda %d del generador."), *GetNameSafe(GetOwner()), Round);
}
