#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "VR/TN_VRMode.h"
#include "TN_VRSubsystem.generated.h"

class ATN_VRRig;
class IInputProcessor;
class SWidget;
class UGameViewportClient;
class UTexture;
class UTexture2D;
class UUserWidget;

/**
 * Modo VR de Tortunavy (Docs/Modo_VR.md). Cada fotograma decide el modo (ETNVRMode) y, con VR, mantiene en el mundo actual
 * un ATN_VRRig para el jugador local: primera persona, aletas con seguimiento, puntero, HUD y menús en el mundo.
 *
 * Cómo se elige el modo, de más a menos fuerte:
 * - Consola TN.VR: -1 (de serie: lo de abajo), 0 apagado, 1 gafas (enciende el HMD si hace falta), 2 simulado sin gafas.
 * - Línea de comandos: -novr (apagado) o -vrsim (simulado). Con -vr el motor enciende las gafas y entra el modo gafas.
 * - Ajuste «Modo VR» del menú (Ajustes > Juego > Realidad virtual): Automático, Desactivado o Simulado.
 * - Automático: gafas si el motor pinta en estéreo (VR Preview del editor, -vr, la build de Quest); si no, apagado.
 *
 * Consola: TN.VR, TN.VR.Status, TN.VR.Recenter, TN.VR.HudDistance, TN.VR.HudFov, TN.VR.MenuDistance, TN.VR.MenuFov,
 * TN.VR.SmoothTurnSpeed.
 */
UCLASS()
class TORTUNABO_API UTN_VRSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UTN_VRSubsystem* Get(const UObject* WorldContext);

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

	ETNVRMode GetMode() const { return Mode; }

	/** El rig del mundo World; con bCreate lo crea si hay VR y es un mundo de juego. */
	ATN_VRRig* GetRig(UWorld* World, bool bCreate);

	/**
	 * Aloja un widget en el panel VR del mundo del widget; false sin VR o sin mundo de juego (entonces va al viewport).
	 * bPlayerScreen: es el de un jugador (TNVR::AddToScreen); al apagar la VR vuelve a su trozo de la pantalla partida (#639).
	 */
	bool HostWidget(UUserWidget* Widget, int32 ZOrder, bool bPlayerScreen = false);

	/** ¿Está el widget en el panel VR del mundo actual? */
	bool IsHostedWidget(const UUserWidget* Widget) const;

	bool HostSlate(UGameViewportClient* Viewport, const TSharedRef<SWidget>& Widget, int32 ZOrder);
	bool UnhostSlate(const TSharedRef<SWidget>& Widget);

	/** ¿Hay un menú delante (panel quieto y puntero encendido)? Lo consulta el procesador de entrada VR. */
	bool IsMenuMode() const;

	/** El rig activo del mundo actual (o nullptr). */
	ATN_VRRig* GetActiveRig() const;

	/** La vista vuelve a mirar al frente desde donde está ahora la cabeza (gafas); el HUD se vuelve a poner delante. */
	void Recenter();

	/**
	 * Pantalla de carga de las gafas: el huevo pintado en una textura, en una capa del compositor, dentro de una playa en 360
	 * (un cubo de capas con el cielo, el horizonte, el mar y la arena), como si se estuviera en un mapa.
	 */
	void ShowLoadingSplash(UTexture* Texture);
	void HideLoadingSplash();

	/** Texto de estado para TN.VR.Status y el registro. */
	FString DescribeStatus() const;

private:
	ETNVRMode ResolveMode();
	void ApplyMode(ETNVRMode NewMode);
	void ApplyComfortSettings(bool bHeadset);
	void EnsureInputProcessor(bool bWanted);

	ETNVRMode Mode = ETNVRMode::Off;
	bool bTriedEnableHMD = false;
	bool bSplashShown = false;

	TWeakObjectPtr<ATN_VRRig> Rig;
	TSharedPtr<IInputProcessor> InputProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> SplashTexture;

	/** Playa en 360 alrededor de la capa de carga: degradado de cielo, mar y arena en las caras de un cubo. */
	void EnsureSplashEnvironment();

	/** Texturas del cubo de la carga: 0 los lados (cielo → horizonte → mar), 1 arriba (cielo), 2 abajo (arena). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> SplashEnvironment;

	/** Variables de consola cambiadas para las gafas (sin desenfoque de movimiento...): el valor de antes, para devolverlo. */
	TMap<FString, FString> SavedCVars;
};
