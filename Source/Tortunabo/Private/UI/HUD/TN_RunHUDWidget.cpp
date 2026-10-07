#include "UI/HUD/TN_RunHUDWidget.h"
#include "TN_HUDStyle.h"
#include "TN_HUDArt.h"
#include "TN_HUDFaces.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_LocText.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "UI/HUD/TN_HoldRingWidget.h"
#include "World/TN_InteractableBase.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Core/TN_InventoryTypes.h"
#include "Player/TN_CarryComponent.h"
#include "Game/TN_CoopItems.h"
#include "Player/TN_InventoryComponent.h"
#include "InputAction.h"
#include "Voice/ProximityVoiceComponent.h"
#include "Settings/TN_InputDeviceSubsystem.h"
#include "UI/HUD/TN_ButtonGlyphWidget.h"

// Con nombre (no anónimo): un using-directive dentro de un namespace anónimo se ve en todo el resto del bloque
// unity y los nombres de TNHUDStyle (Edge, Text, Sand...) chocaban con variables del mapa procedural (C4459).
namespace TNRunHUDDetail
{
	using namespace TNHUDStyle;

	/** Vista previa de los estados del distintivo (para probar y para el equipo de arte). */
	TAutoConsoleVariable<int32> CVarHUDFace(TEXT("tn.HUD.Face"), -1,
		TEXT("HUD: fuerza la cara (0 feliz, 1 caparazón, 2 mareada, 3 victoria); -1 = la real."));
	TAutoConsoleVariable<int32> CVarHUDTalk(TEXT("tn.HUD.Talk"), -1, TEXT("HUD: 1 fuerza el bocadillo de voz, 0 lo apaga; -1 = el real."));
	TAutoConsoleVariable<int32> CVarHUDCrew(TEXT("tn.HUD.CrewPreview"), 0,
		TEXT("HUD: rellena N filas de la tripulación con tu propia tortuga (la 1.ª dice una frase y la 2.ª habla) para ver el diseño sin más jugadores."));
	TAutoConsoleVariable<int32> CVarHUDPrompt(TEXT("tn.HUD.Prompt"), 0,
		TEXT("HUD: 1 enseña el aviso de interacción sin nada al alcance (con la tecla o el botón del aparato de ahora)."));

	/** Inventario: burbujas iguales en columnas de ancho fijo (el aro de cuerda rueda de una a otra). */
	constexpr float BubbleSize = 90.f;
	constexpr float SlotColumn = 162.f;
	constexpr float SlotGap = 8.f;

	/** Rueda radial: tamaño y radio al que van las opciones (mitad del anillo de M_UI_RadialWheel). */
	constexpr float WheelSize = 500.f;
	constexpr float WheelSlotRadius = 0.3135f * WheelSize;

	/** Duración de un bocadillo de chat (s). */
	constexpr float BubbleLife = 4.5f;

	const FLinearColor NavyText = TNHUDArt::Ink;
	/** Compañeros como mucho en la tripulación y en la pista (partidas de hasta ocho). */
	constexpr int32 MaxMates = 7;
	/** Color de cada compañero: su caparazón en la pista y el aro de su cara en la tripulación. */
	const FLinearColor MateColors[MaxMates] = { TNHUDArt::Hex(0x59C96B), TNHUDArt::Hex(0xFFC23D), TNHUDArt::Hex(0xB07CFF),
		TNHUDArt::Hex(0x4FC3F7), TNHUDArt::Hex(0xFF8A65), TNHUDArt::Hex(0xF48FB1), TNHUDArt::Hex(0xC5E1A5) };
	/** Márgenes de caja (fracción de la textura) de los carteles con arte de TNHUDArt. */
	const FMargin CardMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonMargin(0.14f, 0.f, 0.14f, 0.f);
	/** Lado del salvavidas del distintivo (px). */
	constexpr float BadgeRingSize = 196.f;
	/** Relleno a cada lado del nombre dentro de la cinta (px). */
	constexpr float BadgeNamePadding = 28.f;
	/** Ancho máximo del nombre bajo el salvavidas (px): uno más largo se encoge en vez de ensanchar la cinta. */
	constexpr float BadgeNameMaxWidth = 140.f;
	// La cinta nunca es más ancha que el salvavidas: si lo fuera, la columna centrada lo movería (#147).
	static_assert(BadgeNameMaxWidth + 2.f * BadgeNamePadding <= BadgeRingSize, "La cinta del nombre no cabe bajo el salvavidas");
	const FMargin TagMargin(0.2f, 0.f, 0.2f, 0.f);
	const FMargin ChatBubbleMargin(0.26f, 0.3f, 0.18f, 0.45f);

