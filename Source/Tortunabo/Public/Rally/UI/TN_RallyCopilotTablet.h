// Pantallita de la artillera del Rally (#299): tableta de playa resistente al agua con el mapa del tramo (buggies como
// caparazones de colores con su puesto), el perfil de los próximos 400 m con las notas de copiloto encima, la próxima
// nota en grande para cantarla y la munición. Modo compacto en una esquina para la conductora que va sin artillera.
// Todo en C++ sin asset UMG: se dibuja en NativePaint con la paleta Tortunavy (TNHUDArt / TNHUDStyle). Solo lee estado
// replicado (ATN_RallyGameState, la pista local y la torreta), así que vale igual en el servidor y en los clientes.
// Se presenta en la pantalla (TNVR::AddToScreen: el viewport sin VR, el panel del mundo con VR) o dentro de un
// UWidgetComponent (#334: tableta en la mano o en el salpicadero), con el mismo dibujo.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Rally/TN_RallyHitReport.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "Rally/TN_RallyVehicle.h"
#include "TN_RallyCopilotTablet.generated.h"

class APawn;
class APlayerController;
class ATN_Buggy;
class ATN_RallyGameState;
class ATN_RallyTrack;
class UInputComponent;
class UTN_BuggyTurretComponent;
class UWidgetComponent;
struct FTNRallyStanding;
struct FTNRallyTabletPainter;
struct FTNRallyTabletMapProjection;

/** Lo que enseña la tableta. */
UENUM(BlueprintType)
enum class ETNRallyTabletView : uint8
{
	/** No se dibuja (no va en un buggy, la artillera la tiene guardada o no hay pista). */
	Hidden,
	/** Tableta grande de la artillera: mapa, perfil con notas, próxima nota y munición. */
	Full,
	/** Esquina de la conductora sin artillera: mapa pequeño y próxima nota. */
	Compact
};

/** Dónde se presenta la tableta (#334). */
UENUM(BlueprintType)
enum class ETNRallyTabletPresentation : uint8
{
	/** En la pantalla con TNVR::AddToScreen: grande abajo en el centro o compacta en la esquina, como siempre. */
	Screen,
	/** Dentro de un UWidgetComponent: la tableta ocupa todo el panel, centrada y a escala. */
	World
};

namespace TNRallyTabletLayout
{
	/** Cómo se encaja la maqueta de una vista en la geometría del widget (unidades de diseño). */
	struct FFit
	{
		FVector2f Design = FVector2f(1.f, 1.f);
		float MaxWidthFraction = 1.f;
		float MaxHeightFraction = 1.f;
		/** Punto de anclaje (0..1) dentro del espacio libre. */
		FVector2f Anchor = FVector2f(0.5f, 0.5f);
		FVector2f Margin = FVector2f::ZeroVector;
	};

	/** Origen (px locales) y escala de la maqueta encajada. */
	struct FPlacement
	{
		FVector2f Origin = FVector2f::ZeroVector;
		float Scale = 1.f;
	};

	/** Maqueta de la vista: grande (Full) o compacta (Compact); Hidden usa la grande. */
	TORTUNABO_API FVector2f DesignSize(ETNRallyTabletView View);

	/** Encaje de una vista según la presentación: en Screen, el de siempre; en World, todo el panel. */
	TORTUNABO_API FFit FitFor(ETNRallyTabletPresentation Presentation, ETNRallyTabletView View);

	/** Escala la maqueta sin pasar de las fracciones del widget de LocalSize y la ancla con su margen. */
	TORTUNABO_API FPlacement Place(const FVector2f& LocalSize, const FFit& Fit);
}

/** Munición que enseña la tableta; la rellena UTN_RallyCopilotTablet::ReadAmmo a partir de la torreta. */
USTRUCT(BlueprintType)
struct FTNRallyTabletAmmo
{
	GENERATED_BODY()

	/** La que dispara ahora. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally|Tableta")
	ETNRallyAmmo Selected = ETNRallyAmmo::Coco;

	/** Carga especial que lleva (None si no lleva). */
	UPROPERTY(BlueprintReadOnly, Category = "Rally|Tableta")
	ETNRallyAmmo Special = ETNRallyAmmo::None;

	UPROPERTY(BlueprintReadOnly, Category = "Rally|Tableta")
	int32 SpecialCharges = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rally|Tableta")
	float Heat01 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Rally|Tableta")
	bool bOverheated = false;
};

