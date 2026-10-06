// Reparto de la voz por proximidad (UProximityVoiceComponent) entre los jugadores: a quién reenvía el servidor cada paquete.
// Sirve para cualquier PlayerController que reciba voz (ITN_VoiceListener). La regla es lógica pura (TNVoiceRouting, tests
// Tortunabo.Voice.Routing.*).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TN_VoiceRouting.generated.h"

class AActor;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTN_VoiceListener : public UInterface
{
	GENERATED_BODY()
};

/**
 * PlayerController que recibe la voz de los demás. El servidor llama a SendVoiceToOwningClient y el controlador la manda a
 * su máquina con su propia RPC de cliente (las RPC no pueden vivir en una interfaz).
 */
class TORTUNABO_API ITN_VoiceListener
{
	GENERATED_BODY()

public:
	/** Servidor: manda un paquete de voz a la máquina de este jugador. */
	virtual void SendVoiceToOwningClient(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor) = 0;
};

namespace TNVoiceRouting
{
	/** ¿Oye al que habla? Solo dentro de OuterRadiusCm (DistanceSquared en cm²). */
	TORTUNABO_API bool IsInRange(double DistanceSquared, double OuterRadiusCm);

	/**
	 * Oyentes a los que se reenvía un paquete (mismo orden que DistancesSquared, en cm²; false = no se manda): los que están
	 * dentro de OuterRadiusCm y, de ellos, solo los MaxListeners más cercanos (0 = sin tope).
	 */
	TORTUNABO_API TArray<bool> SelectListeners(TConstArrayView<double> DistancesSquared, double OuterRadiusCm, int32 MaxListeners);
}
