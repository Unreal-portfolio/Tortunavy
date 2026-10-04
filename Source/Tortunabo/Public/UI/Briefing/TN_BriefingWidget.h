#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_BriefingWidget.generated.h"

class ATN_GeneralBriefing;
class UScrollBox;
class UTextBlock;
class UTN_ShopButton;
class UVerticalBox;

/**
 * Sesión informativa del General Galápago (ATN_GeneralBriefing), con el estilo de la tienda: a la izquierda el general
 * hablando en su bocadillo (letra a letra) y a la derecha cinco pestañas con su contenido:
 *  - «Misión» (la primera): el modo (Cooperativo o Carrera, con una línea de cada uno) y la dificultad (Fácil, Normal o
 *    Difícil) de la próxima partida. Solo el anfitrión los cambia (TNLobbyMission, sin RPC: su interfaz corre en el
 *    servidor); los demás ven la orden del día, que se replica en el general, y el aviso de que la elige el anfitrión.
 *  - «Cómo se juega»: objetivo, moverse, panzazo, nado, caparazón, llevar y lanzar, peligros y huevos.
 *  - «Modos de juego»: Cooperativo, Carrera, 2 vs 2 y Clásico, y cómo se elige.
 *  - «Reglas»: salida, reaparición, tiempo límite, coger a rivales y juego limpio.
 *  - «Controles»: cada acción con sus teclas reales, leídas de Enhanced Input (teclado y mando).
 * Q/E, Tab, LB/RB o las flechas cambian de pestaña (en «Misión», para el anfitrión, arriba/abajo eligen fila y
 * izquierda/derecha cambian la opción; con el ratón, clic en la opción); arriba/abajo desplazan; Escape o «¡Entendido!»
 * cierran.
 */
UCLASS()
class TORTUNABO_API UTN_BriefingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetGeneral(ATN_GeneralBriefing* InGeneral);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DialogText;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> Scroll;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Page;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopButton>> Tabs;

	/** Ayuda de teclas de abajo (cambia en «Misión» para el anfitrión). */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> KeysHint;

	// ── Pestaña «Misión» (solo mientras se ve) ──

	/** Un botón por modo de TNLobbyMission::MenuModes, en su orden. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopButton>> ModeButtons;

	/** Un botón por dificultad de TNLobbyMission::Difficulties, en su orden. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopButton>> DifficultyButtons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeHeading;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DifficultyHeading;

	/** Karts (solo el anfitrión): una tortuga por kart o por parejas. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopButton>> SeatsButtons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SeatsHeading;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> SeatsRow;

	/** «Orden del día: CARRERA · NORMAL». */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MissionOrders;

	TWeakObjectPtr<ATN_GeneralBriefing> General;
	int32 Tab = 0;
	FString FullLine;
	float Reveal = 0.f;

	/** Fila con el foco del teclado y el mando en «Misión»: 0 = modo, 1 = dificultad, 2 = plazas por kart (karts). */
	int32 MissionRow = 0;
	/** Lo último que se ha pintado en «Misión» (si cambia desde otra máquina, se repinta y el general lo anuncia). */
	ETNProcGameMode ShownMode = ETNProcGameMode::Count;
	ETNProcDifficulty ShownDifficulty = ETNProcDifficulty::Count;

	void BuildTree();
	void ShowTab(int32 Index);
	void Say(const FText& Line);
	void Close();
	void RefreshKeysHint();

	void BuildMissionPage();
	/** Pinta la elección actual y el foco; con bAnnounce, si ha cambiado desde fuera, el general lo anuncia. */
	void RefreshMission(bool bAnnounce);
	/** Clic en un modo o en una dificultad (o ←/→): solo el anfitrión; a los demás, el general les recuerda quién manda. */
	void PickMode(ETNProcGameMode Mode);
	void PickDifficulty(ETNProcDifficulty Difficulty);
	/** Karts: tortugas por kart (1 o 2). Solo el anfitrión. */
	void PickSeats(int32 Seats);
	/** Plazas por kart elegidas en la GameInstance del anfitrión. */
	int32 GetKartSeats() const;
	/** Hay fila de plazas: el anfitrión con los karts elegidos. */
	bool HasSeatsRow() const;
	/** ←/→ con el teclado o el mando: la opción anterior o la siguiente de la fila con el foco. */
	void StepMission(int32 Direction);
	void SetMissionRow(int32 Row);
	/** Si esta máquina puede elegir la misión (es el anfitrión). */
	bool CanChooseMission() const;
	/** La misión que se ve: la de la GameInstance en el anfitrión y la replicada en el general en los demás. */
	ETNProcGameMode GetMissionMode() const;
	ETNProcDifficulty GetMissionDifficulty() const;

	void AddHeading(const FText& Text);
	void AddParagraph(const FText& Text);
	/** Fila de «Controles»: acción y sus teclas (las de teclado en arena, las de mando en azul). */
	void AddControlRow(const FText& ActionName, const TArray<FString>& ActionPaths);

	/** Teclas de teclado y ratón (y de mando aparte) asignadas ahora a una acción de Enhanced Input. */
	void KeysFor(const TArray<FString>& ActionPaths, TArray<FText>& OutKeyboard, TArray<FText>& OutGamepad) const;
};
