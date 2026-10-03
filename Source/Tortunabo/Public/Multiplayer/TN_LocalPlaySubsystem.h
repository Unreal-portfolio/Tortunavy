#pragma once

#include "CoreMinimal.h"
#include "Engine/ViewportSplitScreen.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TN_LocalPlaySubsystem.generated.h"

class APlayerController;
class IInputProcessor;
class UGameViewportClient;
class ULocalPlayer;
class UTN_LocalSplitOverlay;
struct FKeyEvent;

/**
 * @brief Modo local (#311): hasta cuatro jugadores en el mismo PC, a pantalla partida y sin conexión (Standalone, sin Steam
 * ni sesión). Docs/Modo_Local.md.
 *
 * - Empieza con «Local» en el menú principal (StartLocalGame): el jugador 1 va derecho al lobby con lo que usó para elegir
 *   (teclado y ratón, o ese mando). Los demás mandos quedan libres (IPlatformInputDeviceMapper: se sacan del usuario del
 *   jugador 1), así un mando nunca mueve a dos tortugas y el teclado es siempre del jugador 1.
 * - En el lobby, cualquier mando libre que pulsa Start crea su jugador local (UGameInstance::CreateLocalPlayer con el usuario
 *   de ese mando): su PlayerController, su tortuga, su cámara, su HUD y sus controles, como si entrara en la sala. Un
 *   invitado que mantiene B (TNLocalPlay::LeaveHoldSeconds) sale y su vista desaparece.
 * - Pantalla partida: 2 en horizontal y 3-4 en cuadrantes (TNLocalPlay::SplitLayout), escrito en el UGameViewportClient solo
 *   mientras dura la partida local; el cuadrante libre con tres lo tapa UTN_LocalSplitOverlay. Con 3-4 vistas baja un punto
 *   la distancia de dibujo y las sombras.
 * - Al volver al menú (EndLocalGame) quita a los invitados y deja la pantalla, los mandos y la calidad como estaban.
 *
 * Lo que es de cada jugador (ajustes, cosméticos) lo guardan UTN_GameSettingsSubsystem y UMP_GameInstance con
 * TNLocalPlay::ShouldSave: del jugador 1, como siempre; de los invitados, solo en memoria (UTN_LocalPlayerProfile).
 */
