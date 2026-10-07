#pragma once

#include "CoreMinimal.h"
#include "UI/HUD/TN_PlayerHUDWidget.h"
#include "UI/HUD/TN_CoopFlowHUDWidget.h"
#include "UI/HUD/TN_RadialWheelWidgetBase.h"
#include "Styling/SlateBrush.h"
#include "TN_RunHUDWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UProgressBar;
class UTextBlock;
class APlayerController;
class ATortugaCharacter;
class UTN_ButtonGlyphWidget;
class UTN_HoldRingWidget;

/**
 * @brief HUD de la tortuga en partida, estilo Tortunavy (boceto para el equipo de arte), hecho en código.
 *
 * Tortugas que salen del nido y tienen que llegar al mar:
 *  - Distintivo: la cara cartoon de la tortuga (TN_HUDFaces.h) según cómo va: feliz, caparazón cerrado si se mete
 *    dentro, mareada si queda eliminada y con ojos de estrella al llegar, sobre un disco de mar con su salvavidas
 *    (M_UI_TurtleBadge, Scripts/build_ui_assets.py). La estamina no se ve en la interfaz. Debajo, una cinta con el
 *    nombre que no lo tapa. Cuando la tortuga habla por la voz de proximidad, la cara rebota y sale un bocadillo con
 *    barras de volumen.
 *  - Inventario en dos burbujas iguales: el aro de cuerda marca la que está en la aleta y rueda a la otra al cambiar.
 *    Avisos de tormenta, panza arriba y reanimación en carteles azul marino con ola.
 * Hereda toda la lógica de UTN_PlayerHUDWidget creando los widgets que esa clase enlaza por nombre (los que ella
 * rellena y aquí no se ven quedan ocultos y se leen en el Tick).
 * Vista previa de estados en consola: tn.HUD.Face y tn.HUD.Talk.
 */
UCLASS()
class TORTUNABO_API UTN_RunHUDWidget : public UTN_PlayerHUDWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void BuildTree();
	void TickBadge(float DeltaTime);
	void TickInventory(float DeltaTime);
	void TickAlerts(float DeltaTime);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UImage> Badge;
	UPROPERTY(Transient) TObjectPtr<UImage> FaceImage;
	UPROPERTY(Transient) TObjectPtr<UWidget> TalkBubble;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TalkBars;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ItemImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotTags;
	UPROPERTY(Transient) TObjectPtr<UImage> RopeImage;
	/** Chapas que lleva (#858), junto a las burbujas del inventario; oculto sin chapas. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChapaText;

	UPROPERTY(Transient) TObjectPtr<UBorder> StormBanner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StormText;
	UPROPERTY(Transient) TObjectPtr<UBorder> DownBanner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DownText;
	UPROPERTY(Transient) TObjectPtr<UBorder> ReviveBanner;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ReviveBar;
	/** Aviso de interacción: tecla y texto del interactuable al alcance. */
	UPROPERTY(Transient) TObjectPtr<UBorder> PromptCard;
	/** La tecla dibujada (con teclado) y el botón del mando (con mando, #347): solo se ve uno. */
	UPROPERTY(Transient) TObjectPtr<UBorder> PromptKeyCap;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptKeyText;
	UPROPERTY(Transient) TObjectPtr<UTN_ButtonGlyphWidget> PromptGlyph;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptLabel;
	/** Aro de progreso alrededor de la tecla en las interacciones de mantener (rebuscar un decorado). */
	UPROPERTY(Transient) TObjectPtr<UTN_HoldRingWidget> HoldRing;

	TWeakObjectPtr<UObject> LastEquippedIcon;
	TWeakObjectPtr<UObject> LastStoredIcon;
	float Time = 0.f;
	/** Cara mostrada (ETNTurtleFace de TN_HUDFaces.h) y el rebote al cambiar. */
	uint8 ShownFace = 0;
	float FacePop = 0.f;
	/** Hueco del inventario que está en la aleta (0 izquierda, 1 derecha) y posición animada del aro de cuerda. */
	int32 EquippedSide = 0;
	float RopeX = 0.f;
	/** Chapas enseñadas en ChapaText (-1 = aún ninguna). */
	int32 ShownChapas = -1;
	float PromptPop = 0.f;
	float PromptKeyTimer = 0.f;
	/** Aparato y familia del mando con los que se pintó la tecla del aviso: si cambian, se repinta al momento. */
	uint8 PromptKeyDevice = 0xFF;
	uint8 PromptKeyFamily = 0xFF;
	TWeakObjectPtr<AActor> PromptTarget;
	/** Si el aviso mostraba una interacción de mantener en curso (para el rebote al empezar). */
	bool bPromptHolding = false;

	void TickPrompt(float DeltaTime);
	/** La tecla o el botón de interactuar con el aparato de ahora (con lo reasignado en Ajustes). */
	void RefreshPromptKey(const APlayerController* PC, const ATortugaCharacter* Turtle);

	/** Punto blanco del centro de la pantalla: a donde irá lo que se lance (objeto lanzable o el compañero cogido). */
	bool bAimDotShown = false;
	bool ShouldShowAimDot() const;
};

