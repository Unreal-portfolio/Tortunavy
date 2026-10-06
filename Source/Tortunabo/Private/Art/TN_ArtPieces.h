#pragma once

#include "CoreMinimal.h"
#include "Art/TN_Art.h"
#include "../World/ProcMap/TN_ProcMapMeshKit.h"

class AActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;

/**
 * Piezas sustituibles dentro de una malla combinada (Docs/Arte_Assets.md, tipo «Pieza»): el castillo, el valle o las
 * estructuras del mapa procedural meten muchas piezas en unos pocos buffers que suben como
 * secciones de un UProceduralMeshComponent. Mientras se construyen, cada pieza se marca con un FPieceScope (nombre, pivote
 * y los buffers en los que escribe); al subir la sección, UploadSection quita las piezas que tienen sustituto y
 * SpawnPieceArt pone su malla de arte en cada pivote, como instancias hijas del componente.
 *
 * Sin sustitutos, UploadSection sube los buffers tal cual, igual que antes, y no se crea nada más.
 *
 * Con sustituto, lo que se ve sale de la sección sin la pieza (sin colisión) y la colisión sale de los buffers enteros en una
 * sección invisible (CollisionSectionOffset más allá): el juego choca exactamente con lo mismo, con o sin arte (#828).
 *
 * Solo para secciones que se construyen una vez (no las que se actualizan cada fotograma con UpdateMeshSection).
 */
namespace TNArt
{
	using FPieceBuffers = TNProcMesh::FTNProcMeshBuffers;

	/** La sección invisible con la colisión de una sección con piezas quitadas es Section + CollisionSectionOffset. */
	constexpr int32 CollisionSectionOffset = 32;

	/** Tramo [V0, V1) de vértices y [T0, T1) de índices de un buffer que pertenece a una pieza. */
	struct FPieceRange
	{
		const FPieceBuffers* Buffer = nullptr;
		int32 V0 = 0;
		int32 V1 = 0;
		int32 T0 = 0;
		int32 T1 = 0;
	};

	/** Una copia de una pieza: su nombre, su pivote (espacio del componente al que se sube) y sus tramos. */
	struct FPiece
	{
		FName Slot;
		FTransform Pivot;
		TArray<FPieceRange, TInlineAllocator<2>> Ranges;
	};

	/**
	 * Registro de las piezas de una construcción. Group distingue las mallas de arte de cada construcción del mismo actor
	 * (al reconstruir se quitan solo las suyas).
	 */
	class FPieceLog
	{
	public:
		explicit FPieceLog(FName InGroup) : Group(InGroup) {}

		/**
		 * Empieza una copia de Slot con su pivote: todo lo que se añada a Buffers hasta End(Handle) es suyo. Las piezas pueden
		 * anidarse (si la de fuera tiene sustituto, la de dentro va con ella).
		 */
		int32 Begin(FName Slot, const FTransform& Pivot, std::initializer_list<const FPieceBuffers*> Buffers);
		void End(int32 Handle);

		FName GetGroup() const { return Group; }
		const TArray<FPiece>& GetPieces() const { return Pieces; }
		void Reset() { Pieces.Reset(); Starts.Reset(); }

		/** Tramos de B de las piezas con sustituto (bForCollision: solo las que chocan con su malla de arte). */
		void CollectRemoved(const FPieceBuffers& B, bool bForCollision, TArray<FPieceRange>& OutRanges) const;

	private:
		struct FStart
		{
			int32 Piece = INDEX_NONE;
			TArray<FPieceRange, TInlineAllocator<2>> At;
		};
		FName Group;
		TArray<FPiece> Pieces;
		TArray<FStart> Starts;
	};

	/** Marca una pieza en un ámbito: FPieceScope Scope(Log, TN_ART("Lobby.Castle.Tower"), Pivot, { &B, &Decor }). Log nulo: nada. */
	class FPieceScope
	{
	public:
		FPieceScope(FPieceLog* InLog, FName Slot, const FTransform& Pivot, std::initializer_list<const FPieceBuffers*> Buffers)
			: Log(InLog), Handle(InLog ? InLog->Begin(Slot, Pivot, Buffers) : INDEX_NONE)
		{
		}
		FPieceScope(FPieceLog& InLog, FName Slot, const FTransform& Pivot, std::initializer_list<const FPieceBuffers*> Buffers)
			: FPieceScope(&InLog, Slot, Pivot, Buffers)
		{
		}
		~FPieceScope()
		{
			if (Log) { Log->End(Handle); }
		}
		FPieceScope(const FPieceScope&) = delete;
		FPieceScope& operator=(const FPieceScope&) = delete;

	private:
		FPieceLog* Log;
		int32 Handle;
	};

	/** Pivote en (X, Y, Z) girado Yaw grados y con escala Scale. */
	inline FTransform PiecePivot(const FVector& Location, double YawDeg = 0.0, const FVector& Scale = FVector::OneVector)
	{
		return FTransform(FRotator(0.0, YawDeg, 0.0), Location, Scale);
	}

	/**
	 * Pura: copia de In sin los triángulos de los tramos Removed (los de otro buffer se ignoran) ni los vértices que solo usan
	 * ellos. Sin tramos, Out == In.
	 */
	void FilterBuffers(const FPieceBuffers& In, TArrayView<const FPieceRange> Removed, FPieceBuffers& Out);

	/**
	 * Sube B como sección Section de Comp (CreateMeshSection_LinearColor, con colisión si bCollision) y le pone Mat si no es
	 * nulo, quitando las piezas de Log que tienen sustituto. Log nulo o sin sustitutos en B: lo mismo que antes.
	 */
	void UploadSection(UProceduralMeshComponent* Comp, int32 Section, const FPieceBuffers& B, bool bCollision, UMaterialInterface* Mat,
		const FPieceLog* Log, bool bSRGBConversion = false);

	/**
	 * Quita las mallas de arte que puso antes el grupo de Log en el actor de AttachTo y pone, hija de AttachTo, una por cada
	 * pieza con sustituto (una instancia por copia, en Adjust * Pivot). Las copias anidadas dentro de otra con sustituto no
	 * se ponen. No chocan: la colisión es la generada (#828). Anota todas las piezas del
	 * registro (TN.Art.Slots). Llamar siempre, después de subir todo, en el hilo de juego.
	 */
	void SpawnPieceArt(USceneComponent* AttachTo, const FPieceLog& Log);

	/** Quita las mallas de arte del grupo Group del actor (por ejemplo, al vaciar lo construido). */
	void ClearPieceArt(AActor* Owner, FName Group);
}