/** Un buggy en el mapa (copia de lo que hace falta para pintar; sin punteros a actores). */
struct FTNRallyTabletMark
{
	FVector Location = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	int32 Place = 0;
	bool bMine = false;
	/** Retirado o ya en meta: se pinta apagado. */
	bool bOut = false;
};

UCLASS()
class TORTUNABO_API UTN_RallyCopilotTablet : public UUserWidget
{
	GENERATED_BODY()

public:
	// ── Acceso por jugador local (para el peón de la artillera, el HUD o el mando) ─────────────────────────────

	/** Tableta de este jugador local; nullptr si aún no tiene. */
	static UTN_RallyCopilotTablet* FindFor(const APlayerController* Player);

	/** La de este jugador local, creada y añadida al viewport si no la tenía. Nullptr si Player no es local. */
	static UTN_RallyCopilotTablet* FindOrCreateFor(APlayerController* Player);

	/** Abre o guarda la tableta grande del jugador (la crea si hace falta). Devuelve si queda abierta. */
	static bool ToggleFor(APlayerController* Player);

	/** La tableta grande de este jugador está abierta: mientras tanto la torreta no dispara. */
	static bool IsOpenFor(const APlayerController* Player);

	/**
	 * Ata Tab, M y el botón Vista del mando (Gamepad_Special_Left) a ToggleFor del controlador de Pawn. En el editor el
	 * Tabulador abre la pausa (UTN_GameSettingsSubsystem): ahí se usa M o el mando.
	 */
	static void BindToggleKeys(UInputComponent* Input, APawn* Pawn);

	/**
	 * Presenta la tableta del jugador local Player (creada si no la tenía) dentro de Host (#334): la quita de la pantalla y
	 * pone en Host el mismo dibujo. El tamaño del panel lo decide quien lo aloja (TNRallyTabletLayout::DesignSize).
	 * Nullptr si Player no es local o falta Host.
	 */
	static UTN_RallyCopilotTablet* PresentInWorldFor(APlayerController* Player, UWidgetComponent* Host);

	/** Devuelve la tableta del jugador a la pantalla (TNVR::AddToScreen) si estaba en un panel del mundo. */
	static UTN_RallyCopilotTablet* PresentOnScreenFor(APlayerController* Player);

	/** Munición que se enseña. Único punto que lee la torreta: si cambia su API (selección, CycleAmmo), se adapta aquí. */
	static FTNRallyTabletAmmo ReadAmmo(const UTN_BuggyTurretComponent* Turret);

	// ── Estado ────────────────────────────────────────────────────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Rally|Tableta")
	void SetTabletOpen(bool bInOpen);

	UFUNCTION(BlueprintCallable, Category = "Rally|Tableta")
	void ToggleTablet();

	/** Abierta en grande (solo en la vista Full: la compacta nunca bloquea el disparo). */
	UFUNCTION(BlueprintPure, Category = "Rally|Tableta")
	bool IsTabletOpen() const;

	/**
	 * Fuerza el modo compacto (true) o el grande (false) y desactiva la elección automática por plaza. Por defecto
	 * la tableta elige sola: grande para la artillera, compacta para la conductora sin artillera.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rally|Tableta")
	void SetCompactMode(bool bInCompact);

	UFUNCTION(BlueprintPure, Category = "Rally|Tableta")
	bool IsCompactMode() const { return bCompact; }

	/** Vuelve a elegir el modo por la plaza del jugador local. */
	UFUNCTION(BlueprintCallable, Category = "Rally|Tableta")
	void SetAutoRole(bool bInAutoRole);

	UFUNCTION(BlueprintPure, Category = "Rally|Tableta")
	ETNRallyTabletView GetView() const { return View; }

	/** Próxima nota tal como se canta («izquierda 3, no cortes»); vacío si no hay ninguna en los próximos 400 m. */
	UFUNCTION(BlueprintPure, Category = "Rally|Tableta")
	FText GetNextNoteText() const;

	UFUNCTION(BlueprintPure, Category = "Rally|Tableta")
	ETNRallyTabletPresentation GetPresentation() const { return Presentation; }

	/** Registro de impactos (#332): añade Line arriba con su color (quedan las 3 últimas; se apagan a los 12 s). */
	void AddHitLine(const FText& Line, const FLinearColor& Color);

	/** Líneas que hay ahora en el registro de impactos. */
	int32 GetHitLineCount() const { return HitLog.Num(); }

	/** Panel del mundo que la aloja en la presentación World (nullptr en Screen). */
	UWidgetComponent* GetWorldHost() const { return WorldHost.Get(); }