	template <typename T>
	T* Make(UWidgetTree* Tree, const TCHAR* Name = nullptr)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), Name ? FName(Name) : NAME_None);
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Name, const FText& Content, FName Weight, int32 Size, const FLinearColor& Color, bool bOutline = true)
	{
		UTextBlock* T = Make<UTextBlock>(Tree, Name);
		T->SetText(Content);
		StyleText(T, Weight, Size, Color, bOutline);
		if (!bOutline) { T->SetShadowColorAndOpacity(FLinearColor::Transparent); }
		return T;
	}

	UImage* MakeImage(UWidgetTree* Tree, UTexture2D* Tex, const FVector2D& Size, const TCHAR* Name = nullptr)
	{
		UImage* I = Make<UImage>(Tree, Name);
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = Size;
		I->SetBrush(B);
		return I;
	}

	/** Cambia la textura de una imagen conservando su tamaño. */
	void SetImageTexture(UImage* I, UTexture2D* Tex)
	{
		if (!I) { return; }
		FSlateBrush B = I->GetBrush();
		if (B.GetResourceObject() == Tex) { return; }
		B.SetResourceObject(Tex);
		I->SetBrush(B);
	}

	/**
	 * Cartel con arte que se estira como caja (los bordes con dibujo quedan al tamaño real de la textura) y contenido
	 * con relleno. El contenido tiene que medir al menos lo que los bordes, o Slate los encoge y el dibujo deja de
	 * casar con el relleno.
	 */
	UBorder* MakeCard(UWidgetTree* Tree, UTexture2D* Tex, const FMargin& Margin, UWidget* Content, const FMargin& Padding)
	{
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.DrawAs = ESlateBrushDrawType::Box;
		B.Margin = Margin;
		if (Tex) { B.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY()); }
		UBorder* Card = Make<UBorder>(Tree);
		Card->SetBrush(B);
		Card->SetPadding(Padding);
		Card->SetHorizontalAlignment(HAlign_Center);
		Card->SetVerticalAlignment(VAlign_Center);
		if (Content) { Card->SetContent(Content); }
		return Card;
	}

	USizeBox* MakeSize(UWidgetTree* Tree, UWidget* Content, float W, float H)
	{
		USizeBox* S = Make<USizeBox>(Tree);
		if (W > 0.f) { S->SetWidthOverride(W); }
		if (H > 0.f) { S->SetHeightOverride(H); }
		if (Content) { S->SetContent(Content); }
		return S;
	}

	/** Añade a un Overlay con alineación y relleno (por defecto los hijos van arriba a la izquierda a su tamaño). */
	UOverlaySlot* AddAt(UOverlay* Parent, UWidget* W, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Padding = FMargin(0.f))
	{
		UOverlaySlot* Slot = Parent->AddChildToOverlay(W);
		if (Slot)
		{
			Slot->SetHorizontalAlignment(H);
			Slot->SetVerticalAlignment(V);
			Slot->SetPadding(Padding);
		}
		return Slot;
	}

	UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(W);
		Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		Slot->SetAlignment(Anchor);
		Slot->SetPosition(Offset);
		Slot->SetAutoSize(true);
		return Slot;
	}

	UMaterialInstanceDynamic* MakeUIMID(UObject* Outer, const TCHAR* Path)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path);
		return Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	}

	/** Cartel azul marino con la cara de la tortuga a la izquierda y el texto a la derecha. */
	UBorder* MakeFaceCard(UWidgetTree* Tree, ETNTurtleFace Face, float FaceSize, UWidget* Content)
	{
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		UImage* FaceImg = MakeImage(Tree, TNHUDFaces::TurtleFace(Face), FVector2D(FaceSize, FaceSize));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(FaceImg)) { S->SetVerticalAlignment(VAlign_Center); S->SetPadding(FMargin(0.f, -12.f, 10.f, -12.f)); }
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Content)) { S->SetVerticalAlignment(VAlign_Center); }
		return MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, Row, FMargin(22.f, 26.f, 30.f, 40.f));
	}

	/** Bocadillo de voz de ancho Width con cuatro barras de volumen dentro (se animan con AnimateTalkBars). */
	UOverlay* MakeTalkBubble(UWidgetTree* Tree, float Width, TArray<TObjectPtr<UImage>>& OutBars)
	{
		const float Height = Width * 112.f / 128.f;
		UOverlay* Root = Make<UOverlay>(Tree);
		AddAt(Root, MakeImage(Tree, TNHUDArt::TalkBubbleTexture(), FVector2D(Width, Height)), HAlign_Fill, VAlign_Fill);
		UHorizontalBox* Bars = Make<UHorizontalBox>(Tree);
		for (int32 i = 0; i < 4; ++i)
		{
			UImage* BarImg = Make<UImage>(Tree);
			BarImg->SetBrush(Rounded((i % 2) ? TNHUDArt::Sea : TNHUDArt::Navy, 3.f));
			BarImg->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			if (UHorizontalBoxSlot* S = Bars->AddChildToHorizontalBox(MakeSize(Tree, BarImg, Width * 0.068f, Height * 0.34f)))
			{
				S->SetPadding(FMargin(Width * 0.028f, 0.f));
				S->SetVerticalAlignment(VAlign_Center);
			}
			OutBars.Add(BarImg);
		}
		// Centradas en el cuerpo del bocadillo (algo a la derecha y arriba del centro de la textura, por la cola).
		AddAt(Root, Bars, HAlign_Center, VAlign_Center, FMargin(Width * 0.0625f, 0.f, 0.f, Height * 0.143f));
		return Root;
	}

	void AnimateTalkBars(const TArray<TObjectPtr<UImage>>& Bars, int32 From, float Time, float Seed)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			if (UImage* BarImg = Bars.IsValidIndex(From + i) ? Bars[From + i].Get() : nullptr)
			{
				const float Level = 0.25f + 0.75f * FMath::Abs(FMath::Sin(Time * (7.f + 2.3f * i) + Seed + i * 1.7f));
				BarImg->SetRenderScale(FVector2D(1.f, Level));
			}
		}
	}

	/** Compañeros (todos los PlayerState menos el tuyo) en el orden del GameState: fija su color y su fila. */
	TArray<const APlayerState*> CrewOf(const UWorld* World, const APlayerState* Own)
	{
		TArray<const APlayerState*> Out;
		if (const AGameStateBase* GS = World ? World->GetGameState() : nullptr)
		{
			for (const APlayerState* PS : GS->PlayerArray)
			{
				if (PS && PS != Own && !PS->IsInactive()) { Out.Add(PS); }
			}
		}
		return Out;
	}

	/** Bocadillo de chat con la frase dentro; el texto tiene un mínimo de tamaño para que el cuerpo siempre lo contenga. */
	UBorder* MakeChatBubble(UWidgetTree* Tree, int32 FontSize, float MaxWidth, UTextBlock*& OutText)
	{
		OutText = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), FontSize, NavyText, false);
		OutText->SetAutoWrapText(true);
		OutText->SetJustification(ETextJustify::Center);
		USizeBox* Limit = MakeSize(Tree, OutText, 0.f, 0.f);
		Limit->SetMaxDesiredWidth(MaxWidth);
		Limit->SetMinDesiredWidth(34.f);
		Limit->SetMinDesiredHeight(22.f);
		// El cuerpo crema del bocadillo empieza ~9 px por debajo del borde de la textura y acaba ~17 px antes del de
		// abajo (la cola): el relleno deja aire por encima y por debajo de la frase.
		UBorder* Bubble = MakeCard(Tree, TNHUDArt::ChatBubbleTexture(), ChatBubbleMargin, Limit, FMargin(24.f, 17.f, 18.f, 24.f));
		Bubble->SetRenderTransformPivot(FVector2D(0.f, 1.f));
		Bubble->SetVisibility(ESlateVisibility::Collapsed);
		return Bubble;
	}

	/**
	 * Tortuga de un jugador tal como está en esta máquina. La que apunta su PlayerState (GetPawn) solo vale si sigue viva
	 * y la tortuga dice que es de ese jugador; si no (en un cliente puede apuntar a una tortuga ya destruida, de antes de
	 * un cambio de tortuga, o a ninguna), se busca la tortuga cuyo PlayerState es ese, que es el enlace que replica la
	 * propia tortuga (el mismo con el que se ponen los cosméticos).
	 */
	const APawn* TurtleOf(const UWorld* World, const APlayerState* PS)
	{
		const APawn* Linked = PS ? PS->GetPawn() : nullptr;
		if (IsValid(Linked) && !Linked->IsActorBeingDestroyed() && Linked->GetPlayerState() == PS)
		{
			return Linked;
		}
		if (!World || !PS)
		{
			return nullptr;
		}
		for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetPlayerState() == PS)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Cara de una tortuga según su estado: llegada, eliminada, en el caparazón o feliz. */
	ETNTurtleFace FaceFor(const APlayerState* PS, const APawn* Pawn)
	{
		const ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(PS);
		if (TNPS && TNPS->bHasFinishedRun && !TNPS->bIsEliminated) { return ETNTurtleFace::Win; }
		if (TNPS && (TNPS->bIsDBNO || TNPS->bIsEliminated)) { return ETNTurtleFace::Down; }
		const UTN_ShellComponent* ShellComp = Pawn ? Pawn->FindComponentByClass<UTN_ShellComponent>() : nullptr;
		if (ShellComp && ShellComp->IsInShell()) { return ETNTurtleFace::Shell; }
		return ETNTurtleFace::Happy;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD de la tortuga
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RunHUDWidget::NativeOnInitialized()
{
	using namespace TNRunHUDDetail;
	BuildTree();
	Super::NativeOnInitialized();
}

void UTN_RunHUDWidget::BuildTree()
{
	using namespace TNRunHUDDetail;
	if (!WidgetTree || Canvas) { return; }
	UWidgetTree* Tree = WidgetTree;
	Canvas = Make<UCanvasPanel>(Tree, TEXT("RunHUDCanvas"));
	Tree->RootWidget = Canvas;

	// Todas las caras se dibujan ahora (al entrar en el mapa) para que cambiar de estado no dé tirones.
	for (int32 f = 0; f <= static_cast<int32>(ETNTurtleFace::Win); ++f) { TNHUDFaces::TurtleFace(static_cast<ETNTurtleFace>(f)); }

	// La clase base rellena estos widgets (número e iconos del inventario): existen pero no se ven; el Tick los lee para
	// pintar las burbujas.
	{
		UVerticalBox* Feed = Make<UVerticalBox>(Tree);
		SlotEquippedImage = Make<UImage>(Tree, TEXT("SlotEquippedImage"));
		SlotEquippedImage->SetColorAndOpacity(FLinearColor::Transparent);
		SlotStoredImage = Make<UImage>(Tree, TEXT("SlotStoredImage"));
		SlotStoredImage->SetColorAndOpacity(FLinearColor::Transparent);
		// La clase base escribe aquí la puntuación real, que no se enseña.
		ScoreText = MakeText(Tree, TEXT("ScoreText"), FText::AsNumber(0), TEXT("Regular"), 10, Text);
		for (UWidget* W : { static_cast<UWidget*>(SlotEquippedImage), static_cast<UWidget*>(SlotStoredImage), static_cast<UWidget*>(ScoreText) })
		{
			Feed->AddChildToVerticalBox(W);
		}
		Feed->SetVisibility(ESlateVisibility::Collapsed);
		Place(Canvas, Feed, FVector2D(0.f, 0.f), FVector2D(0.f, 0.f));
	}

	// ── Distintivo (abajo a la izquierda): la cara en el disco con su salvavidas y, debajo, la cinta con el nombre ──
	{
		UVerticalBox* Col = Make<UVerticalBox>(Tree);
		UOverlay* Ring = Make<UOverlay>(Tree);
		Badge = Make<UImage>(Tree, TEXT("TurtleBadge"));
		if (UMaterialInterface* BadgeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/HUD/M_UI_TurtleBadge.M_UI_TurtleBadge")))
		{
			Badge->SetBrushFromMaterial(BadgeMaterial);
		}
		AddAt(Ring, MakeSize(Tree, Badge, BadgeRingSize, BadgeRingSize), HAlign_Center, VAlign_Center);
		FaceImage = MakeImage(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy), FVector2D(104.f, 104.f));
		FaceImage->SetRenderTransformPivot(FVector2D(0.5f, 0.85f));
		AddAt(Ring, FaceImage, HAlign_Center, VAlign_Center);
		// Hablando por la voz de proximidad: bocadillo con barras de volumen arriba a la derecha.
		TalkBubble = MakeTalkBubble(Tree, 76.f, TalkBars);
		TalkBubble->SetRenderTransformPivot(FVector2D(0.1f, 0.95f));
		TalkBubble->SetVisibility(ESlateVisibility::Collapsed);
		AddAt(Ring, TalkBubble, HAlign_Right, VAlign_Top, FMargin(0.f, -18.f, -44.f, 0.f));
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Ring)) { S->SetHorizontalAlignment(HAlign_Center); }

		NameText = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 16, FLinearColor::White);
		NameText->SetJustification(ETextJustify::Center);
		// Un nombre largo (hasta 32 caracteres, el máximo de Steam) se encoge para caber en la cinta, como en los resultados de
		// la carrera: si la ensanchara, la columna centrada movería el salvavidas.
		UScaleBox* NameShrink = Make<UScaleBox>(Tree);
		NameShrink->SetStretch(EStretch::ScaleToFit);
		NameShrink->SetStretchDirection(EStretchDirection::DownOnly);
		NameShrink->SetContent(NameText);
		USizeBox* NameFit = MakeSize(Tree, NameShrink, 0.f, 0.f);
		NameFit->SetMaxDesiredWidth(BadgeNameMaxWidth);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(MakeCard(Tree, TNHUDArt::RibbonTexture(), RibbonMargin, NameFit, FMargin(BadgeNamePadding, 17.f, BadgeNamePadding, 19.f))))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetPadding(FMargin(0.f, -8.f, 0.f, 0.f));
		}
		Place(Canvas, Col, FVector2D(0.f, 1.f), FVector2D(22.f, -8.f));
	}

	// ── Inventario (abajo en el centro): dos burbujas iguales; el aro de cuerda marca la que va en la aleta ──
	{
		UOverlay* Root = Make<UOverlay>(Tree);
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		for (int32 i = 0; i < 2; ++i)
		{
			UOverlay* Bubble = Make<UOverlay>(Tree);
			AddAt(Bubble, MakeImage(Tree, TNHUDArt::BubbleIcon(), FVector2D(BubbleSize, BubbleSize)), HAlign_Fill, VAlign_Fill);
			UImage* Item = Make<UImage>(Tree);
			Item->SetColorAndOpacity(FLinearColor::Transparent);
			AddAt(Bubble, Item, HAlign_Fill, VAlign_Fill, FMargin(19.f));
			ItemImages.Add(Item);
			UVerticalBox* Column = Make<UVerticalBox>(Tree);
			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeSize(Tree, Bubble, BubbleSize, BubbleSize))) { S->SetHorizontalAlignment(HAlign_Center); }
			UTextBlock* Tag = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 11, NavyText, false);
			SlotTags.Add(Tag);
			if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeCard(Tree, TNHUDArt::SandTagTexture(), TagMargin, Tag, FMargin(18.f, 12.f, 18.f, 13.f))))
			{
				S->SetHorizontalAlignment(HAlign_Center);
				S->SetPadding(FMargin(0.f, -8.f, 0.f, 0.f));
			}
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(MakeSize(Tree, Column, SlotColumn, 0.f))) { S->SetPadding(FMargin(i == 0 ? 0.f : SlotGap, 0.f, 0.f, 0.f)); }
		}
		SlotTags[0]->SetText(NSLOCTEXT("TNHUD", "InHand", "EN LA ALETA"));
		SlotTags[1]->SetText(NSLOCTEXT("TNHUD", "Stored", "EN EL CAPARAZÓN"));
		AddAt(Root, Row, HAlign_Left, VAlign_Top);
		// El aro de cuerda va por encima y se traslada de una burbuja a la otra (TickInventory).
		RopeImage = MakeImage(Tree, TNHUDArt::RopeRing(), FVector2D(BubbleSize + 10.f, BubbleSize + 10.f), TEXT("SlotEquippedSelector"));
		RopeImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		SlotEquippedSelector = RopeImage;
		AddAt(Root, RopeImage, HAlign_Left, VAlign_Top, FMargin(0.5f * (SlotColumn - BubbleSize) - 5.f, -5.f, 0.f, 0.f));
		Place(Canvas, Root, FVector2D(0.5f, 1.f), FVector2D(0.f, -12.f));
	}

	// ── Avisos: carteles azul marino con ola ──
	{
		UHorizontalBox* StormRow = Make<UHorizontalBox>(Tree);
		if (UHorizontalBoxSlot* S = StormRow->AddChildToHorizontalBox(MakeImage(Tree, TNHUDArt::StormIcon(), FVector2D(58.f, 48.f)))) { S->SetVerticalAlignment(VAlign_Center); }
		StormText = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 23, TNHUDArt::Hex(0xFF9A85));
		if (UHorizontalBoxSlot* S = StormRow->AddChildToHorizontalBox(StormText)) { S->SetVerticalAlignment(VAlign_Center); S->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f)); }
		StormBanner = MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, StormRow, FMargin(26.f, 26.f, 32.f, 40.f));
		StormBanner->SetVisibility(ESlateVisibility::Collapsed);
		StormBanner->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Place(Canvas, StormBanner, FVector2D(0.5f, 0.f), FVector2D(0.f, 118.f));

		DownText = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 21, TNHUDArt::SandC);
		DownBanner = MakeFaceCard(Tree, ETNTurtleFace::Down, 92.f, DownText);
		DownBanner->SetVisibility(ESlateVisibility::Collapsed);
		Place(Canvas, DownBanner, FVector2D(0.5f, 0.5f), FVector2D(0.f, 150.f));

		UVerticalBox* Col = Make<UVerticalBox>(Tree);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(MakeText(Tree, nullptr, NSLOCTEXT("TNHUD", "Reviving", "Dando la vuelta a tu compañero..."), TEXT("Bold"), 16, FLinearColor::White)))
		{
			S->SetHorizontalAlignment(HAlign_Center);
		}
		ReviveBar = Make<UProgressBar>(Tree);
		ReviveBar->SetWidgetStyle(Bar(7.f));
		ReviveBar->SetFillColorAndOpacity(TNHUDArt::SeaLight);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(MakeSize(Tree, ReviveBar, 240.f, 14.f))) { S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }
		ReviveBanner = MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, Col, FMargin(26.f, 26.f, 26.f, 40.f));
		ReviveBanner->SetVisibility(ESlateVisibility::Collapsed);
		Place(Canvas, ReviveBanner, FVector2D(0.5f, 0.5f), FVector2D(0.f, 90.f));
	}

	// ── Aviso de interacción: tecla en un botón azul marino y el texto del interactuable al alcance ──
	{
		/** Alto de la tecla y del botón del mando (px). */
		constexpr float PromptKeyHeight = 42.f;
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		PromptKeyText = MakeText(Tree, nullptr, INVTEXT("E"), TEXT("Black"), 22, TNHUDArt::Cream, false);
		PromptKeyText->SetJustification(ETextJustify::Center);
		UBorder* KeyCap = Make<UBorder>(Tree);
		KeyCap->SetBrush(Rounded(TNHUDArt::Navy, 10.f, TNHUDArt::Cream, 2.5f));
		KeyCap->SetPadding(FMargin(12.f, 2.f, 12.f, 4.f));
		KeyCap->SetHorizontalAlignment(HAlign_Center);
		KeyCap->SetVerticalAlignment(VAlign_Center);
		KeyCap->SetContent(PromptKeyText);
		PromptKeyCap = KeyCap;
		// Con mando, el botón dibujado en lugar de la tecla (RefreshPromptKey elige cuál se ve).
		PromptGlyph = Make<UTN_ButtonGlyphWidget>(Tree);
		PromptGlyph->SetGlyphHeight(PromptKeyHeight);
		PromptGlyph->SetVisibility(ESlateVisibility::Collapsed);
		// La tecla, con el aro de mantener alrededor: solo se ve en las interacciones de mantener (rebuscar un
		// decorado) y se llena en dorado mientras la tecla siga pulsada. Plegado no cuenta en el tamaño del aviso.
		UOverlay* KeyStack = Make<UOverlay>(Tree);
		AddAt(KeyStack, MakeSize(Tree, KeyCap, 0.f, PromptKeyHeight), HAlign_Center, VAlign_Center);
		AddAt(KeyStack, PromptGlyph, HAlign_Center, VAlign_Center);
		HoldRing = Make<UTN_HoldRingWidget>(Tree);
		HoldRing->SetRingSize(66.f, 6.f);
		HoldRing->SetVisibility(ESlateVisibility::Collapsed);
		AddAt(KeyStack, HoldRing, HAlign_Center, VAlign_Center);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(KeyStack))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}
		PromptLabel = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 20, NavyText, false);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(PromptLabel)) { S->SetVerticalAlignment(VAlign_Center); }
		PromptCard = MakeCard(Tree, TNHUDArt::SandTagTexture(), TagMargin, Row, FMargin(18.f, 8.f, 26.f, 10.f));
		PromptCard->SetRenderTransformPivot(FVector2D(0.5f, 1.f));
		PromptCard->SetVisibility(ESlateVisibility::Collapsed);
		Place(Canvas, PromptCard, FVector2D(0.5f, 1.f), FVector2D(0.f, -200.f));
	}
}

