#include "Audio/TN_AudioVoices.h"

#include "Components/AudioComponent.h"
#include "Components/SynthComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

float TNAudioVoices::PriorityFor(ERank Rank)
{
	switch (Rank)
	{
	case ERank::Reserved:   return 100.f;
	case ERank::Background: return 0.5f;
	case ERank::World:
	default:                return 1.f;
	}
}

bool TNAudioVoices::IsAlwaysPlay(ERank Rank)
{
	return Rank == ERank::Reserved;
}

const TCHAR* TNAudioVoices::RankName(ERank Rank)
{
	switch (Rank)
	{
	case ERank::Reserved:   return TEXT("reservada");
	case ERank::Background: return TEXT("fondo");
	case ERank::World:
	default:                return TEXT("mundo");
	}
}

TNAudioVoices::ERank TNAudioVoices::RankForOwner(const AActor* InOwner)
{
	if (const APawn* Pawn = Cast<APawn>(InOwner))
	{
		// Un bot también es «local» en el servidor: solo cuenta la tortuga de un jugador de esta máquina.
		return (Pawn->IsPlayerControlled() && Pawn->IsLocallyControlled()) ? ERank::Reserved : ERank::World;
	}
	if (const APlayerController* Controller = Cast<APlayerController>(InOwner))
	{
		return Controller->IsLocalController() ? ERank::Reserved : ERank::World;
	}
	return ERank::World;
}

void TNAudioVoices::Apply(USynthComponent& Synth, ERank Rank)
{
	// USynthComponent copia bAlwaysPlay a su componente de audio solo al crearlo (OnRegister): después hay que tocar los dos.
	Synth.bAlwaysPlay = IsAlwaysPlay(Rank);
	if (UAudioComponent* Audio = Synth.GetAudioComponent())
	{
		Apply(*Audio, Rank);
	}
}

void TNAudioVoices::Apply(UAudioComponent& Audio, ERank Rank)
{
	Audio.bAlwaysPlay = IsAlwaysPlay(Rank);
	// Mundo: la prioridad del propio sonido (1 de serie en los sintetizadores y en los recursos, o la que le haya dado quien
	// lo hizo). Fondo: por debajo, a la fuerza. Reservada: da igual, manda bAlwaysPlay.
	Audio.bOverridePriority = Rank == ERank::Background;
	Audio.Priority = PriorityFor(Rank);
}
