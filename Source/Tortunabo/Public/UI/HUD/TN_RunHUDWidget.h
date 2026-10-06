#pragma once

#include "CoreMinimal.h"
#include "UI/HUD/TN_PlayerHUDWidget.h"
#include "UI/HUD/TN_CoopFlowHUDWidget.h"
#include "UI/HUD/TN_RadialWheelWidgetBase.h"
#include "Styling/SlateBrush.h"
#include "TN_RunHUDWidget.generated.h"

class ATN_CoopPlayerState;
class ATN_PathStorm;
class ATN_ProcMapGenerator;
class UBorder;
class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UOverlay;
class UProgressBar;
class UTextBlock;
class APlayerController;
class ATortugaCharacter;
class UTN_ButtonGlyphWidget;
class UTN_HoldRingWidget;
class UTN_ScoreShellSynthComponent;

/**
 * Icono de concha que vuela al contador del HUD (UTN_RunHUDWidget): nace donde estaba la concha en pantalla, sale de un
 * saltito a su hueco alrededor, espera su turno y vuela en arco hasta la concha del contador, donde suma Part.
 */
struct FTNShellFlight
{
	/** Donde nace (la concha en pantalla), su hueco tras el saltito y el punto de control del arco (espacio del HUD). */
	FVector2D From = FVector2D::ZeroVector;
	FVector2D Rest = FVector2D::ZeroVector;
	FVector2D Bend = FVector2D::ZeroVector;
	/** Hora del HUD en que nace, en que sale hacia el contador y lo que tarda en llegar (s). */
	float Born = 0.f;
	float Launch = 0.f;
	float Flight = 0.6f;
	/** Giro (grados) y su velocidad (grados/s), tamaño en px y puntos que suma al llegar. */
	float Angle = 0.f;
	float SpinRate = 0.f;
	float Size = 34.f;
	int32 Part = 1;
	/** Tamaño de la concha (TNScoreShells::ETier) y si es el último icono de su recogida (rebote más fuerte). */
	uint8 Tier = 0;
	bool bLast = false;
	/** Lo que se pinta en este fotograma: posición, escala, opacidad y dos puntos de estela (solo en vuelo). */
	FVector2D Pos = FVector2D::ZeroVector;
	float Scale = 0.f;
	float Alpha = 0.f;
	bool bFlying = false;
	FVector2D TrailA = FVector2D::ZeroVector;
	FVector2D TrailB = FVector2D::ZeroVector;
};

