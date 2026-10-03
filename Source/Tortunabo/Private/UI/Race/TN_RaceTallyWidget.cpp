#include "UI/Race/TN_RaceTallyWidget.h"
#include "TN_RaceArt.h"
#include "TN_RaceUIKit.h"
#include "Audio/TN_ScoreShellSynthComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "World/TN_ScoreShells.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNRaceTallyDetail
{
	/** Columna de un jugador (lienzo propio, en unidades de la interfaz a 1080 p). */
	constexpr float ColumnW = 240.f;
	constexpr float ColumnH = 632.f;
	constexpr float ColumnGap = 26.f;
	/** Ancho máximo de la fila de columnas (px a 1080 p): con siete u ocho jugadores se encoge para caber. */
	constexpr float MaxRowWidth = 1820.f;
	/** Huecos en zigzag: del de abajo (el primero) al de arriba, y la corona del campeón encima. */
	constexpr float SocketSize = 100.f;
	constexpr float ShellSize = 88.f;
	constexpr float ZigX = 48.f;
	constexpr float SlotTopY = 150.f;
	constexpr float SlotBottomY = 370.f;
	constexpr float CrownY = 58.f;
	constexpr float CrownW = 84.f;
	constexpr float CrownH = 63.f;
	/** Cara en su aro y etiqueta del nombre. */
	constexpr float FaceY = 500.f;
	constexpr float RingSize = 150.f;
	constexpr float NameY = 604.f;
	/** Concha de la ronda al nacer en el centro de la pantalla (px) y cuánto dura cada tramo (s). */
	constexpr float FlightSize = 210.f;
	constexpr float HoverSeconds = 0.55f;
	constexpr float FlightSeconds = 0.65f;
	/** Medias conchas: una cada tanto (s) y lo que tarda la otra mitad en encajar con la que ya había. */
	constexpr float HalfStagger = 0.35f;
	constexpr float JoinSeconds = 0.25f;
	/** Burbujas del fondo. */
	constexpr int32 NumBubbles = 14;

	/** Centro del hueco K (0 = abajo) en el lienzo de la columna. */
	FVector2D SlotCenter(int32 K, int32 Target)
	{
		const float Step = (SlotBottomY - SlotTopY) / static_cast<float>(FMath::Max(1, Target - 1));
		return FVector2D(ColumnW * 0.5f + ((K % 2) == 0 ? -ZigX : ZigX), SlotBottomY - Step * K);
	}

	/** Giro de reposo de la concha del hueco K (se van alternando, como colgadas de la cuerda). */
	float SlotAngle(int32 K)
	{
		return K % 2 == 0 ? -10.f : 10.f;
	}

	/** Tramo de cuerda entre dos puntos del lienzo de la columna. */
	void AddRope(UWidgetTree* Tree, UCanvasPanel* Col, const FVector2D& From, const FVector2D& To)
	{
		const FVector2D Delta = To - From;
		UImage* Rope = TNRaceUI::Make<UImage>(Tree);
		Rope->SetBrush(TNHUDStyle::Rounded(TNHUDArt::Hex(0xE9C48A), 5.f, TNHUDArt::Hex(0x8A6A3A), 1.5f));
		Rope->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Delta.Y), static_cast<float>(Delta.X))));
		TNRaceUI::PlaceAt(Col, Rope, (From + To) * 0.5, FVector2D(Delta.Size(), 10.f), FVector2D(0.5f, 0.5f));
	}

	/** Imagen de concha (entera o media) en el hueco, oculta hasta que le toque. */
	UImage* AddSlotShell(UWidgetTree* Tree, UCanvasPanel* Col, UTexture2D* Texture, const FVector2D& Center, int32 K)
	{
		UImage* Shell = TNRaceUI::MakeImage(Tree, Texture, FVector2D(ShellSize, ShellSize));
		Shell->SetRenderTransformPivot(FVector2D(0.5f, 0.6f));
		Shell->SetRenderOpacity(0.f);
		Shell->SetRenderTransformAngle(SlotAngle(K));
		TNRaceUI::PlaceAt(Col, Shell, Center, FVector2D(ShellSize, ShellSize), FVector2D(0.5f, 0.5f));
		return Shell;
	}

	/** Número pseudoaleatorio estable en [0, 1) por índice (burbujas). */
	float Hash01(int32 I, int32 Salt)
	{
		uint32 H = static_cast<uint32>(I) * 2654435761u ^ static_cast<uint32>(Salt) * 40503u;
		H ^= H >> 15; H *= 2246822519u; H ^= H >> 13;
		return static_cast<float>(H & 0xFFFF) / 65536.f;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construcción
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceTallyWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
}

