#include "UI/HUD/TN_CoopFlowHUDWidget.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_LocText.h"
#include "UI/HUD/TN_ResultsTexts.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"
#include "Player/MP_GamePlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CoopFlowHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureRuntimeWidgets();

	// Make sure ResultsOverlay starts hidden even if the designer forgot
	if (ResultsOverlay)
	{
		ResultsOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	RefreshTexts();
}

void UTN_CoopFlowHUDWidget::NativeDestruct()
{
	UnbindQuickChat();
	Super::NativeDestruct();
}

void UTN_CoopFlowHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// ── Chat fade-out ─────────────────────────────────────────────────────────
	if (bChatFadingOut && ChatHistoryBox)
	{
		ChatCurrentOpacity -= InDeltaTime / FMath::Max(ChatFadeOutSeconds, KINDA_SMALL_NUMBER);
		if (ChatCurrentOpacity <= 0.f)
		{
			ChatCurrentOpacity = 0.f;
			bChatFadingOut = false;
			ChatHistoryBox->SetVisibility(ESlateVisibility::Collapsed);
			ChatHistoryBox->ClearChildren();
		}
		else
		{
			ChatHistoryBox->SetRenderOpacity(ChatCurrentOpacity);
		}
	}

	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator < RefreshInterval)
	{
		return;
	}

	RefreshAccumulator = 0.f;
	RefreshTexts();
}

// ─────────────────────────────────────────────────────────────────────────────
// C++ fallback runtime widget creation (used when widget has no BP designer)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CoopFlowHUDWidget::EnsureRuntimeWidgets()
{
	if (!WidgetTree)
	{
		return;
	}

	if (!RootContainer)
	{
		RootContainer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootContainer"));
		if (RootContainer && !WidgetTree->RootWidget)
		{
			WidgetTree->RootWidget = RootContainer;
		}
	}

	if (!PrimaryText && RootContainer)
	{
		PrimaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PrimaryText"));
		if (PrimaryText)
		{
			PrimaryText->SetAutoWrapText(true);
			PrimaryText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			if (UVerticalBoxSlot* VBoxSlot = RootContainer->AddChildToVerticalBox(PrimaryText))
			{
				VBoxSlot->SetPadding(FMargin(16.f, 24.f, 16.f, 4.f));
			}
		}
	}

	if (!SecondaryText && RootContainer)
	{
		SecondaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SecondaryText"));
		if (SecondaryText)
		{
			SecondaryText->SetAutoWrapText(true);
			SecondaryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.f)));
			if (UVerticalBoxSlot* VBoxSlot = RootContainer->AddChildToVerticalBox(SecondaryText))
			{
				VBoxSlot->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));
			}
		}
	}
	// Note: ResultsOverlay and its children are designer-only; no C++ fallback needed.
}

// ─────────────────────────────────────────────────────────────────────────────
// Main refresh (called every 0.1 s)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CoopFlowHUDWidget::RefreshTexts()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ATN_CoopGameState* GameState = PC->GetWorld()
		? PC->GetWorld()->GetGameState<ATN_CoopGameState>()
		: nullptr;

	if (!GameState)
	{
		UnbindQuickChat();
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	BindQuickChat(GameState);

	// ── Detect state change ──────────────────────────────────────────────────
	if (!bFlowStateInitialized || GameState->MatchFlowState != LastKnownFlowState)
	{
		LastKnownFlowState    = GameState->MatchFlowState;
		bFlowStateInitialized = true;
		HandleFlowStateChange(LastKnownFlowState, GameState);
		OnFlowStateChanged(LastKnownFlowState); // optional BP hook
	}

	// ── Update status-strip texts ────────────────────────────────────────────
	const bool bShow = ShouldBeVisible(GameState->MatchFlowState);
	SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (bShow)
	{
		if (PrimaryText)   { PrimaryText->SetText(BuildPrimaryText(GameState)); }
		if (SecondaryText) { SecondaryText->SetText(BuildSecondaryText(GameState)); }
	}

	// ── Update results countdown every refresh tick ──────────────────────────
	if (bResultsVisible)
	{
		RefreshResultsCountdown(GameState);
		RefreshResultsExtras(GameState);
	}
}

