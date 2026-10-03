// Confirmación de impactos del Rally (#332). El proyectil lo resuelve el servidor; con este aviso, las dos ocupantes del
// buggy que dispara saben que han dado (marca en la mira 0,3 s y un sonido corto) y las del buggy alcanzado, quién les da.
// Las dos lo ven en el registro de la pantallita (las 3 últimas líneas). El servidor manda el aviso por una RPC de cliente
// fiable de ATN_RallyPlayerController a cada ocupante humana; las líneas y la marca son lógica pura (tests
// Tortunabo.Rally.HitReport.*).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyVehicle.h"
#include "TN_RallyHitReport.generated.h"

class APawn;
class ATN_Buggy;

/** Zona del buggy alcanzado (TNRallyCombat::EHitZone, replicable). */
UENUM()
enum class ETNRallyHitZone : uint8
{
	Front,
	Side,
	Rear
};

/** Un impacto visto desde un buggy: lo ha dado (bOutgoing) o lo ha recibido. */
USTRUCT()
struct FTNRallyHitReport
{
	GENERATED_BODY()

	UPROPERTY()
	ETNRallyAmmo Ammo = ETNRallyAmmo::Coco;

	UPROPERTY()
	ETNRallyHitZone Zone = ETNRallyHitZone::Side;

	/** true: lo ha disparado el buggy propio; false: le han dado al buggy propio. */
	UPROPERTY()
	bool bOutgoing = true;

	/** El escudo de burbuja lo ha anulado. */
	UPROPERTY()
	bool bBlocked = false;

	/** El otro buggy (el alcanzado o el que dispara); nulo si se ha dado a sí mismo (mortero). */
	UPROPERTY()
	TObjectPtr<APawn> OtherVehicle = nullptr;
};

namespace TNRallyHitLog
{
	/** Líneas del registro de la pantallita. */
	inline constexpr int32 MaxLines = 3;
	/** Lo que dura la marca de acierto en la mira (s). */
	inline constexpr float MarkerSeconds = 0.3f;
	/** Una línea se apaga en la pantallita pasado este tiempo (s). */
	inline constexpr float LineSeconds = 12.f;

	struct FLine
	{
		FText Text;
		FLinearColor Color = FLinearColor::White;
		/** Segundos que lleva en el registro. */
		float Age = 0.f;
	};

	/** Nombre corto de la munición («Coco», «Tinta»…). */
	TORTUNABO_API FText AmmoName(ETNRallyAmmo Ammo);

	/** «morro», «lateral» o «cola». */
	TORTUNABO_API FText ZoneName(ETNRallyHitZone Zone);

	/**
	 * Línea del registro: «Coco → Rojo · morro» al dar y «Te da Rojo: Tinta · cola» al recibir («Te dan: Tinta · cola» si
	 * no se sabe quién). Si el escudo lo anula, «escudo» en vez de la zona.
	 */
	TORTUNABO_API FText LineFor(const FTNRallyHitReport& Report, const FText& OtherName);

	/** Registro con Line añadida arriba: como mucho MaxLines, la más nueva primero. */
	TORTUNABO_API TArray<FLine> Push(const TArray<FLine>& Lines, const FLine& Line);

	/** Registro envejecido Dt segundos, sin las líneas que pasan de LineSeconds. */
	TORTUNABO_API TArray<FLine> Age(const TArray<FLine>& Lines, float Dt);

	/** La marca de la mira y el sonido de acierto: solo en el buggy que dispara (al recibir ya se ve y se oye el golpe). */
	TORTUNABO_API bool ShowsMarker(const FTNRallyHitReport& Report);

	/**
	 * Solo servidor: avisa a las ocupantes humanas de Shooter (si no es Victim) y de Victim de un impacto de Ammo en
	 * WorldPoint. Sin buggy que dispara, solo a las de Victim.
	 */
	TORTUNABO_API void NotifyServer(ATN_Buggy* Shooter, ATN_Buggy* Victim, ETNRallyAmmo Ammo, const FVector& WorldPoint, bool bBlocked);
}
