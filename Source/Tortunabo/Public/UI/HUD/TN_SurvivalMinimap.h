// Minimapa de Supervivencia: el de la conductora del Rally (UTN_RallyCopilotTablet en su vista compacta: funda de goma
// coral, pantalla azul marino, el camino en planta con la salida, la meta y los tramos con agua, y las tortugas como
// caparazones de su color) con la tormenta del camino (ATN_PathStorm) encima: el tramo que ya se ha tragado, su frente
// atravesado en el camino y la nube con rayo detrás del frente.
// Lo pone UTN_RunHUDWidget en todos los modos de LVL_Run, pero solo se dibuja con un mapa de Supervivencia generado
// (ATN_ProcMapGenerator). Lee el camino del generador y el frente replicado de la tormenta, así que vale igual en el
// servidor y en los clientes. Todo en C++ sin asset UMG, con las piezas de dibujo de la tableta.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "TN_SurvivalMinimap.generated.h"

class ATN_ProcMapGenerator;
class ATN_PathStorm;
struct FTNRallyTabletPainter;
struct FTNRallyTabletMapProjection;

/** Una tortuga en el minimapa (copia de lo que hace falta para pintar; sin punteros a actores). */
struct FTNSurvivalMinimapMark
{
	FVector Location = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	/** La tuya (o la que sigues de fantasma): encima de las demás y con el aro de oro. */
	bool bMine = false;
	/** Ya en la meta o eliminada: se pinta apagada. */
	bool bOut = false;
};

UCLASS()
class TORTUNABO_API UTN_SurvivalMinimap : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Lado de la maqueta (unidades de diseño): se escala para caber en el hueco que le da el HUD. */
	static constexpr float DesignSide = 360.f;

	/** Tortugas que se pintan; las pasa el HUD cada fotograma con los colores de la tripulación. */
	void SetMarks(TArray<FTNSurvivalMinimapMark>&& InMarks) { Marks = MoveTemp(InMarks); }

	/** Hay un mapa de Supervivencia generado y se dibuja. */
	bool HasMap() const { return Path.Num() > 1; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** Rehace el camino si el generador tiene un mapa nuevo (cada nivel de Supervivencia genera el suyo). */
	void RefreshPath();
	void RefreshStorm();

	void PaintPath(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection) const;
	void PaintStorm(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection) const;
	void PaintStormCloud(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection, const FBox2D& Area) const;
	/** Dirección del camino en el frente, ya en el panel. */
	FVector2f FrontDirectionOnPanel(const FTNRallyTabletMapProjection& Projection) const;
	void PaintMarks(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection, const FBox2D& Area) const;

	TWeakObjectPtr<ATN_ProcMapGenerator> Generator;
	TWeakObjectPtr<ATN_PathStorm> Storm;
	float LookupTimer = 0.f;

	/** Generación y largo del camino con que se hizo Path (para rehacerlo con el mapa del nivel siguiente). */
	int32 PathGeneration = -1;
	float PathLength = -1.f;
	/** Muestras del camino principal, de la salida a la playa final, su progreso (cm) y cuáles quedan bajo el mar. */
	TArray<FVector> Path;
	TArray<float> PathProgress;
	TArray<bool> PathWet;
	/** Caja en planta del camino (cm) para escalar el mapa. */
	FBox2D MapBounds = FBox2D(ForceInit);

	TArray<FTNSurvivalMinimapMark> Marks;

	/** La tormenta está en marcha, su frente (cm del camino) y dónde queda en el mundo con la dirección del camino. */
	bool bHasStorm = false;
	float StormFront = 0.f;
	FVector StormFrontLocation = FVector::ZeroVector;
	FVector StormFrontDirection = FVector::ForwardVector;

	/** Nube de tormenta (TNHUDArt::StormIcon) que marca el frente. */
	FSlateBrush StormCloudBrush;
	/** Reloj para los latidos (s). */
	float Clock = 0.f;
};
