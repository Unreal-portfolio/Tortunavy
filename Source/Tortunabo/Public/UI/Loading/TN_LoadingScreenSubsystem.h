#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TN_LoadingScreenSubsystem.generated.h"

class STN_EggLoadingScreen;
class STN_GoBanner;
class SWidget;
class UGameViewportClient;
struct FWorldContext;

namespace TNEggAudio
{
	// Parámetros atómicos compartidos con el generador de audio (TN_LoadingScreenSubsystem.cpp).
	struct FEggSharedParams;
}

namespace TNEggLoading
{
	/** Por qué está el huevo cerrado antes de que empiece un viaje (mientras tanto no se rompe). */
	enum class EHold : uint8
	{
		None,
		/** Host, Join, vuelta al menú... (UMP_GameInstance::ShowLoadingScreen); HideLoadingScreen lo cancela. */
		Travel,
		/** Todos listos en los huevos del lobby: se abre si la cuenta atrás se cancela. */
		Lobby,
		/** Último segundo de los resultados, antes de volver al cuartel. */
		Results
	};
}

/**
 * Sonidos del huevo, sintetizados en tiempo real (sin archivos): el «¡clac!» de las mitades al cerrarse (golpe grave
 * con chasquido y rebote), crujidos de cáscara (ruido filtrado con chasquidos y un poco de cuerpo), el «¡pum!» (golpe
 * grave que cae de tono, con soplido y chasquido) y el «fiuu» de las mitades al salir despedidas. 2D: suena en la
 * interfaz.
 */
UCLASS(ClassGroup = (Audio))
class TORTUNABO_API UTN_EggSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_EggSynthComponent(const FObjectInitializer& ObjectInitializer);

	/** Un crujido (Strength 0-1). */
	void PlayCrack(float Strength);

	/** El «¡pum!» al reventar. */
	void PlayPop();

	/** El «¡clac!» de las dos mitades al juntarse (Strength 0-1). */
	void PlayKnock(float Strength);

	/** Soplido que sube de tono: las mitades salen despedidas o se retiran (Strength 0-1). */
	void PlayWhoosh(float Strength);

	/**
	 * Arranca el sintetizador si estaba parado y lo mantiene en marcha IdleStopSeconds; los Play* lo llaman solos. Antes
	 * se arrancaba una vez y no se paraba nunca: el del mando ocupaba una voz del mezclador toda la partida (#737). 2D
	 * (interfaz) con voz reservada; con espacialización (el huevo del fantasma en el mundo), como el resto del mundo.
	 */
	void KeepAwake();

	/** Segundos sin ningún sonido nuevo tras los que se para (el más largo, el «¡pum!», dura menos de un segundo). */
	static constexpr float IdleStopSeconds = 3.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	TSharedPtr<TNEggAudio::FEggSharedParams, ESPMode::ThreadSafe> SharedParams;
	float IdleLeft = 0.f;
};

/**
 * Pantalla de carga del huevo (Tortunavy): el huevo ocupa la pantalla entera. Sus dos mitades entran desde arriba y
 * desde abajo, se cierran tapándolo todo y, cuando el mapa está listo (partida empezada, tortuga propia y, en el mapa
 * procedural, terreno generado en esta máquina), el huevo tiembla, se agrieta y revienta con crujidos y un «¡pum!»
 * sintetizados; las mitades salen despedidas y dejan ver la partida.
 *
 * Cuándo se cierra:
 * - Al pedir un viaje (UMP_GameInstance::ShowLoadingScreen, que llaman Host, Join y la vuelta al menú): CloseForTravel
 *   lo cierra en el acto y lo mantiene cerrado hasta que el viaje acaba; HideLoadingScreen (sesión fallida, sin
 *   partidas...) lo vuelve a abrir si el viaje no llegó a empezar. RunWhenClosed deja salir el viaje cuando ya está
 *   cerrado del todo, para que el LoadMap no congele el cierre a medias.
 * - En el lobby, cuando todos están en los huevos y empieza la cuenta atrás (ATN_CoopGameState::CountdownValue > 0 o
 *   MatchFlowState en Countdown o Cinematic): se cierra en todas las pantallas y, si la cuenta atrás se cancela, se abre.
 * - En los mapas de partida, en el último segundo de los resultados (vuelta al cuartel).
 * - En cualquier LoadMap (PreLoadMapWithContext) y viaje sin cortes (FWorldDelegates::OnSeamlessTravelStart).
 *
 * Durante el viaje la pantalla no se ve nunca: LoadMap bloqueante fuera del editor → MoviePlayer pinta el mismo huevo
 * (misma semilla y mismo origen de tiempos) en su hilo; en PIE se pinta un fotograma con el huevo cerrado antes de que
 * el LoadMap congele el viewport. En el viaje sin cortes el hilo de juego sigue vivo y el huevo se anima todo el rato.
 *
 * Mapa procedural (GameState ATN_ProcMapGameState, o uno cooperativo con el GameMode ATN_ProcMapGameMode): con el mapa
 * listo en esta máquina el huevo sigue cerrado («Preparando la salida», «Esperando a las demás tortugas») hasta que
 * empieza la ronda (MatchFlowState == InProgress o bRoundInProgress; como mucho 40 s) y entonces se rompe con
 * «¡ADELANTE!» y una frase de ánimo en lugar del «¡PUM!». En las rondas siguientes (el mapa se regenera sin viajar y no
 * hay huevo) sale el mismo rótulo solo, encima del juego (STN_GoBanner), cada vez que la ronda vuelve a InProgress.
 *
 * Pruebas por consola: TN.Loading.Test (se cierra y se rompe a los 3 s), TN.Loading.Test.Close (se cierra y espera;
 * TN.Loading.Test.Hold hace lo mismo), TN.Loading.Test.Break (rompe el que esté a la vista), TN.Loading.Test.Open (lo
 * abre sin romperlo), TN.Loading.Test.Go (se cierra y se rompe con «¡ADELANTE!») y TN.Loading.Test.GoOnly (el rótulo
 * solo, sin huevo).
 */