/**
 * @brief Cartel de estado, resultados, tripulación y mensajes de la partida en el estilo Tortunavy, hechos en código.
 * Hereda la lógica de UTN_CoopFlowHUDWidget creando los widgets que esa clase enlaza por nombre.
 *  - Durante la partida el cartel de estado no se ve; si te eliminan sale un
 *    cartel con la cara mareada y en los resultados la cara va con ojos de estrella si llegaste o mareada si no.
 *  - Tripulación (a la izquierda): la cara de cada compañero según cómo va (caparazón, eliminación, llegada), en un
 *    aro de su color y con su nombre. Las frases del chat rápido salen en un
 *    bocadillo junto a la cara de quien las dice (las tuyas, junto a tu distintivo) en vez de en un chat global, y
 *    cuando alguien habla por la voz de proximidad le sale un bocadillo con barras de volumen.
 */
UCLASS()
class TORTUNABO_API UTN_RunFlowHUDWidget : public UTN_CoopFlowHUDWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void OnQuickChatEntryReceived_Implementation(int32 Sequence, const FText& SenderName, const FText& MessageText,
		UTexture2D* Icon, float ServerTimeSeconds) override;

private:
	void BuildTree();
	void TickCrew(float DeltaTime);
	void ShowBubble(int32 Row, const FText& MessageText);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UWidget> StatusCard;
	UPROPERTY(Transient) TObjectPtr<UWidget> OutCard;
	UPROPERTY(Transient) TObjectPtr<UImage> ResultsFace;

	/** Filas de la tripulación (hasta 7 compañeros) y, en la última posición de las listas de bocadillos, el tuyo. */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> CrewRows;
	/** La columna de la tripulación (se junta y encoge con más de tres compañeros). */
	UPROPERTY(Transient) TObjectPtr<UWidget> CrewBox;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CrewFaces;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CrewRings;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CrewNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> CrewTalk;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CrewTalkBars;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Bubbles;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BubbleTexts;

	TArray<int32> CrewPlayerIds;
	TArray<float> BubbleTime;
	TArray<uint8> CrewFaceShown;
	/** Disposición de la tripulación que se ve (0: hasta tres; 1: cuatro o cinco; 2: seis o siete). */
	int32 CrewLayoutShown = -1;
	float Time = 0.f;
};

/**
 * @brief Rueda radial (emotes y frases rápidas) en el estilo Tortunavy, hecha en código: un salvavidas azul marino con
 * gajos (M_UI_RadialWheel, Scripts/build_ui_assets.py) cuyo gajo apuntado se ilumina, las opciones en su gajo, la
 * cara de la tortuga en el centro, el título arriba y la opción elegida abajo. Centrada en la pantalla, que es desde
 * donde AMP_GamePlayerController mide el ratón, así que el gajo iluminado es siempre el que señala el ratón.
 */
UCLASS()
class TORTUNABO_API UTN_RunRadialWheelWidget : public UTN_RadialWheelWidgetBase
{
	GENERATED_BODY()

public:
	void SetTitle(const FText& InTitle);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void BP_OnEntriesSet_Implementation(const TArray<FTN_RadialWheelEntryView>& InEntries) override;

private:
	void BuildTree();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UImage> Disc;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DiscMID;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> SlotLayer;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SlotWidgets;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotLabels;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChoiceText;
	UPROPERTY(Transient) TObjectPtr<UWidget> ChoiceTag;

	FText PendingTitle;
	int32 ShownSelection = -2;
	/** Si la ayuda de la rueda se escribió para el mando (apuntar con el stick) o para el ratón. */
	bool bShownPad = false;
	float Time = 0.f;
};