void UTN_RaceTallyWidget::BuildTree()
{
	using namespace TNRaceTallyDetail;
	if (!WidgetTree || Canvas) { return; }
	UWidgetTree* Tree = WidgetTree;
	Canvas = TNRaceUI::Make<UCanvasPanel>(Tree, TEXT("RaceTallyCanvas"));
	Tree->RootWidget = Canvas;

	// Fondo: el mar azul marino con rayos de luz y la orilla de arena abajo; encima, burbujas que suben.
	TNRaceUI::Fill(Canvas, TNRaceUI::MakeImage(Tree, TNRaceArt::TallyBackdrop(), FVector2D(256.f, 256.f)));
	BubbleLayer = TNRaceUI::Make<UCanvasPanel>(Tree);
	TNRaceUI::Fill(Canvas, BubbleLayer);
	for (int32 i = 0; i < NumBubbles; ++i)
	{
		const float Size = 14.f + 22.f * Hash01(i, 3);
		UImage* Bubble = TNRaceUI::MakeImage(Tree, TNHUDArt::BubbleIcon(), FVector2D(Size, Size));
		Bubble->SetVisibility(ESlateVisibility::HitTestInvisible);
		TNRaceUI::PlaceAt(BubbleLayer, Bubble, FVector2D::ZeroVector, FVector2D(Size, Size), FVector2D(0.5f, 0.5f));
		Bubbles.Add(Bubble);
	}

	// Columnas de los jugadores (se rellenan en Setup).
	ColumnBox = TNRaceUI::Make<UHorizontalBox>(Tree);
	TNRaceUI::Place(Canvas, ColumnBox, FVector2D(0.5f, 0.5f), FVector2D(0.f, 60.f));

	// Arriba: la ronda en una cinta coral y, debajo, el cartel con el resultado.
	RoundText = TNRaceUI::MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 30, FLinearColor::White);
	TNRaceUI::Place(Canvas, TNRaceUI::MakeCard(Tree, TNHUDArt::RibbonTexture(), TNRaceUI::RibbonMargin, RoundText, FMargin(60.f, 18.f, 60.f, 20.f)),
		FVector2D(0.5f, 0.f), FVector2D(0.f, 34.f));
	{
		UHorizontalBox* Row = TNRaceUI::Make<UHorizontalBox>(Tree);
		ResultFace = TNRaceUI::MakeImage(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy), FVector2D(76.f, 76.f));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(ResultFace)) { S->SetVerticalAlignment(VAlign_Center); S->SetPadding(FMargin(0.f, -14.f, 12.f, -14.f)); }
		ResultText = TNRaceUI::MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 30, TNHUDArt::SandC);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(ResultText)) { S->SetVerticalAlignment(VAlign_Center); }
		UBorder* Card = TNRaceUI::MakeCard(Tree, TNHUDArt::CardTexture(), TNRaceUI::CardMargin, Row, FMargin(26.f, 26.f, 34.f, 40.f));
		Card->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		ResultCard = Card;
		TNRaceUI::Place(Canvas, Card, FVector2D(0.5f, 0.f), FVector2D(0.f, 118.f));
	}

	// Abajo: la cuenta atrás en una etiqueta de arena.
	CountdownText = TNRaceUI::MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 24, TNHUDArt::Ink, false);
	UBorder* Tag = TNRaceUI::MakeCard(Tree, TNHUDArt::SandTagTexture(), TNRaceUI::TagMargin, CountdownText, FMargin(34.f, 15.f, 34.f, 16.f));
	Tag->SetVisibility(ESlateVisibility::Collapsed);
	CountdownTag = Tag;
	TNRaceUI::Place(Canvas, Tag, FVector2D(0.5f, 1.f), FVector2D(0.f, -30.f));

	// Vista previa por consola: una etiqueta arriba a la izquierda.
	UBorder* Preview = TNRaceUI::MakeCard(Tree, TNHUDArt::RibbonTexture(), TNRaceUI::RibbonMargin,
		TNRaceUI::MakeText(Tree, NSLOCTEXT("TNRace", "Preview", "VISTA PREVIA"), TEXT("Bold"), 15, FLinearColor::White), FMargin(34.f, 14.f, 34.f, 16.f));
	Preview->SetVisibility(ESlateVisibility::Collapsed);
	PreviewTag = Preview;
	TNRaceUI::Place(Canvas, Preview, FVector2D(0.f, 0.f), FVector2D(24.f, 24.f));

	// Pinceles de lo que se pinta a mano: la concha que vuela, su brillo y los destellos.
	FlyingBrush = TNRaceUI::TextureBrush(TNHUDArt::ShellIconTier(3), FVector2D(128.f, 128.f));
	GlowBrush = TNRaceUI::TextureBrush(TNRaceArt::SoftGlow(), FVector2D(128.f, 128.f));
	SparkBrush = TNRaceUI::TextureBrush(TNRaceArt::Sparkle(), FVector2D(64.f, 64.f));
}

