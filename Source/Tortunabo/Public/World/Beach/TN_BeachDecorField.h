#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Beach/TN_BeachLayout.h"
#include "TN_BeachDecorField.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Huella de una pieza del decorado para rebuscarla: la caja de su malla fija, en planta (cápsula a lo largo del lado largo). */
struct FTNBeachDecorShape
{
	/** Centro en el suelo (mundo, a la cota del origen del elemento) y eje del lado largo (mundo, en planta). */
	FVector Center = FVector::ZeroVector;
	FVector Axis = FVector::ForwardVector;
	/** Radio, semilargo del eje de la cápsula (0 = redonda) y alto aproximado (cm). */
	float Radius = 100.f;
	float HalfLength = 0.f;
	float Height = 150.f;
};

/** Recuento del decorado de esta máquina (TN.Beach.Perf). */
struct FTNBeachDecorStats
{
	/** Piezas del reparto montadas (sin las quitadas) y quitadas (ClearElementsAround). */
	int32 Items = 0;
	int32 CutItems = 0;
	/** Instancias de malla fija, de la parte que se mueve y de las copias que solo dan sombra. */
	int32 BodyInstances = 0;
	int32 MovingInstances = 0;
	int32 ShadowTwinInstances = 0;
	/** De las fijas: con colisión y con sombra siempre (lo grande). */
	int32 CollisionInstances = 0;
	int32 ShadowInstances = 0;
	/** Componentes instanciados en uso y partes animadas que se mueven ahora (cerca de una cámara local). */
	int32 Components = 0;
	int32 Animators = 0;
	/** Tiempo de CPU del último montaje (ms) y en cuántos fotogramas se hizo. */
	double BuildMs = 0.0;
	int32 BuildFrames = 0;
	int32 Round = 0;
};

namespace TNBeachDecorFieldTypes
{
	/** Por dónde va el montaje. */
	enum class EStage : uint8
	{
		Idle,
		/** Recorre el reparto: receta de cada pieza (montarla cuesta la primera vez) y su sitio, por lotes. */
		Collect,
		/** Un componente instanciado por lote (malla fija, parte que se mueve y copia de sombra). */
		Components,
		Done
	};

	/** Una pieza del decorado del reparto. */
	struct FItem
	{
		int32 LayoutIndex = INDEX_NONE;
		ETNBeachElement Element = ETNBeachElement::Coconut;
		int32 Seed = 0;
		float Size = 1.f;
		/** Huella en planta (espacio del generador): cápsula de Pos a lo largo de Axis. */
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Axis = FVector2D(1.0, 0.0);
		double Radius = 0.0;
		double HalfLength = 0.0;
		/** Origen del elemento (en la arena) y malla fija, en el espacio del campo (el del generador). */
		FTransform ItemXf;
		FTransform BodyXf;
		/** Lote de la malla fija (INDEX_NONE en los tramos, que tienen muchas piezas). */
		int32 BodyBatch = INDEX_NONE;
		/** Parte que se mueve: su lote, su instancia y su pose quieta (la del instante 0). */
		int32 MovingBatch = INDEX_NONE;
		int32 MovingInstance = INDEX_NONE;
		FTransform MovingRestXf;
		/** Animación (índice en AnimRecipes) y distancia a una cámara hasta la que se mueve. */
		int32 AnimRecipe = INDEX_NONE;
		float AnimRange = 0.f;
		bool bTiled = false;
		bool bCut = false;
	};

	/** Todas las instancias de una malla (una receta, o su parte que se mueve) con la misma forma de dibujarse. */
	struct FBatch
	{
		uint32 Key = 0;
		ETNBeachElement Element = ETNBeachElement::Coconut;
		UStaticMesh* Mesh = nullptr;
		bool bMoving = false;
		bool bCollision = false;
		bool bBlocksCamera = false;
		/** La receta da sombra; si además es grande, siempre (si no, con su copia de sombra, solo cerca). */
		bool bRecipeShadow = false;
		float MaxSize = 0.f;
		TArray<FTransform> Transforms;
		/** Pieza (índice en Items) de cada instancia. */
		TArray<int32> Owners;
		/** Componentes (índices en Comps; INDEX_NONE si no tiene): el que se ve y el que solo da sombra de cerca. */
		int32 Comp = INDEX_NONE;
		int32 TwinComp = INDEX_NONE;
	};

	/** Cómo se mueve la parte animada de una receta (copia de su TNBeachProp::FPropInfo, que es de un archivo privado). */
	struct FAnimRecipe
	{
		uint8 Anim = 0;
		FVector Pivot = FVector::ZeroVector;
		FVector Axis = FVector(0.0, 1.0, 0.0);
		float Amp = 0.f;
		float Rate = 0.f;
		bool bCastShadow = true;
		UStaticMesh* Mesh = nullptr;
	};

