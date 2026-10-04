#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "TN_ProcMapMeshKit.h"

/**
 * Mallas estáticas construidas en ejecución a partir de buffers de caras planas (vegetación, objetos,
 * géiseres, partículas de los efectos). El color de vértice se guarda decodificado de sRGB para que el
 * material reciba el de la paleta.
 */
namespace TNProcRuntimeMesh
{
	/** Inversa de la codificación sRGB de un canal. */
	inline float SRGBToLinear(float C)
	{
		const float V = FMath::Clamp(C, 0.f, 1.f);
		return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f);
	}

	/**
	 * Densidad de UV del canal 0 (unidades de mundo por unidad de UV) calculada como la del motor
	 * (FUVDensityAccumulator): media de sqrt(área / área UV) de cada triángulo ponderada por sqrt(área),
	 * descartando el 10 % de cada extremo. 0 si ningún triángulo tiene área en mundo y en UV.
	 */
	inline float ComputeUVDensity(const TNProcMesh::FTNProcMeshBuffers& B)
	{
		struct FSample { float Density; float Weight; };
		TArray<FSample> Samples;
		Samples.Reserve(B.Tris.Num() / 3);
		for (int32 t = 0; t + 2 < B.Tris.Num(); t += 3)
		{
			const int32 I0 = B.Tris[t], I1 = B.Tris[t + 1], I2 = B.Tris[t + 2];
			const double Area = FVector::CrossProduct(B.Verts[I1] - B.Verts[I0], B.Verts[I2] - B.Verts[I0]).Size();
			const FVector2D UV01 = B.UVs[I1] - B.UVs[I0];
			const FVector2D UV02 = B.UVs[I2] - B.UVs[I0];
			const double UVArea = FMath::Abs(UV01.X * UV02.Y - UV01.Y * UV02.X);
			if (Area > UE_SMALL_NUMBER && UVArea > UE_SMALL_NUMBER)
			{
				Samples.Add({ static_cast<float>(FMath::Sqrt(Area / UVArea)), static_cast<float>(FMath::Sqrt(Area)) });
			}
		}
		Samples.Sort([](const FSample& A, const FSample& C) { return A.Density < C.Density; });
		const int32 Discard = FMath::FloorToInt(0.1f * static_cast<float>(Samples.Num()));
		double Weighted = 0.0, Weight = 0.0;
		for (int32 i = Discard; i < Samples.Num() - Discard; ++i)
		{
			Weighted += static_cast<double>(Samples[i].Density) * Samples[i].Weight;
			Weight += Samples[i].Weight;
		}
		return Weight > UE_SMALL_NUMBER ? static_cast<float>(Weighted / Weight) : 0.f;
	}

	/**
	 * Malla estática en ejecución a partir de unos buffers de caras planas (con una caja de colisión si se
	 * pide). El alfa del color de vértice es el peso de balanceo del viento del material: Wind x (altura
	 * relativa)^Exponent, 0 en la base (troncos, raíces) y Wind en lo más alto; con FixedAlpha >= 0, ese
	 * alfa en todos los vértices (opacidad de los efectos translúcidos); con FixedAlpha = -2, el alfa de los
	 * propios buffers (peso del aleteo de los pájaros).
	 */
	inline UStaticMesh* MakeStaticMesh(UObject* Outer, const TNProcMesh::FTNProcMeshBuffers& B, UMaterialInterface* Material, bool bBoxCollision = false,
		float Wind = 0.f, float WindExponent = 1.5f, float FixedAlpha = -1.f)
	{
		if (B.IsEmpty()) { return nullptr; }
		double MinZ = 1e18, MaxZ = -1e18;
		for (const FVector& V : B.Verts) { MinZ = FMath::Min(MinZ, V.Z); MaxZ = FMath::Max(MaxZ, V.Z); }
		const double SpanZ = FMath::Max(1.0, MaxZ - FMath::Max(0.0, MinZ));
		static const FName SlotName(TEXT("Flora"));
		FMeshDescription Desc;
		FStaticMeshAttributes Attributes(Desc);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = Desc.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = SlotName;

		const int32 NumVerts = B.Verts.Num();
		Desc.ReserveNewVertices(NumVerts);
		Desc.ReserveNewVertexInstances(NumVerts);
		Desc.ReserveNewTriangles(B.Tris.Num() / 3);
		TArray<FVertexInstanceID> Instances;
		Instances.SetNum(NumVerts);
		for (int32 i = 0; i < NumVerts; ++i)
		{
			const FVertexID V = Desc.CreateVertex();
			Positions[V] = FVector3f(B.Verts[i]);
			const FVertexInstanceID VI = Desc.CreateVertexInstance(V);
			const FVector N = B.Normals[i];
			const FVector T = FVector::CrossProduct(FMath::Abs(N.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector, N).GetSafeNormal();
			Normals[VI] = FVector3f(N);
			Tangents[VI] = FVector3f(T);
			Signs[VI] = 1.f;
			// La malla guarda el color en sRGB (ToFColor(true)) y el nodo VertexColor lo lee tal cual:
			// se decodifica antes para que el material reciba el color lineal de la paleta (como el
			// de las mallas procedurales) y no uno aclarado.
			const double Rel = FMath::Clamp((B.Verts[i].Z - FMath::Max(0.0, MinZ)) / SpanZ, 0.0, 1.0);
			const float Sway = FixedAlpha >= 0.f ? FixedAlpha
				: (FixedAlpha < -1.5f ? B.Colors[i].A : (Wind > 0.f ? Wind * static_cast<float>(FMath::Pow(Rel, static_cast<double>(WindExponent))) : 0.f));
			Colors[VI] = FVector4f(SRGBToLinear(B.Colors[i].R), SRGBToLinear(B.Colors[i].G), SRGBToLinear(B.Colors[i].B), Sway);
			UVs.Set(VI, 0, FVector2f(B.UVs[i]));
			Instances[i] = VI;
		}
		for (int32 t = 0; t + 2 < B.Tris.Num(); t += 3)
		{
			FVertexInstanceID Tri[3] = { Instances[B.Tris[t]], Instances[B.Tris[t + 1]], Instances[B.Tris[t + 2]] };
			Desc.CreateTriangle(Group, MakeArrayView(Tri, 3));
		}

		// Fuera de toda duplicación (PIE, copiar y pegar): la copia saldría sin descripción de malla ("Bad
		// MeshDescription" y Min LOD fuera de rango al empezar el PIE); cada actor la rehace en su BeginPlay/OnConstruction.
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient | RF_DuplicateTransient);
		// Datos de UV del streaming de texturas calculados aquí: sin datos de editor (build empaquetada)
		// UStaticMesh::UpdateUVChannelData no hace nada y la malla quedaba sin ellos, con lo que saltaba el
		// ensure de UStaticMesh::GetUVChannelData al registrarla en el streaming (#390).
		FStaticMaterial StaticMaterial(Material, SlotName);
		StaticMaterial.UVChannelData = FMeshUVChannelInfo(0.f);
		StaticMaterial.UVChannelData.LocalUVDensities[0] = ComputeUVDensity(B);
		Mesh->GetStaticMaterials().Add(StaticMaterial);
		// Chaos cocina la colisión compleja (malla de triángulos) leyendo los índices y posiciones en CPU: sin
		// bAllowCPUAccess, el editor y el cook avisan «GetPhysicsTriMeshData: CPU data not available» y la build
		// empaquetada rechaza la malla («cannot be accessed at runtime»), y la pieza se queda sin colisión compleja
		// (issue #344). BuildFromMeshDescriptions la usa también para dejar en CPU el índice de cada LOD.
		Mesh->bAllowCPUAccess = true;
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bMarkPackageDirty = false;
		Params.bBuildSimpleCollision = bBoxCollision;
		Params.bCommitMeshDescription = false;
		Params.bFastBuild = true;
		TArray<const FMeshDescription*> Descs;
		Descs.Add(&Desc);
		Mesh->BuildFromMeshDescriptions(Descs, Params);
		return Mesh;
	}
}