void UTN_RaceTallyWidget::Setup(const FTNRaceTallySetup& InSetup)
{
	BuildTree();
	TallySetup = InSetup;
	TallySetup.Target = FMath::Clamp(TallySetup.Target, 1, 5);
	const int32 MaxHalves = TallySetup.Target * 2;
	for (FTNRaceTallyRow& Row : TallySetup.Rows)
	{
		Row.HalvesBefore = FMath::Clamp(Row.HalvesBefore, 0, MaxHalves);
		Row.HalvesGained = FMath::Clamp(Row.HalvesGained, 0, 2);
	}
	if (HasWinner())
	{
		// La primera en el agua gana la entera (dos medias); si ya no le cabe, se le hace sitio en el último hueco.
		FTNRaceTallyRow& Winner = TallySetup.Rows[TallySetup.WinnerRow];
		Winner.HalvesGained = 2;
		Winner.HalvesBefore = FMath::Min(Winner.HalvesBefore, MaxHalves - 2);
	}
	if (!TallySetup.Rows.IsValidIndex(TallySetup.ChampionRow))
	{
		TallySetup.ChampionRow = INDEX_NONE;
	}
	TallySetup.SprintRows.RemoveAll([this](int32 Row) { return !TallySetup.Rows.IsValidIndex(Row); });
	Time = 0.f;
	bLanded = false;
	bAppeared = false;
	bCrownSounded = false;
	bFlyVisible = false;
	bFlying = false;
	NobodyPomAt = -1.f;
	PomStep = 0;
	ResultShown = -1;
	DismissAt = -1.f;
	Sparks.Reset();
	SetRenderOpacity(1.f);
	BuildColumns();
	PlanTimeline();
	if (RoundText)
	{
		RoundText->SetText(FText::Format(NSLOCTEXT("TNRace", "RoundTitle", "RONDA {0}"), FText::AsNumber(FMath::Max(1, TallySetup.Round))));
	}
	if (PreviewTag) { PreviewTag->SetVisibility(TallySetup.bPreview ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	TickTexts();
}

void UTN_RaceTallyWidget::BuildColumns()
{
	using namespace TNRaceTallyDetail;
	if (!ColumnBox) { return; }
	ColumnBox->ClearChildren();
	Columns.Reset();
	UWidgetTree* Tree = WidgetTree;
	const int32 Target = TallySetup.Target;
	for (int32 c = 0; c < TallySetup.Rows.Num(); ++c)
	{
		const FTNRaceTallyRow& Row = TallySetup.Rows[c];
		FTNTallyColumn Column;
		UCanvasPanel* Col = TNRaceUI::Make<UCanvasPanel>(Tree);

		// Cuerda del zigzag: de hueco en hueco y del último a la corona.
		for (int32 k = 0; k + 1 < Target; ++k) { AddRope(Tree, Col, SlotCenter(k, Target), SlotCenter(k + 1, Target)); }
		AddRope(Tree, Col, SlotCenter(Target - 1, Target), FVector2D(ColumnW * 0.5f, CrownY + 10.f));

		// Corona del campeón arriba (apagada; baja a la cabeza del que la gana).
		UImage* CrownImg = TNRaceUI::MakeImage(Tree, TNRaceArt::Crown(), FVector2D(CrownW, CrownH));
		CrownImg->SetColorAndOpacity(FLinearColor(0.6f, 0.62f, 0.7f, 0.5f));
		CrownImg->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		TNRaceUI::PlaceAt(Col, CrownImg, FVector2D(ColumnW * 0.5f, CrownY), FVector2D(CrownW, CrownH), FVector2D(0.5f, 0.5f));
		Column.Crown = CrownImg;

		// Huecos y, en cada uno, sus dos medias conchas y la entera (encima).
		for (int32 k = 0; k < Target; ++k)
		{
			const FVector2D Center = SlotCenter(k, Target);
			UImage* Socket = TNRaceUI::MakeImage(Tree, TNRaceArt::ShellSocket(), FVector2D(SocketSize, SocketSize));
			TNRaceUI::PlaceAt(Col, Socket, Center, FVector2D(SocketSize, SocketSize), FVector2D(0.5f, 0.5f));
			Column.Sockets.Add(Socket);
			Column.HalvesA.Add(AddSlotShell(Tree, Col, TNRaceArt::HalfShell(false), Center, k));
			Column.HalvesB.Add(AddSlotShell(Tree, Col, TNRaceArt::HalfShell(true), Center, k));
			Column.Shells.Add(AddSlotShell(Tree, Col, TNHUDArt::ShellIconTier(3), Center, k));
			Column.SocketLevel.Add(0);
			Column.SocketFrom.Add(0);
			Column.SocketChangedAt.Add(-1.f);
		}

		// Brillo detrás de la cara (del que celebra), aro del color de la tortuga y la cara con su piel.
		UImage* GlowImg = TNRaceUI::MakeImage(Tree, TNRaceArt::SoftGlow(), FVector2D(330.f, 330.f));
		GlowImg->SetColorAndOpacity(TNHUDArt::Gold);
		GlowImg->SetRenderOpacity(0.f);
		TNRaceUI::PlaceAt(Col, GlowImg, FVector2D(ColumnW * 0.5f, FaceY), FVector2D(330.f, 330.f), FVector2D(0.5f, 0.5f));
		Column.Glow = GlowImg;
		UImage* Ring = TNRaceUI::Make<UImage>(Tree);
		Ring->SetBrush(TNHUDStyle::Rounded(TNRaceArt::UIColorOf(this, Row.Look), RingSize * 0.5f, TNHUDArt::Navy, 5.f));
		TNRaceUI::PlaceAt(Col, Ring, FVector2D(ColumnW * 0.5f, FaceY), FVector2D(RingSize, RingSize), FVector2D(0.5f, 0.5f));
		UImage* FaceImg = TNRaceUI::MakeImage(Tree, TNRaceArt::TurtleFaceFor(this, Row.Look, ETNTurtleFace::Happy), FVector2D(RingSize, RingSize));
		FaceImg->SetRenderTransformPivot(FVector2D(0.5f, 0.85f));
		TNRaceUI::PlaceAt(Col, FaceImg, FVector2D(ColumnW * 0.5f, FaceY - 4.f), FVector2D(RingSize - 6.f, RingSize - 6.f), FVector2D(0.5f, 0.5f));
		Column.Face = FaceImg;

		// Nombre en una etiqueta de arena (dorada si es el tuyo) y, en el tuyo, «TÚ» en una cinta coral.
		UTextBlock* NameText = TNRaceUI::MakeText(Tree, TNLocText::PlayerName(Row.Name), TEXT("Bold"), 20, TNHUDArt::Ink, false);
		NameText->SetJustification(ETextJustify::Center);
		// Un nombre largo (hasta 32) se encoge para caber en la etiqueta, centrado, en vez de cortarse por la derecha.
		UScaleBox* NameShrink = TNRaceUI::Make<UScaleBox>(Tree);
		NameShrink->SetStretch(EStretch::ScaleToFit);
		NameShrink->SetStretchDirection(EStretchDirection::DownOnly);
		NameShrink->SetContent(NameText);
		UBorder* NameTag = TNRaceUI::MakeCard(Tree, TNHUDArt::SandTagTexture(), TNRaceUI::TagMargin, NameShrink, FMargin(28.f, 13.f, 28.f, 14.f));
		if (Row.bLocal) { NameTag->SetBrush(TNRaceUI::BoxBrush(TNHUDArt::SandTagTexture(), TNRaceUI::TagMargin, TNHUDArt::Hex(0xFFE08A))); }
		USizeBox* NameFit = TNRaceUI::MakeSize(Tree, NameTag, 0.f, 0.f);
		NameFit->SetMaxDesiredWidth(ColumnW + 10.f);
		UCanvasPanelSlot* NameSlot = TNRaceUI::Place(Col, NameFit, FVector2D(0.f, 0.f), FVector2D(ColumnW * 0.5f, NameY));
		NameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		if (Row.bLocal)
		{
			UBorder* You = TNRaceUI::MakeCard(Tree, TNHUDArt::RibbonTexture(), TNRaceUI::RibbonMargin,
				TNRaceUI::MakeText(Tree, NSLOCTEXT("TNRace", "You", "TÚ"), TEXT("Bold"), 14, FLinearColor::White), FMargin(26.f, 13.f, 26.f, 15.f));
			You->SetRenderTransformAngle(-8.f);
			UCanvasPanelSlot* YouSlot = TNRaceUI::Place(Col, You, FVector2D(0.f, 0.f), FVector2D(ColumnW * 0.5f + 62.f, FaceY - 70.f));
			YouSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		}

		USizeBox* Root = TNRaceUI::MakeSize(Tree, Col, ColumnW, ColumnH);
		Root->SetRenderTransformPivot(FVector2D(0.5f, 1.f));
		Root->SetRenderOpacity(0.f);
		if (UHorizontalBoxSlot* S = ColumnBox->AddChildToHorizontalBox(Root))
		{
			S->SetPadding(FMargin(c == 0 ? 0.f : ColumnGap, 0.f, 0.f, 0.f));
			S->SetVerticalAlignment(VAlign_Bottom);
		}
		Column.Root = Root;
		Columns.Add(Column);
	}
	// Ocho columnas miden 2102 px: la fila entera se encoge (desde su centro) para caber en la pantalla. CenterOf mide con
	// la geometría ya escalada, así que la concha que vuela sigue cayendo en su hueco.
	const int32 NumColumns = Columns.Num();
	const float RowWidth = NumColumns * ColumnW + FMath::Max(0, NumColumns - 1) * ColumnGap;
	const float RowScale = FMath::Min(1.f, MaxRowWidth / FMath::Max(1.f, RowWidth));
	ColumnBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	ColumnBox->SetRenderScale(FVector2D(RowScale, RowScale));
}

void UTN_RaceTallyWidget::PlanTimeline()
{
	using namespace TNRaceTallyDetail;
	// Entran las columnas y aparecen una a una las conchas que ya tenía cada jugador (la media, al final); al acabar nace
	// la entera de la ronda y, cuando cae, saltan las medias de la cuenta atrás.
	float LastPop = 0.45f;
	NumHalfRows = 0;
	for (int32 c = 0; c < Columns.Num(); ++c)
	{
		FTNTallyColumn& Column = Columns[c];
		const FTNRaceTallyRow& Row = TallySetup.Rows[c];
		Column.InAt = 0.12f + 0.11f * c;
		Column.ShownHalves = 0;
		Column.bWinnerCounted = false;
		Column.bHalfCounted = false;
		int32 Before = Row.HalvesBefore;
		if (TallySetup.bAlreadyLanded) { Before = FMath::Min(Before + Row.HalvesGained, TallySetup.Target * 2); }
		Column.BeforeFullAt.Reset();
		for (int32 k = 0; k < Before / 2; ++k)
		{
			Column.BeforeFullAt.Add(0.55f + 0.12f * c + 0.1f * k);
			LastPop = FMath::Max(LastPop, Column.BeforeFullAt.Last());
		}
		Column.BeforeHalfAt = (Before % 2) ? 0.55f + 0.12f * c + 0.1f * (Before / 2) : -1.f;
		LastPop = FMath::Max(LastPop, Column.BeforeHalfAt);
		Column.HalfOrder = INDEX_NONE;
		if (!TallySetup.bAlreadyLanded && c != TallySetup.WinnerRow && Row.HalvesGained == 1)
		{
			Column.HalfOrder = NumHalfRows++;
		}
	}
	AppearAt = FMath::Max(1.25f, LastPop + 0.45f);
	if (TallySetup.bAlreadyLanded)
	{
		// Ya se vio llegar: se celebra en cuanto salen las conchas.
		LaunchAt = AppearAt;
		LandAt = AppearAt;
		HalvesFromAt = AppearAt;
		FinalAt = AppearAt;
		VerdictAt = AppearAt + 0.3f;
		DoneAt = VerdictAt + ((IsChampionTally() || IsSprintTally()) ? 2.4f : 1.6f);
		return;
	}
	LaunchAt = AppearAt + HoverSeconds;
	LandAt = LaunchAt + FlightSeconds;
	HalvesFromAt = HasWinner() ? LandAt + 0.45f : AppearAt + 0.2f;
	FinalAt = NumHalfRows > 0 ? HalvesFromAt + HalfStagger * static_cast<float>(NumHalfRows - 1) + 0.3f : (HasWinner() ? LandAt : AppearAt);
	VerdictAt = FinalAt + 0.5f;
	DoneAt = (IsChampionTally() || IsSprintTally()) ? VerdictAt + 2.5f : FinalAt + ((HasWinner() || HasHalves()) ? 2.4f : 2.2f);
}

int32 UTN_RaceTallyWidget::NewSlot() const
{
	return HasWinner() ? FMath::Clamp(TallySetup.Rows[TallySetup.WinnerRow].HalvesBefore / 2, 0, TallySetup.Target - 1) : INDEX_NONE;
}

float UTN_RaceTallyWidget::HalfLandAt(int32 ColumnIndex) const
{
	const FTNTallyColumn* Column = Columns.IsValidIndex(ColumnIndex) ? &Columns[ColumnIndex] : nullptr;
	return Column && Column->HalfOrder >= 0 ? HalvesFromAt + TNRaceTallyDetail::HalfStagger * static_cast<float>(Column->HalfOrder) : -1.f;
}

int32 UTN_RaceTallyWidget::HalvesShownAt(int32 ColumnIndex) const
{
	if (!Columns.IsValidIndex(ColumnIndex)) { return 0; }
	const FTNTallyColumn& Column = Columns[ColumnIndex];
	int32 Halves = 0;
	for (const float At : Column.BeforeFullAt)
	{
		if (Time >= At) { Halves += 2; }
	}
	if (Column.BeforeHalfAt >= 0.f && Time >= Column.BeforeHalfAt) { Halves += 1; }
	if (ColumnIndex == TallySetup.WinnerRow && !TallySetup.bAlreadyLanded && bLanded) { Halves += 2; }
	const float HalfAt = HalfLandAt(ColumnIndex);
	if (HalfAt >= 0.f && Time >= HalfAt) { Halves += 1; }
	return FMath::Min(Halves, TallySetup.Target * 2);
}

bool UTN_RaceTallyWidget::IsChampionTally() const
{
	return TallySetup.Rows.IsValidIndex(TallySetup.ChampionRow);
}

void UTN_RaceTallyWidget::UpdateWinner(int32 InWinnerRow)
{
	if (InWinnerRow == TallySetup.WinnerRow || Time >= LaunchAt || TallySetup.bAlreadyLanded) { return; }
	TallySetup.WinnerRow = TallySetup.Rows.IsValidIndex(InWinnerRow) ? InWinnerRow : INDEX_NONE;
	if (HasWinner())
	{
		FTNRaceTallyRow& Winner = TallySetup.Rows[TallySetup.WinnerRow];
		Winner.HalvesGained = 2;
		Winner.HalvesBefore = FMath::Min(Winner.HalvesBefore, TallySetup.Target * 2 - 2);
	}
	const float Now = Time;
	PlanTimeline();
	AppearAt = FMath::Max(AppearAt, Now);
	LaunchAt = FMath::Max(LaunchAt, AppearAt + TNRaceTallyDetail::HoverSeconds);
	LandAt = LaunchAt + TNRaceTallyDetail::FlightSeconds;
	ResultShown = -1;
}

void UTN_RaceTallyWidget::SetSecondsLeft(float InSeconds)
{
	SecondsLeft = FMath::Max(0.f, InSeconds);
}

void UTN_RaceTallyWidget::Dismiss()
{
	if (DismissAt < 0.f) { DismissAt = Time; }
}

FText UTN_RaceTallyWidget::JoinNames(const TArray<int32>& RowIndices) const
{
	TArray<FText> Names;
	for (const int32 RowIndex : RowIndices)
	{
		if (!TallySetup.Rows.IsValidIndex(RowIndex)) { continue; }
		const FString& Name = TallySetup.Rows[RowIndex].Name;
		Names.Add(TNLocText::PlayerName(Name));
	}
	if (Names.Num() == 0) { return FText::GetEmpty(); }
	if (Names.Num() == 1) { return Names[0]; }
	const FText Last = Names.Pop();
	return FText::Format(NSLOCTEXT("TNRace", "NamesAnd", "{0} y {1}"), FText::Join(NSLOCTEXT("TNRace", "NamesComma", ", "), Names), Last);
}

// ─────────────────────────────────────────────────────────────────────────────
// Animación
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceTallyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Dt = FMath::Min(InDeltaTime, 0.1f);
	Time += Dt;
	const FVector2f Size = FVector2f(MyGeometry.GetLocalSize());
	if (Size.X > 1.f) { LocalSize = FVector2D(Size.X, Size.Y); }

	TickFlight(MyGeometry);
	TickColumns(Dt);
	TickTexts();
	TickBubbles();
	TickSparks(Dt);

	if (DismissAt >= 0.f)
	{
		const float Out = FMath::Clamp((Time - DismissAt) / 0.35f, 0.f, 1.f);
		SetRenderOpacity(1.f - Out);
		if (Out >= 1.f)
		{
			RemoveFromParent();
			return;
		}
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UTN_RaceTallyWidget::TickColumns(float DeltaTime)
{
	using namespace TNRaceTallyDetail;
	const bool bNobody = !HasWinner() && !HasHalves() && Time >= AppearAt;
	for (int32 c = 0; c < Columns.Num(); ++c)
	{
		FTNTallyColumn& Column = Columns[c];
		const bool bWinner = c == TallySetup.WinnerRow;

		// Entrada con rebote desde abajo.
		if (UWidget* Root = Column.Root.Get())
		{
			const float In = (Time - Column.InAt) / 0.4f;
			Root->SetRenderOpacity(FMath::Clamp(In * 2.f, 0.f, 1.f));
			const float Pop = TNRaceUI::PopIn(In);
			Root->SetRenderScale(FVector2D(FMath::Max(0.01f, Pop), FMath::Max(0.01f, Pop)));
		}

		// Conchas: las que ya tenía, con un «pom» que sube de nota (la media, más bajito); la entera de la ronda suena al
		// caer (Land) y la media de la cuenta atrás, con un «plin» pequeño.
		const int32 Halves = HalvesShownAt(c);
		if (Halves > Column.ShownHalves)
		{
			const float OwnHalfAt = HalfLandAt(c);
			if (bWinner && bLanded && !Column.bWinnerCounted && !TallySetup.bAlreadyLanded)
			{
				Column.bWinnerCounted = true;
			}
			else if (OwnHalfAt >= 0.f && Time >= OwnHalfAt && !Column.bHalfCounted)
			{
				Column.bHalfCounted = true;
				PlaySound(true, 1, 3.f, 0.85f);
				Column.FacePop = 1.f;
			}
			else
			{
				const bool bHalfPop = Halves - Column.ShownHalves == 1;
				PlaySound(false, bHalfPop ? 0 : 1, static_cast<float>(TNScoreShells::PomSemitones(PomStep)) - (bHalfPop ? 5.f : 0.f), bHalfPop ? 0.6f : 0.75f);
				++PomStep;
			}
			Column.ShownHalves = Halves;
		}
		TickSockets(c);

		// Cara: feliz y balanceándose; la que celebra (la entera al caer, la media al llegar, la campeona con su corona o
		// las del sprint al anunciarlo), con ojos de estrella y saltando; si nadie llegó al agua, mareadas.
		const float HalfAt = HalfLandAt(c);
		const bool bWinnerCelebrate = bWinner && bLanded;
		const bool bHalfCelebrate = HalfAt >= 0.f && Time >= HalfAt;
		const bool bChampionCol = c == TallySetup.ChampionRow && Time >= VerdictAt;
		const bool bSprintCol = TallySetup.SprintRows.Contains(c) && Time >= VerdictAt && IsSprintTally();
		const bool bCelebrate = bWinnerCelebrate || bHalfCelebrate || bChampionCol || bSprintCol;
		const float CelebrateFrom = bWinnerCelebrate ? LandAt : (bHalfCelebrate ? HalfAt : VerdictAt);
		const float JumpHeight = (bWinnerCelebrate || bChampionCol) ? 22.f : (bSprintCol ? 16.f : 12.f);
		const ETNTurtleFace Face = bCelebrate ? ETNTurtleFace::Win : (bNobody ? ETNTurtleFace::Down : ETNTurtleFace::Happy);
		if (Column.FaceShown != static_cast<uint8>(Face))
		{
			Column.FaceShown = static_cast<uint8>(Face);
			Column.FacePop = 1.f;
			TNRaceUI::SetImageTexture(Column.Face.Get(), TNRaceArt::TurtleFaceFor(this, TallySetup.Rows[c].Look, Face));
		}
		Column.FacePop = FMath::Max(0.f, Column.FacePop - DeltaTime * 3.f);
		if (UImage* FaceImg = Column.Face.Get())
		{
			const float Pop = 1.f + 0.25f * FMath::Sin(Column.FacePop * PI);
			if (bCelebrate)
			{
				const float Jump = FMath::Abs(FMath::Sin((Time - CelebrateFrom) * 7.f));
				FaceImg->SetRenderTranslation(FVector2D(0.f, -JumpHeight * Jump));
				FaceImg->SetRenderTransformAngle(9.f * FMath::Sin((Time - CelebrateFrom) * 5.f));
				FaceImg->SetRenderScale(FVector2D(Pop * (1.f + 0.06f * Jump), Pop * (1.f + 0.06f * Jump)));
			}
			else if (bNobody)
			{
				FaceImg->SetRenderTranslation(FVector2D::ZeroVector);
				FaceImg->SetRenderTransformAngle(12.f * FMath::Sin(Time * 2.6f + c * 1.3f));
				FaceImg->SetRenderScale(FVector2D(Pop, Pop));
			}
			else
			{
				FaceImg->SetRenderTranslation(FVector2D(0.f, -4.f * FMath::Sin(Time * 2.2f + c * 0.9f)));
				FaceImg->SetRenderTransformAngle(3.f * FMath::Sin(Time * 1.7f + c));
				FaceImg->SetRenderScale(FVector2D(Pop, Pop));
			}
		}

		// Brillo detrás del que celebra (dorado; más flojo con la media; coral y latiendo en el sprint) y destellos
		// alrededor de la entera y de la campeona.
		if (UImage* GlowImg = Column.Glow.Get())
		{
			const float Strength = (bWinnerCelebrate || bChampionCol) ? 1.f : (bSprintCol ? 0.8f : (bHalfCelebrate ? 0.45f : 0.f));
			const float Glow = Strength * FMath::Clamp((Time - CelebrateFrom) / 0.4f, 0.f, 1.f) * (0.65f + 0.2f * FMath::Sin(Time * (bSprintCol ? 7.f : 4.f)));
			GlowImg->SetColorAndOpacity(bSprintCol && !bChampionCol ? TNHUDArt::CoralC : TNHUDArt::Gold);
			GlowImg->SetRenderOpacity(Glow);
			if ((bWinnerCelebrate || bChampionCol) && Time >= Column.NextSparkle)
			{
				Column.NextSparkle = Time + 0.3f;
				if (Column.Face.IsValid())
				{
					FVector2D FaceCenter;
					if (CenterOf(Column.Face.Get(), GetCachedGeometry(), FaceCenter))
					{
						const float A = FMath::FRandRange(0.f, 2.f * PI);
						Burst(FaceCenter + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 80.f, 1, 60.f, 30.f);
					}
				}
			}
		}

		// Corona: apagada arriba; a la campeona le baja a la cabeza.
		if (UImage* CrownImg = Column.Crown.Get())
		{
			const float Drop = bChampionCol ? TNRaceUI::Smooth((Time - VerdictAt) / 0.7f) : 0.f;
			if (bChampionCol && Drop > 0.f && !bCrownSounded)
			{
				bCrownSounded = true;
				PlaySound(true, 1, 0.f, 0.8f);
			}
			const float HeadY = FaceY - RingSize * 0.5f - 22.f;
			const float Bounce = Drop >= 1.f ? 6.f * FMath::Abs(FMath::Sin((Time - VerdictAt) * 7.f)) : 0.f;
			CrownImg->SetRenderTranslation(FVector2D(0.f, (HeadY - CrownY) * Drop - Bounce));
			CrownImg->SetRenderScale(FVector2D(1.f + 0.3f * Drop, 1.f + 0.3f * Drop));
			CrownImg->SetRenderTransformAngle(Drop > 0.f ? 10.f * FMath::Sin(Time * 3.f) * Drop : 0.f);
			CrownImg->SetColorAndOpacity(FMath::Lerp(FLinearColor(0.6f, 0.62f, 0.7f, 0.5f), FLinearColor::White, Drop));
		}
	}
}

void UTN_RaceTallyWidget::TickSockets(int32 ColumnIndex)
{
	using namespace TNRaceTallyDetail;
	FTNTallyColumn& Column = Columns[ColumnIndex];
	for (int32 k = 0; k < Column.Shells.Num(); ++k)
	{
		// Cada hueco, dos medias: vacío, media (la de arriba a la izquierda) o entera.
		const int32 Level = FMath::Clamp(Column.ShownHalves - 2 * k, 0, 2);
		if (Level != Column.SocketLevel[k])
		{
			Column.SocketFrom[k] = Column.SocketLevel[k];
			Column.SocketLevel[k] = Level;
			Column.SocketChangedAt[k] = Time;
			if (Level > 0 && Time >= AppearAt && Column.Sockets.IsValidIndex(k))
			{
				// Las de esta ronda salen con destellos (la entera los suyos al caer, en Land).
				FVector2D SocketCenter;
				if (CenterOf(Column.Sockets[k].Get(), GetCachedGeometry(), SocketCenter) && !(ColumnIndex == TallySetup.WinnerRow && k == NewSlot()))
				{
					Burst(SocketCenter, Level == 2 ? 10 : 6, Level == 2 ? 300.f : 200.f, 26.f);
				}
			}
		}
		UImage* Full = Column.Shells[k].Get();
		UImage* HalfA = Column.HalvesA.IsValidIndex(k) ? Column.HalvesA[k].Get() : nullptr;
		UImage* HalfB = Column.HalvesB.IsValidIndex(k) ? Column.HalvesB[k].Get() : nullptr;
		if (!Full || !HalfA || !HalfB) { continue; }
		const float Since = Time - Column.SocketChangedAt[k];
		const float Base = SlotAngle(k);
		// Las de esta ronda se aplastan al caer y se enderezan (rebote); las que ya tenía solo aparecen con un rebote.
		const bool bFresh = Column.SocketChangedAt[k] >= AppearAt - 0.01f && !TallySetup.bAlreadyLanded;
		Full->SetRenderOpacity(0.f);
		HalfA->SetRenderOpacity(0.f);
		HalfB->SetRenderOpacity(0.f);
		if (Level == 1)
		{
			// Media concha: salta a su hueco con un rebote y se mece un momento.
			const float Pop = TNRaceUI::PopIn(Since / 0.3f);
			HalfA->SetRenderOpacity(1.f);
			HalfA->SetRenderScale(FVector2D(Pop, Pop));
			HalfA->SetRenderTransformAngle(Base + (bFresh ? 14.f * FMath::Sin(Since * 16.f) * FMath::Exp(-Since * 5.f) : 0.f));
		}
		else if (Level == 2 && Column.SocketFrom[k] == 1 && Since < JoinSeconds)
		{
			// La otra mitad llega desde abajo a la derecha y encaja con la que había.
			const float Slide = 1.f - TNRaceUI::Smooth(Since / JoinSeconds);
			HalfA->SetRenderOpacity(1.f);
			HalfA->SetRenderScale(FVector2D(1.f, 1.f));
			HalfA->SetRenderTransformAngle(Base);
			HalfB->SetRenderOpacity(FMath::Clamp(Since / (JoinSeconds * 0.4f), 0.f, 1.f));
			HalfB->SetRenderScale(FVector2D(1.f, 1.f));
			HalfB->SetRenderTransformAngle(Base);
			HalfB->SetRenderTranslation(FVector2D(34.f, 34.f) * Slide);
		}
		else if (Level == 2)
		{
			const bool bJoined = Column.SocketFrom[k] == 1;
			const float Since2 = Since - (bJoined ? JoinSeconds : 0.f);
			const float Pop = bJoined ? 1.f : TNRaceUI::PopIn(Since2 / 0.28f);
			const float Squash = (bFresh && Since2 < 0.6f) ? 0.25f * FMath::Sin(Since2 * 18.f) * FMath::Exp(-Since2 * 6.f) : 0.f;
			Full->SetRenderOpacity(1.f);
			Full->SetRenderScale(FVector2D(Pop * (1.f + Squash), Pop * (1.f - Squash)));
			Full->SetRenderTransformAngle(Base);
		}
	}
}

void UTN_RaceTallyWidget::TickFlight(const FGeometry& MyGeometry)
{
	using namespace TNRaceTallyDetail;
	FlyFrom = FVector2D(LocalSize.X * 0.5f, LocalSize.Y * 0.44f);
	bFlyVisible = false;
	bFlying = false;
	if (!HasWinner())
	{
		// Nadie llegó al agua: dos «pom» que bajan, «pom... pom» (con medias conchas sueltas, no).
		if (!HasHalves() && Time >= AppearAt && !bAppeared)
		{
			bAppeared = true;
			PlaySound(false, 0, -5.f, 0.9f);
			NobodyPomAt = Time + 0.28f;
		}
		if (NobodyPomAt >= 0.f && Time >= NobodyPomAt)
		{
			NobodyPomAt = -1.f;
			PlaySound(false, 0, -10.f, 0.9f);
		}
		return;
	}
	if (TallySetup.bAlreadyLanded)
	{
		if (!bLanded && Time >= LandAt) { bLanded = true; bAppeared = true; }
		return;
	}
	if (Time < AppearAt || bLanded) { return; }

	if (!bAppeared)
	{
		bAppeared = true;
		PlaySound(true, 0, 0.f, 0.6f);
		Burst(FlyFrom, 10, 260.f, 30.f);
	}
	bFlyVisible = true;
	if (Time < LaunchAt)
	{
		// Nace en el centro con un rebote y gira meciéndose mientras brilla.
		const float Since = Time - AppearAt;
		FlyPos = FlyFrom + FVector2D(0.f, -8.f * FMath::Sin(Since * 9.f));
		FlySize = FlightSize * TNRaceUI::PopIn(Since / 0.35f);
		FlyAngle = 14.f * FMath::Sin(Since * 8.f);
		FlyGlow = FMath::Clamp(Since / 0.25f, 0.f, 1.f);
		FlyTrailA = FlyPos;
		FlyTrailB = FlyPos;
		return;
	}

	// Vuelo en arco hasta el hueco de la ganadora (curva de Bézier), acelerando al final y dando una vuelta.
	FVector2D Goal = FlyFrom;
	const FTNTallyColumn& Column = Columns[TallySetup.WinnerRow];
	const int32 SlotIndex = NewSlot();
	if (Column.Sockets.IsValidIndex(SlotIndex)) { CenterOf(Column.Sockets[SlotIndex].Get(), MyGeometry, Goal); }
	const FVector2D Bend(FMath::Lerp(FlyFrom.X, Goal.X, 0.5f), FMath::Min(FlyFrom.Y, Goal.Y) - 240.f);
	auto Along = [this, &Goal, &Bend](float E)
	{
		const float Inv = 1.f - E;
		return FlyFrom * (Inv * Inv) + Bend * (2.f * Inv * E) + Goal * (E * E);
	};
	const float U = FMath::Clamp((Time - LaunchAt) / FlightSeconds, 0.f, 1.f);
	const float Eased = U * U * (0.45f + 0.55f * U);
	bFlying = true;
	FlyPos = Along(Eased);
	FlyTrailA = Along(FMath::Max(0.f, Eased - 0.06f));
	FlyTrailB = Along(FMath::Max(0.f, Eased - 0.12f));
	FlySize = FMath::Lerp(FlightSize, ShellSize, Eased);
	FlyAngle = 360.f * Eased;
	FlyGlow = 1.f - 0.6f * Eased;
	if (U >= 1.f)
	{
		Land();
	}
}

void UTN_RaceTallyWidget::Land()
{
	using namespace TNRaceTallyDetail;
	if (bLanded) { return; }
	bLanded = true;
	bFlyVisible = false;
	bFlying = false;
	// Desde la llegada de verdad (el vuelo puede empezar tarde si el ganador llegó tarde por red): las medias, el
	// veredicto y el final.
	LandAt = Time;
	HalvesFromAt = LandAt + 0.45f;
	FinalAt = NumHalfRows > 0 ? HalvesFromAt + HalfStagger * static_cast<float>(NumHalfRows - 1) + 0.3f : LandAt;
	VerdictAt = FinalAt + 0.5f;
	DoneAt = (IsChampionTally() || IsSprintTally()) ? VerdictAt + 2.5f : FinalAt + 2.4f;
	const bool bCrowns = TallySetup.ChampionRow == TallySetup.WinnerRow && NumHalfRows == 0;
	PlaySound(true, bCrowns ? 3 : 2, 0.f, 1.f);
	Burst(FlyPos, 16, 420.f, 34.f);
	if (FTNTallyColumn* Column = Columns.IsValidIndex(TallySetup.WinnerRow) ? &Columns[TallySetup.WinnerRow] : nullptr)
	{
		Column->FacePop = 1.f;
		Column->NextSparkle = Time + 0.25f;
	}
}

void UTN_RaceTallyWidget::TickTexts()
{
	// Cartel de arriba: «Recuento de conchas» hasta que nace la concha de la ronda; luego, quién gana entera y quién
	// media (o nadie); al final, la campeona o el sprint final.
	int32 Want = Time >= AppearAt ? 1 : 0;
	if ((IsChampionTally() || IsSprintTally()) && Time >= VerdictAt) { Want = 2; }
	if (Want != ResultShown && ResultText && ResultFace)
	{
		ResultShown = Want;
		TArray<int32> HalfRows;
		for (int32 c = 0; c < Columns.Num(); ++c)
		{
			if (Columns[c].HalfOrder >= 0) { HalfRows.Add(c); }
		}
		HalfRows.Sort([this](int32 A, int32 B) { return Columns[A].HalfOrder < Columns[B].HalfOrder; });
		if (Want == 0)
		{
			ResultText->SetText(NSLOCTEXT("TNRace", "TallyTitle", "Recuento de conchas"));
			TNRaceUI::SetImageTexture(ResultFace, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy));
		}
		else if (Want == 2 && IsChampionTally())
		{
			const FTNRaceTallyRow& Champ = TallySetup.Rows[TallySetup.ChampionRow];
			const FText Name = TNLocText::PlayerName(Champ.Name);
			ResultText->SetText(FText::Format(NSLOCTEXT("TNRace", "ChampionWins", "¡{0} gana la partida!"), Name));
			TNRaceUI::SetImageTexture(ResultFace, TNRaceArt::TurtleFaceFor(this, Champ.Look, ETNTurtleFace::Win));
		}
		else if (Want == 2)
		{
			ResultText->SetText(FText::Format(NSLOCTEXT("TNRace", "SprintTie", "¡Empate en lo más alto entre {0}! ¡Sprint final!"), JoinNames(TallySetup.SprintRows)));
			TNRaceUI::SetImageTexture(ResultFace, TNHUDFaces::TurtleFace(ETNTurtleFace::Win));
		}
		else if (HasWinner())
		{
			const FTNRaceTallyRow& Winner = TallySetup.Rows[TallySetup.WinnerRow];
			const FText Name = TNLocText::PlayerName(Winner.Name);
			if (TallySetup.bTimeLimit)
			{
				// Nadie llegó al agua: la concha es de la más cerca del mar al acabarse el tiempo de la ronda.
				ResultText->SetText(FText::Format(NSLOCTEXT("TNRace", "RoundWinnerTimeLimit", "¡Se acabó el tiempo! Nadie llegó al agua: concha para {0}, la más cerca del mar."), Name));
			}
			else if (HalfRows.Num() == 0)
			{
				ResultText->SetText(FText::Format(NSLOCTEXT("TNRace", "RoundWinner", "¡Concha para {0}!"), Name));
			}
			else
			{
				ResultText->SetText(FText::Format(HalfRows.Num() == 1
					? NSLOCTEXT("TNRace", "RoundWinnerHalf", "¡Concha para {0}! Media para {1}.")
					: NSLOCTEXT("TNRace", "RoundWinnerHalves", "¡Concha para {0}! Medias para {1}."), Name, JoinNames(HalfRows)));
			}
			TNRaceUI::SetImageTexture(ResultFace, TNRaceArt::TurtleFaceFor(this, Winner.Look, ETNTurtleFace::Win));
		}
		else if (HalfRows.Num() > 0)
		{
			ResultText->SetText(FText::Format(NSLOCTEXT("TNRace", "RoundHalvesOnly", "Media concha para {0}."), JoinNames(HalfRows)));
			TNRaceUI::SetImageTexture(ResultFace, TNRaceArt::TurtleFaceFor(this, TallySetup.Rows[HalfRows[0]].Look, ETNTurtleFace::Win));
		}
		else
		{
			ResultText->SetText(NSLOCTEXT("TNRace", "Nobody", "¡Nadie ha llegado al agua! Esta vez no hay concha."));
			TNRaceUI::SetImageTexture(ResultFace, TNHUDFaces::TurtleFace(ETNTurtleFace::Down));
		}
	}
	if (ResultCard)
	{
		const float Since = ResultShown == 2 ? Time - VerdictAt : (ResultShown == 1 ? Time - AppearAt : Time);
		const float Pop = TNRaceUI::PopIn(Since / 0.35f);
		ResultCard->SetRenderScale(FVector2D(Pop, Pop));
		ResultCard->SetRenderTransformAngle(ResultShown >= 1 ? 1.5f * FMath::Sin(Time * 2.f) : 0.f);
	}

	// Cuenta atrás de la fase.
	if (CountdownTag && CountdownText)
	{
		const int32 Seconds = FMath::CeilToInt(SecondsLeft);
		CountdownTag->SetVisibility(Seconds > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Seconds > 0 && Seconds != ShownSeconds)
		{
			ShownSeconds = Seconds;
			const FText Pattern = IsChampionTally() ? NSLOCTEXT("TNRace", "ToPodium", "¡Al podio en {0}!")
				: (IsSprintTally() ? NSLOCTEXT("TNRace", "ToSprint", "¡Sprint final en {0}!") : NSLOCTEXT("TNRace", "NextRound", "Siguiente ronda en {0}"));
			CountdownText->SetText(FText::Format(Pattern, FText::AsNumber(Seconds)));
		}
		CountdownTag->SetRenderScale(FVector2D(1.f + 0.03f * FMath::Sin(Time * 4.f)));
	}
}

void UTN_RaceTallyWidget::TickBubbles()
{
	using namespace TNRaceTallyDetail;
	// Burbujas que suben del fondo del mar hasta la superficie, meciéndose (se apagan arriba).
	const float W = static_cast<float>(LocalSize.X);
	const float H = static_cast<float>(LocalSize.Y);
	for (int32 i = 0; i < Bubbles.Num(); ++i)
	{
		UImage* Bubble = Bubbles[i];
		if (!Bubble) { continue; }
		const float Speed = 0.05f + 0.07f * Hash01(i, 7);
		const float Rise = FMath::Frac(Time * Speed + Hash01(i, 11));
		const float X = W * (0.04f + 0.92f * Hash01(i, 5)) + 14.f * FMath::Sin(Time * 1.3f + i * 1.7f);
		const float Y = H * 0.78f - Rise * H * 0.8f;
		Bubble->SetRenderTranslation(FVector2D(X, Y));
		Bubble->SetRenderOpacity(0.55f * FMath::Clamp(Rise * 6.f, 0.f, 1.f) * FMath::Clamp((1.f - Rise) * 4.f, 0.f, 1.f));
	}
}

void UTN_RaceTallyWidget::TickSparks(float DeltaTime)
{
	for (int32 i = Sparks.Num() - 1; i >= 0; --i)
	{
		FTNTallySpark& Spark = Sparks[i];
		Spark.Age += DeltaTime;
		if (Spark.Age >= Spark.Life)
		{
			Sparks.RemoveAtSwap(i);
			continue;
		}
		Spark.Pos += Spark.Vel * DeltaTime;
		Spark.Vel *= FMath::Max(0.f, 1.f - 3.f * DeltaTime);
		Spark.Vel.Y += 180.f * DeltaTime;
		Spark.Angle += Spark.Spin * DeltaTime;
	}
}

void UTN_RaceTallyWidget::Burst(const FVector2D& Where, int32 Count, float Speed, float Size)
{
	static const FLinearColor Tints[] = { TNHUDArt::Gold, FLinearColor::White, TNHUDArt::Hex(0xFF9AD8), TNHUDArt::SeaLight };
	for (int32 i = 0; i < Count; ++i)
	{
		FTNTallySpark Spark;
		const float A = 2.f * PI * (static_cast<float>(i) + FMath::FRandRange(0.f, 0.7f)) / FMath::Max(1, Count);
		const float V = Speed * FMath::FRandRange(0.6f, 1.1f);
		Spark.Pos = Where;
		Spark.Vel = FVector2D(FMath::Cos(A), FMath::Sin(A)) * V;
		Spark.Life = FMath::FRandRange(0.45f, 0.8f);
		Spark.Size = Size * FMath::FRandRange(0.7f, 1.2f);
		Spark.Angle = FMath::FRandRange(0.f, 90.f);
		Spark.Spin = FMath::FRandRange(-360.f, 360.f);
		Spark.Tint = Tints[FMath::RandRange(0, 3)];
		Sparks.Add(Spark);
	}
}

void UTN_RaceTallyWidget::PlaySound(bool bPlin, uint8 Tier, float Semitones, float Volume)
{
	if (!Synth.IsValid()) { Synth = UTN_ScoreShellSynthComponent::Attach2D(GetOwningPlayer()); }
	if (UTN_ScoreShellSynthComponent* Comp = Synth.Get())
	{
		Comp->TriggerSound(bPlin ? ETNScoreShellSound::Plin : ETNScoreShellSound::Pom, Tier, Semitones, Volume);
	}
}

bool UTN_RaceTallyWidget::CenterOf(const UWidget* Widget, const FGeometry& MyGeometry, FVector2D& OutCenter) const
{
	if (!Widget) { return false; }
	const FGeometry& Geo = Widget->GetCachedGeometry();
	if (FVector2f(Geo.GetLocalSize()).X <= 1.f) { return false; }
	const FVector2f Abs = FVector2f(Geo.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f)));
	const FVector2f Local = FVector2f(MyGeometry.AbsoluteToLocal(Abs));
	OutCenter = FVector2D(Local.X, Local.Y);
	return true;
}