UCLASS()
class TORTUNABO_API UTN_LoadingScreenSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
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

	/**
	 * Cierra el huevo en el acto (con su animación) para un viaje que está a punto de empezar y lo mantiene cerrado hasta
	 * que el viaje acabe y el mapa nuevo esté listo. Si ya se está viajando, solo cambia el texto. Devuelve false si no
	 * hay huevo que enseñar (servidor dedicado, sin Slate).
	 */
	bool CloseForTravel(const FString& InStatus);

	/** Si el huevo se cerró para un viaje que no ha llegado a empezar, se abre otra vez (sin romperse). */
	void CancelPendingClose();

	/**
	 * Ejecuta Action cuando el huevo está cerrado del todo y quieto (en el acto si ya lo está o si no hay huevo; como
	 * mucho 1,5 s después). Para lanzar el viaje sin que el LoadMap congele el cierre a medias.
	 */
	void RunWhenClosed(TFunction<void()> Action);

	/** Enseña el huevo (cerrándose, o ya cerrado con bStartClosed) con ese texto de estado. */
	void BeginLoading(const FString& InStatus, bool bStartClosed = false);

	/** Cambia el texto de estado (sin puntos suspensivos: se animan solos). */
	void SetStatus(const FString& InStatus);

	/**
	 * Rompe el huevo ya, esté o no listo el mapa; con bWithGo, con «¡ADELANTE!» y una frase de ánimo en vez del «¡PUM!»
	 * (salida de la ronda del mapa procedural).
	 */
	void BreakNow(bool bWithGo = false);

	/** Abre el huevo sin romperlo (las mitades se retiran por donde vinieron). */
	void OpenNow();

	/** Mantiene el huevo cerrado hasta BreakNow u OpenNow (pruebas). */
	void SetHold(bool bInHold) { bHold = bInHold; }

	/** Rompe solo pasados unos segundos, sin mirar el mapa ni la ronda (pruebas); con bWithGo, con «¡ADELANTE!». */
	void BreakAfter(float Seconds, bool bWithGo = false);

	/**
	 * Enseña «¡ADELANTE!» solo, sin huevo, encima del juego y sin bloquear los clics: sale pasados DelaySeconds, se queda
	 * unos 3 s y se quita solo. Es lo que sale al empezar las rondas siguientes del mapa procedural.
	 */
	void ShowGoBanner(float DelaySeconds = 0.f);

	/** true mientras el huevo está en pantalla (cerrándose, cerrado, abriéndose o rompiéndose, «¡ADELANTE!» incluido). */
	bool IsShowing() const { return Screen.IsValid(); }

	/** Texto de estado amable para un mapa («Rumbo al cuartel», «Incubando la partida»...). */
	static FString FriendlyStatusForMap(const FString& MapName);