void UTN_CoopFlowHUDWidget::RefreshResultsExtras(const ATN_CoopGameState* GameState)
{
	if (EndTitleText)
	{
		const FText Title = GameState ? TNResultsTexts::JumperTitle(GameState->JumperTitle) : FText::GetEmpty();
		if (!Title.EqualTo(EndTitleText->GetText()))
		{
			EndTitleText->SetText(Title);
		}
		EndTitleText->SetVisibility(Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	const APlayerController* PC = GetOwningPlayer();
	const ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (CoopScoreText)
	{
		const FText Breakdown = PS ? TNResultsTexts::CoopScoreBreakdown(PS->CoopScore) : FText::GetEmpty();
		if (!Breakdown.EqualTo(CoopScoreText->GetText()))
		{
			CoopScoreText->SetText(Breakdown);
		}
		CoopScoreText->SetVisibility(Breakdown.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UTN_CoopFlowHUDWidget::BindQuickChat(ATN_CoopGameState* GameState)
{
	if (!GameState)
	{
		UnbindQuickChat();
		return;
	}

	if (BoundQuickChatGameState == GameState)
	{
		if (!bQuickChatHistoryReplayed)
		{
			ReplayQuickChatHistory(GameState);
		}
		// ── Bind flow state delegate si no está ya bound ──────────────────────
		if (BoundFlowGameState != GameState)
		{
			if (BoundFlowGameState)
			{
				BoundFlowGameState->OnMatchFlowStateChanged.RemoveDynamic(
					this, &UTN_CoopFlowHUDWidget::OnMatchFlowStateChangedHandler);
			}
			GameState->OnMatchFlowStateChanged.AddUniqueDynamic(
				this, &UTN_CoopFlowHUDWidget::OnMatchFlowStateChangedHandler);
			BoundFlowGameState = GameState;
		}
		return;
	}

	UnbindQuickChat();
	BoundQuickChatGameState = GameState;
	GameState->OnQuickChatReceived.AddUniqueDynamic(this, &UTN_CoopFlowHUDWidget::HandleQuickChatReceived);
	ReplayQuickChatHistory(GameState);

	// ── Bind flow state delegate ──────────────────────────────────────────────
	if (BoundFlowGameState != GameState)
	{
		if (BoundFlowGameState)
		{
			BoundFlowGameState->OnMatchFlowStateChanged.RemoveDynamic(
				this, &UTN_CoopFlowHUDWidget::OnMatchFlowStateChangedHandler);
		}
		GameState->OnMatchFlowStateChanged.AddUniqueDynamic(
			this, &UTN_CoopFlowHUDWidget::OnMatchFlowStateChangedHandler);
		BoundFlowGameState = GameState;
	}
}

void UTN_CoopFlowHUDWidget::UnbindQuickChat()
{
	if (BoundQuickChatGameState)
	{
		BoundQuickChatGameState->OnQuickChatReceived.RemoveDynamic(this, &UTN_CoopFlowHUDWidget::HandleQuickChatReceived);
		BoundQuickChatGameState = nullptr;
	}

	bQuickChatHistoryReplayed = false;
	LastQuickChatSequenceSeen = 0;

	// ── Unbind flow state delegate ────────────────────────────────────────────
	if (BoundFlowGameState)
	{
		if (IsValid(BoundFlowGameState))
		{
			BoundFlowGameState->OnMatchFlowStateChanged.RemoveDynamic(
				this, &UTN_CoopFlowHUDWidget::OnMatchFlowStateChangedHandler);
		}
		BoundFlowGameState = nullptr;
	}
}

void UTN_CoopFlowHUDWidget::ReplayQuickChatHistory(const ATN_CoopGameState* GameState)
{
	if (!GameState)
	{
		return;
	}

	for (const FTN_QuickChatEntry& Entry : GameState->QuickChatHistory)
	{
		HandleQuickChatReceived(Entry);
	}

	bQuickChatHistoryReplayed = true;
}

void UTN_CoopFlowHUDWidget::HandleQuickChatReceived(const FTN_QuickChatEntry& Entry)
{
	if (Entry.Sequence <= LastQuickChatSequenceSeen)
	{
		return;
	}

	LastQuickChatSequenceSeen = Entry.Sequence;

	FText SenderName = FText::GetEmpty();
	FText MessageText = FText::GetEmpty();
	UTexture2D* Icon = nullptr;

	bool bResolved = false;
	if (const AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(GetOwningPlayer()))
	{
		bResolved = PC->ResolveQuickChatDisplayData(Entry, SenderName, MessageText, Icon);
	}

	if (!bResolved)
	{
		if (BoundQuickChatGameState)
		{
			SenderName = BoundQuickChatGameState->ResolveQuickChatSenderName(Entry.SenderPlayerId);
		}

		if (MessageText.IsEmpty())
		{
			MessageText = FText::Format(NSLOCTEXT("TNHUD", "QuickChatFallback", "Mensaje #{0}"), TNLocText::Int(static_cast<int32>(Entry.MessageID)));
		}
	}

	OnQuickChatEntryReceived(Entry.Sequence, SenderName, MessageText, Icon, Entry.ServerTime);
}

// ─────────────────────────────────────────────────────────────────────────────
// Results panel — C++ logic
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CoopFlowHUDWidget::HandleFlowStateChange(ETNMatchFlowState NewState, const ATN_CoopGameState* GameState)
{
	if (NewState == ETNMatchFlowState::Results)
	{
		ShowResultsPanel(GameState);
	}
	else if (bResultsVisible)
	{
		HideResultsPanel();
	}
}

void UTN_CoopFlowHUDWidget::ShowResultsPanel(const ATN_CoopGameState* GameState)
{
	bResultsVisible = true;

	if (ResultsOverlay)
	{
		ResultsOverlay->SetVisibility(ESlateVisibility::Visible);
	}

	// ── Gather local player stats ────────────────────────────────────────────
	const ATN_CoopPlayerState* TNPS = nullptr;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		TNPS = PC->GetPlayerState<ATN_CoopPlayerState>();
	}

	const int32  Rank          = TNPS ? TNPS->FinishRank       : 0;
	const float  Time          = TNPS ? TNPS->FinishTimeSeconds : -1.f;
	const bool   bEliminated   = TNPS && TNPS->bIsEliminated;
	const bool   bFinishedNorm = TNPS && TNPS->bHasFinishedRun && Rank > 0 && !bEliminated;

	// ── Title ────────────────────────────────────────────────────────────────
	if (ResultsTitle)
	{
		ResultsTitle->SetText(BuildRankTitle(Rank, bEliminated));
	}

	// ── Rank line ────────────────────────────────────────────────────────────
	if (ResultsRankText)
	{
		if (bEliminated)
		{
			ResultsRankText->SetText(NSLOCTEXT("TNHUD", "ResultsRankEliminated", "Eliminado"));
		}
		else if (bFinishedNorm)
		{
			ResultsRankText->SetText(FText::Format(NSLOCTEXT("TNHUD", "ResultsRankPlace", "Puesto: #{0}"), TNLocText::Int(Rank)));
		}
		else
		{
			ResultsRankText->SetText(FText::GetEmpty());
		}
	}

	// ── Time line ────────────────────────────────────────────────────────────
	if (ResultsTimeText)
	{
		ResultsTimeText->SetText((bFinishedNorm && Time > 0.f)
			? FText::Format(NSLOCTEXT("TNHUD", "ResultsTime", "Tiempo: {0}s"), TNLocText::OneDecimal(Time))
			: FText::GetEmpty());
	}

	// ── Spectator hint ───────────────────────────────────────────────────────
	if (SpectatorHint)
	{
		SpectatorHint->SetText(bEliminated
			? NSLOCTEXT("TNHUD", "ResultsSpectatorHint", "Scroll para cambiar de jugador")
			: FText::GetEmpty());
	}

	// ── Initial countdown ────────────────────────────────────────────────────
	RefreshResultsCountdown(GameState);
	RefreshResultsExtras(GameState);

	// ── Scoreboard global ────────────────────────────────────────────────────
	RefreshScoreboard(GameState);

	// Suscribirse al delegate para refresh si llegan results más tarde
	// (ej: jugador termina al final mientras Results ya se mostraba para los demás)
	if (ATN_CoopGameState* MutGS = const_cast<ATN_CoopGameState*>(GameState))
	{
		MutGS->OnRaceResultsUpdated.RemoveDynamic(this, &UTN_CoopFlowHUDWidget::HandleRaceResultsUpdated);
		MutGS->OnRaceResultsUpdated.AddDynamic(this, &UTN_CoopFlowHUDWidget::HandleRaceResultsUpdated);
	}
}

void UTN_CoopFlowHUDWidget::HandleRaceResultsUpdated()
{
	if (!bResultsVisible) { return; }
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const ATN_CoopGameState* GS = PC->GetWorld()->GetGameState<ATN_CoopGameState>())
		{
			RefreshScoreboard(GS);
		}
	}
}

void UTN_CoopFlowHUDWidget::RefreshScoreboard(const ATN_CoopGameState* GameState)
{
	if (!GameState) { return; }

	const TArray<FTN_RaceResultEntry>& Results = GameState->RaceResults;

	for (int32 i = 0; i < MaxScoreboardRows; ++i)
	{
		const FTN_RaceResultEntry* Entry = (i < Results.Num()) ? &Results[i] : nullptr;
		FillScoreboardRow(i, Entry);
	}
	ApplyScoreboardDensity(Results.Num());
}

void UTN_CoopFlowHUDWidget::GetScoreboardRowTexts(int32 RowIndex, UTextBlock* (&OutTexts)[4]) const
{
	OutTexts[0] = OutTexts[1] = OutTexts[2] = OutTexts[3] = nullptr;
	switch (RowIndex)
	{
	case 0: OutTexts[0] = Row1RankText; OutTexts[1] = Row1NameText; OutTexts[2] = Row1TimeText; OutTexts[3] = Row1ScoreText; break;
	case 1: OutTexts[0] = Row2RankText; OutTexts[1] = Row2NameText; OutTexts[2] = Row2TimeText; OutTexts[3] = Row2ScoreText; break;
	case 2: OutTexts[0] = Row3RankText; OutTexts[1] = Row3NameText; OutTexts[2] = Row3TimeText; OutTexts[3] = Row3ScoreText; break;
	case 3: OutTexts[0] = Row4RankText; OutTexts[1] = Row4NameText; OutTexts[2] = Row4TimeText; OutTexts[3] = Row4ScoreText; break;
	case 4: OutTexts[0] = Row5RankText; OutTexts[1] = Row5NameText; OutTexts[2] = Row5TimeText; OutTexts[3] = Row5ScoreText; break;
	case 5: OutTexts[0] = Row6RankText; OutTexts[1] = Row6NameText; OutTexts[2] = Row6TimeText; OutTexts[3] = Row6ScoreText; break;
	case 6: OutTexts[0] = Row7RankText; OutTexts[1] = Row7NameText; OutTexts[2] = Row7TimeText; OutTexts[3] = Row7ScoreText; break;
	case 7: OutTexts[0] = Row8RankText; OutTexts[1] = Row8NameText; OutTexts[2] = Row8TimeText; OutTexts[3] = Row8ScoreText; break;
	default: break;
	}
}

void UTN_CoopFlowHUDWidget::ApplyScoreboardDensity(int32 NumEntries)
{
	if (ScoreboardRowPanels.Num() == 0)
	{
		return;
	}
	// Hasta cuatro resultados, las cuatro filas de siempre; con más, una fila por resultado y más compactas (letra y
	// relleno algo menores) para que quepan los ocho de la sesión.
	const int32 Shown = FMath::Clamp(NumEntries, 4, MaxScoreboardRows);
	const bool bCompact = Shown > 4;
	for (int32 Row = 0; Row < ScoreboardRowPanels.Num(); ++Row)
	{
		if (UBorder* Panel = ScoreboardRowPanels[Row])
		{
			Panel->SetVisibility(Row < Shown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			Panel->SetPadding(bCompact ? FMargin(14.f, 2.f) : FMargin(14.f, 5.f));
		}
		UTextBlock* Cells[4];
		GetScoreboardRowTexts(Row, Cells);
		for (UTextBlock* Cell : Cells)
		{
			if (!Cell)
			{
				continue;
			}
			FSlateFontInfo Font = Cell->GetFont();
			if (ScoreboardBaseFontSize <= 0.f)
			{
				ScoreboardBaseFontSize = Font.Size;
			}
			const float Wanted = bCompact ? FMath::RoundToFloat(ScoreboardBaseFontSize * 0.88f) : ScoreboardBaseFontSize;
			if (!FMath::IsNearlyEqual(Font.Size, Wanted))
			{
				Font.Size = Wanted;
				Cell->SetFont(Font);
			}
		}
	}
}

void UTN_CoopFlowHUDWidget::FillScoreboardRow(int32 RowIndex, const FTN_RaceResultEntry* Entry)
{
	UTextBlock* Cells[4];
	GetScoreboardRowTexts(RowIndex, Cells);
	UTextBlock* Rank = Cells[0];
	UTextBlock* Name = Cells[1];
	UTextBlock* Time = Cells[2];
	UTextBlock* Score = Cells[3];

	if (!Entry)
	{
		// Fila vacía: limpiar texto
		if (Rank)  Rank->SetText(FText::GetEmpty());
		if (Name)  Name->SetText(FText::GetEmpty());
		if (Time)  Time->SetText(FText::GetEmpty());
		if (Score) Score->SetText(FText::GetEmpty());
		return;
	}

	if (Rank)
	{
		Rank->SetText(Entry->bIsEliminated
			? INVTEXT("✗")
			: FText::Format(NSLOCTEXT("TNHUD", "ScoreboardRank", "{0}º"), Entry->FinishRank));
	}
	if (Name)  Name->SetText(TNLocText::Literal(Entry->PlayerName));
	if (Time)
	{
		Time->SetText(Entry->bIsEliminated
			? INVTEXT("—")
			: FText::Format(NSLOCTEXT("TNHUD", "ScoreboardTime", "{0}s"), TNLocText::OneDecimal(Entry->FinishTimeSeconds)));
	}
	if (Score) Score->SetText(FText::AsNumber(Entry->RaceScore));
}

void UTN_CoopFlowHUDWidget::HideResultsPanel()
{
	bResultsVisible = false;

	if (ResultsOverlay)
	{
		ResultsOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTN_CoopFlowHUDWidget::RefreshResultsCountdown(const ATN_CoopGameState* GameState)
{
	if (!ResultsCountdown || !GameState)
	{
		return;
	}

	ResultsCountdown->SetText(
		FText::Format(NSLOCTEXT("TNHUD", "ReturningToLobby", "Volviendo al lobby en: {0}"), TNLocText::Int(GameState->CountdownValue)));
}

FText UTN_CoopFlowHUDWidget::BuildRankTitle(int32 FinishRank, bool bEliminated) const
{
	if (bEliminated)
	{
		return NSLOCTEXT("TNHUD", "ResultsTitleEliminated", "¡Eliminado!");
	}

	switch (FinishRank)
	{
	case 1:  return NSLOCTEXT("TNHUD", "ResultsTitleFirst", "¡Primero!");
	case 2:  return NSLOCTEXT("TNHUD", "ResultsTitleSecond", "Segundo");
	case 3:  return NSLOCTEXT("TNHUD", "ResultsTitleThird", "Tercero");
	default:
		return FinishRank > 0
			? FText::Format(NSLOCTEXT("TNHUD", "ResultsTitlePlace", "Puesto #{0}"), TNLocText::Int(FinishRank))
			: NSLOCTEXT("TNHUD", "ResultsTitleEliminated", "¡Eliminado!");
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Status-strip text builders
// ─────────────────────────────────────────────────────────────────────────────

FText UTN_CoopFlowHUDWidget::BuildPrimaryText(const ATN_CoopGameState* GameState) const
{
	switch (GameState->MatchFlowState)
	{
	case ETNMatchFlowState::WaitingForPlayers:
		return FText::Format(NSLOCTEXT("TNHUD", "FlowWaiting", "Sala: {0}/{1} | Zona: {2}/{3}"),
			TNLocText::Int(GameState->ConnectedPlayers), TNLocText::Int(GameState->ExpectedPlayers),
			TNLocText::Int(GameState->PlayersInStartZone), TNLocText::Int(GameState->ConnectedPlayers));
	case ETNMatchFlowState::Countdown:
		return FText::Format(NSLOCTEXT("TNHUD", "FlowCountdown", "¡Todos listos! Empieza en: {0}"), TNLocText::Int(GameState->CountdownValue));
	case ETNMatchFlowState::Cinematic:
		return NSLOCTEXT("TNHUD", "FlowPreparing", "Preparando...");
	case ETNMatchFlowState::InProgress:
		return FText::Format(NSLOCTEXT("TNHUD", "FlowInProgress", "Carrera en curso. Meta: {0}/{1}"),
			TNLocText::Int(GameState->FinishedPlayers), TNLocText::Int(GameState->ExpectedPlayers));
	case ETNMatchFlowState::Results:
		return FText::Format(NSLOCTEXT("TNHUD", "ReturningToLobby", "Volviendo al lobby en: {0}"), TNLocText::Int(GameState->CountdownValue));
	default:
		return FText::GetEmpty();
	}
}

FText UTN_CoopFlowHUDWidget::BuildSecondaryText(const ATN_CoopGameState* GameState) const
{
	switch (GameState->MatchFlowState)
	{
	case ETNMatchFlowState::WaitingForPlayers:
		return NSLOCTEXT("TNHUD", "FlowWaitingHint", "El contador arranca cuando todos los jugadores conectados estén en la zona.");
	case ETNMatchFlowState::Countdown:
		return FText::Format(NSLOCTEXT("TNHUD", "FlowCountdownDetail", "Conectados: {0}/{1} | Zona: {2}/{3}"),
			TNLocText::Int(GameState->ConnectedPlayers), TNLocText::Int(GameState->ExpectedPlayers),
			TNLocText::Int(GameState->PlayersInStartZone), TNLocText::Int(GameState->ConnectedPlayers));
	case ETNMatchFlowState::Cinematic:
		return NSLOCTEXT("TNHUD", "FlowCinematicHint", "Mantente preparado para el viaje al mapa de carrera.");
	case ETNMatchFlowState::InProgress:
		if (const APlayerController* PC = GetOwningPlayer())
		{
			if (const ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
			{
				if (TNPS->DeathZoneTimeRemaining >= 0.f)
				{
					return FText::Format(NSLOCTEXT("TNHUD", "FlowDeathZone", "Peligro: sal de la zona de muerte ({0}s)"),
						TNLocText::OneDecimal(TNPS->DeathZoneTimeRemaining));
				}
			}
		}
		return NSLOCTEXT("TNHUD", "FlowFinishHint", "Cruza la meta para pasar a espectador.");
	case ETNMatchFlowState::Results:
		return FText::GetEmpty(); // Results panel covers this state
	default:
		return FText::GetEmpty();
	}
}

bool UTN_CoopFlowHUDWidget::ShouldBeVisible(ETNMatchFlowState State) const
{
	return State == ETNMatchFlowState::WaitingForPlayers
		|| State == ETNMatchFlowState::Countdown
		|| State == ETNMatchFlowState::Cinematic
		|| State == ETNMatchFlowState::InProgress
		|| State == ETNMatchFlowState::Results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quick Chat feed — C++ implementation
// ─────────────────────────────────────────────────────────────────────────────

UHorizontalBox* UTN_CoopFlowHUDWidget::BuildChatRow(const FText& SenderName, const FText& MessageText, UTexture2D* Icon)
{
	UHorizontalBox* Row = NewObject<UHorizontalBox>(this);

	if (Icon)
	{
		UImage* IconWidget = NewObject<UImage>(this);
		IconWidget->SetBrushFromTexture(Icon, false);
		IconWidget->SetDesiredSizeOverride(FVector2D(20.f, 20.f));
		if (UHorizontalBoxSlot* HSlot = Row->AddChildToHorizontalBox(IconWidget))
		{
			HSlot->SetVerticalAlignment(VAlign_Center);
			HSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		}
	}

	UTextBlock* TextWidget = NewObject<UTextBlock>(this);
	TextWidget->SetText(FText::Format(NSLOCTEXT("TNHUD", "ChatLine", "{0}: {1}"), SenderName, MessageText));
	TextWidget->SetColorAndOpacity(FSlateColor(FLinearColor(ChatTextColor)));
	TextWidget->SetAutoWrapText(true);
	if (UHorizontalBoxSlot* HSlot = Row->AddChildToHorizontalBox(TextWidget))
	{
		HSlot->SetVerticalAlignment(VAlign_Center);
		HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	return Row;
}

void UTN_CoopFlowHUDWidget::OnQuickChatEntryReceived_Implementation(
	int32 Sequence, const FText& SenderName, const FText& MessageText,
	UTexture2D* Icon, float ServerTimeSeconds)
{
	if (!ChatHistoryBox)
	{
		return;
	}

	// Si el feed ya estaba oculto o casi invisible, limpiar historial para
	// que solo aparezca el nuevo mensaje (no los mensajes viejos de golpe).
	if (ChatCurrentOpacity < 0.1f || ChatHistoryBox->GetVisibility() == ESlateVisibility::Collapsed)
	{
		ChatHistoryBox->ClearChildren();
	}

	// ── Build entry row: [Icon?] [Sender: Message] ────────────────────────────
	UHorizontalBox* Row = BuildChatRow(SenderName, MessageText, Icon);

	// ── Add to feed and enforce max line count ────────────────────────────────
	if (UVerticalBoxSlot* VSlot = ChatHistoryBox->AddChildToVerticalBox(Row))
	{
		VSlot->SetPadding(FMargin(0.f, 2.f));
	}

	while (ChatHistoryBox->GetChildrenCount() > MaxChatLines)
	{
		ChatHistoryBox->RemoveChildAt(0);
	}

	// Restaurar visibilidad y opacidad; resetear timer de inactividad.
	ChatHistoryBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	ChatHistoryBox->SetRenderOpacity(1.f);
	bChatFadingOut     = false;
	ChatCurrentOpacity = 1.f;

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->GetWorldTimerManager().SetTimer(
			ChatFadeTimer, this, &UTN_CoopFlowHUDWidget::StartChatFade,
			ChatInactivitySeconds, false);
	}
}

void UTN_CoopFlowHUDWidget::StartChatFade()
{
	bChatFadingOut = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnMatchFlowStateChanged delegate handler — backup channel beside polling
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CoopFlowHUDWidget::OnMatchFlowStateChangedHandler(ETNMatchFlowState NewState)
{
	// Guard: abort if we already processed this state (prevents double-fire with the 0.1s polling path)
	if (bFlowStateInitialized && NewState == LastKnownFlowState)
	{
		return;
	}

	ATN_CoopGameState* GameState = GetOwningPlayer() && GetOwningPlayer()->GetWorld()
		? GetOwningPlayer()->GetWorld()->GetGameState<ATN_CoopGameState>()
		: nullptr;

	if (!GameState)
	{
		return;
	}

	// Sync polling state to avoid double-firing when RefreshTexts next runs
	LastKnownFlowState    = NewState;
	bFlowStateInitialized = true;

	HandleFlowStateChange(NewState, GameState);
	OnFlowStateChanged(NewState);

	const bool bShow = ShouldBeVisible(NewState);
	SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