	/** Parte animada que se mueve ahora (componente de la reserva, cerca de una cámara local). */
	struct FAnimator
	{
		int32 Item = INDEX_NONE;
		int32 PoolIndex = INDEX_NONE;
		float Time = 0.f;
		int32 ClamCycle = -1;
		bool bClamHeld = false;
	};
}

/**
 * Decorado de la ronda de la playa, local en cada máquina (Docs/Modo_Carrera.md, «Rendimiento y red»). El reparto es
 * determinista con la semilla, así que el servidor y cada cliente montan aquí el mismo decorado sin replicar nada: las
 * piezas de la categoría Decor (cocos, rocas, castillos, sacos...) agrupadas en mallas instanciadas jerárquicas (HISM)
 * por elemento y variante, con las recetas de siempre (TNBeachDecorKit: TN_BeachPropMeshes.h y
 * TN_BeachMilitaryMeshes.h) y la misma colocación que ATN_BeachDecor.
 *
 * - Colisión por instancia: la de la receta (cajas, esferas y cápsulas en el BodySetup de la malla compartida), igual que
 *   con un actor por pieza: se sube, se cubre y se mete debajo igual. La cámara solo choca con lo grande y macizo.
 * - Sombra: lo grande (10 m de huella o más), siempre; lo demás, con una copia que solo da sombra (no se dibuja en el
 *   pase principal) y deja de darla a ShadowNearDistance de la cámara. Menos páginas de sombra virtual que marcar.
 * - Lo pequeño deja de dibujarse a 60 veces su huella (de 120 a 600 m), como antes; lo de más de 10 m, nunca.
 * - Lo que se mueve (medusa, almeja, vela, red, banderas): en lejos, quieto dentro de su lote; cerca de una cámara local
 *   (60-200 m según el tamaño, como mucho MaxAnimators a la vez), un componente de la reserva lo mueve y su instancia
 *   quieta se esconde. Ni en el servidor dedicado ni en el editor.
 * - Se monta por partes (StepBuild con un presupuesto de tiempo por fotograma): ATN_BeachRaceGenerator espera a que esté
 *   entero para dar la ronda por lista.
 * - CutCircle quita (en esta máquina) lo que toca un círculo: el generador lo replica para el nido del sprint.
 *
 * Lo crea y lo destruye el generador (sin replicar; RF_Transient); va pegado a él, en su espacio.
 */
UCLASS(NotPlaceable, Transient)
class TORTUNABO_API ATN_BeachDecorField : public AActor
{
	GENERATED_BODY()

public:
	ATN_BeachDecorField();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Empieza a montar el decorado de este reparto (quita antes el que hubiera). */
	void BeginBuild(const TNBeachLayout::FRoundLayout& Layout, int32 Round);

	/**
	 * Lo mismo con las piezas ya colocadas: InPlacements[i] es el origen de InItems[i] en el espacio del campo, con su cota
	 * resuelta fuera (los mapas de terreno fijo, #652, que no son la playa de la carrera y no tienen su SandZ). Lo que no
	 * es de la categoría Decor se ignora. Los índices de InItems hacen de índices del reparto (HasItem, CutCircle...).
	 */
	void BeginBuildPlaced(const TArray<TNBeachLayout::FItem>& InItems, const TArray<FTransform>& InPlacements, int32 Round);

	/** Sigue montando hasta gastar BudgetSeconds (al menos un paso); true cuando está todo. */
	bool StepBuild(double BudgetSeconds);

	/** Quita todo el decorado (playa vacía). */
	void ClearDecor();

	bool IsBuilding() const { return Stage == TNBeachDecorFieldTypes::EStage::Collect || Stage == TNBeachDecorFieldTypes::EStage::Components; }
	bool IsBuiltFor(int32 Round) const { return Stage == TNBeachDecorFieldTypes::EStage::Done && BuiltRound == Round; }
	int32 GetBuiltRound() const { return BuiltRound; }

	/**
	 * Quita en esta máquina el decorado cuya huella toca el círculo (centro en el espacio del generador, radio en cm) y
	 * devuelve cuántas piezas. Rehace solo los lotes tocados.
	 */
	int32 CutCircle(const FVector2D& LocalCenter, double Radius);

	/** Si la pieza ItemIndex del reparto es decorado montado aquí (y no se ha quitado). */
	bool HasItem(int32 ItemIndex) const;