UCLASS()
class TORTUNABO_API UTN_LocalPlaySubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** El subsistema de la GameInstance de WorldContext (null en servidor dedicado o sin GameInstance). */
	static UTN_LocalPlaySubsystem* Get(const UObject* WorldContext);

	/** true en una partida local (desde «Local» en el menú principal hasta volver a él). */
	static bool IsLocalGame(const UObject* WorldContext);

	/** true si PC es el jugador 1 de esta máquina (en red, siempre que sea local: solo hay uno). */
	static bool IsPrimaryPlayer(const APlayerController* PC);

	/** true si PC es un invitado del modo local (jugadores 2 a 4). */
	static bool IsGuest(const APlayerController* PC);

	/** Número del jugador local de PC (1 a 4); 1 fuera del modo local. */
	static int32 GetPlayerNumber(const APlayerController* PC);

	/** ¿Se guarda lo que cambia PC (ajustes, controles, cosméticos, puntos, tutorial)? TNLocalPlay::ShouldSave. */
	static bool ShouldSaveFor(const APlayerController* PC);

	/** true si el mundo de WorldContext es un lobby (ATN_HQGameMode). */
	static bool IsLobbyWorld(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	bool IsLocalMode() const { return bLocalMode; }

	/** Jugadores locales ahora (vistas en pantalla). */
	int32 GetNumPlayers() const;

	/**
	 * «Local» en el menú principal: empieza la partida local y el jugador 1 viaja al lobby sin red (Map, sin ?listen). Su
	 * aparato es el último que ha tocado (UInputDeviceSubsystem): si es un mando, se lo queda; si no, todos quedan libres.
	 */
	void StartLocalGame(const FString& LobbyMap);

	/** El mando Device pide entrar (Start). true si ha entrado (TNLocalPlay::DecideJoin). */
	bool TryJoin(FInputDeviceId Device);

	/** Pruebas (TN.Local.AddGuest): un invitado sin mando, para ver la pantalla partida sin tener cuatro mandos. */
	bool AddTestGuest();

	/** Saca a un invitado de la partida (B mantenido en el lobby, «Dejar de jugar» en la pausa, TN.Local.RemoveGuest). */
	bool RemoveGuest(ULocalPlayer* Player);

	/** Saca al invitado de PC (si lo es y está en el lobby: TNLocalPlay::CanLeave). */
	bool LeaveGame(APlayerController* PC);

	/**
	 * Fin de la partida local (volver al menú): quita a los invitados (sus tortugas, sus PlayerController y sus vistas) y deja
	 * la pantalla partida, los mandos, la calidad y la escala de la interfaz como estaban. Se puede llamar aunque no lo sea.
	 */
	void EndLocalGame();

	/** Cuánto lleva el invitado Player manteniendo B para salir (0..1); negativo si no lo mantiene. */
	float GetLeaveProgress(const ULocalPlayer* Player) const;

	/** Escala de más de la interfaz por la pantalla partida (TNLocalPlay::UIScaleForViews; 1 fuera del modo local). */
	float GetSplitUIScale() const;

	/** El procesador de entrada: Start de un mando libre (entrar) y B de un invitado (salir). */
	bool HandleKeyDown(const FKeyEvent& Event);
	void HandleKeyUp(const FKeyEvent& Event);

private:
	bool bLocalMode = false;

	/** Mando con el que el jugador 1 eligió «Local» (o ninguno: teclado y ratón). */
	FInputDeviceId PrimaryPad;

	/** Mandos sacados del usuario del jugador 1 al empezar: a quién eran y a quién se dieron (para devolverlos al acabar). */
	struct FReleasedPad
	{
		FInputDeviceId Device;
		FPlatformUserId Original;
		FPlatformUserId Assigned;
	};
	TArray<FReleasedPad> ReleasedPads;

	/** Reparto de la pantalla de antes (para devolverlo) y el viewport en el que se escribió el nuestro. */
	TArray<FSplitscreenData> SavedSplitscreenInfo;
	TWeakObjectPtr<UGameViewportClient> PatchedViewport;
	int32 AppliedViews = 0;

	/**
	 * Calidad bajada con 3-4 vistas: con la calidad temporal de Scalability (ToggleTemporaryQualityLevels), así lo que se
	 * guarda en GameUserSettings.ini sigue siendo la de verdad y lo que se cambie en los gráficos mientras tanto se respeta.
	 */
	bool bQualityReduced = false;

	/** Hasta cuándo (segundos de la aplicación) se ve el número de cada jugador en su vista tras cambiar el reparto. */
	double TagsUntil = 0.0;

	/** Mandos que han pulsado Start y jugadores que se van: se atienden en Tick, fuera del reparto de la entrada de Slate. */
	TArray<FInputDeviceId> PendingJoins;
	TArray<TWeakObjectPtr<ULocalPlayer>> PendingRemovals;

	/** Mandos conectados (o reconectados) durante la partida local: se repasan en Tick. */
	TArray<FInputDeviceId> PendingConnections;

	/** Mando de cada invitado (para devolvérselo si se desconecta y vuelve). */
	TMap<FInputDeviceId, FPlatformUserId> GuestPads;

	/** Invitados manteniendo B: su usuario y desde cuándo (segundos de la aplicación). */
	struct FLeaveHold
	{
		FPlatformUserId User;
		double Since = 0.0;
	};
	TArray<FLeaveHold> LeaveHolds;

	TSharedPtr<IInputProcessor> InputProcessor;
	FDelegateHandle ConnectionChangeHandle;

	/** Lo que se pinta encima de las vistas: el cuadrante libre, «Pulsa Start para unirte» y quién está saliendo. */
	UPROPERTY(Transient)
	TObjectPtr<UTN_LocalSplitOverlay> Overlay;

	/** Saca del usuario del jugador 1 los mandos que no son el suyo (TNLocalPlay::PadsToRelease). */
	void ReleaseSecondaryPads();
	/** Devuelve los mandos sacados a su usuario de antes. */
	void RestoreReleasedPads();
	/** Un mando conectado durante la partida local que el sistema da al jugador 1 (y no es el suyo): libre. */
	void HandleDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device);

	/** Escribe el reparto de TNLocalPlay::SplitLayout en el viewport (o devuelve el de antes con bRestore). */
	void ApplySplitLayout(bool bRestore);
	/** Calidad con 3-4 vistas (o la de antes con bRestore). */
	void ApplyQuality(int32 Views, bool bRestore);
	/** Nombre «Jugador N» de cada jugador local (en el servidor, que es esta máquina). */
	void ApplyPlayerNames(UWorld* World);
	/** El cuadrante libre y los avisos, en pantalla (o fuera). */
	void UpdateOverlay(UWorld* World);
	/** Invitados que llevan el tiempo manteniendo B: fuera. */
	void TickLeaveHolds();

	/** Usuario del jugador 1 (el de su ULocalPlayer). */
	FPlatformUserId GetPrimaryUser() const;
	/** Un usuario de la plataforma sin mandos ni jugador (para dar un mando libre o un invitado de pruebas). */
	FPlatformUserId FindFreeUser() const;
	/** Atiende un mando conectado durante la partida local (PendingConnections). */
	void ProcessConnection(FInputDeviceId Device);

	/** Jugador local de un usuario de la plataforma (null si ese usuario no juega). */
	ULocalPlayer* FindPlayerForUser(FPlatformUserId User) const;
	/** true si Device es un mando (no el teclado ni el ratón). */
	static bool IsGamepadDevice(FInputDeviceId Device);
	/** Crea el jugador local del usuario User con el número siguiente; true si ha entrado. */
	bool CreateGuest(FPlatformUserId User);
};