	/** Distancia por delante que cubren el perfil y las notas (cm). */
	UPROPERTY(EditAnywhere, Category = "Rally|Tableta", meta = (ClampMin = "5000"))
	float LookAheadCm = 40000.f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** Crea la tableta de Player y la registra, sin ponerla en ningún sitio. */
	static UTN_RallyCopilotTablet* CreateFor(APlayerController* Player);
	/** Está puesta ahora en la pantalla o en su panel del mundo. */
	bool IsPresented() const;
	void ShowOnScreen();
	void ShowInWorld(UWidgetComponent& Host);

	// TN_RallyCopilotTablet.cpp: estado que se refresca en NativeTick.
	const ATN_Buggy* FindLocalBuggy(const ATN_RallyGameState* RallyState) const;
	const FTNRallyStanding* FindLocalStanding(const ATN_RallyGameState* RallyState) const;
	void RefreshView(const ATN_RallyGameState* RallyState);
	void RefreshTrack(const ATN_RallyGameState& RallyState);
	void RefreshMarks(const ATN_RallyGameState& RallyState);
	void RefreshProgress(const ATN_Buggy& Buggy);
	void RefreshMapBounds();

	// TN_RallyCopilotTabletPaint.cpp: dibujo (constante, solo lee lo de arriba).
	void PaintFull(FTNRallyTabletPainter& Painter) const;
	void PaintCompact(FTNRallyTabletPainter& Painter) const;
	void PaintMap(FTNRallyTabletPainter& Painter, const FBox2D& Area, bool bSmall) const;
	void PaintMarks(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection, bool bSmall) const;
	void PaintProfile(FTNRallyTabletPainter& Painter, const FBox2D& Area) const;
	void PaintProfileNotes(FTNRallyTabletPainter& Painter, const FBox2D& Area) const;
	/** Munición en la tableta compacta (conductora sola): la que dispara, la especial y la próxima caja. */
	void PaintCompactAmmo(FTNRallyTabletPainter& Painter, const FBox2D& Area) const;
	/** «Caja a 120 m» (la próxima fila de cajas) o vacío si no hay ninguna en los próximos metros. */
	FText NextBoxText() const;
	void PaintNextNote(FTNRallyTabletPainter& Painter, const FBox2D& Area, bool bSmall) const;
	void PaintAmmo(FTNRallyTabletPainter& Painter, const FBox2D& Area) const;
	void PaintHeader(FTNRallyTabletPainter& Painter) const;
	/** Registro de impactos (#332) sobre el pie del mapa: la línea más nueva arriba y las viejas apagándose. */
	void PaintHitLog(FTNRallyTabletPainter& Painter, const FBox2D& Area, bool bSmall) const;

	ETNRallyTabletView View = ETNRallyTabletView::Hidden;
	ETNRallyTabletPresentation Presentation = ETNRallyTabletPresentation::Screen;
	/** Débil: el panel es del actor que lo aloja y la tableta no lo mantiene vivo. */
	TWeakObjectPtr<UWidgetComponent> WorldHost;
	bool bOpen = false;
	bool bCompact = false;
	bool bAutoRole = true;

	/** Pista de la que salen las notas (para rehacerlas si cambia). */
	TWeakObjectPtr<const ATN_RallyTrack> NotesTrack;
	float NotesTrackLengthCm = 0.f;
	TNRallyPaceNotes::FTrackNotes TrackNotes;
	/** Caja en planta del eje (cm) para escalar el mapa. */
	FBox2D MapBounds = FBox2D(ForceInit);

	TArray<FTNRallyTabletMark> Marks;
	TArray<TNRallyPaceNotes::FNoteAhead> Ahead;
	/** Arcos de las filas de cajas en el eje de las notas (ATN_RallyTrack::GetAmmoRowArcs, reescalados). */
	TArray<double> AmmoRowNoteArcs;
	/** Distancias a las filas de cajas de los próximos LookAheadCm (cm), de la más cercana a la más lejana. */
	TArray<double> BoxesAhead;
	FTNRallyTabletAmmo Ammo;
	FLinearColor MyColor = FLinearColor::White;
	/** Arco del buggy propio en la pista local (cm de la spline) y en el eje de las notas. */
	double TrackArcCm = 0.0;
	double MyArcCm = 0.0;
	bool bHasArc = false;
	/** Registro de impactos, la línea más nueva primero. */
	TArray<TNRallyHitLog::FLine> HitLog;
	/** Reloj para los parpadeos (s). */
	float Clock = 0.f;
};
