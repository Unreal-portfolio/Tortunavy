#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTN_ArtCatalog;
struct FTNArtOverride;

/**
 * Nombre de una pieza de arte («Zona.Parte[.Parte...]», p. ej. TN_ART("Lobby.Castle.Tower")). Todo nombre que use el código
 * se escribe así: el test Tortunabo.Art.SlotsInCode lo busca en Source/ y exige que esté en la tabla de piezas
 * (Private/Art/TN_ArtSlots_*.inl), y que toda pieza de la tabla salga en el código.
 */
#define TN_ART(Name) FName(TEXT(Name))

/**
 * Sustitución de las mallas generadas desde C++ por las de Arte (Docs/Arte_Assets.md).
 *
 * Cada pieza visible que el código genera tiene un nombre estable. Si algún catálogo de UTN_ArtSettings le da una malla, se
 * dibuja esa (con sus materiales y su ajuste); si no, la generada, exactamente como antes. Lo funcional no cambia: la
 * colisión generada se queda (invisible) salvo que la pieza pida bUseArtCollision, y los activadores, volúmenes, animaciones
 * por código e interacción siguen en sus componentes. Es solo visual y cada máquina lo resuelve igual (los catálogos se
 * cocinan), así que no hay nada que replicar.
 *
 * Tres formas, según cómo se genera la pieza:
 * - Componente suelto (SetMesh/ApplyToComponent): el componente deja de dibujarse pero conserva su malla, su colisión, su
 *   transformación y su visibilidad lógica; una hija UTN_ArtMeshComponent dibuja la de arte con el ajuste y se mueve, se
 *   esconde y se enseña con él.
 * - Instancias (ApplyToInstances/UpdateInstances): un ISM/HISM cambia de malla y se quedan sus instancias; el ajuste se
 *   aplica a cada una.
 * - Pieza de una malla combinada (Private/Art/TN_ArtPieces.h): se quita de la sección y se pone la de arte en su sitio.
 *
 * La carga de los catálogos y las mallas es síncrona y solo ocurre al montar el nivel (la primera vez que se pide una pieza);
 * queda en caché mientras dura ese mundo. Cada mundo de juego o del editor que empieza (partida, PIE, viaje, abrir un nivel)
 * vuelve a leer los catálogos, así que se ve lo que tengan en ese momento aunque el cambio no haya pasado por el panel de
 * detalles (Python, un asset recargado de git). Editar un catálogo, su botón «Aplicar cambios» o TN.Art.Reload además rehacen
 * en el editor lo que se ve sin jugar (NotifyCatalogsChanged). Consola: TN.Art.Slots, TN.Art.Reload, TN.Art.Enabled.
 */
namespace TNArt
{
	/** Sustituto de una pieza, ya cargado. */
	struct FResolved
	{
		TObjectPtr<UStaticMesh> Mesh = nullptr;
		/** Por ranura de la malla; nulo = el de la malla. */
		TArray<TObjectPtr<UMaterialInterface>> Materials;
		FTransform Adjust = FTransform::Identity;
		bool bUseArtCollision = false;
		/** Hueso o socket de la tortuga (piezas Turtle.*; NAME_None = el de la tabla). */
		FName Bone;
	};

	/**
	 * «Zona.Parte[.Parte...]»: zona Lobby, ProcMap, Beach o Turtle (piezas pegadas a la tortuga, TNTurtleArt); cada parte
	 * empieza por mayúscula y solo lleva letras y cifras.
	 */
	TORTUNABO_API bool IsValidSlotName(const FString& Name);

	/** Zona de un nombre («Lobby» de «Lobby.Castle.Tower»). */
	TORTUNABO_API FString ZoneOf(const FString& Name);

	/**
	 * Pura: la entrada de Slot en el primer catálogo que la tenga con malla (los nulos se saltan), o nullptr. Una entrada sin
	 * malla no cuenta.
	 */
	TORTUNABO_API const FTNArtOverride* FindIn(TArrayView<const UTN_ArtCatalog* const> Catalogs, FName Slot);

	/**
	 * Sustituto de la pieza (cargado y en caché) o nullptr: sin catálogo, sin entrada o sin malla, malla que no carga
	 * (Warning una vez) o TN.Art.Enabled 0. Solo en el hilo de juego.
	 */
	TORTUNABO_API const FResolved* Find(FName Slot);

