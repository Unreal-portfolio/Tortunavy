// Reparto de la voz por proximidad (UProximityVoiceComponent) entre los jugadores: a quién reenvía el servidor cada paquete y
// si se oye a volumen completo. Sirve para cualquier PlayerController que reciba voz (ITN_VoiceListener) y para los modos
// con «interfono» (ITN_VoiceIntercom): en el Rally, las dos ocupantes de un buggy se oyen siempre a volumen completo y el
// resto por proximidad. La regla es lógica pura (TNVoiceRouting, tests Tortunabo.Rally.Voice.*).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TN_VoiceRouting.generated.h"

class AActor;
class APlayerState;

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
	/**
	 * Servidor: manda un paquete de voz a la máquina de este jugador.
	 * @param bIntercom true si el que habla comparte interfono con este jugador: se oye sin atenuar por distancia.
	 */
	virtual void SendVoiceToOwningClient(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor,
		bool bIntercom) = 0;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTN_VoiceIntercom : public UInterface
{
	GENERATED_BODY()
};

/** PlayerState de un modo con interfono: los jugadores del mismo grupo se oyen a volumen completo estén donde estén. */
class TORTUNABO_API ITN_VoiceIntercom
{
	GENERATED_BODY()

public:
	/** Grupo de interfono (en el Rally, el equipo del buggy en que va sentado); INDEX_NONE = sin interfono. */
	virtual int32 GetVoiceIntercomGroup() const = 0;
};

namespace TNVoiceRouting
{
	enum class ERoute : uint8
	{
		/** No se le manda: demasiado lejos y sin interfono. */
		None,
		/** Se le manda y se oye atenuada por la distancia (InnerRadius / OuterRadius del componente). */
		Proximity,
		/** Mismo interfono: se le manda siempre y se oye a volumen completo, sin atenuar. */
		Intercom
	};

	/** Un posible oyente de un paquete: su grupo de interfono y su distancia al cuadrado al que habla (cm²). */
	struct FCandidate
	{
		int32 IntercomGroup = INDEX_NONE;
		double DistanceSquared = 0.0;
	};

	/** ¿Comparten interfono? Solo si los dos tienen grupo y es el mismo. */
	TORTUNABO_API bool SharesIntercom(int32 SpeakerGroup, int32 ListenerGroup);

	/** Ruta de un paquete para un oyente: interfono si lo comparten; si no, proximidad dentro de OuterRadiusCm. */
	TORTUNABO_API ERoute Route(int32 SpeakerGroup, const FCandidate& Listener, double OuterRadiusCm);

	/**
	 * Oyentes a los que se reenvía un paquete y con qué ruta (mismo orden que Candidates; None = no se manda). Los de
	 * interfono van siempre y no cuentan en el tope; los de proximidad, solo los MaxProximityListeners más cercanos (0 = sin
	 * tope).
	 */
	TORTUNABO_API TArray<ERoute> SelectListeners(int32 SpeakerGroup, TConstArrayView<FCandidate> Candidates, double OuterRadiusCm,
		int32 MaxProximityListeners);

	/** Grupo de interfono de un PlayerState (INDEX_NONE si no implementa ITN_VoiceIntercom o no tiene grupo). */
	TORTUNABO_API int32 IntercomGroupOf(const APlayerState* PlayerState);
}
