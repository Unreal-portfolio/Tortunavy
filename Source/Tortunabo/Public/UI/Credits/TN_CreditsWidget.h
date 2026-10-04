#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TN_CreditsWidget.generated.h"

class APlayerController;
class UButton;
class UScrollBox;
class UTexture2D;
class UTN_PauseRow;
class UWidgetTree;

/**
 * @brief Créditos de Tortunavy (Docs/Creditos.md): equipo, arte de terceros, fuentes tipográficas con su licencia y el aviso del
 * motor, leídos de Content/Credits/Credits.json (TNCredits::LoadFile). Montado en código con el estilo del menú de pausa.
 *
 * Dos formas:
 *  - Página del menú de pausa (CreatePage): solo la tarjeta con la lista. Escape, B, Tab y Start los atiende el menú de pausa.
 *  - Pantalla completa encima del menú principal (OpenOver): velo, cinta «CRÉDITOS», la tarjeta, «Volver» y la ayuda. El menú
 *    que la abre se esconde mientras tanto y, al cerrarla, vuelve con el foco en el botón que la abrió.
 *
 * La lista no tiene filas enfocables: el foco se queda en este widget, que desplaza la lista con las flechas, la cruceta, W y
 * S, los sticks (desplazamiento continuo), Re Pág y Av Pág o LB y RB, e Inicio y Fin; la rueda del ratón la mueve la propia lista.
 */
UCLASS()
class TORTUNABO_API UTN_CreditsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Página dentro de otro menú (el de pausa). */
	static UTN_CreditsWidget* CreatePage(UUserWidget* OwnerMenu);

	/**
	 * Pantalla completa encima de Menu (puede ser nulo), con el foco. Menu se esconde hasta que se cierre y entonces el foco vuelve
	 * a FocusBack.
	 */
	static UTN_CreditsWidget* OpenOver(APlayerController* PC, UUserWidget* Menu, UWidget* FocusBack);

	/**
	 * Botón «Créditos» para un menú hecho en Blueprint: copia el aspecto de StyleSource y se coloca justo delante de Before en su
	 * caja de botones (o abajo en el centro si los botones no están en una caja). Se llama «CreditsButton».
	 */
	static UButton* AddMenuButton(UWidgetTree* Tree, UButton* StyleSource, UButton* Before);

	/** Icono del botón de la portada del menú de pausa (estrella de tinta, como los demás). */
	static UTexture2D* MenuIcon();

	/** Foco en la lista (desplazamiento con teclado y mando). */
	void FocusList();

	/** Cierra la pantalla completa (devuelve el menú de debajo). En una página no hace nada: la cierra su menú. */
	void Close();

	/** Desplaza la lista Delta unidades (positivo, hacia abajo). */
	void ScrollBy(float Delta);

	/** Secciones que se han cargado (0 si el fichero falta o está mal). */
	int32 GetSectionCount() const { return SectionCount; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;

private:
	/** Monta el árbol (una vez) para la forma elegida. */
	void BuildTree(bool bInStandalone);
	UWidget* BuildCard(float ListHeight);
	void FillList();
	void AddHeader(const FText& Title);
	void AddEntryRow(const FText& Name, const FText& Value, const FText& Value2);
	void AddNote(const FText& Note, int32 FontSize);
	/** Teclas de desplazamiento (y las que aquí no hacen nada, para que no se escapen): true si la ha atendido. */
	bool HandleScrollKey(const FKey& Key);

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> List;

	/** «Volver» (solo la pantalla completa). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> BackRow;

	bool bStandalone = false;
	bool bBuilt = false;
	bool bClosing = false;
	int32 SectionCount = 0;

	/** Menú escondido mientras está abierta la pantalla completa, cómo estaba y adónde vuelve el foco. */
	TWeakObjectPtr<UUserWidget> HiddenMenu;
	ESlateVisibility HiddenMenuVisibility = ESlateVisibility::SelfHitTestInvisible;
	TWeakObjectPtr<UWidget> FocusBackTarget;

	/** Inclinación vertical del stick (−1 abajo, 1 arriba): desplaza la lista cada fotograma. */
	float StickScroll = 0.f;
};