private:
	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandleSeamlessTravelStart(UWorld* World, const FString& MapName);

	/** Cierres automáticos: cuenta atrás del lobby (y su cancelación) y final de los resultados. */
	void UpdateAutoClose(UWorld* World, double Now);
	void CloseAndHold(TNEggLoading::EHold Reason, const FString& InStatus);
	void FlushWhenClosed(bool bForce);

	void AddToViewport();
	void RemoveFromViewport();
	void Hide();
	bool IsWorldReady(UWorld* World, double Now) const;
	static bool IsLobbyWorld(const UWorld* World);
	static FString CleanStatus(const FString& InStatus);
	void TickCloseSound(double Now);
	void TickBreakSounds();
	UTN_EggSynthComponent* EnsureSynth();

	/** Mapa procedural: con el mapa ya listo, el huevo espera a la ronda (o la rompe con «¡ADELANTE!»). */
	void TickRoundGate(UWorld* World, double Now);
	/** Texto de la espera a la ronda («Preparando la salida», «Esperando a las demás tortugas»). */
	void ShowRoundWaitStatus(const UWorld* World);
	/** Mira si la ronda del mapa procedural acaba de empezar (rondas siguientes: «¡ADELANTE!» sin huevo). */
	void UpdateRoundWatch(UWorld* World);
	void HandleRoundStarted();
	/** Sonido de entrada y retirada de «¡ADELANTE!» sin huevo. */
	void TickGoBanner(double Now);
	void RemoveGoBanner();

	TSharedPtr<STN_EggLoadingScreen> Screen;
	TSharedPtr<SWidget> ScreenInViewport;
	TWeakObjectPtr<UGameViewportClient> ViewportUsed;
	/** «¡ADELANTE!» sin huevo (rondas siguientes), el viewport donde está y si ya sonó su entrada. */
	TSharedPtr<STN_GoBanner> GoBanner;
	TWeakObjectPtr<UGameViewportClient> GoBannerViewport;
	bool bGoBannerCuePlayed = false;

	/** Mundo cuya ronda se vigila y si su ronda estaba en juego en el fotograma anterior. */
	TWeakObjectPtr<UWorld> RoundWatchWorld;
	bool bRoundWasLive = false;

	UPROPERTY(Transient)
	TObjectPtr<UTN_EggSynthComponent> Synth;

	FDelegateHandle PreLoadHandle;
	FDelegateHandle PostLoadHandle;
	FDelegateHandle SeamlessStartHandle;

	/** Cerrado a la espera de un viaje que aún no ha empezado (y desde cuándo). */
	TNEggLoading::EHold HoldReason = TNEggLoading::EHold::None;
	double HoldStartTime = 0.0;
	/** La cuenta atrás del lobby lleva cancelada desde este momento (< 0: no lo está). */
	double LobbyCancelSince = -1.0;
	/**
	 * El lobby ya pasó a la pausa de antes del viaje (Cinematic): el huevo no se abre aunque la cuenta atrás parezca
	 * cancelada (el servidor destruye las tortugas justo antes de viajar); si el viaje no llega, lo abre el tope de la
	 * espera.
	 */
	bool bLobbyTravelImminent = false;
	/** Último texto puesto por los cierres automáticos o la espera a la ronda (para no rehacerlo en cada fotograma). */
	FString LastAutoStatus;

	/** Acciones que esperan a que el huevo esté cerrado del todo (RunWhenClosed) y desde cuándo. */
	TArray<TFunction<void()>> PendingWhenClosed;
	double PendingSince = 0.0;

	/** Entre PreLoadMap y PostLoadMap (LoadMap bloqueante) y desde cuándo. */
	bool bInHardLoad = false;
	double HardLoadStartTime = 0.0;
	/** Primera carga del juego: el texto «Cargando» no lo cambia el aviso del GameInstance («Volviendo al menú»). */
	bool bKeepLoadStatus = false;
	bool bHold = false;
	/** Momento (FPlatformTime) en que el mapa terminó de cargar; < 0 mientras sigue viajando. */
	double LoadDoneTime = -1.0;
	/** Momento en que el mapa quedó listo en esta máquina (o se agotó su espera); desde aquí cuenta la de la ronda. */
	double WorldReadyTime = -1.0;
	/** Rotura programada (pruebas); < 0 = ninguna. Con bBreakAtWithGo, con «¡ADELANTE!». */
	double BreakAtTime = -1.0;
	bool bBreakAtWithGo = false;
	/** Sonidos de la rotura ya disparados (bit por grieta y por golpe). */
	uint32 FiredBreakCues = 0;
	/** Movimiento del huevo cuyo «¡clac!» ya sonó. */
	int32 FiredKnockSerial = -1;
	FString LoadingMapName;
};
