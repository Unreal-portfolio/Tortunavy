#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_TurtleActionSfx.generated.h"

class USoundBase;
class UTN_ShellImpactSynthComponent;
class UTN_ScoreShellSynthComponent;

/**
 * Sonidos de acción de la tortuga (Docs/Sonido_Tortuga.md, «Acciones»). Cada uno es un UPROPERTY EditDefaultsOnly de
 * ATortugaCharacter que el Blueprint puede cambiar; si queda vacío, los que tienen sustituto los toca
 * UTN_TurtleActionSynthComponent con los sintetizadores del proyecto.
 */
enum class ETNTurtleActionSfx : uint8
{
	Knockdown,
	Kill,
	Pickup,
	Throw,
	Consume,
	ReviveSuccess,
	DBNOHeartbeat,
	TotemSelfRevive,
	Count
};

namespace TNTurtleActionSfx
{
	/** Nombre del UPROPERTY de ATortugaCharacter que guarda el sonido del evento. */
	TORTUNABO_API FName PropertyName(ETNTurtleActionSfx Sfx);

	/** Recurso del proyecto que lleva de serie (cadena vacía si no hay ninguno y suena el sintetizador). */
	TORTUNABO_API const TCHAR* DefaultAssetPath(ETNTurtleActionSfx Sfx);

	/** true si, con el UPROPERTY vacío, el evento suena igualmente con UTN_TurtleActionSynthComponent. */
	TORTUNABO_API bool HasSynthFallback(ETNTurtleActionSfx Sfx);

	/** Solo dentro de un constructor: el recurso de serie del evento (null si no tiene o no se encuentra). */
	TORTUNABO_API USoundBase* FindDefaultSound(ETNTurtleActionSfx Sfx);

	/**
	 * Suena Sound una vez en Location. Si el recurso no trae atenuación propia, se le pone la natural (pleno hasta
	 * InnerRadius cm, silencio en OuterRadius): sin ella el motor lo tocaba en 2D y lo oía todo el mapa igual de fuerte.
	 * Con Source, el rango de voz de quien lo hace (TNAudioVoices::RankForOwner): la tortuga propia nunca se queda sin voz.
	 */
	TORTUNABO_API void PlayAt(UWorld* World, USoundBase* Sound, const FVector& Location, float InnerRadius, float OuterRadius,
		const AActor* Source = nullptr);

	/**
	 * true mientras el latido sintetizado de Owner debe seguir: es una tortuga derribada y controlada en esta máquina.
	 * La misma condición con la que ATortugaCharacter::OnDBNOAudioFinished repite el latido con recurso.
	 */
	TORTUNABO_API bool ShouldKeepHeartbeat(const AActor* Owner);
}

/**
 * Sustitutos sintetizados de los sonidos de acción que no tienen archivo en el proyecto, sin archivos de audio:
 *  - Derribo: «¡clonc!» de caparazón (UTN_ShellImpactSynthComponent, timbre Otra tortuga), en 3D.
 *  - Reanimar y tótem: arpegio de campanitas (UTN_ScoreShellSynthComponent, «¡plin!»), más grave que el de las conchas
 *    de puntos para no confundirlo con ellas, en 3D.
 *  - Latido del derribo: «pom-pom» grave y en bucle (el «pom» del contador, dos octavas abajo), en 2D y solo para el
 *    jugador derribado.
 * Lo crea la tortuga la primera vez que hace falta (FindOrAddTo), solo en máquinas con audio; transitorio y local.
 */
UCLASS(ClassGroup = (Audio))
class TORTUNABO_API UTN_TurtleActionSynthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_TurtleActionSynthComponent();

	/** El de InOwner o uno nuevo. Null en servidor dedicado, sin audio o fuera de un mundo de juego. */
	static UTN_TurtleActionSynthComponent* FindOrAddTo(AActor* InOwner);

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void PlayKnockdown();
	void PlayRevive(bool bTotem);
	void StartHeartbeat();
	void StopHeartbeat();
	bool IsHeartbeatActive() const;

	/** Fuerza del «¡clonc!» del derribo (0..1). */
	UPROPERTY(EditDefaultsOnly, Category = "ActionSfx", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KnockdownStrength = 0.85f;

	/** Tono del «¡clonc!» (1 = el de serie; más bajo, más pesado). */
	UPROPERTY(EditDefaultsOnly, Category = "ActionSfx", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float KnockdownPitch = 0.85f;

	/** Transporte (semitonos) del arpegio de reanimar respecto al de las conchas de puntos. */
	UPROPERTY(EditDefaultsOnly, Category = "ActionSfx", meta = (ClampMin = "-24.0", ClampMax = "24.0"))
	float ReviveSemitones = -5.f;

	/** Segundos entre latidos. */
	UPROPERTY(EditDefaultsOnly, Category = "ActionSfx", meta = (ClampMin = "0.3"))
	float HeartbeatPeriod = 0.9f;

	/** Segundos entre el «pom» fuerte y el flojo de cada latido. */
	UPROPERTY(EditDefaultsOnly, Category = "ActionSfx", meta = (ClampMin = "0.05"))
	float HeartbeatEchoDelay = 0.18f;

	/** Volumen del latido (0..2). */
	UPROPERTY(EditDefaultsOnly, Category = "ActionSfx", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HeartbeatVolume = 0.9f;

private:
	void Beat();
	void BeatEcho();
	UTN_ShellImpactSynthComponent* GetImpactSynth();
	UTN_ScoreShellSynthComponent* GetChimeSynth();
	UTN_ScoreShellSynthComponent* GetHeartSynth();

	UPROPERTY(Transient)
	TObjectPtr<UTN_ShellImpactSynthComponent> ImpactSynth;

	UPROPERTY(Transient)
	TObjectPtr<UTN_ScoreShellSynthComponent> ChimeSynth;

	UPROPERTY(Transient)
	TObjectPtr<UTN_ScoreShellSynthComponent> HeartSynth;

	FTimerHandle HeartbeatTimer;
	FTimerHandle HeartbeatEchoTimer;
};