void UTN_RunHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace TNRunHUDDetail;
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	TickBadge(InDeltaTime);
	TickInventory(InDeltaTime);
	TickAlerts(InDeltaTime);
	TickPrompt(InDeltaTime);
	const bool bWantsDot = ShouldShowAimDot();
	if (bWantsDot != bAimDotShown)
	{
		bAimDotShown = bWantsDot;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool UTN_RunHUDWidget::ShouldShowAimDot() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
	if (!Turtle || PC->ShouldShowMouseCursor() || Turtle->UsesCameraThrowAim() == false)
	{
		return false;
	}
	if (Turtle->GetCarryComponent() && Turtle->GetCarryComponent()->IsCarrying())
	{
		return true;
	}
	const UTN_InventoryComponent* Inv = Turtle->GetInventoryComponent();
	if (!Inv || !Inv->HasEquippedItem())
	{
		return false;
	}
	const FTN_InventoryItem& Equipped = Inv->GetEquippedItem();
	const ETN_ItemUseType Use = Equipped.UseType;
	if (Use == ETN_ItemUseType::CoopItem)
	{
		// Del coop, los que se apuntan: se lanzan o disparan hacia la mira.
		return TNCoopItems::IsAimed(TNCoopItems::KindOf(Equipped));
	}
	return Use == ETN_ItemUseType::Throwable;
}

