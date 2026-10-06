#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "TN_FlipperSlapComponent.generated.h"

class ATortugaCharacter;
class UTN_RaceItemSynthComponent;

/**
 * Guantazo con la aleta (#832, #709): con el botón de ataque (la E) sin objeto ni arma en las aletas, un golpe muy rápido
 * (TNFlipperSlap::SwingSeconds) que no corta el movimiento y que deja mareado medio segundo (el mareo de siempre) y da un
 * empujoncito, sin derribo, a la tortuga que haya delante. Vale en todos los modos; en Todos contra Todos es el ataque sin
 * arma. Las cuentas (tiempos, alcance, cono, empujón) están en Player/TN_FlipperSlapRules.h.
 *
 * Red:
 *  - Quien golpea (el dueño, o el anfitrión con la suya) mueve la aleta y suena el soplido al pulsar (TrySlap), sin esperar al
 *    servidor, y le pide el golpe (ServerSlap).
 *  - El servidor decide todo lo demás (ResolveOnServer): que se pueda, la espera entre golpes, la tortuga a la que da (la más
 *    cercana y mejor centrada en el cono hacia donde mira la cámara, sin escenario de por medio), su mareo y su empujón.
 *  - Después avisa a todas las máquinas con pantalla (MulticastSlap): los demás empiezan la aleta y el soplido; el dueño ya
 *    los llevaba. El destello y el «¡clap!» salen en el sitio del golpe en el momento del impacto del movimiento (o ya, si
 *    el aviso llega más tarde).
 * Todo lo cosmético es local y sin archivos: el soplido y el golpe, del sintetizador de los objetos de carrera
 * (UTN_RaceItemSynthComponent), y el destello, el estallido de estrellitas de ATN_RaceBurstFX.
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_FlipperSlapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_FlipperSlapComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Quien la controla: da el guantazo ya si puede (no hay espera ni nada se lo impide) y lo pide al servidor. true si lo
	 * ha dado. No hace nada en las máquinas que no la controlan.
	 */
	bool TrySlap();

	/** Fase (0..1) del golpe en curso en esta máquina; -1 si no hay. La lee UTN_TurtleAnimInstance para mover la aleta. */
	float GetSwingPhase() const;

	/** Si la tortuga puede dar un guantazo ahora (viva, en pie, fuera del caparazón y del agua, sin llevar a nadie...). */
	static bool CanSlap(const ATortugaCharacter* Turtle);

	/** Si un guantazo puede mover ahora a Turtle (viva, en pie, fuera del caparazón y de brazos ajenos, no invulnerable). */
	static bool CanBeSlapped(const ATortugaCharacter* Turtle);

private:
	/** El dueño pide el golpe; el servidor lo decide. Fiable: uno por pulsación, con la espera entre medias. */
	UFUNCTION(Server, Reliable)
	void ServerSlap();

	/** El servidor avisa a todas las máquinas: bHit si dio a alguien (en ImpactPoint, donde sale el destello). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSlap(bool bHit, FVector_NetQuantize10 ImpactPoint);

	ATortugaCharacter* GetTurtle() const;

	/** Servidor: elige a quién da, lo marea y lo empuja, y avisa a todos. Now = hora del servidor. */
	void ResolveOnServer(double Now);

	/** Mueve la aleta (la fase empieza en Now) y suena el soplido. */
	void BeginSwing(double Now);

	/** Destello y golpe en Point cuando toca el impacto de este movimiento (o ya, si ese momento ha pasado). */
	void ScheduleImpact(const FVector& Point);

	void PlayImpact(FVector Point);

	/** Sintetizador de la tortuga: se crea la primera vez; null en servidor dedicado o sin audio. */
	UTN_RaceItemSynthComponent* GetSynth();

	/** Hora (del mundo, en esta máquina) a la que empezó el último golpe que se ve aquí. */
	double SwingStart = -100.0;

	/** Servidor: hora a la que empezó el último golpe que aceptó. */
	double LastServerSlap = -100.0;

	FTimerHandle ImpactTimer;

	UPROPERTY(Transient)
	TObjectPtr<UTN_RaceItemSynthComponent> Synth;
};