	/**
	 * Huella para rebuscar la pieza ItemIndex del reparto: la caja de su malla fija girada, inclinada y escalada como ese
	 * ejemplar (cápsula a lo largo del lado largo); la sombrilla, el montón de arena de su pie. False en los tramos y en
	 * lo que no está.
	 */
	bool GetSearchShape(int32 ItemIndex, FTNBeachDecorShape& Out) const;

	/** Caja (mundo) de la malla fija de la pieza ItemIndex del reparto (false si no está o es un tramo). */
	bool GetItemBounds(int32 ItemIndex, FBox& OutWorldBox) const;

	FTNBeachDecorStats GetStats() const;

	/** Lo grande (radio de huella con su tamaño, cm) da sombra siempre; lo demás, solo cerca. */
	static constexpr double ShadowBigRadius = 1000.0;

	/** Distancia a la cámara (cm) hasta la que da sombra el decorado pequeño y mediano. */
	static constexpr float ShadowNearDistance = 9000.f;

	/** Partes animadas que se mueven a la vez como mucho (las más cercanas a una cámara local). */
	static constexpr int32 MaxAnimators = 40;

protected:
	/** Raíz (en el espacio del generador). */
	UPROPERTY(VisibleAnywhere, Category = "Beach")
	TObjectPtr<USceneComponent> FieldRoot;

private:
	/** Componentes instanciados de los lotes (se reutilizan de una ronda a otra por clave). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Comps;

	/** Reserva de componentes que mueven la parte animada de lo que está cerca de una cámara. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AnimatorPool;

	/** Clave de cada componente (lote y tipo: fijo, que se mueve o copia de sombra), para reutilizarlo. */
	TMap<uint64, int32> CompByKey;

	TArray<TNBeachDecorFieldTypes::FItem> Items;
	TArray<TNBeachDecorFieldTypes::FBatch> Batches;
	TMap<uint32, int32> BatchByKey;
	/** Pieza (índice en Items) de cada elemento del reparto (INDEX_NONE si no es decorado). */
	TArray<int32> ItemOfLayout;
	/** Recetas con animación (una por lote de parte que se mueve). */
	TArray<TNBeachDecorFieldTypes::FAnimRecipe> AnimRecipes;
	TMap<uint32, int32> AnimRecipeByKey;
	TArray<int32> AnimItems;
	/** Huecos para mover partes animadas (el hueco i usa AnimatorPool[i]; libre si su Item es INDEX_NONE). */
	TArray<TNBeachDecorFieldTypes::FAnimator> Animators;

	/** El reparto que se monta (copia de sus piezas de decorado: el del generador puede cambiar mientras). */
	TArray<TNBeachLayout::FItem> PendingItems;
	TArray<int32> PendingLayoutIndex;
	/** Dónde va cada una (ItemPlacement, con la ronda a mano). */
	TArray<FTransform> PendingXf;
	int32 NextPending = 0;
	int32 NextBatch = 0;
	int32 PendingRound = 0;
	int32 BuiltRound = 0;
	TNBeachDecorFieldTypes::EStage Stage = TNBeachDecorFieldTypes::EStage::Idle;
	double BuildSeconds = 0.0;
	int32 BuildFrames = 0;
	float AnimCheckClock = 0.f;
	bool bVisuals = true;

	/** Crea (o reutiliza) el componente de un lote: Kind 0 = fijo, 1 = parte que se mueve, 2 = copia de sombra. */
	int32 AcquireComp(uint32 BatchKey, uint8 Kind);
	void SetupBatchComps(int32 BatchIndex);
	/** Vuelve a poner las instancias de un lote sin las piezas quitadas. */
	void RefillBatch(int32 BatchIndex);
	void AddItem(const TNBeachLayout::FItem& Item, int32 LayoutIndex, const FTransform& ItemXf);
	/** Prepara el montaje de las piezas pendientes ya apuntadas (PendingItems, PendingXf). */
	void StartPendingBuild(int32 Round);
	int32 BatchFor(uint32 Key, ETNBeachElement Element, UStaticMesh* Mesh, bool bMoving, bool bCollision, bool bBlocksCamera, bool bCastShadow);
	void FinishBuild();

	// ── Partes animadas (máquinas con pantalla) ──
	void UpdateAnimators();
	void StartAnimator(int32 ItemIndex);
	void StopAnimator(int32 AnimatorIndex);
	void StopAllAnimators();
	void PoseAnimator(int32 Slot, float DeltaSeconds);
	bool IsSomeoneOnTop(const TNBeachDecorFieldTypes::FItem& Item) const;
};
