#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Menu/TN_RoomMenuWidget.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "MP_MainMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * @brief Widget del menú principal (WBP_MainMenuWidget: botones Host / Find / Quit y log de status).
 *
 * Primero se elige cómo jugar (#311), con los mismos botones del Blueprint: «Local» (hasta cuatro en este PC a pantalla
 * partida, sin conexión: UMP_GameInstance::StartLocalGame, el jugador 1 va derecho al lobby y los mandos se unen allí con
 * Start) u «Online» (lo de siempre: salas, Steam, hasta ocho), «Ajustes» y «Salir». «Online» cambia los rótulos a los de
 * abajo y «Salir» pasa a ser «Volver» (también Escape o B).
 *
 * Los tres botones del Blueprint (mismo estilo visual; aquí solo cambian sus textos): «Crear partida», «Unirse» y «Salir».
 * Crear y Unirse abren las pantallas de salas (UTN_RoomMenuWidget, montadas en código encima de este widget, que se
 * esconde mientras tanto; Docs/Salas.md):
 *  - Crear partida: modo (Cooperativo o Carrera), pública o privada, 4, 6 u 8 plazas, nombre al azar y, en la privada,
 *    su código. El modo va a UMP_GameInstance::SelectedProcMode (HostRoom) y sobrevive al viaje.
 *  - Unirse: con un código o de la lista de salas públicas. Unirse no toca el modo (lo decide el anfitrión).
 *
 * Un cuarto botón, «Ajustes», se monta en código entre «Unirse» y «Salir» (copia el aspecto y la colocación de «Unirse»; el
 * Blueprint no cambia) y abre los mismos ajustes que el menú de pausa (UTN_GameSettingsSubsystem::OpenMainMenuSettings): idioma,
 * gráficos, sonido, voz y controles. Mientras están abiertos este menú se queda a la vista pero sin recibir clics ni foco.
 *
 * Se suscribe al delegate OnStatusChanged del UMP_GameInstance para reflejar estado de sesión y errores en StatusText, y
 * al llegar enseña el aviso que haya dejado la GameInstance (expulsado, sala cerrada o llena, el anfitrión se fue...).
 */
UCLASS()
class TORTUNABO_API UMP_MainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> HostButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> FindButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> QuitButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

private:
	/** «Crear partida»: abre la pantalla de crear sala. */
	UFUNCTION()
	void OnHostClicked();

	/** «Unirse»: abre la pantalla de unirse (código y lista de salas públicas). */
	UFUNCTION()
	void OnFindClicked();

	/** «Salir»: cierra el juego. */
	UFUNCTION()
	void OnQuitClicked();

	/** «Ajustes»: abre los ajustes del juego encima de este menú. */
	UFUNCTION()
	void OnSettingsClicked();

	/** «Créditos»: abre la pantalla de créditos encima de este menú (UTN_CreditsWidget, Docs/Creditos.md). */
	UFUNCTION()
	void OnCreditsClicked();

	/** Monta el botón «Ajustes» junto a los del Blueprint (una sola vez). */
	void BuildSettingsButton();

	/** Los ajustes se han cerrado: este menú vuelve a recibir clics y el foco vuelve a «Ajustes». */
	void HandleSettingsClosed();

	UFUNCTION()
	void OnGameInstanceStatusChanged(const FString& StatusMessage);

	void SetStatus(const FString& Message);

	/** Abre una pantalla de salas (la crea la primera vez). */
	void OpenRooms(ETNRoomMenuPage Page);

	/** La pantalla de salas se abre o se cierra: este menú se esconde o vuelve (con el foco en su botón). */
	void HandleRoomsOpenChanged(bool bOpen);

	/** Estado del GameInstance (o el saludo si aún no hay nada). */
	FString BuildIdleStatus() const;

	/** Primera página: «Local» u «Online» (bOnline = false), o la de siempre de las partidas en red (true). */
	void ShowModePage(bool bOnline);

	/** true en la página de las partidas en red (Crear partida, Unirse, Ajustes, Volver). */
	bool bOnlinePage = false;

	/** Pantallas de salas (widget propio en la pantalla, encima de este). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_RoomMenuWidget> RoomMenu;

	/** Visibilidad de este menú antes de abrir las salas (para devolverla al cerrarlas). */
	ESlateVisibility VisibilityBeforeRooms = ESlateVisibility::SelfHitTestInvisible;

	/** Botón que abrió las salas o los ajustes (el foco vuelve a él). */
	TWeakObjectPtr<UButton> RoomsOpener;

	/** «Ajustes»: el botón hecho en código. */
	UPROPERTY(Transient)
	TObjectPtr<UButton> SettingsButton;

	/** «Créditos»: el botón hecho en código, entre «Ajustes» y «Salir». */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CreditsButton;

	/** Los ajustes están abiertos encima de este menú (se vigila en NativeTick hasta que se cierren). */
	bool bSettingsOpen = false;

	/** Visibilidad de este menú antes de abrir los ajustes (para devolverla al cerrarlos). */
	ESlateVisibility VisibilityBeforeSettings = ESlateVisibility::SelfHitTestInvisible;
};
