#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Multiplayer/TN_RichPresenceRules.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TN_RichPresenceSubsystem.generated.h"

/**
 * @brief Presencia de Steam: la lista de amigos enseña dónde está cada tortuga («En el menú», «En el lobby (3/8)», «Coop,
 *        nivel 2», «Carrera, ronda 4», «Supervivencia, nivel 5»).
 *
 * No depende de ningún evento: cada PollSeconds mira el GameState del mundo actual (la clase del GameMode, que replica a los
 * invitados; la ronda o el nivel; las plazas de la sala) y, si el texto ha cambiado, lo manda con IOnlinePresence. El
 * subsistema de Steam vuelve a poner «connect» con la sesión, así que «Unirse a la partida» desde la lista de amigos sigue
 * pasando por la sala (cierre y plazas en HandleGameModePreLogin).
 *
 * Sin Steam (subsistema NULL, Steam cerrado o sin sesión iniciada) no manda nada: solo apunta el cambio en el log. Con el
 * AppID 480 de pruebas las claves #TN_* no existen en Steamworks y la lista solo dice el juego; el texto «status» se ve en
 * «Ver información del juego». Las reglas están en TN_RichPresenceRules.h.
 */
UCLASS()
class TORTUNABO_API UTN_RichPresenceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** El estado del mundo actual; vacío mientras no hay GameState (al arrancar o cambiando de mapa). */
	TOptional<FTNPresenceState> ReadState() const;

	/** Segundos entre dos lecturas del estado. */
	static constexpr float PollSeconds = 2.f;

private:
	bool Poll(float DeltaTime);

	/** Manda la presencia a Steam. false si no hay Steam o la sesión no está iniciada (se reintenta en la siguiente lectura). */
	bool Push(const FTNPresenceInfo& Info) const;

	FTSTicker::FDelegateHandle PollHandle;

	/** Lo último que se mandó (FTNPresenceInfo::Signature) y lo último que se apuntó en el log. */
	FString LastPushed;
	FString LastLogged;
};