void UTN_RunHUDWidget::TickPrompt(float DeltaTime)
{
	using namespace TNRunHUDDetail;
	if (!PromptCard) { return; }
	const APlayerController* PC = GetOwningPlayer();
	ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
	ATN_InteractableBase* Target = Turtle ? Turtle->GetFocusedInteractable() : nullptr;
	// Con un menú abierto (tienda, probador, ruedas) no hace falta: el cursor está a la vista.
	const bool bShow = Target && Target->CanInteract(Turtle) && !PC->ShouldShowMouseCursor() && !Target->GetPromptText().IsEmpty();
	if (!bShow)
	{
		// Vista previa (tn.HUD.Prompt 1): el aviso sin nada al alcance, con la tecla o el botón de interactuar de verdad.
		const bool bPreview = Turtle && CVarHUDPrompt.GetValueOnGameThread() > 0;
		PromptCard->SetVisibility(bPreview ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bPreview)
		{
			PromptLabel->SetText(NSLOCTEXT("Tortunabo", "InteractPrompt", "Interactuar"));
			PromptKeyTimer -= DeltaTime;
			RefreshPromptKey(PC, Turtle);
		}
		PromptTarget.Reset();
		bPromptHolding = false;
		return;
	}
	if (PromptTarget.Get() != Target)
	{
		PromptTarget = Target;
		PromptLabel->SetText(Target->GetPromptText());
		PromptPop = 1.f;
	}
	PromptKeyTimer -= DeltaTime;
	RefreshPromptKey(PC, Turtle);
	PromptCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	// Interacciones de mantener (rebuscar): el aro se llena con el progreso que cuenta el servidor (estado replicado);
	// recién pulsada la tecla, mientras llega su respuesta, sale vacío. Al empezar, el aviso da un saltito.
	if (HoldRing)
	{
		const bool bHoldKind = Target->GetHoldDuration() > 0.f;
		float HoldProgress = bHoldKind ? Target->GetHoldProgress(Turtle) : -1.f;
		if (bHoldKind && HoldProgress < 0.f && Turtle->GetHoldInteractable() == Target) { HoldProgress = 0.f; }
		HoldRing->SetVisibility(bHoldKind ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		HoldRing->SetProgress(FMath::Max(0.f, HoldProgress));
		const bool bHolding = HoldProgress >= 0.f;
		if (bHolding && !bPromptHolding) { PromptPop = FMath::Max(PromptPop, 0.5f); }
		bPromptHolding = bHolding;
	}
	PromptPop = FMath::Max(0.f, PromptPop - DeltaTime * 4.f);
	const float Bob = 1.f + 0.03f * FMath::Sin(Time * 4.f);
	const float Pop = 1.f + 0.25f * PromptPop * PromptPop;
	PromptCard->SetRenderScale(FVector2D(Bob * Pop, Bob * Pop));
}

void UTN_RunHUDWidget::RefreshPromptKey(const APlayerController* PC, const ATortugaCharacter* Turtle)
{
	// Al cambiar de teclado a mando (o de mando), al momento; si no, de vez en cuando, por si se reasigna en Ajustes.
	constexpr float PromptKeyRefreshSeconds = 2.f;
	const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(PC);
	const ETNInputDevice Device = Devices ? Devices->GetDevice(PC) : ETNInputDevice::KeyboardMouse;
	const ETNPadFamily Family = Devices ? Devices->GetPadFamily() : ETNPadFamily::Xbox;
	const bool bChanged = PromptKeyDevice != static_cast<uint8>(Device) || PromptKeyFamily != static_cast<uint8>(Family);
	if (!bChanged && PromptKeyTimer > 0.f)
	{
		return;
	}
	PromptKeyTimer = PromptKeyRefreshSeconds;
	PromptKeyDevice = static_cast<uint8>(Device);
	PromptKeyFamily = static_cast<uint8>(Family);
	const FKey Key = Devices ? Devices->KeyForAction(PC, Turtle->GetInteractAction()) : FKey();
	const bool bGlyph = PromptGlyph && TNInputGlyphs::DeviceOfKey(Key) == ETNInputDevice::Gamepad && PromptGlyph->SetKey(Key, Family);
	if (PromptGlyph) { PromptGlyph->SetVisibility(bGlyph ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	if (PromptKeyCap) { PromptKeyCap->SetVisibility(bGlyph ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
	if (!bGlyph && Key.IsValid())
	{
		PromptKeyText->SetText(UTN_GameSettingsSubsystem::KeyDisplayName(Key));
	}
}

void UTN_RunHUDWidget::TickBadge(float DeltaTime)
{
	using namespace TNRunHUDDetail;
	// La tortuga propia y su jugador.
	const APlayerController* OwningPC = GetOwningPlayer();
	const APawn* Pawn = OwningPC ? OwningPC->GetPawn() : nullptr;
	const APlayerState* PS = OwningPC ? OwningPC->PlayerState.Get() : nullptr;

	if (NameText)
	{
		const FText Shown = TNLocText::PlayerName(PS ? PS->GetPlayerName() : FString());
		if (!NameText->GetText().ToString().Equals(Shown.ToString())) { NameText->SetText(Shown); }
	}

	// Cara según cómo va la tortuga.
	const ETNTurtleFace Prev = static_cast<ETNTurtleFace>(ShownFace);
	ETNTurtleFace Face = FaceFor(PS, Pawn);
	if (CVarHUDFace.GetValueOnGameThread() >= 0) { Face = static_cast<ETNTurtleFace>(FMath::Clamp(CVarHUDFace.GetValueOnGameThread(), 0, static_cast<int32>(ETNTurtleFace::Win))); }
	if (Face != Prev)
	{
		ShownFace = static_cast<uint8>(Face);
		SetImageTexture(FaceImage, TNHUDFaces::TurtleFace(Face));
		FacePop = 1.f;
	}
	FacePop = FMath::Max(0.f, FacePop - DeltaTime * 4.f);

	// Hablando por la voz de proximidad: la cara rebota como si hablara y sale el bocadillo con las barras.
	const UProximityVoiceComponent* Voice = Pawn ? Pawn->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
	const int32 ForceTalk = CVarHUDTalk.GetValueOnGameThread();
	const bool bTalking = ForceTalk >= 0 ? ForceTalk > 0 : (Voice && Voice->IsHeardSpeaking());
	float Scale = 1.f + 0.22f * FMath::Sin(FacePop * PI);
	if (bTalking) { Scale *= 1.f + 0.07f * FMath::Abs(FMath::Sin(Time * 17.f)); }
	if (FaceImage) { FaceImage->SetRenderScale(FVector2D(Scale, Scale)); }
	if (TalkBubble)
	{
		TalkBubble->SetVisibility(bTalking ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bTalking)
		{
			TalkBubble->SetRenderTransformAngle(5.f * FMath::Sin(Time * 6.f));
			AnimateTalkBars(TalkBars, 0, Time, 0.f);
		}
	}
}

void UTN_RunHUDWidget::TickInventory(float DeltaTime)
{
	using namespace TNRunHUDDetail;
	if (ItemImages.Num() < 2 || SlotTags.Num() < 2) { return; }
	// La clase base pinta el equipado y el guardado en sus imágenes (ocultas); aquí se reparten entre las dos
	// burbujas. Si solo se han intercambiado (rotar objetos), los objetos se quedan donde estaban y es el aro de
	// cuerda el que rueda a la otra burbuja.
	auto IconOf = [](const UImage* I) -> UObject*
	{
		return I && I->GetColorAndOpacity().A > 0.01f ? I->GetBrush().GetResourceObject() : nullptr;
	};
	UObject* Equipped = IconOf(SlotEquippedImage);
	UObject* Stored = IconOf(SlotStoredImage);
	if (Equipped != LastEquippedIcon.Get() || Stored != LastStoredIcon.Get())
	{
		const bool bSwapped = (Equipped || Stored) && Equipped == LastStoredIcon.Get() && Stored == LastEquippedIcon.Get();
		if (bSwapped) { EquippedSide = 1 - EquippedSide; }
		LastEquippedIcon = Equipped;
		LastStoredIcon = Stored;
		UObject* Shown[2];
		Shown[EquippedSide] = Equipped;
		Shown[1 - EquippedSide] = Stored;
		for (int32 i = 0; i < 2; ++i)
		{
			SetImageTexture(ItemImages[i], Cast<UTexture2D>(Shown[i]));
			ItemImages[i]->SetColorAndOpacity(Shown[i] ? FLinearColor::White : FLinearColor::Transparent);
		}
		SlotTags[EquippedSide]->SetText(NSLOCTEXT("TNHUD", "InHand", "EN LA ALETA"));
		SlotTags[1 - EquippedSide]->SetText(NSLOCTEXT("TNHUD", "Stored", "EN EL CAPARAZÓN"));
	}
	// El aro rueda (se traslada y gira) hasta la burbuja de la aleta.
	const float Pitch = SlotColumn + SlotGap;
	RopeX = FMath::FInterpTo(RopeX, EquippedSide * Pitch, DeltaTime, 12.f);
	if (RopeImage)
	{
		RopeImage->SetRenderTranslation(FVector2D(RopeX, 0.f));
		RopeImage->SetRenderTransformAngle(RopeX / Pitch * 180.f);
	}
}

int32 UTN_RunHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	int32 DotLayer = Layer;
	if (bAimDotShown)
	{
		// Punto blanco en el centro de la pantalla (con un borde negro para que se vea sobre cualquier fondo).
		// El color va en MakeBox: el tinte del brush no llega al relleno del RoundedBox y el aro salía blanco (#264).
		static const FSlateRoundedBoxBrush DotRim(FLinearColor::White, 8.25f);
		static const FSlateRoundedBoxBrush Dot(FLinearColor::White, 5.25f);
		const FVector2f Center = AllottedGeometry.GetLocalSize() * 0.5f;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(16.5f, 16.5f),
			FSlateLayoutTransform(Center - FVector2f(8.25f, 8.25f))), &DotRim, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, Tint.A));
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(FVector2f(10.5f, 10.5f),
			FSlateLayoutTransform(Center - FVector2f(5.25f, 5.25f))), &Dot, ESlateDrawEffect::None, FLinearColor::White * Tint);
		DotLayer = Layer + 2;
	}
	return DotLayer;
}

