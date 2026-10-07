#include "World/TN_TctShoreMarks.h"
#include "World/ProcMap/TN_ProcMapMath.h"

namespace TNTctShoreDetail
{
	/** De cada 256 casillas de la banda, cuántas llevan marca (≈ una de cada tres: una marca cada par de metros de orilla). */
	constexpr uint32 MarkChanceByte = 84u;
	/** Lejos de una salida o un punto de objetos (uu). */
	constexpr double KeepOutMargin = 150.0;
	/** Postes: tope y separación mínima entre dos (uu). */
	constexpr int32 MaxPosts = 24;
	constexpr double PostSpacing = 1100.0;
}

TArray<TNTctScenery::FShoreMark> TNTctScenery::PlanShoreMarks(const FHeightField& Field, float TargetZ, const TArray<FKeepOut>& KeepOuts,
	uint32 Seed, int32 MaxMarks)
{
	using namespace TNTctShoreDetail;
	using namespace TNProcMap;
	TArray<FShoreMark> Marks;
	if (!Field.IsValid() || MaxMarks <= 0)
	{
		return Marks;
	}
	struct FKeyed
	{
		uint32 Key = 0;
		FShoreMark Mark;
	};
	TArray<FKeyed> Keyed;
	const float Low = TargetZ - ShoreBandBelow;
	const float High = TargetZ + ShoreBandAbove;
	for (int32 Y = 0; Y < Field.NY; ++Y)
	{
		for (int32 X = 0; X < Field.NX; ++X)
		{
			const float Z = Field.Get(X, Y);
			if (Z <= NoGround * 0.5f || Z < Low - 12.f || Z > High + 12.f)
			{
				continue;
			}
			const uint32 Hash = HashCell(Seed ^ 0x5304Eu, X, Y);
			if ((Hash & 0xFFu) >= MarkChanceByte)
			{
				continue;
			}
			FRng Rng(static_cast<uint64>(Hash));
			const FVector2D P = Field.Origin + FVector2D((X + Rng.Range(-0.45, 0.45)) * Field.Cell, (Y + Rng.Range(-0.45, 0.45)) * Field.Cell);
			float Ground = 0.f;
			if (!Field.HeightAt(P, Ground) || Ground < Low || Ground > High || KeepOutDistance(KeepOuts, P) < KeepOutMargin)
			{
				continue;
			}
			FKeyed& Item = Keyed.AddDefaulted_GetRef();
			Item.Key = Hash32(Hash ^ Seed);
			Item.Mark.Kind = Rng.Unit() < 0.55 ? ShoreKindFoam : ShoreKindAlgae;
			Item.Mark.Variant = static_cast<uint8>(Rng.RangeInt(0, 1));
			Item.Mark.Location = FVector(FMath::RoundToDouble(P.X), FMath::RoundToDouble(P.Y), FMath::RoundToDouble(Ground));
			Item.Mark.YawDeg = FMath::RoundToFloat(static_cast<float>(Rng.Range(0.0, 360.0)));
			Item.Mark.Scale = FMath::RoundToFloat(static_cast<float>(Rng.Range(0.8, 1.35)) * 100.f) / 100.f;
		}
	}
	// Mezcladas con la semilla: lo que se enseña primero queda repartido por toda la orilla.
	Keyed.Sort([](const FKeyed& A, const FKeyed& B) { return A.Key != B.Key ? A.Key < B.Key : A.Mark.Location.X < B.Mark.Location.X; });
	if (Keyed.Num() > MaxMarks)
	{
		Keyed.SetNum(MaxMarks);
	}
	// Algún poste, separado de los demás: sale de las marcas que ya había (misma casilla).
	TArray<FVector2D> Posts;
	for (FKeyed& Item : Keyed)
	{
		if (Posts.Num() < MaxPosts && Item.Key % 7u == 0u)
		{
			const FVector2D Here(Item.Mark.Location.X, Item.Mark.Location.Y);
			bool bClear = true;
			for (const FVector2D& Other : Posts)
			{
				bClear &= FVector2D::Distance(Here, Other) >= PostSpacing;
			}
			if (bClear)
			{
				Posts.Add(Here);
				Item.Mark.Kind = ShoreKindPost;
				Item.Mark.Variant = 0;
				Item.Mark.Scale = 1.f;
			}
		}
		Marks.Add(Item.Mark);
	}
	return Marks;
}