/**
 * @brief HUD de la tortuga en partida, estilo Tortunavy (boceto para el equipo de arte), hecho en código.
 *
 * Tortugas que salen del nido y tienen que llegar al mar:
 *  - Distintivo: la cara cartoon de la tortuga (TN_HUDFaces.h) según cómo va: feliz, caparazón cerrado si se mete
 *    dentro, mareada si queda eliminada y con ojos de estrella al llegar, sobre un disco de mar con su salvavidas
 *    (M_UI_TurtleBadge, Scripts/build_ui_assets.py). La estamina no se ve en la interfaz. Debajo, una cinta con el
 *    nombre que no lo tapa. Cuando la tortuga habla por la voz de proximidad, la cara rebota y sale un bocadillo con
 *    barras de volumen.
 *  - Pista de la playa al mar (mapa procedural): del nido con la tortuguita asomando a la ola con la bandera de
 *    meta; tu cara avanza por la arena, los compañeros son caparazones de colores y la nube de tormenta te persigue
 *    oscureciendo la arena que ya se ha tragado.
 *  - Puntos en una concha; inventario en dos burbujas iguales: el aro de cuerda marca la que está en la aleta y
 *    rueda a la otra al cambiar. Avisos de tormenta, panza arriba y reanimación en carteles azul marino con ola.
 *  - Al coger una concha, iconos de su tamaño salen de donde estaba en pantalla, dan un saltito y vuelan en arco al
 *    contador (hasta 15, que se reparten el valor); cada uno suma su parte con un rebote del número y de la concha del
 *    contador y un «pom» que sube por la escala. Las recogidas seguidas hacen cola y el número acaba siempre en la
 *    puntuación real (RaceScore): lo que sube sin concha (la llegada) sale de la tortuga, y si baja se ajusta solo.
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
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void BuildTree();
	void TickBadge(float DeltaTime);
	void TickInventory(float DeltaTime);
	void TickTrack(float DeltaTime);
	void TickScore(float DeltaTime);
	void TickAlerts(float DeltaTime);

	// ── Conchas que vuelan al contador ──
	/** Se engancha al aviso de conchas del PlayerState local (y vuelve a empezar la cuenta si cambia). */
	void BindShellEvents();
	void UnbindShellEvents();
	/** Aviso del PlayerState: una concha de Value puntos de tamaño Tier se ha cogido en WorldLocation. */
	void HandleScoreShellCollected(int32 Value, uint8 Tier, const FVector& WorldLocation);
	/** Pone en cola los iconos de una recogida de Value puntos que nacen en From (espacio del HUD). */
	void EnqueueShellBurst(int32 Value, uint8 Tier, const FVector2D& From);
	/** Mueve los iconos, suma los que llegan (rebote y «pom») y cuadra la cuenta con RaceScore. */
	void TickShellFlights(float DeltaTime, const FGeometry& MyGeometry);
	/** La tortuga local en pantalla (espacio del HUD), o un punto abajo en el centro si no se ve. */
	FVector2D TurtleScreenPoint() const;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UImage> Badge;
	UPROPERTY(Transient) TObjectPtr<UImage> FaceImage;
	UPROPERTY(Transient) TObjectPtr<UWidget> TalkBubble;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TalkBars;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ItemImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotTags;
	UPROPERTY(Transient) TObjectPtr<UImage> RopeImage;

	UPROPERTY(Transient) TObjectPtr<UOverlay> TrackRoot;
	UPROPERTY(Transient) TObjectPtr<UImage> StormShade;
	UPROPERTY(Transient) TObjectPtr<UImage> StormMarker;
	UPROPERTY(Transient) TObjectPtr<UImage> MiniFace;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MateMarkers;

	UPROPERTY(Transient) TObjectPtr<UOverlay> ScoreRoot;
	UPROPERTY(Transient) TObjectPtr<UBorder> StormBanner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StormText;
	UPROPERTY(Transient) TObjectPtr<UBorder> SeagullBanner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SeagullText;
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

	TWeakObjectPtr<ATN_ProcMapGenerator> Generator;
	TWeakObjectPtr<ATN_PathStorm> Storm;
	TWeakObjectPtr<UObject> LastEquippedIcon;
	TWeakObjectPtr<UObject> LastStoredIcon;
	float Time = 0.f;
	/** Cara mostrada (ETNTurtleFace de TN_HUDFaces.h) y el rebote al cambiar. */
	uint8 ShownFace = 0;
	float FacePop = 0.f;
	/** Hueco del inventario que está en la aleta (0 izquierda, 1 derecha) y posición animada del aro de cuerda. */
	int32 EquippedSide = 0;
	float RopeX = 0.f;
	float ShownProgress = 0.f;
	float ShownStorm = 0.f;
	float LookupTimer = 0.f;
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

	// ── Contador de conchas ──
	/** Número que se ve (lo que ya ha llegado) y la concha del contador (destino de los iconos, rebota al sumar). */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient) TObjectPtr<UImage> CounterShell;
	/** «+N» que sale junto al contador mientras llegan los iconos de una o varias recogidas seguidas. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GainText;
	/** Sintetizador 2D del «pom» (en el PlayerController). */
	TWeakObjectPtr<UTN_ScoreShellSynthComponent> PomSynth;
	TWeakObjectPtr<ATN_CoopPlayerState> ShellEventsPS;
	FDelegateHandle ShellEventsHandle;
	/** Iconos de concha de cada tamaño para pintarlos (NativePaint). */
	FSlateBrush ShellBrushes[4];
	TArray<FTNShellFlight> Flights;
	/** Centro de la concha del contador y tamaño del HUD (espacio del HUD), del último fotograma. */
	FVector2D CounterTarget = FVector2D::ZeroVector;
	FVector2D HUDSize = FVector2D::ZeroVector;
	/** Número que enseña el contador (-1 = sin empezar) y puntos que vuelan todavía. */
	int32 ShownScore = -1;
	int32 LastShownNumber = -1;
	int32 InFlightValue = 0;
	/** Cuándo puede salir el siguiente icono (cola de recogidas) y cuánto lleva la cuenta descuadrada. */
	float NextLaunchAt = 0.f;
	float UnexplainedFor = 0.f;
	float ExcessFor = 0.f;
	/** Tanda de «pom»: paso de la escala y cuándo sonó el último. */
	int32 PomStep = 0;
	float LastPomAt = -10.f;
	/** Rebote del contador, destello de su concha al acabar una grande o una reina, y el «+N». */
	float CounterPop = 0.f;
	float CounterGlow = 0.f;
	uint8 CounterGlowTier = 0;
	int32 GainShown = 0;
	float GainAge = 10.f;
	bool bShellPaintDirty = false;
	/** Punto blanco del centro de la pantalla: a donde irá lo que se lance (objeto lanzable, tinta o el compañero cogido). */
	bool bAimDotShown = false;
	bool ShouldShowAimDot() const;
};

/**
 * @brief Cartel de estado, resultados, tripulación y mensajes de la partida en el estilo Tortunavy, hechos en código.
 * Hereda la lógica de UTN_CoopFlowHUDWidget creando los widgets que esa clase enlaza por nombre.
 *  - Durante la carrera el cartel de estado no se ve (el objetivo ya lo cuenta la pista); si te eliminan sale un
 *    cartel con la cara mareada y en los resultados la cara va con ojos de estrella si llegaste o mareada si no.
 *  - Tripulación (a la izquierda): la cara de cada compañero según cómo va (caparazón, eliminación, llegada), en un
 *    aro de su color (el mismo que su caparazón en la pista) y con su nombre. Las frases del chat rápido salen en un
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
