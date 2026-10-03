// La artillera canta notas y avisos rápidos a la conductora (#330). La artillera pide cantar la próxima nota de copiloto
// (F · A) o un aviso rápido («¡Turbo ya!» con 1 · cruceta arriba, «¡Frena!» con 2 · B); el servidor lo valida (artillera
// sentada, nota entre 0 y 600 m por delante, 0,6 s entre cantos) y lo reenvía solo a las dos ocupantes, que lo oyen con la
// señal del copiloto (al lado de la curva, tantos pitidos como el grado) y la conductora ve la placa 1,5 s
// (UTN_RallyCopilotComponent). Reglas puras en TNRallyCrewCalls (tests Tortunabo.Rally.CrewCalls.*).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyCopilotCalls.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "TN_RallyCrewCalls.generated.h"

class ATN_Buggy;
class ATN_RallyTrack;

/** Avisos rápidos de la artillera. */
UENUM()
enum class ETNRallyQuickCall : uint8
{
	/** «¡Turbo ya!» */
	Boost,
	/** «¡Frena!» */
	Brake
};

/** Lo que canta la artillera, tal como viaja del servidor a las ocupantes: una nota de copiloto o un aviso rápido. */
USTRUCT()
struct FTNRallyCrewCall
{
	GENERATED_BODY()

	UPROPERTY()
	bool bQuick = false;

	UPROPERTY()
	ETNRallyQuickCall Quick = ETNRallyQuickCall::Boost;

	/** Nota (si no es un aviso rápido): TNRallyPaceNotes::ENoteKind, ETurnDirection, grado y «no cortes». */
	UPROPERTY()
	uint8 Kind = 0;

	UPROPERTY()
	uint8 Direction = 0;

	UPROPERTY()
	uint8 Grade = 0;

	UPROPERTY()
	bool bDontCut = false;

	/** Giro total de la curva (grados): distingue la horquilla. */
	UPROPERTY()
	float AngleDeg = 0.f;
};

namespace TNRallyCrewCalls
{
	/** Una nota se puede cantar si empieza entre 0 y esto por delante del buggy (cm). */
	inline constexpr double MaxNoteAheadCm = 60000.0;
	/** Espera mínima entre dos cantos de la artillera (s). */
	inline constexpr double MinSecondsBetweenCalls = 0.6;
	/** Margen con que el servidor reconoce la nota que pide el cliente por su arco (cm). */
	inline constexpr double ArcMatchToleranceCm = 150.0;

	/** El servidor acepta el canto: la artillera está sentada, la nota está a 0..MaxNoteAheadCm y ha esperado lo mínimo. */
	TORTUNABO_API bool CanGunnerCall(bool bSeatedGunner, double NoteDistanceCm, double SecondsSinceLastCall);

	/** Un aviso rápido: solo la artillera sentada y con la espera mínima. */
	TORTUNABO_API bool CanGunnerQuickCall(bool bSeatedGunner, double SecondsSinceLastCall);

	/** Índice en Ahead de la nota cuyo arco está a ArcMatchToleranceCm de NoteArcCm, o INDEX_NONE. */
	TORTUNABO_API int32 FindCalledNote(TConstArrayView<TNRallyPaceNotes::FNoteAhead> Ahead, double NoteArcCm);

	/** Canto de una nota (lo que viaja por red) y la nota que se rehace en la máquina que lo recibe. */
	TORTUNABO_API FTNRallyCrewCall MakeNoteCall(const TNRallyPaceNotes::FPaceNote& Note);
	TORTUNABO_API TNRallyPaceNotes::FPaceNote NoteFromCall(const FTNRallyCrewCall& Call);
	TORTUNABO_API FTNRallyCrewCall MakeQuickCall(ETNRallyQuickCall Quick);

	/** Señal de un aviso rápido: centrada; el turbo, tres pitidos agudos; el freno, dos graves. */
	TORTUNABO_API TNRallyCopilot::FCallSignal QuickSignal(ETNRallyQuickCall Quick);

	/** Placa del aviso rápido: «¡Turbo ya!» o «¡Frena!». */
	TORTUNABO_API FText QuickHeadline(ETNRallyQuickCall Quick);

	TORTUNABO_API FLinearColor QuickColor(ETNRallyQuickCall Quick);

	/** Aviso rápido de una entrada de la artillera: +1 turbo, -1 freno (0 = ninguno). */
	TORTUNABO_API bool QuickFromAxis(float Axis, ETNRallyQuickCall& OutQuick);

	/**
	 * Notas de la pista y arco del buggy en el eje de las notas (como el copiloto automático). Lo usan la artillera para
	 * elegir la próxima nota y el servidor para validarla. Sin estado replicado: cada máquina lo calcula con su pista.
	 */
	struct TORTUNABO_API FNoteFollower
	{
		/** Rehace las notas si la pista es otra o ha cambiado. False si la pista aún no está lista. */
		bool Sync(const ATN_RallyTrack& Track);
		/** Arco de Location en el eje de las notas (sigue el anterior; si se pierde, lo busca en toda la pista). */
		double NoteArcAt(const FVector& Location);
		/** Notas que empiezan entre el buggy (en Location) y MaxNoteAheadCm por delante. */
		TArray<TNRallyPaceNotes::FNoteAhead> NotesAheadOf(const FVector& Location);

		TNRallyPaceNotes::FTrackNotes Notes;

	private:
		TWeakObjectPtr<const ATN_RallyTrack> Track;
		float TrackLengthCm = 0.f;
		double TrackArcCm = 0.0;
		bool bHasArc = false;
	};

	/** Solo servidor: manda Call a las ocupantes humanas de Buggy (la conductora y la artillera). */
	TORTUNABO_API void SendToCrew(ATN_Buggy& Buggy, const FTNRallyCrewCall& Call);
}