int32 UTN_RaceTallyWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	auto Box = [&](const FSlateBrush* Brush, int32 AtLayer, const FVector2D& Center, float Size, float AngleDeg, const FLinearColor& Color)
	{
		if (Size <= 0.5f) { return; }
		FSlateDrawElement::MakeRotatedBox(OutDrawElements, AtLayer, AllottedGeometry.ToPaintGeometry(FVector2f(Size, Size),
			FSlateLayoutTransform(FVector2f(static_cast<float>(Center.X) - 0.5f * Size, static_cast<float>(Center.Y) - 0.5f * Size))), Brush, ESlateDrawEffect::None,
			FMath::DegreesToRadians(AngleDeg), TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Color * Tint);
	};

	// La concha de la ronda: brillo dorado detrás, estela al volar y la concha girando.
	if (bFlyVisible)
	{
		Box(&GlowBrush, Layer + 1, FlyPos, FlySize * 2.3f, 0.f, FLinearColor(TNHUDArt::Gold.R, TNHUDArt::Gold.G, TNHUDArt::Gold.B, 0.7f * FlyGlow));
		if (bFlying)
		{
			Box(&FlyingBrush, Layer + 2, FlyTrailB, FlySize * 0.6f, FlyAngle - 40.f, FLinearColor(1.f, 1.f, 1.f, 0.22f));
			Box(&FlyingBrush, Layer + 2, FlyTrailA, FlySize * 0.8f, FlyAngle - 20.f, FLinearColor(1.f, 1.f, 1.f, 0.4f));
		}
		Box(&FlyingBrush, Layer + 3, FlyPos, FlySize, FlyAngle, FLinearColor::White);
	}
	// Destellos.
	for (const FTNTallySpark& Spark : Sparks)
	{
		const float Life = FMath::Clamp(Spark.Age / Spark.Life, 0.f, 1.f);
		const float Size = Spark.Size * (Life < 0.2f ? Life / 0.2f : 1.f - (Life - 0.2f) / 0.8f);
		FLinearColor Color = Spark.Tint;
		Color.A = 1.f - Life * Life;
		Box(&SparkBrush, Layer + 4, Spark.Pos, Size, Spark.Angle, Color);
	}
	return Layer + 4;
}
