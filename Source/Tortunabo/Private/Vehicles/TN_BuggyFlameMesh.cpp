#include "TN_BuggyFlameMesh.h"

#include "Engine/StaticMesh.h"
#include "UObject/Package.h"
#include "Vehicles/TN_BuggyMath.h"
#include "World/ProcMap/TN_ProcMapMeshKit.h"
#include "World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "World/TN_LootGlowKit.h"

namespace TNBuggyFlameMesh
{
	namespace
	{
		constexpr int32 ConeSegments = 16;
		/** Alturas de los anillos (fracción del alto, de la base a la punta): tres bandas de color. */
		constexpr double BandHeights[] = { 0.0, 0.4, 0.75, 1.0 };
		const FLinearColor TipYellow(1.f, 0.86f, 0.42f);

		FVector RingPoint(int32 Segment, double Height01)
		{
			const double Half = 0.5 * TNBuggy::BasicConeSizeCm;
			const double Radius = Half * (1.0 - Height01);
			const double Angle = UE_DOUBLE_TWO_PI * Segment / ConeSegments;
			return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, -Half + Height01 * TNBuggy::BasicConeSizeCm);
		}
	}

	UMaterialInterface* GlowMaterial()
	{
		return TNLootGlow::GlowMaterial();
	}

	FLinearColor TipColor(const FLinearColor& BaseColor)
	{
		return FLinearColor::LerpUsingHSV(BaseColor, TipYellow, 0.7f);
	}

	UStaticMesh* GlowCone(const FLinearColor& BaseColor)
	{
		static TMap<uint32, TWeakObjectPtr<UStaticMesh>> Cache;
		const uint32 Key = GetTypeHash(BaseColor.ToFColor(true));
		if (const TWeakObjectPtr<UStaticMesh>* Found = Cache.Find(Key))
		{
			if (UStaticMesh* Existing = Found->Get())
			{
				return Existing;
			}
		}
		UMaterialInterface* Material = GlowMaterial();
		if (!Material)
		{
			return nullptr;
		}
		const FLinearColor Tip = TipColor(BaseColor);
		TNProcMesh::FTNProcMeshBuffers Mesh;
		const int32 Bands = UE_ARRAY_COUNT(BandHeights) - 1;
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			const FLinearColor Color = FLinearColor::LerpUsingHSV(BaseColor, Tip, static_cast<float>(Band) / (Bands - 1));
			for (int32 Segment = 0; Segment < ConeSegments; ++Segment)
			{
				const FVector A = RingPoint(Segment, BandHeights[Band]);
				const FVector B = RingPoint(Segment + 1, BandHeights[Band]);
				const FVector C = RingPoint(Segment + 1, BandHeights[Band + 1]);
				const FVector D = RingPoint(Segment, BandHeights[Band + 1]);
				const FVector Outward = FVector(A.X + B.X, A.Y + B.Y, 0.0);
				if (Band == Bands - 1)
				{
					// La última banda acaba en la punta: triángulos.
					Mesh.AddTri(A, B, C, Outward, Color);
				}
				else
				{
					Mesh.AddQuad(A, B, C, D, Outward, Color);
				}
			}
		}
		// Tapa de la base (dentro del escape; se ve si la llama apunta a la cámara).
		const FVector BaseCenter(0.0, 0.0, -0.5 * TNBuggy::BasicConeSizeCm);
		for (int32 Segment = 0; Segment < ConeSegments; ++Segment)
		{
			Mesh.AddTri(BaseCenter, RingPoint(Segment, 0.0), RingPoint(Segment + 1, 0.0), -FVector::UpVector, BaseColor);
		}
		UStaticMesh* Cone = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Mesh, Material, false, 0.f, 1.f, 0.f);
		if (Cone)
		{
			// Compartida por todos los buggies y fuera del recolector, como las mallas de TNLootGlow.
			Cone->AddToRoot();
		}
		Cache.Add(Key, Cone);
		return Cone;
	}
}