void UTN_RunHUDWidget::TickAlerts(float DeltaTime)
{
	using namespace TNRunHUDDetail;
	// Avisos de la tortuga propia.
	const APlayerController* OwningPC = GetOwningPlayer();
	const ATN_CoopPlayerState* PS = OwningPC ? OwningPC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;

	// Tormenta: cuenta atrás mientras se está dentro.
	const bool bInStorm = PS && PS->DeathZoneTimeRemaining >= 0.f && PS->bIsAlive;
	if (StormBanner)
	{
		StormBanner->SetVisibility(bInStorm ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bInStorm && StormText)
		{
			FNumberFormattingOptions OneDecimal;
			OneDecimal.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
			StormText->SetText(FText::Format(NSLOCTEXT("TNHUD", "Storm", "¡La tormenta te alcanza! ¡Al agua!   {0}"), FText::AsNumber(PS->DeathZoneTimeRemaining, &OneDecimal)));
			StormBanner->SetRenderScale(FVector2D(1.f + 0.04f * FMath::Abs(FMath::Sin(Time * 6.f))));
		}
	}

	// Panza arriba: lo que queda para que un compañero te dé la vuelta.
	const bool bDown = PS && PS->bIsDBNO;
	if (DownBanner)
	{
		DownBanner->SetVisibility(bDown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bDown && DownText)
		{
			DownText->SetText(FText::Format(NSLOCTEXT("TNHUD", "Down", "¡Panza arriba!\nUn compañero puede darte la vuelta:  {0} s"),
				FText::AsNumber(FMath::Max(0, FMath::CeilToInt(PS->DBNOBleedoutTimeRemaining)))));
		}
	}

	// Dando la vuelta a un compañero.
	const ATortugaCharacter* Turtle = OwningPC ? Cast<ATortugaCharacter>(OwningPC->GetPawn()) : nullptr;
	const bool bReviving = Turtle && Turtle->bIsReviving;
	if (ReviveBanner)
	{
		ReviveBanner->SetVisibility(bReviving ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bReviving && ReviveBar) { ReviveBar->SetPercent(FMath::Clamp(Turtle->ReviveProgress, 0.f, 1.f)); }
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Cartel de estado, resultados, tripulación y mensajes
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RunFlowHUDWidget::NativeOnInitialized()
{
	using namespace TNRunHUDDetail;
	BuildTree();
	Super::NativeOnInitialized();
}

void UTN_RunFlowHUDWidget::BuildTree()
{
	using namespace TNRunHUDDetail;
	if (!WidgetTree || Canvas) { return; }
	UWidgetTree* Tree = WidgetTree;
	Canvas = Make<UCanvasPanel>(Tree, TEXT("RunFlowCanvas"));
	Tree->RootWidget = Canvas;

	// ── Cartel de estado (arriba a la izquierda, fuera de la carrera): azul marino con ola, ancla y dos líneas ──
	{
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(MakeImage(Tree, TNHUDArt::AnchorIcon(), FVector2D(40.f, 40.f)))) { S->SetVerticalAlignment(VAlign_Center); }
		RootContainer = Make<UVerticalBox>(Tree, TEXT("RootContainer"));
		PrimaryText = MakeText(Tree, TEXT("PrimaryText"), FText::GetEmpty(), TEXT("Bold"), 23, TNHUDArt::SandC);
		PrimaryText->SetAutoWrapText(true);
		RootContainer->AddChildToVerticalBox(PrimaryText);
		SecondaryText = MakeText(Tree, TEXT("SecondaryText"), FText::GetEmpty(), TEXT("Regular"), 16, TNHUDArt::Foam);
		SecondaryText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* S = RootContainer->AddChildToVerticalBox(SecondaryText)) { S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f)); }
		USizeBox* Limit = MakeSize(Tree, RootContainer, 0.f, 0.f);
		Limit->SetMaxDesiredWidth(470.f);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Limit)) { S->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f)); S->SetVerticalAlignment(VAlign_Center); }
		// El filo de arriba del cartel mide ~26 px (CardMargin): el texto empieza por debajo, no pegado al borde.
		UBorder* Status = MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, Row, FMargin(22.f, 28.f, 30.f, 42.f));
		Status->SetHorizontalAlignment(HAlign_Left);
		Status->SetRenderTransformAngle(-1.5f);
		StatusCard = Status;
		Place(Canvas, Status, FVector2D(0.f, 0.f), FVector2D(22.f, 14.f));
	}

	// ── Fuera de carrera (te han eliminado): cara mareada ──
	{
		UVerticalBox* Lines = Make<UVerticalBox>(Tree);
		Lines->AddChildToVerticalBox(MakeText(Tree, nullptr, NSLOCTEXT("TNHUD", "OutTitle", "¡Fuera de carrera!"), TEXT("Bold"), 24, TNHUDArt::Hex(0xFF9A85)));
		if (UVerticalBoxSlot* S = Lines->AddChildToVerticalBox(MakeText(Tree, nullptr, NSLOCTEXT("TNHUD", "OutHint", "Anima a los demás hasta que lleguen al agua."), TEXT("Regular"), 15, TNHUDArt::Foam)))
		{
			S->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}
		UBorder* Out = MakeFaceCard(Tree, ETNTurtleFace::Down, 88.f, Lines);
		Out->SetVisibility(ESlateVisibility::Collapsed);
		OutCard = Out;
		Place(Canvas, Out, FVector2D(0.5f, 0.f), FVector2D(0.f, 120.f));
	}

	// ── Tripulación (izquierda): la cara de cada compañero en un aro de su color, su nombre y sus bocadillos ──
	{
		UVerticalBox* Crew = Make<UVerticalBox>(Tree);
		for (int32 i = 0; i < MaxMates; ++i)
		{
			UOverlay* Row = Make<UOverlay>(Tree);
			UOverlay* Portrait = Make<UOverlay>(Tree);
			UImage* RingImg = Make<UImage>(Tree);
			RingImg->SetBrush(Rounded(MateColors[i], 36.f, TNHUDArt::Navy, 3.f));
			CrewRings.Add(RingImg);
			AddAt(Portrait, MakeSize(Tree, RingImg, 72.f, 72.f), HAlign_Center, VAlign_Center);
			UImage* FaceImg = MakeImage(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy), FVector2D(68.f, 68.f));
			CrewFaces.Add(FaceImg);
			AddAt(Portrait, FaceImg, HAlign_Center, VAlign_Center);
			UHorizontalBox* Line = Make<UHorizontalBox>(Tree);
			Line->AddChildToHorizontalBox(MakeSize(Tree, Portrait, 76.f, 76.f));
			UTextBlock* NameTxt = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 13, TNHUDArt::Cream);
			CrewNames.Add(NameTxt);
			UBorder* NamePill = Make<UBorder>(Tree);
			StylePanel(NamePill, TNHUDArt::Hex(0x0A1C38, 0.75f), 10.f, FMargin(10.f, 3.f, 12.f, 4.f), FLinearColor::Transparent, 0.f);
			NamePill->SetContent(NameTxt);
			if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(NamePill)) { S->SetVerticalAlignment(VAlign_Bottom); S->SetPadding(FMargin(-10.f, 0.f, 0.f, 4.f)); }
			AddAt(Row, Line, HAlign_Left, VAlign_Bottom);
			// Voz: bocadillo pequeño con barras arriba a la derecha de la cara.
			UOverlay* Talk = MakeTalkBubble(Tree, 52.f, CrewTalkBars);
			Talk->SetVisibility(ESlateVisibility::Collapsed);
			CrewTalk.Add(Talk);
			AddAt(Row, Talk, HAlign_Left, VAlign_Top, FMargin(56.f, -6.f, 0.f, 0.f));
			// Frase del chat rápido: bocadillo a la derecha, pasado el de la voz para no pisarse, con la cola hacia la cara;
			// crece hacia arriba (anclado por abajo, por encima del nombre) si la frase ocupa varias líneas.
			UTextBlock* Say = nullptr;
			UBorder* Bubble = MakeChatBubble(Tree, 17, 240.f, Say);
			Bubbles.Add(Bubble);
			BubbleTexts.Add(Say);
			AddAt(Row, Bubble, HAlign_Left, VAlign_Bottom, FMargin(108.f, 0.f, 0.f, 36.f));
			Row->SetVisibility(ESlateVisibility::Collapsed);
			CrewRows.Add(Row);
			if (UVerticalBoxSlot* S = Crew->AddChildToVerticalBox(Row)) { S->SetPadding(FMargin(0.f, 34.f, 0.f, 0.f)); }
		}
		CrewPlayerIds.Init(INDEX_NONE, MaxMates);
		CrewFaceShown.Init(0xFF, MaxMates);
		CrewBox = Crew;
		Place(Canvas, Crew, FVector2D(0.f, 0.3f), FVector2D(20.f, 0.f));
	}

	// ── Tu frase: bocadillo junto a tu distintivo (abajo a la izquierda) ──
	{
		UTextBlock* Say = nullptr;
		UBorder* Bubble = MakeChatBubble(Tree, 18, 260.f, Say);
		Bubbles.Add(Bubble);
		BubbleTexts.Add(Say);
		Place(Canvas, Bubble, FVector2D(0.f, 1.f), FVector2D(226.f, -150.f));
	}
	BubbleTime.Init(0.f, Bubbles.Num());

	// El chat global de la clase base no se usa (las frases salen en bocadillos); la caja existe oculta.
	{
		ChatHistoryBox = Make<UVerticalBox>(Tree, TEXT("ChatHistoryBox"));
		ChatHistoryBox->SetVisibility(ESlateVisibility::Collapsed);
		Place(Canvas, ChatHistoryBox, FVector2D(0.f, 0.f), FVector2D(0.f, 0.f));
	}

	// ── Aviso de espectador (abajo, sobre el inventario) ──
	{
		SpectatorHint = MakeText(Tree, TEXT("SpectatorHint"), FText::GetEmpty(), TEXT("Bold"), 15, TNHUDArt::SandC);
		SpectatorHint->SetVisibility(ESlateVisibility::Collapsed);
		Place(Canvas, SpectatorHint, FVector2D(0.5f, 1.f), FVector2D(0.f, -170.f));
	}

	// ── Resultados: el mar oscurecido y un cartel con la cara, el puesto y la clasificación en conchas ──
	{
		UVerticalBox* Board = Make<UVerticalBox>(Tree);
		ResultsFace = MakeImage(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Win), FVector2D(150.f, 150.f));
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(ResultsFace)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, -70.f, 0.f, 0.f)); }
		ResultsTitle = MakeText(Tree, TEXT("ResultsTitle"), FText::GetEmpty(), TEXT("Bold"), 44, TNHUDArt::SandC);
		ResultsTitle->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(ResultsTitle)) { S->SetHorizontalAlignment(HAlign_Center); }
		ResultsRankText = MakeText(Tree, TEXT("ResultsRankText"), FText::GetEmpty(), TEXT("Bold"), 24, FLinearColor::White);
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(ResultsRankText)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }
		ResultsTimeText = MakeText(Tree, TEXT("ResultsTimeText"), FText::GetEmpty(), TEXT("Regular"), 20, TNHUDArt::Foam);
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(ResultsTimeText)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, 2.f, 0.f, 14.f)); }
		// Puntuación final del Coop (#789): solo se ve si la partida la tiene.
		CoopScoreText = MakeText(Tree, TEXT("CoopScoreText"), FText::GetEmpty(), TEXT("Regular"), 18, FLinearColor::White);
		CoopScoreText->SetJustification(ETextJustify::Center);
		CoopScoreText->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(CoopScoreText)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f)); }
		// Títulos de fin de partida (#798): Saltarín, en dorado.
		EndTitleText = MakeText(Tree, TEXT("EndTitleText"), FText::GetEmpty(), TEXT("Bold"), 20, TNHUDArt::Gold);
		EndTitleText->SetJustification(ETextJustify::Center);
		EndTitleText->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(EndTitleText)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f)); }

		// Clasificación: una fila por jugador que cabe (puesto, nombre, tiempo, puntos con su concha), alternando el fondo.
		// Las cuatro primeras se ven siempre; de la quinta a la octava, solo si hay tantos resultados (ApplyScoreboardDensity,
		// que también las compacta).
		TObjectPtr<UTextBlock>* Cells[MaxScoreboardRows][4] = {
			{ &Row1RankText, &Row1NameText, &Row1TimeText, &Row1ScoreText },
			{ &Row2RankText, &Row2NameText, &Row2TimeText, &Row2ScoreText },
			{ &Row3RankText, &Row3NameText, &Row3TimeText, &Row3ScoreText },
			{ &Row4RankText, &Row4NameText, &Row4TimeText, &Row4ScoreText },
			{ &Row5RankText, &Row5NameText, &Row5TimeText, &Row5ScoreText },
			{ &Row6RankText, &Row6NameText, &Row6TimeText, &Row6ScoreText },
			{ &Row7RankText, &Row7NameText, &Row7TimeText, &Row7ScoreText },
			{ &Row8RankText, &Row8NameText, &Row8TimeText, &Row8ScoreText } };
		const float Widths[4] = { 60.f, 270.f, 130.f, 90.f };
		ScoreboardRowPanels.Reset();
		for (int32 r = 0; r < MaxScoreboardRows; ++r)
		{
			UHorizontalBox* Line = Make<UHorizontalBox>(Tree);
			for (int32 c = 0; c < 4; ++c)
			{
				UTextBlock* Cell = MakeText(Tree, nullptr, FText::GetEmpty(), c == 1 ? TEXT("Bold") : TEXT("Regular"), 19, c == 0 ? TNHUDArt::SandC : FLinearColor::White);
				Cell->SetJustification(c >= 2 ? ETextJustify::Right : ETextJustify::Left);
				*Cells[r][c] = Cell;
				if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(MakeSize(Tree, Cell, Widths[c], 0.f))) { S->SetVerticalAlignment(VAlign_Center); }
			}
			if (UHorizontalBoxSlot* S = Line->AddChildToHorizontalBox(MakeImage(Tree, TNHUDArt::ShellIcon(), FVector2D(30.f, 30.f))))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
			}
			UBorder* Stripe = Make<UBorder>(Tree);
			StylePanel(Stripe, (r % 2) == 0 ? TNHUDArt::Hex(0x62D2EA, 0.1f) : FLinearColor::Transparent, 12.f, FMargin(14.f, 5.f), FLinearColor::Transparent, 0.f);
			Stripe->SetContent(Line);
			if (r >= 4) { Stripe->SetVisibility(ESlateVisibility::Collapsed); }
			ScoreboardRowPanels.Add(Stripe);
			if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(Stripe)) { S->SetPadding(FMargin(0.f, 1.f)); }
		}
		ResultsCountdown = MakeText(Tree, TEXT("ResultsCountdown"), FText::GetEmpty(), TEXT("Regular"), 17, TNHUDArt::Foam);
		if (UVerticalBoxSlot* S = Board->AddChildToVerticalBox(ResultsCountdown)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f)); }

		UBorder* BoardCard = MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, Board, FMargin(42.f, 30.f, 42.f, 50.f));
		UBorder* Dim = Make<UBorder>(Tree);
		StylePanel(Dim, TNHUDArt::Hex(0x06121F, 0.55f), 0.f, FMargin(0.f), FLinearColor::Transparent, 0.f);
		Dim->SetContent(BoardCard);
		Dim->SetHorizontalAlignment(HAlign_Center);
		Dim->SetVerticalAlignment(VAlign_Center);
		ResultsOverlay = Dim;
		ResultsOverlay->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* DimSlot = Canvas->AddChildToCanvas(Dim);
		DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DimSlot->SetOffsets(FMargin(0.f));
	}
}

