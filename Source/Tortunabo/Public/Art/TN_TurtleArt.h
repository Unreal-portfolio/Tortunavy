#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "TN_TurtleArt.generated.h"

class UAnimSequence;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkinnedAsset;

/** Animaciones de la tortuga que se configuran en UTN_ArtSettings («Tortuga|Animaciones»). */
enum class ETNTurtleClip : uint8
{
	Idle,
	Walk,
	Cheer,
	Salute,
};

/**
 * La tortuga de Arte (Docs/Arte_Assets.md, «La tortuga»): una sola fuente para el cuerpo y piezas sueltas pegadas a sus huesos.
 *
 * - Cuerpo: el componente Mesh del personaje de la tortuga (UTN_ArtSettings::TurtleCharacter, BP_TortugaCharacter) es la
 *   única fuente. Las copias que no son el personaje (tendero, general, escaparate de cosméticos, podio) toman de ahí la
 *   malla, sus materiales y la diferencia de transformación respecto a la de demo (ApplyBody): cambiar la malla del
 *   Blueprint cambia todas las tortugas del juego. Sus animaciones salen de los ajustes (GetClip), no de rutas en el código.
 * - Piezas: mallas estáticas del catálogo de arte (Turtle.Shell, Turtle.Helmet, Turtle.Eyes, Turtle.Tongue) pegadas a un
 *   hueso de la malla (ApplyPieces, desde UTN_CosmeticLook::ApplyLook, que viste a todas las tortugas). Se exportan en el
 *   espacio del cuerpo, siguen la animación y el ragdoll y se esconden con él. Son visuales y locales: cada máquina las
 *   pone igual a partir de los catálogos (que se cocinan) y del aspecto ya replicado, sin red.
 */
namespace TNTurtleArt
{
	/** Pieza de la tortuga: su nombre en los catálogos y el hueso que sigue si el catálogo no dice otro. */
	struct FPieceInfo
	{
		FName Slot;
		FName DefaultBone;
	};

	/** Las piezas que se pueden pegar a la tortuga (las Turtle.* de la tabla de piezas, Private/Art/TN_ArtSlots_Turtle.inl). */
	TORTUNABO_API TArrayView<const FPieceInfo> GetPieces();

	/** Turtle.Helmet: el casco de serie de Arte (se esconde con un casco de la tienda y quita el pintado de la de demo). */
	TORTUNABO_API FName HelmetPiece();

	/** Clase del personaje de la tortuga (la de los ajustes, cargada) o nullptr si no carga (Error en el log, una vez). */
	TORTUNABO_API UClass* GetCharacterClass();

	/** Componente Mesh del objeto por defecto del personaje de la tortuga: la fuente de la malla, sus materiales y su escala. */
	TORTUNABO_API const USkeletalMeshComponent* GetTemplateMesh();

	/** Malla esquelética de la tortuga (la del personaje) o nullptr. */
	TORTUNABO_API USkeletalMesh* GetMesh();

	/**
	 * Transformación de Mesh en BP_TortugaCharacter con la malla de demo: (0, 0, -70), giro -90 y escala 2,5. Las copias se
	 * colocaron a mano con ella; si el Blueprint pone otra (una malla con otro pivote, otra orientación u otra escala), las
	 * copias aplican la misma diferencia (GetCopyCorrection).
	 */
	TORTUNABO_API const FTransform& GetReferenceMeshTransform();

	/**
	 * Pura: lo que se aplica (en el espacio de la malla) a la transformación de demo de una copia para que siga al personaje
	 * con CharacterMesh = transformación de Mesh en su Blueprint. Identidad si coincide con la de referencia.
	 */
	TORTUNABO_API FTransform ComputeCopyCorrection(const FTransform& CharacterMesh);

	/** ComputeCopyCorrection con el Mesh del personaje de la tortuga (identidad si no hay). */
	TORTUNABO_API FTransform GetCopyCorrection();

	/**
	 * Viste una copia con el cuerpo de la tortuga: la malla del personaje (y sus materiales, si el Blueprint los cambia) y la
	 * transformación DemoRelative (la que se le daba con la malla de demo) corregida con GetCopyCorrection. Devuelve true si
	 * ha cambiado la malla: los materiales de partida que guarde quien la llama (UTN_CosmeticLook::ApplyLook) ya no valen.
	 * Sin personaje o sin malla, deja la que tenga.
	 */
	TORTUNABO_API bool ApplyBody(USkeletalMeshComponent* Copy, const FTransform& DemoRelative);

	/** Animación de la tortuga de los ajustes (cargada) o nullptr si no hay. */
	TORTUNABO_API UAnimSequence* GetClip(ETNTurtleClip Clip);

	/**
	 * Es una tortuga (las maquetas de LVL_Lobby que esconden el tendero, el general y el lobby): la malla del personaje, otra
	 * con su mismo esqueleto o la de demo (TotugaDemo_Rig*, las maquetas que aún la llevan).
	 */
	TORTUNABO_API bool IsTurtleMesh(const USkinnedAsset* Asset);

	/**
	 * Pura: transformación relativa al hueso Bone (o socket) de Asset que deja en el espacio de la malla, en la postura de
	 * referencia, una pieza exportada con el cuerpo y movida por Adjust. Sin el hueso, Adjust (relativa al componente).
	 */
	TORTUNABO_API FTransform ComputePieceRelative(const USkinnedAsset* Asset, FName Bone, const FTransform& Adjust);

	/**
	 * Pega a Body las piezas de la tortuga que tengan malla en los catálogos, quita las que ya no la tengan y esconde el casco
	 * de serie (Turtle.Helmet) si bCosmeticHelmet (se lleva uno de la tienda). Solo en mundos de juego o en componentes
	 * transitorios (TNArt::CanModify), y nunca en un servidor dedicado. Las piezas copian las luces, sombras y capturas de Body y se esconden con él. Devuelve
	 * true si Body lleva el casco de serie de Arte (aunque ahora lo tape uno de la tienda).
	 */
	TORTUNABO_API bool ApplyPieces(USkeletalMeshComponent* Body, bool bCosmeticHelmet);

	/** Piezas pegadas a Body (para las capturas que solo dibujan una lista de componentes: escaparate y podio). */
	TORTUNABO_API void GetPieceComponents(const USkeletalMeshComponent* Body, TArray<UPrimitiveComponent*>& Out);

	/** Para los tests: usa este componente como Mesh del personaje (nullptr vuelve al del Blueprint de los ajustes). */
	TORTUNABO_API void SetTemplateMeshForTest(const USkeletalMeshComponent* Template);
}

/**
 * Pieza de arte pegada a un hueso de la tortuga (TNTurtleArt::ApplyPieces). Transitoria: no se guarda ni se replica; cada
 * máquina la pone igual. Se ve cuando se ve su malla y su dueño la ve como ve la malla.
 */
UCLASS(ClassGroup = (Tortunavy), meta = (BlueprintSpawnableComponent = false))
class TORTUNABO_API UTN_TurtlePieceComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UTN_TurtlePieceComponent(const FObjectInitializer& ObjectInitializer);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Copia de la malla a la que va pegada lo que decide si se ve (visibilidad, dueño, sombra oculta). */
	void SyncWithBody();

	/** Pieza que dibuja («Turtle.Shell»). */
	FName Slot;

	/** Escondida aunque se vea la malla (el casco de serie con un casco de la tienda puesto). */
	bool bSuppressed = false;
};
