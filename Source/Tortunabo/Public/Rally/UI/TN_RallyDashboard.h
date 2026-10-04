// Interfaz diegética de la conductora del Rally (#299): la conductora no tiene HUD de pantalla salvo los avisos (semáforo,
// contramano, reaparición y resultados); la velocidad, el turbo y la vida van en el salpicadero del buggy, y el puesto y la
// vuelta en un cartel del arco antivuelco; encima del salpicadero, la placa de la nota cantada (#331). Son widgets en el
// mundo (UWidgetComponent) que solo existen en la máquina de la conductora (no se replican; a la artillera le taparían la
// vista): los pone ATN_RallyPlayerController en su buggy. Medidas pensadas para leerse a 1080p con la cámara de persecución (TNRallyDashboard::ProjectedGlyphPx, tests Tortunabo.Rally.Dashboard.*).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SceneComponent.h"
#include "TN_RallyDashboard.generated.h"

class APlayerController;
class ATN_Buggy;
class UBorder;
class UProgressBar;
class UTextBlock;
class UWidget;
class UWidgetComponent;
struct FTNRallyStanding;

/** Qué panel del buggy pinta un UTN_RallyDashboardWidget. */
UENUM()
enum class ETNRallyDashboardPanel : uint8
{
	/** Salpicadero: velocidad, turbo y vida del buggy. */
	Dash,
	/** Cartel del arco antivuelco: puesto y vuelta (o puerta, o el tiempo de meta). */
	RollBar,
	/** Placa de la nota cantada (#331): sobre el salpicadero, solo mientras dura el canto. */
	Call
};

namespace TNRallyDashboard
{
	/** Medidas de un panel en el buggy, relativas al asiento de la conductora (ATN_Buggy::DriverSeatLocal). */
	struct FPanelLayout
	{
		FVector OffsetFromDriverSeat = FVector::ZeroVector;
		/** Yaw 180: el panel mira hacia atrás (hacia la cámara de persecución y la conductora); el pitch lo inclina hacia arriba. */
		FRotator Rotation = FRotator(0.0, 180.0, 0.0);
		FIntPoint DrawSizePx = FIntPoint(480, 240);
		/** Centímetros del mundo por píxel del widget. */
		float CmPerPx = 0.25f;
		/** Tamaño de la fuente del dato principal (velocidad o puesto), en píxeles del widget. */
		int32 MainFontPx = 120;
	};

	TORTUNABO_API FPanelLayout DashLayout();
	TORTUNABO_API FPanelLayout RollBarLayout();
	TORTUNABO_API FPanelLayout CallLayout();

	/** Alto de las mayúsculas y las cifras respecto al tamaño de la fuente (Roboto: ~0,71). */
	inline constexpr float CapHeightRatio = 0.71f;

	/** Alto en el mundo (cm) de las cifras del dato principal de un panel. */
	TORTUNABO_API float MainGlyphCm(const FPanelLayout& Layout);

	/**
	 * Alto en pantalla (px) de un objeto de GlyphCm a DistanceCm de la cámara, con el FOV horizontal dado y una pantalla de
	 * ScreenSize (relación de aspecto incluida).
	 */
	TORTUNABO_API float ProjectedGlyphPx(float GlyphCm, float DistanceCm, float HorizontalFovDeg, const FIntPoint& ScreenSize);

	/** Línea de vuelta del cartel: «¡Meta! 1:52.3», «Vuelta 2/3 · Puerta 4/9» o «Puerta 4/9». */
	TORTUNABO_API FText LapLine(const FTNRallyStanding& Mine, int32 Laps, int32 NumGates, bool bCircuit);

	/** «2.º / 6». */
	TORTUNABO_API FText PlaceLine(int32 Place, int32 Total);

	/** Tiempo de carrera «m:ss.d». */
	TORTUNABO_API FText RaceTime(float Seconds);

	/**
	 * Si los paneles 3D se pintan enmascarados (#605; consola TN.Rally.PanelMasked, 1 por defecto). Translúcidos no escriben
	 * velocidad: el suavizado temporal (TSR) y el desenfoque de movimiento les ponían la del fondo y dejaban estela fantasma
	 * con el buggy en marcha. Enmascarados se pintan en el pase base, con su velocidad y su profundidad, y se leen nítidos.
	 */
	TORTUNABO_API bool UseMaskedPanels();
}

/** Un panel del buggy (C++ sin asset UMG): lee el buggy y su fila de puestos (estado replicado). */
UCLASS()
class TORTUNABO_API UTN_RallyDashboardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(ATN_Buggy* InBuggy, ETNRallyDashboardPanel InPanel);

	/** Solo en la placa (Call): enseña Headline en grande y Detail debajo durante Seconds, con el borde en Accent. */
	void ShowCall(const FText& Headline, const FText& Detail, const FLinearColor& Accent, float Seconds);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildDash();
	void BuildRollBar();
	void BuildCall();
	void RefreshDash(const ATN_Buggy& Buggy);
	void RefreshRollBar(const ATN_Buggy& Buggy);
	void TickCall(float DeltaTime);

	TWeakObjectPtr<ATN_Buggy> Buggy;
	ETNRallyDashboardPanel Panel = ETNRallyDashboardPanel::Dash;
	float RefreshAccumulator = 1.f;
	/** Lo que le queda a la placa en pantalla (s). */
	float CallRemaining = 0.f;

	/** Contenido de la placa: se pliega cuando no hay canto (el widget sigue vivo para seguir contando). */
	UPROPERTY(Transient) TObjectPtr<UWidget> CallContent;
	UPROPERTY(Transient) TObjectPtr<UBorder> CallBack;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> MainText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubText;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> BoostBar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> HealthBar;
};

/** Los dos paneles en el buggy local. Solo en la máquina de las ocupantes: nunca en el servidor dedicado ni en los demás. */
UCLASS(Transient)
class TORTUNABO_API UTN_RallyDashboardComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTN_RallyDashboardComponent();

	/**
	 * Pone los paneles en Buggy (si no los tiene ya) para el jugador local Player. Nullptr si Player no es local, la
	 * máquina no pinta o falta algo.
	 */
	static UTN_RallyDashboardComponent* AttachTo(ATN_Buggy* Buggy, APlayerController* Player);

	/** Quita los paneles de Buggy en esta máquina (la conductora pasa a artillera). */
	static void RemoveFrom(ATN_Buggy* Buggy);

	/** Los paneles de este buggy en esta máquina (nullptr si no tiene). */
	static UTN_RallyDashboardComponent* FindOn(const ATN_Buggy* Buggy);

	/** Placa de la nota cantada sobre el salpicadero (#331), durante Seconds. */
	void ShowCall(const FText& Headline, const FText& Detail, const FLinearColor& Accent, float Seconds);

protected:
	virtual void OnUnregister() override;

private:
	UWidgetComponent* MakePanel(ATN_Buggy& Buggy, APlayerController& Player, ETNRallyDashboardPanel Kind,
		const TNRallyDashboard::FPanelLayout& Layout);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidgetComponent>> Panels;

	UPROPERTY(Transient)
	TObjectPtr<UTN_RallyDashboardWidget> CallWidget;
};