void UTN_RunFlowHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace TNRunHUDDetail;
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	const UWorld* World = GetWorld();
	const ATN_CoopGameState* GS = World ? World->GetGameState<ATN_CoopGameState>() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	const ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	const bool bRacing = GS && GS->MatchFlowState == ETNMatchFlowState::InProgress;

	// En carrera el objetivo ya lo cuenta la pista del mar: sin cartel de estado.
	if (StatusCard) { StatusCard->SetVisibility(bRacing ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }

	const bool bOut = bRacing && PS && PS->bIsEliminated;
	if (OutCard)
	{
		OutCard->SetVisibility(bOut ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bOut) { OutCard->SetRenderTransformAngle(2.f * FMath::Sin(Time * 2.f)); }
	}

	// Resultados: ojos de estrella si llegaste al agua, mareada si no; el título, del color de la medalla.
	if (ResultsOverlay && ResultsOverlay->IsVisible() && PS)
	{
		const bool bArrived = PS->bHasFinishedRun && !PS->bIsEliminated && PS->FinishRank > 0;
		SetImageTexture(ResultsFace, TNHUDFaces::TurtleFace(bArrived ? ETNTurtleFace::Win : ETNTurtleFace::Down));
		if (ResultsFace) { ResultsFace->SetRenderScale(FVector2D(1.f + 0.05f * FMath::Sin(Time * 3.f))); }
		if (ResultsTitle)
		{
			FLinearColor Medal = TNHUDArt::Hex(0xFF9A85);
			if (bArrived)
			{
				Medal = PS->FinishRank == 1 ? TNHUDArt::Gold : PS->FinishRank == 2 ? TNHUDArt::Hex(0xDDE6EE) : PS->FinishRank == 3 ? TNHUDArt::Hex(0xE8A56B) : TNHUDArt::SandC;
			}
			ResultsTitle->SetColorAndOpacity(FSlateColor(Medal));
		}
	}

	TickCrew(InDeltaTime);
}