	/** Malla que se dibuja para la pieza: la de arte si tiene sustituto; si no, Generated. Anota la pieza. */
	TORTUNABO_API UStaticMesh* Resolve(FName Slot, UStaticMesh* Generated);

	/** Anota que la pieza existe y lo que dibuja ahora (TN.Art.Slots). Las funciones de abajo ya lo hacen. */
	TORTUNABO_API void NoteSlot(FName Slot, const UObject* Current = nullptr);

	/**
	 * Componente suelto: le pone la malla generada (como SetStaticMesh) y aplica el sustituto de Slot (ApplyToComponent).
	 * Con Generated nulo la pieza no existe y no se pone arte.
	 */
	TORTUNABO_API void SetMesh(UStaticMeshComponent* Comp, UStaticMesh* Generated, FName Slot);

	/**
	 * Componente suelto, con su malla ya puesta. Con sustituto, el componente deja de dibujarse (conserva malla, colisión,
	 * transformación y visibilidad lógica) y una hija UTN_ArtMeshComponent dibuja la malla de arte con el ajuste; con
	 * bUseArtCollision, la colisión pasa a la hija. Sin sustituto, no toca nada (y deshace lo de una construcción anterior).
	 * Solo en mundos de juego o con componentes transitorios: un componente guardado con el nivel no se modifica en el editor.
	 */
	TORTUNABO_API void ApplyToComponent(UStaticMeshComponent* Comp, FName Slot);

	/**
	 * ISM/HISM con sus instancias ya puestas: con sustituto cambia la malla y los materiales, aplica el ajuste a cada
	 * instancia y, si el componente choca y la pieza no trae colisión propia, deja un gemelo invisible con la malla generada
	 * que conserva la colisión (solo para instancias que no se mueven). Sin sustituto, nada.
	 */
	TORTUNABO_API void ApplyToInstances(UInstancedStaticMeshComponent* ISM, FName Slot);

	/**
	 * BatchUpdateInstancesTransforms para instancias animadas por código: con el ajuste del sustituto de ese ISM, si lo
	 * tiene (ApplyToInstances). Sin sustituto es la llamada de siempre.
	 */
	TORTUNABO_API bool UpdateInstances(UInstancedStaticMeshComponent* ISM, int32 StartInstanceIndex, TArrayView<const FTransform> Transforms,
		bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false);

	/**
	 * Se pueden cambiar sus mallas: mundo de juego, o componente (o actor) que no se guarda con el nivel. Las funciones de
	 * arriba no hacen nada con los demás (en el editor, lo que se guarda con el nivel se queda generado).
	 */
	TORTUNABO_API bool CanModify(const UActorComponent* Comp);

	/** Es el gemelo invisible con la colisión generada que deja ApplyToInstances (para quien recicla sus ISM). */
	TORTUNABO_API bool IsCollisionTwin(const UActorComponent* Comp);

	/** Vacía la caché de catálogos y mallas: la próxima pieza que se pida los vuelve a leer (lo hace cada mundo que empieza). */
	TORTUNABO_API void InvalidateCache();

	/**
	 * Los catálogos han cambiado (editar uno o los ajustes, «Aplicar cambios», TN.Art.Reload): vacía la caché, sube
	 * GetCatalogVersion y, en el editor, rehace en el siguiente fotograma los actores del nivel abierto que dibujan piezas
	 * sustituibles sin jugar (castillo, valle...), para que se vea sin darle al Play.
	 */
	TORTUNABO_API void NotifyCatalogsChanged();

	/** Sube con cada NotifyCatalogsChanged: quien solo se reconstruye si cambia algo lo mete en su clave (el valle del lobby). */
	TORTUNABO_API uint32 GetCatalogVersion();

	/**
	 * Pura: piezas de Catalog con malla cuyo nombre no está en la tabla de piezas (nombre mal escrito o pieza que el código ya
	 * no genera): no cambian nada. Se avisan con un Warning al leer los catálogos y al darle a «Aplicar cambios».
	 */
	TORTUNABO_API TArray<FName> FindUnknownPieces(const UTN_ArtCatalog* Catalog);

	/** Para los tests: usa estos catálogos en lugar de los de los ajustes. Vacío: vuelve a los de los ajustes. */
	TORTUNABO_API void SetCatalogsForTest(const TArray<UTN_ArtCatalog*>& Catalogs);

	/** Piezas anotadas en esta sesión (nombre → veces), para los tests. */
	TORTUNABO_API TMap<FName, int32> GetNotedSlots();
}