void UTN_RunFlowHUDWidget::TickCrew(float DeltaTime)
{
	using namespace TNRunHUDDetail;
	const APlayerController* PC = GetOwningPlayer();
	// Todos menos la tortuga del distintivo: la tuya.
	TArray<const APlayerState*> Crew = CrewOf(GetWorld(), PC ? PC->PlayerState.Get() : nullptr);
	const int32 Preview = FMath::Min(CVarHUDCrew.GetValueOnGameThread(), CrewRows.Num());
	if (Preview > 0 && PC && PC->PlayerState)
	{
		Crew.Init(PC->PlayerState.Get(), Preview);
		if (Bubbles.IsValidIndex(0) && BubbleTime[0] <= 0.f) { ShowBubble(0, NSLOCTEXT("TNHUD", "PreviewSay", "¡Por aquí, que hay conchas!")); }
	}
	for (int32 i = 0; i < CrewRows.Num(); ++i)
	{
		const APlayerState* PS = Crew.IsValidIndex(i) ? Crew[i] : nullptr;
		CrewRows[i]->SetVisibility(PS ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		CrewPlayerIds[i] = PS ? PS->GetPlayerId() : INDEX_NONE;
		if (!PS) { continue; }
		const APawn* Pawn = TurtleOf(GetWorld(), PS);
		const ETNTurtleFace Face = FaceFor(PS, Pawn);
		if (static_cast<uint8>(Face) != CrewFaceShown[i])
		{
			CrewFaceShown[i] = static_cast<uint8>(Face);
			SetImageTexture(CrewFaces[i], TNHUDFaces::TurtleFace(Face));
		}
		const FString PlayerName = PS->GetPlayerName();
		if (!CrewNames[i]->GetText().ToString().Equals(PlayerName)) { CrewNames[i]->SetText(TNLocText::Literal(PlayerName)); }
		// Voz: el bocadillo con barras mientras llega su audio.
		const UProximityVoiceComponent* Voice = Pawn ? Pawn->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
		const bool bTalking = (Voice && Voice->IsHeardSpeaking()) || (Preview > 1 && i == 1);
		CrewTalk[i]->SetVisibility(bTalking ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bTalking) { AnimateTalkBars(CrewTalkBars, i * 4, Time, i * 2.1f); }
		CrewFaces[i]->SetRenderScale(FVector2D(bTalking ? 1.f + 0.06f * FMath::Abs(FMath::Sin(Time * 17.f + i)) : 1.f));
	}

	// Con más de tres compañeros (hasta siete), las filas se juntan y, con seis o siete, se encogen un poco: caben entre el
	// marcador de arriba y tu distintivo de abajo.
	int32 VisibleRows = 0;
	for (const TObjectPtr<UWidget>& CrewRow : CrewRows)
	{
		if (CrewRow && CrewRow->GetVisibility() != ESlateVisibility::Collapsed) { ++VisibleRows; }
	}
	const int32 Layout = VisibleRows <= 3 ? 0 : (VisibleRows <= 5 ? 1 : 2);
	if (Layout != CrewLayoutShown)
	{
		CrewLayoutShown = Layout;
		const float Gap = Layout == 0 ? 34.f : (Layout == 1 ? 12.f : 4.f);
		for (const TObjectPtr<UWidget>& CrewRow : CrewRows)
		{
			if (UVerticalBoxSlot* RowSlot = CrewRow ? Cast<UVerticalBoxSlot>(CrewRow->Slot) : nullptr) { RowSlot->SetPadding(FMargin(0.f, Gap, 0.f, 0.f)); }
		}
		if (CrewBox)
		{
			const float Scale = Layout == 2 ? 0.88f : 1.f;
			CrewBox->SetRenderTransformPivot(FVector2D(0.f, 0.3f));
			CrewBox->SetRenderScale(FVector2D(Scale, Scale));
		}
	}

	// Bocadillos del chat: entran con un rebote y se van encogiendo al final.
	for (int32 b = 0; b < Bubbles.Num(); ++b)
	{
		if (BubbleTime[b] <= 0.f) { continue; }
		BubbleTime[b] -= DeltaTime;
		const float In = FMath::Clamp((BubbleLife - BubbleTime[b]) / 0.18f, 0.f, 1.f);
		const float Out = FMath::Clamp(BubbleTime[b] / 0.3f, 0.f, 1.f);
		Bubbles[b]->SetRenderScale(FVector2D(0.55f + 0.45f * FMath::Min(In, Out) + 0.08f * FMath::Sin(In * PI)));
		Bubbles[b]->SetRenderOpacity(Out);
		if (BubbleTime[b] <= 0.f) { Bubbles[b]->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

void UTN_RunFlowHUDWidget::ShowBubble(int32 Row, const FText& MessageText)
{
	using namespace TNRunHUDDetail;
	if (!Bubbles.IsValidIndex(Row) || !BubbleTexts.IsValidIndex(Row)) { return; }
	BubbleTexts[Row]->SetText(MessageText);
	Bubbles[Row]->SetVisibility(ESlateVisibility::HitTestInvisible);
	Bubbles[Row]->SetRenderOpacity(1.f);
	BubbleTime[Row] = BubbleLife;
}

void UTN_RunFlowHUDWidget::OnQuickChatEntryReceived_Implementation(int32 Sequence, const FText& SenderName, const FText& MessageText,
	UTexture2D* Icon, float ServerTimeSeconds)
{
	// Sin chat global: la frase sale en un bocadillo junto a la cara de quien la dice. Las viejas (el historial que
	// se repasa al crear el widget) no se enseñan.
	const UWorld* World = GetWorld();
	const ATN_CoopGameState* GS = World ? World->GetGameState<ATN_CoopGameState>() : nullptr;
	if (!GS || GS->GetServerWorldTimeSeconds() - ServerTimeSeconds > 6.0) { return; }
	int32 SenderId = INDEX_NONE;
	for (const FTN_QuickChatEntry& Entry : GS->QuickChatHistory)
	{
		if (Entry.Sequence == Sequence) { SenderId = Entry.SenderPlayerId; }
	}
	// Junto a la cara de quien la dice: la del distintivo (la tuya) o su fila.
	const APlayerController* OwningPC = GetOwningPlayer();
	const APlayerState* Own = OwningPC ? OwningPC->PlayerState.Get() : nullptr;
	int32 Row = Bubbles.Num() - 1;
	if (SenderId != INDEX_NONE && !(Own && Own->GetPlayerId() == SenderId))
	{
		TickCrew(0.f);
		Row = CrewPlayerIds.IndexOfByKey(SenderId);
		if (Row == INDEX_NONE) { return; }
	}
	ShowBubble(Row, MessageText);
}

// ─────────────────────────────────────────────────────────────────────────────
// Rueda radial (emotes y frases)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RunRadialWheelWidget::NativeOnInitialized()
{
	using namespace TNRunHUDDetail;
	BuildTree();
	Super::NativeOnInitialized();
}

void UTN_RunRadialWheelWidget::SetTitle(const FText& InTitle)
{
	using namespace TNRunHUDDetail;
	PendingTitle = InTitle;
	if (TitleText) { TitleText->SetText(InTitle); }
}

void UTN_RunRadialWheelWidget::BuildTree()
{
	using namespace TNRunHUDDetail;
	if (!WidgetTree || Canvas) { return; }
	UWidgetTree* Tree = WidgetTree;
	Canvas = Make<UCanvasPanel>(Tree, TEXT("WheelCanvas"));
	Tree->RootWidget = Canvas;

	// Velo azul marino sobre la partida.
	UBorder* Veil = Make<UBorder>(Tree);
	StylePanel(Veil, TNHUDArt::Hex(0x06121F, 0.35f), 0.f, FMargin(0.f), FLinearColor::Transparent, 0.f);
	UCanvasPanelSlot* VeilSlot = Canvas->AddChildToCanvas(Veil);
	VeilSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	VeilSlot->SetOffsets(FMargin(0.f));

	// La rueda, centrada en la pantalla (desde donde se mide el ratón).
	UOverlay* Wheel = Make<UOverlay>(Tree);
	Disc = Make<UImage>(Tree);
	DiscMID = MakeUIMID(this, TEXT("/Game/UI/HUD/M_UI_RadialWheel.M_UI_RadialWheel"));
	if (DiscMID) { Disc->SetBrushFromMaterial(DiscMID); }
	AddAt(Wheel, Disc, HAlign_Fill, VAlign_Fill);
	SlotLayer = Make<UCanvasPanel>(Tree);
	AddAt(Wheel, SlotLayer, HAlign_Fill, VAlign_Fill);
	AddAt(Wheel, MakeImage(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy), FVector2D(WheelSize * 0.3f, WheelSize * 0.3f)), HAlign_Center, VAlign_Center);
	UCanvasPanelSlot* WheelSlot = Canvas->AddChildToCanvas(MakeSize(Tree, Wheel, WheelSize, WheelSize));
	WheelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	WheelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	WheelSlot->SetAutoSize(true);
	WheelSlot->SetPosition(FVector2D::ZeroVector);

	// Título en una cinta arriba y la opción apuntada en una etiqueta de arena abajo.
	TitleText = MakeText(Tree, nullptr, PendingTitle, TEXT("Bold"), 22, FLinearColor::White);
	Place(Canvas, MakeCard(Tree, TNHUDArt::RibbonTexture(), RibbonMargin, TitleText, FMargin(48.f, 18.f, 48.f, 20.f)), FVector2D(0.5f, 0.5f),
		FVector2D(0.f, -WheelSize * 0.5f - 14.f));
	ChoiceText = MakeText(Tree, nullptr, FText::GetEmpty(), TEXT("Bold"), 20, NavyText, false);
	UBorder* Tag = MakeCard(Tree, TNHUDArt::SandTagTexture(), TagMargin, ChoiceText, FMargin(28.f, 15.f, 28.f, 16.f));
	ChoiceTag = Tag;
	Place(Canvas, Tag, FVector2D(0.5f, 0.5f), FVector2D(0.f, WheelSize * 0.5f + 22.f));
}

void UTN_RunRadialWheelWidget::BP_OnEntriesSet_Implementation(const TArray<FTN_RadialWheelEntryView>& InEntries)
{
	using namespace TNRunHUDDetail;
	BuildTree();
	if (!SlotLayer) { return; }
	SlotLayer->ClearChildren();
	SlotWidgets.Reset();
	SlotLabels.Reset();
	const int32 N = InEntries.Num();
	if (DiscMID)
	{
		DiscMID->SetScalarParameterValue(TEXT("Slices"), static_cast<float>(FMath::Max(1, N)));
		DiscMID->SetScalarParameterValue(TEXT("Selected"), -1.f);
		DiscMID->SetScalarParameterValue(TEXT("OffsetDeg"), SelectionAngleOffsetDegrees);
	}
	if (N == 0) { return; }
	UWidgetTree* Tree = WidgetTree;
	const float Step = 2.f * PI / N;
	const float Offset = FMath::DegreesToRadians(SelectionAngleOffsetDegrees);
	for (int32 i = 0; i < N; ++i)
	{
		const FTN_RadialWheelEntryView& Entry = InEntries[i];
		// Mismo convenio que UTN_RadialWheelWidgetBase::UpdateInputVector: ángulo matemático con Y hacia arriba.
		const float Angle = Offset + Step * i;
		UVerticalBox* Box = Make<UVerticalBox>(Tree);
		if (Entry.Icon)
		{
			if (UVerticalBoxSlot* S = Box->AddChildToVerticalBox(MakeImage(Tree, Entry.Icon, FVector2D(44.f, 44.f)))) { S->SetHorizontalAlignment(HAlign_Center); }
		}
		UTextBlock* Label = MakeText(Tree, nullptr, Entry.Label, TEXT("Bold"), 15, TNHUDArt::Cream);
		Label->SetJustification(ETextJustify::Center);
		Label->SetAutoWrapText(true);
		USizeBox* Limit = MakeSize(Tree, Label, 0.f, 0.f);
		Limit->SetMaxDesiredWidth(118.f);
		if (UVerticalBoxSlot* S = Box->AddChildToVerticalBox(Limit)) { S->SetHorizontalAlignment(HAlign_Center); }
		Box->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		if (!Entry.bEnabled) { Box->SetRenderOpacity(0.4f); }
		UCanvasPanelSlot* S = SlotLayer->AddChildToCanvas(Box);
		S->SetAnchors(FAnchors(0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetAutoSize(true);
		S->SetPosition(FVector2D(FMath::Cos(Angle), -FMath::Sin(Angle)) * WheelSlotRadius);
		SlotWidgets.Add(Box);
		SlotLabels.Add(Label);
	}
	ShownSelection = -2;
}

void UTN_RunRadialWheelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace TNRunHUDDetail;
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	const int32 Sel = GetSelectedIndex();
	// Con mando se apunta con el stick: la ayuda cambia al momento si se cambia de aparato con la rueda abierta (#347).
	const bool bPad = UTN_GameSettingsSubsystem::IsUsingGamepad(GetOwningPlayer());
	if (Sel != ShownSelection || bPad != bShownPad)
	{
		ShownSelection = Sel;
		bShownPad = bPad;
		if (DiscMID) { DiscMID->SetScalarParameterValue(TEXT("Selected"), static_cast<float>(Sel)); }
		const TArray<FTN_RadialWheelEntryView>& List = GetEntries();
		if (ChoiceText)
		{
			const FText Hint = bPad ? NSLOCTEXT("TNHUD", "WheelHintPad", "Apunta con el stick") : NSLOCTEXT("TNHUD", "WheelHint", "Apunta con el ratón");
			ChoiceText->SetText(List.IsValidIndex(Sel) ? List[Sel].Label : Hint);
		}
		for (int32 i = 0; i < SlotLabels.Num(); ++i)
		{
			SlotLabels[i]->SetColorAndOpacity(FSlateColor(i == Sel ? TNHUDArt::Gold : TNHUDArt::Cream));
		}
	}
	for (int32 i = 0; i < SlotWidgets.Num(); ++i)
	{
		const float Scale = i == Sel ? 1.18f + 0.04f * FMath::Sin(Time * 8.f) : 1.f;
		SlotWidgets[i]->SetRenderScale(FVector2D(Scale, Scale));
	}
	if (ChoiceTag) { ChoiceTag->SetRenderTransformAngle(Sel >= 0 ? 2.f * FMath::Sin(Time * 5.f) : 0.f); }
}

int32 UTN_RunRadialWheelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// La rueda la pinta el material: sin las líneas de la clase base.
	return UUserWidget::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}
