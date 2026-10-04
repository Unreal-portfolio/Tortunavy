// ─────────────────────────────────────────────────────────────────────────────
// Cofre de la playa (ETNBeachElement::TreasureChest): el elemento del reparto (ATN_BeachChest,
// solo en el servidor) y el cofre que se ve y se abre (ATN_BeachChestSpot, replicado). Variante
// playera del cofre del lobby (ATN_TreasureChest): 5,5 s manteniendo E, de lo mejor para
// avanzar, un segundo objeto y conchas de puntos que saltan alrededor, una vez por ronda.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachChest.h"
#include "Art/TN_Art.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_ScorePickup.h"
#include "World/TN_ScoreShells.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_Log.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/Package.h"
#include "../TN_LootGlowKit.h"
#include "../../Lobby/TN_CastleKit.h"

namespace TNBeachChestDetail
{
	using FBuffers = TNProcMesh::FTNProcMeshBuffers;

	// ── Medidas ──
	/** Veces el cofre del lobby: las mallas se hacen con sus medidas y los componentes van a esta escala. */
	constexpr double ChestScale = 2.2;
	// Medidas del cofre del lobby (cm, locales: origen en el suelo en el centro, +X es el frente, Z arriba).
	/** Medio fondo, medio ancho y alto de la caja (la tapa va encima). */
	constexpr double ChestHalfD = 50.0;
	constexpr double ChestHalfW = 78.0;
	constexpr double ChestBodyH = 64.0;
	/** Grueso de las paredes. */
	constexpr double ChestWallT = 7.0;
	/** Tapa de medio cañón: radio (vuela 3 cm por delante y por detrás), medio largo (vuela 3 por los lados) y grueso. */
	constexpr double LidRadius = ChestHalfD + 3.0;
	constexpr double LidSide = ChestHalfW + 3.0;
	constexpr double LidThick = 6.0;
	constexpr int32 LidSegments = 8;
	/** Flejes: a qué distancia del centro van y su medio ancho. */
	constexpr double StrapY = ChestHalfW * 0.56;
	constexpr double StrapHalf = 6.0;
	/** Alto total con la tapa cerrada. */
	constexpr double ChestTopZ = ChestBodyH + LidRadius;
	/** Cama de monedas de dentro. */
	constexpr double CoinBedZ = ChestBodyH - 18.0;

	// ── Juego ──
	/** Segundos manteniendo E (un rebuscable, 1,3 s). */
	constexpr float OpenSeconds = 5.5f;
	/** Huella para abrirlo (cm, ya a escala): su borde queda un palmo por delante del frente. */
	constexpr float SpotRadius = static_cast<float>(ChestHalfD * ChestScale + 45.0);
	/** Un resultado que llega con menos de esto (s) se vive (tapa que salta, destellos); más tarde, ya abierto. */
	constexpr double FreshSeconds = 1.5;

	// ── Tapa ──
	/** Mientras se abre sube a tirones de LidPryStartDeg a LidPryEndDeg (grados) según el progreso. */
	constexpr float LidPryStartDeg = 12.f;
	constexpr float LidPryEndDeg = 48.f;
	/** Abierta del todo al vaciarse (se queda así), empujón con el que salta al abrirse (grados/s) y tope. */
	constexpr float LidOpenDeg = 104.f;
	constexpr float LidPopKick = 520.f;
	constexpr float LidMaxDeg = 126.f;
	/** Muelle de la tapa (rebota un poco). */
	constexpr float LidStiffness = 160.f;
	constexpr float LidDamping = 13.f;
	/** Velocidad (grados/s) a partir de la que el golpe contra la caja suena. */
	constexpr float LidThumpSpeed = 70.f;

	// ── Luz y columna ──
	/** Luz de dentro (lúmenes): la que late por la rendija, la de la tapa abierta, el fogonazo al vaciarse y la apagada. */
	constexpr float IdleLumens = 900.f;
	constexpr float SearchLumens = 3200.f;
	constexpr float FlashLumens = 6000.f;
	constexpr float SpentLumens = 160.f;
	constexpr double FlashSeconds = 0.9;
	/** Segundos que se ve todavía el tesoro de dentro tras abrirse (lo que tardan en salir los premios). */
	constexpr double TreasureHideDelay = 0.15;
	/** Columna de luz: radio y alto (veces la malla de TNLootGlow: 50 y 100 cm), a qué distancia se deja de ver y lo que tarda en irse. */
	constexpr double BeamRadiusScale = 2.6;
	constexpr double BeamHeightScale = 30.0;
	constexpr float BeamDrawDistance = 45000.f;
	constexpr double BeamFadeSeconds = 0.8;
	/** Con la cámara más cerca que esto (cm) la luz late a cada fotograma. */
	constexpr double NearViewDistance = 6000.0;

	// ── Premios ──
	/** Objetos de más, además del de siempre, y conchas de puntos (cuatro de 25, una de 50 y otra de 50 o de 100). */
	constexpr int32 BonusItems = 1;
	constexpr int32 ShellValues[] = { 25, 25, 25, 25, 50 };
	/** Probabilidad de que la última concha sea una reina de 100 (si no, otra de 50). */
	constexpr float GrandShellChance = 0.4f;
	/** Hueco sin premios a cada lado de quien lo abre (grados): ahí cae el objeto de siempre. */
	constexpr double FrontGapDeg = 40.0;
	/** Dónde caen, más allá del borde de la caja (cm): el objeto de siempre, el de más y las conchas. */
	constexpr float ItemRingMin = 90.f;
	constexpr float ItemRingMax = 150.f;
	constexpr float ShellRingMin = 110.f;
	constexpr float ShellRingMax = 280.f;
	/** Un suelo más de esto (cm) por encima o por debajo del cofre no vale (lo de lo alto de una fortaleza no cae al pie). */
	constexpr double GroundTolerance = 220.0;
	/** Saltos: cuándo sale el primero, entre uno y otro, lo que dura cada uno, lo alto y con qué tamaño asoman. */
	constexpr double PrizeDelay = 0.12;
	constexpr double PrizeStagger = 0.08;
	constexpr double PrizeHopSeconds = 0.8;
	constexpr double PrizeApex = 230.0;
	constexpr double PrizeStartScale = 0.3;

	/** Hasta cuándo (s desde que se abre) puede quedar algún premio en el aire. */
	double PrizeWindow(int32 Count)
	{
		return PrizeDelay + PrizeStagger * FMath::Max(0, Count - 1) + PrizeHopSeconds + 0.2;
	}

	// ── Mallas (medidas del lobby; los componentes las escalan) ──

	/** Caja entre dos esquinas dadas en cualquier orden. */
	void AddSlab(FBuffers& B, double X0, double X1, double Y0, double Y1, double Z0, double Z1, const FLinearColor& Color)
	{
		TNCastleKit::AddAxisBox(B, FVector(FMath::Min(X0, X1), FMath::Min(Y0, Y1), FMath::Min(Z0, Z1)),
			FVector(FMath::Max(X0, X1), FMath::Max(Y0, Y1), FMath::Max(Z0, Z1)), Color);
	}

	/** Remache: cabeza baja de seis lados en P, que sale hacia Out. */
	void AddRivet(FBuffers& B, const FVector& P, const FVector& Out, const FLinearColor& Color)
	{
		TNProcMesh::TNProcAddCylinder(B, P - Out * 0.5, P + Out * 2.2, 2.7, 2.0, 6, Color);
	}

	/** Percebe: cono bajo de placas claras con la boca oscura, pegado en P a una cara que mira hacia Out. */
	void AddBarnacle(FBuffers& B, const FVector& P, const FVector& Out, double Size)
	{
		TNProcMesh::TNProcAddCylinder(B, P - Out * 0.5, P + Out * (Size * 0.9), Size, Size * 0.45, 6, TNCastleKit::Pal(0xDCD6C8));
		TNProcMesh::TNProcAddCylinder(B, P + Out * (Size * 0.85), P + Out * (Size * 1.05), Size * 0.42, Size * 0.18, 6, TNCastleKit::Pal(0x5E5A52));
	}

	/** Estrella de mar pegada a una cara: centro C, N hacia fuera de la cara y U hacia arriba en ella. */
	void AddWallStar(FBuffers& B, const FVector& C, const FVector& N, const FVector& U, double Size, double Spin, const FLinearColor& Color)
	{
		const FVector V = FVector::CrossProduct(N, U).GetSafeNormal();
		const double Half = UE_DOUBLE_PI / 5.0;
		auto Around = [&](double A, double R, double Out) { return C + (U * FMath::Cos(A) + V * FMath::Sin(A)) * R + N * Out; };
		for (int32 k = 0; k < 5; ++k)
		{
			const double A0 = Spin + UE_DOUBLE_TWO_PI * k / 5.0;
			const FVector Tip = Around(A0, Size, 1.5);
			const FVector Inner0 = Around(A0 + Half, Size * 0.38, 2.5);
			const FVector Inner1 = Around(A0 - Half, Size * 0.38, 2.5);
			B.AddTri(C + N * 4.0, Inner1, Tip, N, Color);
			B.AddTri(C + N * 4.0, Tip, Inner0, N, Color * 0.9f);
		}
	}

	/**
	 * Tira de alga que cuelga desde Top y baja Length por una cara (N hacia fuera; Side, de lado en la cara), ondulando y
	 * afinándose hacia la punta. Dos caras.
	 */
	void AddSeaweedStrand(FBuffers& B, const FVector& Top, const FVector& N, const FVector& Side, double Length, double Width, int32 Seed)
	{
		constexpr int32 Steps = 6;
		const FLinearColor GreenA = TNCastleKit::Pal(0x3F7D3A);
		const FLinearColor GreenB = TNCastleKit::Pal(0x2F6B45);
		const double Phase = 2.1 * Seed;
		auto Spine = [&](double T)
		{
			return Top - FVector(0.0, 0.0, Length * T) + Side * (5.0 * FMath::Sin(T * 7.0 + Phase)) + N * (1.2 + 1.5 * T);
		};
		for (int32 s = 0; s < Steps; ++s)
		{
			const double T0 = static_cast<double>(s) / Steps;
			const double T1 = static_cast<double>(s + 1) / Steps;
			const double W0 = Width * (1.0 - 0.7 * T0);
			const double W1 = Width * (1.0 - 0.7 * T1);
			const FVector P0 = Spine(T0);
			const FVector P1 = Spine(T1);
			const FLinearColor Color = (s % 2) ? GreenA : GreenB;
			B.AddQuad(P0 - Side * W0, P0 + Side * W0, P1 + Side * W1, P1 - Side * W1, N, Color);
			B.AddQuad(P0 - Side * W0, P0 + Side * W0, P1 + Side * W1, P1 - Side * W1, -N, Color * 0.8f);
		}
	}

	/** Arena amontonada al pie de la caja, como si la hubiera traído la marea. */
	void AddSandMound(FBuffers& B)
	{
		const FLinearColor SandA = TNCastleKit::Pal(0xE9CF96);
		const FLinearColor SandB = TNCastleKit::Pal(0xDDBF84);
		constexpr int32 Seg = 32;
		auto Rim = [](double A)
		{
			// Del centro hacia A, hasta el borde de la caja (con 5 cm de holgura).
			const double C = FMath::Max(FMath::Abs(FMath::Cos(A)), 1e-3);
			const double S = FMath::Max(FMath::Abs(FMath::Sin(A)), 1e-3);
			const double T = FMath::Min((ChestHalfD + 5.0) / C, (ChestHalfW + 5.0) / S);
			return FVector(FMath::Cos(A) * T, FMath::Sin(A) * T, 0.0);
		};
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = UE_DOUBLE_TWO_PI * k / Seg;
			const double A1 = UE_DOUBLE_TWO_PI * (k + 1) / Seg;
			const FVector D0(FMath::Cos(A0), FMath::Sin(A0), 0.0);
			const FVector D1(FMath::Cos(A1), FMath::Sin(A1), 0.0);
			const double H0 = 11.0 + 4.0 * TNProcMesh::TNProcHashNoise(k, 0, 0xC4E5u);
			const double H1 = 11.0 + 4.0 * TNProcMesh::TNProcHashNoise((k + 1) % Seg, 0, 0xC4E5u);
			const double R0 = 26.0 + 8.0 * TNProcMesh::TNProcHashNoise(k, 1, 0xC4E5u);
			const double R1 = 26.0 + 8.0 * TNProcMesh::TNProcHashNoise((k + 1) % Seg, 1, 0xC4E5u);
			const FVector In0 = Rim(A0) + FVector(0.0, 0.0, H0);
			const FVector In1 = Rim(A1) + FVector(0.0, 0.0, H1);
			const FVector Out0 = Rim(A0) + D0 * R0 + FVector(0.0, 0.0, -3.0);
			const FVector Out1 = Rim(A1) + D1 * R1 + FVector(0.0, 0.0, -3.0);
			B.AddQuad(In0, In1, Out1, Out0, FVector::UpVector, (k % 2) ? SandA : SandB);
		}
	}

	/**
	 * Caja del cofre, variante playera del del lobby: tablones blanqueados por el sol y el agua, flejes y asas de hierro
	 * oxidado, borde, cantoneras y cerradura dorados; percebes por abajo, algas colgando del borde, una estrella de mar
	 * pegada a un lado y arena amontonada al pie.
	 */
	void BuildBody(FBuffers& B)
	{
		const FLinearColor WoodA = TNCastleKit::Pal(0xA98A66);
		const FLinearColor WoodB = TNCastleKit::Pal(0x927252);
		const FLinearColor WoodDark = TNCastleKit::Pal(0x5C4430);
		const FLinearColor WoodIn = TNCastleKit::Pal(0x3E2C1E);
		const FLinearColor Iron = TNCastleKit::Pal(0x6A4B3B);
		const FLinearColor Gold = TNCastleKit::Pal(0xFFCB3D);
		const FLinearColor GoldDeep = TNCastleKit::Pal(0xE0A12A);
		const FLinearColor Keyhole = TNCastleKit::Pal(0x24160C);
		constexpr double DIn = ChestHalfD - ChestWallT;
		constexpr double WIn = ChestHalfW - ChestWallT;

		// Paredes (por dentro, madera oscura; por fuera las tapan los tablones), zócalo y fondo.
		AddSlab(B, DIn, ChestHalfD, -ChestHalfW, ChestHalfW, 0.0, ChestBodyH, WoodIn);
		AddSlab(B, -ChestHalfD, -DIn, -ChestHalfW, ChestHalfW, 0.0, ChestBodyH, WoodIn);
		AddSlab(B, -DIn, DIn, WIn, ChestHalfW, 0.0, ChestBodyH, WoodIn);
		AddSlab(B, -DIn, DIn, -ChestHalfW, -WIn, 0.0, ChestBodyH, WoodIn);
		AddSlab(B, -DIn, DIn, -WIn, WIn, 0.0, 2.0, WoodIn);
		AddSlab(B, -ChestHalfD - 4.0, ChestHalfD + 4.0, -ChestHalfW - 4.0, ChestHalfW + 4.0, 0.0, 8.0, WoodDark);

		// Tablones por fuera: tres hileras con una junta oscura entre ellas, en las cuatro caras.
		constexpr int32 Rows = 3;
		const double RowH = (ChestBodyH - 15.0) / Rows;
		for (int32 r = 0; r < Rows; ++r)
		{
			const double Z0 = r == 0 ? 7.0 : 8.0 + RowH * r + 0.8;
			const double Z1 = r == Rows - 1 ? ChestBodyH - 6.0 : 8.0 + RowH * (r + 1) - 0.8;
			AddSlab(B, ChestHalfD - 1.0, ChestHalfD + 2.0, -ChestHalfW + 1.0, ChestHalfW - 1.0, Z0, Z1, (r % 2) ? WoodA : WoodB);
			AddSlab(B, -ChestHalfD - 2.0, -ChestHalfD + 1.0, -ChestHalfW + 1.0, ChestHalfW - 1.0, Z0, Z1, (r % 2) ? WoodB : WoodA);
			AddSlab(B, -ChestHalfD + 1.0, ChestHalfD - 1.0, ChestHalfW - 1.0, ChestHalfW + 2.0, Z0, Z1, (r % 2) ? WoodB : WoodA);
			AddSlab(B, -ChestHalfD + 1.0, ChestHalfD - 1.0, -ChestHalfW - 2.0, -ChestHalfW + 1.0, Z0, Z1, (r % 2) ? WoodA : WoodB);
		}

		// Borde dorado arriba (un poco por encima de las paredes: sin parpadeo entre caras).
		const double BandZ0 = ChestBodyH - 7.0;
		const double BandZ1 = ChestBodyH + 0.5;
		AddSlab(B, ChestHalfD - 1.0, ChestHalfD + 3.5, -ChestHalfW - 3.5, ChestHalfW + 3.5, BandZ0, BandZ1, GoldDeep);
		AddSlab(B, -ChestHalfD - 3.5, -ChestHalfD + 1.0, -ChestHalfW - 3.5, ChestHalfW + 3.5, BandZ0, BandZ1, GoldDeep);
		AddSlab(B, -ChestHalfD + 1.0, ChestHalfD - 1.0, ChestHalfW - 1.0, ChestHalfW + 3.5, BandZ0, BandZ1, GoldDeep);
		AddSlab(B, -ChestHalfD + 1.0, ChestHalfD - 1.0, -ChestHalfW - 3.5, -ChestHalfW + 1.0, BandZ0, BandZ1, GoldDeep);

		// Cantoneras doradas con dos remaches por cara y flejes de hierro oxidado delante y detrás.
		for (const double Sx : { -1.0, 1.0 })
		{
			for (const double Sy : { -1.0, 1.0 })
			{
				AddSlab(B, Sx * (ChestHalfD - 5.0), Sx * (ChestHalfD + 4.5), Sy * (ChestHalfW - 5.0), Sy * (ChestHalfW + 4.5), 6.0, ChestBodyH - 5.5, Gold);
				for (const double RivetZ : { 20.0, 42.0 })
				{
					AddRivet(B, FVector(Sx * (ChestHalfD + 4.5), Sy * (ChestHalfW - 0.5), RivetZ), FVector(Sx, 0.0, 0.0), GoldDeep);
					AddRivet(B, FVector(Sx * (ChestHalfD - 0.5), Sy * (ChestHalfW + 4.5), RivetZ), FVector(0.0, Sy, 0.0), GoldDeep);
				}
				AddSlab(B, Sx * (ChestHalfD + 1.0), Sx * (ChestHalfD + 4.0), Sy * StrapY - StrapHalf, Sy * StrapY + StrapHalf, 7.5, ChestBodyH - 6.5, Iron);
				for (const double RivetZ : { 18.0, 33.0, 48.0 })
				{
					AddRivet(B, FVector(Sx * (ChestHalfD + 4.0), Sy * StrapY, RivetZ), FVector(Sx, 0.0, 0.0), Gold);
				}
			}
		}

		// Lados: fleje en medio y un asa de hierro colgando de dos pletinas.
		for (const double Sy : { -1.0, 1.0 })
		{
			AddSlab(B, -StrapHalf, StrapHalf, Sy * (ChestHalfW + 1.0), Sy * (ChestHalfW + 4.0), 7.5, ChestBodyH - 6.5, Iron);
			for (const double RivetZ : { 16.0, 48.0 })
			{
				AddRivet(B, FVector(0.0, Sy * (ChestHalfW + 4.0), RivetZ), FVector(0.0, Sy, 0.0), Gold);
			}
			const double HandleZ = ChestBodyH * 0.55;
			const double Near = Sy * (ChestHalfW + 4.0);
			const double Far = Sy * (ChestHalfW + 11.0);
			for (const double PlateX : { -13.0, 13.0 })
			{
				AddSlab(B, PlateX - 3.5, PlateX + 3.5, Sy * (ChestHalfW + 1.0), Sy * (ChestHalfW + 5.5), HandleZ - 4.5, HandleZ + 4.5, Iron);
			}
			B.AddBeam(FVector(-13.0, Near, HandleZ), FVector(-13.0, Far, HandleZ - 7.0), 1.8, Iron);
			B.AddBeam(FVector(-13.0, Far, HandleZ - 7.0), FVector(13.0, Far, HandleZ - 7.0), 1.8, Iron);
			B.AddBeam(FVector(13.0, Far, HandleZ - 7.0), FVector(13.0, Near, HandleZ), 1.8, Iron);
		}

		// Cerradura: chapa dorada con el ojo de la llave y cuatro remaches.
		AddSlab(B, ChestHalfD + 1.5, ChestHalfD + 5.5, -13.0, 13.0, ChestBodyH - 34.0, ChestBodyH - 5.0, Gold);
		TNProcMesh::TNProcAddCylinder(B, FVector(ChestHalfD + 5.3, 0.0, ChestBodyH - 25.0), FVector(ChestHalfD + 6.3, 0.0, ChestBodyH - 25.0), 3.4, 3.4, 8, Keyhole);
		AddSlab(B, ChestHalfD + 5.3, ChestHalfD + 6.3, -1.5, 1.5, ChestBodyH - 31.0, ChestBodyH - 25.0, Keyhole);
		for (const double PlateY : { -9.5, 9.5 })
		{
			for (const double PlateZ : { ChestBodyH - 30.0, ChestBodyH - 9.0 })
			{
				AddRivet(B, FVector(ChestHalfD + 5.5, PlateY, PlateZ), FVector(1.0, 0.0, 0.0), GoldDeep);
			}
		}

		// De la playa: percebes por la parte de abajo de las cuatro caras (lejos de la cerradura y los flejes).
		for (int32 i = 0; i < 18; ++i)
		{
			const int32 Face = i % 4;
			const double U = TNProcMesh::TNProcHashNoise(i, 1, 0xBA27u);
			const double Z = 12.0 + 12.0 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(i, 2, 0xBA27u));
			const double Size = 2.6 + 1.4 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(i, 3, 0xBA27u));
			if (Face < 2)
			{
				const double Sx = Face == 0 ? 1.0 : -1.0;
				const double Y = U * (ChestHalfW - 12.0);
				if (Face == 0 && FMath::Abs(Y) < 18.0) { continue; }
				AddBarnacle(B, FVector(Sx * (ChestHalfD + 2.0), Y, Z), FVector(Sx, 0.0, 0.0), Size);
			}
			else
			{
				const double Sy = Face == 2 ? 1.0 : -1.0;
				AddBarnacle(B, FVector(U * (ChestHalfD - 12.0), Sy * (ChestHalfW + 2.0), Z), FVector(0.0, Sy, 0.0), Size);
			}
		}
		// Algas colgando del borde: tres por delante a la izquierda y dos por el lado derecho.
		AddSeaweedStrand(B, FVector(ChestHalfD + 4.0, -ChestHalfW + 14.0, ChestBodyH), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), 44.0, 4.0, 1);
		AddSeaweedStrand(B, FVector(ChestHalfD + 4.0, -ChestHalfW + 24.0, ChestBodyH), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), 34.0, 3.4, 2);
		AddSeaweedStrand(B, FVector(ChestHalfD + 4.0, -ChestHalfW + 33.0, ChestBodyH), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), 26.0, 3.0, 3);
		AddSeaweedStrand(B, FVector(22.0, ChestHalfW + 4.0, ChestBodyH), FVector(0.0, 1.0, 0.0), FVector(-1.0, 0.0, 0.0), 38.0, 3.6, 4);
		AddSeaweedStrand(B, FVector(31.0, ChestHalfW + 4.0, ChestBodyH), FVector(0.0, 1.0, 0.0), FVector(-1.0, 0.0, 0.0), 28.0, 3.0, 5);
		// Una estrella de mar pegada al lado derecho, detrás del asa.
		AddWallStar(B, FVector(-30.0, ChestHalfW + 2.0, 34.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), 11.0, 0.4, TNCastleKit::Pal(0xFF8A70));
		// Arena al pie.
		AddSandMound(B);
	}

	/** Montones de monedas de dentro: centro, radio de la base y alto sobre la cama. */
	struct FCoinPile
	{
		double X;
		double Y;
		double Radius;
		double Height;
	};
	const FCoinPile CoinPiles[] = { { -8.0, -22.0, 30.0, 14.0 }, { 10.0, 26.0, 26.0, 11.0 }, { 20.0, -2.0, 16.0, 7.0 } };

	double PileHeightAt(double X, double Y)
	{
		double Best = 0.0;
		for (const FCoinPile& Pile : CoinPiles)
		{
			const double Dist = FVector2D::Distance(FVector2D(X, Y), FVector2D(Pile.X, Pile.Y));
			Best = FMath::Max(Best, Pile.Height * (1.0 - Dist / Pile.Radius));
		}
		return Best;
	}

	/** Gema tallada (pabellón hacia abajo y corona con tabla) con el centro de la cintura en C. */
	void AddGem(FBuffers& B, const FVector& C, double Size, const FLinearColor& Color)
	{
		const FVector Up(0.0, 0.0, 1.0);
		TNProcMesh::TNProcAddCylinder(B, C - Up * (Size * 0.9), C, Size * 0.08, Size, 6, Color * 0.8f);
		TNProcMesh::TNProcAddCylinder(B, C, C + Up * (Size * 0.45), Size, Size * 0.55, 6, Color);
	}

	/** Perla: dos conos de seis lados unidos por la cintura. */
	void AddPearl(FBuffers& B, const FVector& C, double Radius)
	{
		const FVector Up(0.0, 0.0, 1.0);
		const FLinearColor Pearl = TNCastleKit::Pal(0xF4EEE6);
		TNProcMesh::TNProcAddCylinder(B, C - Up * Radius, C, Radius * 0.25, Radius, 6, Pearl * 0.9f);
		TNProcMesh::TNProcAddCylinder(B, C, C + Up * Radius, Radius, Radius * 0.25, 6, Pearl);
	}

	/**
	 * El tesoro de dentro: cama de monedas con montones, monedas sueltas, gemas, una copa y, de la playa, un collar de
	 * perlas, perlas sueltas y una vieira dorada. Se esconde al vaciarse el cofre.
	 */
	void BuildTreasure(FBuffers& B)
	{
		const FVector Up(0.0, 0.0, 1.0);
		const FLinearColor Gold = TNCastleKit::Pal(0xFFCB3D);
		const FLinearColor GoldDeep = TNCastleKit::Pal(0xE0A12A);
		const FLinearColor CoinA = TNCastleKit::Pal(0xFFD447);
		const FLinearColor CoinB = TNCastleKit::Pal(0xF2B632);
		constexpr double DIn = ChestHalfD - ChestWallT;
		constexpr double WIn = ChestHalfW - ChestWallT;

		AddSlab(B, -DIn - 0.5, DIn + 0.5, -WIn - 0.5, WIn + 0.5, 2.0, CoinBedZ, CoinB);
		for (const FCoinPile& Pile : CoinPiles)
		{
			TNProcMesh::TNProcAddCylinder(B, FVector(Pile.X, Pile.Y, CoinBedZ - 0.5), FVector(Pile.X, Pile.Y, CoinBedZ + Pile.Height), Pile.Radius, Pile.Radius * 0.2, 10, CoinA);
		}
		for (int32 c = 0; c < 16; ++c)
		{
			const double CoinX = (DIn - 7.0) * TNProcMesh::TNProcHashNoise(c, 1, 43u);
			const double CoinY = (WIn - 7.0) * TNProcMesh::TNProcHashNoise(c, 2, 43u);
			const double Tilt = 0.45 * TNProcMesh::TNProcHashNoise(c, 3, 43u);
			const double Spin = UE_DOUBLE_PI * TNProcMesh::TNProcHashNoise(c, 4, 43u);
			const FVector Axis = FVector(FMath::Cos(Spin) * Tilt, FMath::Sin(Spin) * Tilt, 1.0).GetSafeNormal();
			const FVector CoinAt(CoinX, CoinY, CoinBedZ + PileHeightAt(CoinX, CoinY) + 0.6);
			TNProcMesh::TNProcAddCylinder(B, CoinAt, CoinAt + Axis * 1.4, 5.5, 5.5, 10, (c % 2) ? CoinA : CoinB);
		}
		AddGem(B, FVector(-22.0, 34.0, CoinBedZ + 7.0), 6.5, TNCastleKit::Pal(0xE63946));
		AddGem(B, FVector(24.0, -36.0, CoinBedZ + 6.0), 6.0, TNCastleKit::Pal(0x2EC4B6));
		AddGem(B, FVector(-8.0, -22.0, CoinBedZ + 16.0), 7.0, TNCastleKit::Pal(0x9B5DE5));
		AddGem(B, FVector(12.0, 26.0, CoinBedZ + 12.5), 5.0, TNCastleKit::Pal(0x4CC9F0));
		const FVector Cup(-26.0, -40.0, CoinBedZ);
		TNProcMesh::TNProcAddCylinder(B, Cup, Cup + Up * 1.8, 6.0, 5.0, 8, Gold);
		TNProcMesh::TNProcAddCylinder(B, Cup + Up * 1.8, Cup + Up * 9.0, 1.6, 1.6, 6, Gold);
		TNProcMesh::TNProcAddCylinder(B, Cup + Up * 9.0, Cup + Up * 18.0, 3.0, 7.0, 10, GoldDeep);

		// Collar de perlas echado sobre el montón grande (un arco de cuentas) y unas perlas sueltas.
		for (int32 p = 0; p < 11; ++p)
		{
			const double A = UE_DOUBLE_PI * (0.15 + 0.7 * p / 10.0);
			const double PX = -8.0 + FMath::Cos(A) * 22.0;
			const double PY = -22.0 + FMath::Sin(A) * 22.0 - 10.0;
			AddPearl(B, FVector(PX, PY, CoinBedZ + PileHeightAt(PX, PY) + 2.4), 2.2);
		}
		AddPearl(B, FVector(28.0, 18.0, CoinBedZ + PileHeightAt(28.0, 18.0) + 3.0), 3.0);
		AddPearl(B, FVector(-30.0, 8.0, CoinBedZ + PileHeightAt(-30.0, 8.0) + 3.0), 2.6);
		// Vieira dorada asomando del montón pequeño.
		TNCastleKit::AddScallop(B, FVector(22.0, -4.0, CoinBedZ + 9.0), FVector(0.35, 0.0, 1.0).GetSafeNormal(), FVector(1.0, 0.0, -0.35).GetSafeNormal(), 9.0, Gold);
	}

	/** Punto de la tapa (locales de la tapa: origen en la bisagra) a Rad del eje del medio cañón, en el ángulo Theta y a Y. */
	FVector LidArcPoint(double Rad, double Theta, double Y)
	{
		return FVector(ChestHalfD + Rad * FMath::Cos(Theta), Y, Rad * FMath::Sin(Theta));
	}

	/**
	 * Tapa de medio cañón (locales de la tapa: origen en la bisagra, +X hacia el frente), como la del lobby con los flejes
	 * oxidados, percebes por encima y una tira de alga que la cruza de atrás adelante y cuelga por el frente.
	 */
	void BuildLid(FBuffers& B)
	{
		const FLinearColor WoodA = TNCastleKit::Pal(0xA98A66);
		const FLinearColor WoodB = TNCastleKit::Pal(0x927252);
		const FLinearColor WoodDark = TNCastleKit::Pal(0x5C4430);
		const FLinearColor WoodIn = TNCastleKit::Pal(0x3E2C1E);
		const FLinearColor Iron = TNCastleKit::Pal(0x6A4B3B);
		const FLinearColor Gold = TNCastleKit::Pal(0xFFCB3D);
		const FLinearColor GoldDeep = TNCastleKit::Pal(0xE0A12A);
		constexpr double RIn = LidRadius - LidThick;

		for (int32 k = 0; k < LidSegments; ++k)
		{
			const double Th0 = UE_DOUBLE_PI * k / LidSegments;
			const double Th1 = UE_DOUBLE_PI * (k + 1) / LidSegments;
			const double ThM = (Th0 + Th1) * 0.5;
			const FVector Out(FMath::Cos(ThM), 0.0, FMath::Sin(ThM));
			// Un tablón por tramo, por fuera; por dentro, madera oscura.
			B.AddQuad(LidArcPoint(LidRadius, Th0, -LidSide), LidArcPoint(LidRadius, Th1, -LidSide), LidArcPoint(LidRadius, Th1, LidSide),
				LidArcPoint(LidRadius, Th0, LidSide), Out, (k % 2) ? WoodA : WoodB);
			B.AddQuad(LidArcPoint(RIn, Th0, -LidSide), LidArcPoint(RIn, Th1, -LidSide), LidArcPoint(RIn, Th1, LidSide), LidArcPoint(RIn, Th0, LidSide),
				-Out, WoodIn);
			for (const double Sy : { -1.0, 1.0 })
			{
				// Testeros: medio disco macizo a cada lado con filo dorado.
				const FVector SideN(0.0, Sy, 0.0);
				const FVector Hub(ChestHalfD, Sy * LidSide, 0.0);
				const FVector E0 = LidArcPoint(LidRadius, Th0, Sy * LidSide);
				const FVector E1 = LidArcPoint(LidRadius, Th1, Sy * LidSide);
				B.AddTri(Hub, E0, E1, SideN, WoodB);
				B.AddTri(Hub, E0, E1, -SideN, WoodIn);
				B.AddBeam(LidArcPoint(LidRadius + 0.5, Th0, Sy * (LidSide - 1.5)), LidArcPoint(LidRadius + 0.5, Th1, Sy * (LidSide - 1.5)), 2.6, Gold);
				// Fleje de hierro por encima del tablón (cara de fuera y los dos cantos).
				const double Y0 = Sy * StrapY - StrapHalf;
				const double Y1 = Sy * StrapY + StrapHalf;
				B.AddQuad(LidArcPoint(LidRadius + 2.5, Th0, Y0), LidArcPoint(LidRadius + 2.5, Th1, Y0), LidArcPoint(LidRadius + 2.5, Th1, Y1),
					LidArcPoint(LidRadius + 2.5, Th0, Y1), Out, Iron);
				B.AddQuad(LidArcPoint(LidRadius - 0.5, Th0, Y0), LidArcPoint(LidRadius - 0.5, Th1, Y0), LidArcPoint(LidRadius + 2.5, Th1, Y0),
					LidArcPoint(LidRadius + 2.5, Th0, Y0), FVector(0.0, -1.0, 0.0), Iron);
				B.AddQuad(LidArcPoint(LidRadius - 0.5, Th0, Y1), LidArcPoint(LidRadius - 0.5, Th1, Y1), LidArcPoint(LidRadius + 2.5, Th1, Y1),
					LidArcPoint(LidRadius + 2.5, Th0, Y1), FVector(0.0, 1.0, 0.0), Iron);
			}
		}

		// Cantos de abajo (delante y detrás): se ven con la tapa abierta.
		for (const double Edge : { 0.0, UE_DOUBLE_PI })
		{
			B.AddQuad(LidArcPoint(RIn, Edge, -LidSide), LidArcPoint(LidRadius, Edge, -LidSide), LidArcPoint(LidRadius, Edge, LidSide),
				LidArcPoint(RIn, Edge, LidSide), FVector(0.0, 0.0, -1.0), WoodDark);
		}

		// Remaches de los flejes y bisagras detrás.
		for (const double Sy : { -1.0, 1.0 })
		{
			for (const int32 Step : { 1, 4, 7 })
			{
				const double Th = UE_DOUBLE_PI * Step / LidSegments;
				AddRivet(B, LidArcPoint(LidRadius + 2.5, Th, Sy * StrapY), FVector(FMath::Cos(Th), 0.0, FMath::Sin(Th)), Gold);
			}
			TNProcMesh::TNProcAddCylinder(B, FVector(-1.5, Sy * StrapY - 9.0, 0.5), FVector(-1.5, Sy * StrapY + 9.0, 0.5), 3.6, 3.6, 8, Iron);
		}

		// Labio dorado por el canto de delante y pasador que baja sobre la cerradura.
		const double FrontX = ChestHalfD + LidRadius;
		AddSlab(B, FrontX - 3.0, FrontX + 1.5, -LidSide - 1.0, LidSide + 1.0, 0.0, 7.0, Gold);
		AddSlab(B, FrontX + 1.0, 2.0 * ChestHalfD + 8.5, -6.5, 6.5, -19.0, 4.0, Gold);
		AddRivet(B, FVector(2.0 * ChestHalfD + 8.5, 0.0, -3.0), FVector(1.0, 0.0, 0.0), GoldDeep);

		// Emblema: vieira dorada en el segundo tablón de delante.
		{
			const double ThA = UE_DOUBLE_PI / LidSegments;
			const double ThB = 2.0 * UE_DOUBLE_PI / LidSegments;
			const FVector PA = LidArcPoint(LidRadius, ThA, 0.0);
			const FVector PB = LidArcPoint(LidRadius, ThB, 0.0);
			const double ThC = (ThA + ThB) * 0.5;
			const FVector Along = (PB - PA).GetSafeNormal();
			TNCastleKit::AddScallop(B, (PA + PB) * 0.5 - Along * 3.0, FVector(FMath::Cos(ThC), 0.0, FMath::Sin(ThC)), Along, 10.5, Gold);
		}

		// De la playa: percebes por encima (entre los flejes) y una tira de alga que cruza la tapa y cuelga por delante.
		for (int32 i = 0; i < 7; ++i)
		{
			const double Th = UE_DOUBLE_PI * (0.25 + 0.6 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(i, 5, 0xBA27u)));
			const double Y = (StrapY - StrapHalf - 6.0) * TNProcMesh::TNProcHashNoise(i, 6, 0xBA27u) + (i % 2 ? 14.0 : -14.0);
			const FVector Out(FMath::Cos(Th), 0.0, FMath::Sin(Th));
			AddBarnacle(B, LidArcPoint(LidRadius + 0.5, Th, FMath::Clamp(Y, -LidSide + 6.0, LidSide - 6.0)), Out,
				2.4 + 1.2 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(i, 7, 0xBA27u)));
		}
		{
			const FLinearColor GreenA = TNCastleKit::Pal(0x3F7D3A);
			const FLinearColor GreenB = TNCastleKit::Pal(0x2F6B45);
			constexpr double WeedY = -StrapY * 0.35;
			constexpr double WeedHalf = 4.2;
			constexpr int32 Steps = 10;
			for (int32 s = 0; s < Steps; ++s)
			{
				const double Th0 = UE_DOUBLE_PI * (1.0 - static_cast<double>(s) / Steps);
				const double Th1 = UE_DOUBLE_PI * (1.0 - static_cast<double>(s + 1) / Steps);
				const double Y0 = WeedY + 4.0 * FMath::Sin(s * 1.3);
				const double Y1 = WeedY + 4.0 * FMath::Sin((s + 1) * 1.3);
				const FVector Out(FMath::Cos((Th0 + Th1) * 0.5), 0.0, FMath::Sin((Th0 + Th1) * 0.5));
				B.AddQuad(LidArcPoint(LidRadius + 1.4, Th0, Y0 - WeedHalf), LidArcPoint(LidRadius + 1.4, Th0, Y0 + WeedHalf),
					LidArcPoint(LidRadius + 1.4, Th1, Y1 + WeedHalf), LidArcPoint(LidRadius + 1.4, Th1, Y1 - WeedHalf), Out, (s % 2) ? GreenA : GreenB);
			}
			// Y cuelga por delante del labio.
			const FVector Hang = LidArcPoint(LidRadius + 1.4, 0.0, WeedY + 4.0 * FMath::Sin(Steps * 1.3));
			AddSeaweedStrand(B, Hang, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), 22.0, WeedHalf, 7);
		}
	}

	/** Las tres mallas (caja, tesoro y tapa), iguales para todos los cofres: se hacen una vez y se comparten. */
	UStaticMesh* SharedMesh(int32 Piece)
	{
		static TWeakObjectPtr<UStaticMesh> Cached[3];
		if (Piece < 0 || Piece > 2)
		{
			return nullptr;
		}
		if (Cached[Piece].IsValid())
		{
			return Cached[Piece].Get();
		}
		UMaterialInterface* Mat = TNCastleKit::VertexColorMaterial();
		if (!Mat)
		{
			return nullptr;
		}
		FBuffers B;
		if (Piece == 0) { BuildBody(B); }
		else if (Piece == 1) { BuildTreasure(B); }
		else { BuildLid(B); }
		UStaticMesh* Built = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, Mat);
		if (Built)
		{
			// Fuera del recolector, como las mallas compartidas de TNLootGlow.
			Built->AddToRoot();
			Cached[Piece] = Built;
		}
		return Built;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachChest (el elemento del reparto)
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachChest::ATN_BeachChest()
{
	// Solo en el servidor: lo que se ve y se replica es su cofre (ATN_BeachChestSpot).
	bReplicates = false;
}

void ATN_BeachChest::ApplySpec()
{
	UWorld* World = GetWorld();
	if (!World || IsValid(ChestSpot))
	{
		return;
	}
	// En la partida, solo el servidor (o quien no sea un cliente: en un cliente, un elemento local no crea otro cofre).
	const bool bEditorPreview = !World->IsGameWorld();
	if (!bEditorPreview && World->GetNetMode() == NM_Client)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (bEditorPreview)
	{
		// Ronda de prueba del editor: no se guarda con el nivel.
		Params.ObjectFlags |= RF_Transient;
	}
	const FTransform Where(FRotator(0.0, GetActorRotation().Yaw, 0.0), GetActorLocation());
	ChestSpot = World->SpawnActor<ATN_BeachChestSpot>(ATN_BeachChestSpot::StaticClass(), Where, Params);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] cofre %s en %s."), *GetNameSafe(ChestSpot), *Where.GetLocation().ToString());
}

ATN_BeachChestSpot* ATN_BeachChest::GetChestSpot() const
{
	return ChestSpot;
}

void ATN_BeachChest::Destroyed()
{
	// Con el elemento se va su cofre (y este, lo que soltó y nadie cogió).
	if (IsValid(ChestSpot))
	{
		ChestSpot->Destroy();
	}
	ChestSpot = nullptr;
	Super::Destroyed();
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachChestSpot (el cofre que se ve y se abre)
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachChestSpot::ATN_BeachChestSpot()
{
	using namespace TNBeachChestDetail;
	PromptText = NSLOCTEXT("Tortunabo", "BeachChestPrompt", "Mantén para abrir el cofre");
	// Más largo que un rebuscable y siempre con premio; una vez por ronda para todas.
	SearchSeconds = OpenSeconds;
	LootChance = 1.f;
	bRepeatable = false;
	// Suena a chismes y monedas más que a arena.
	RummagePitch = 1.4f;
	// Grande y a la escala de la playa: chispitas desde más lejos. El anillo fijo abarca su huella (SpotRadius) y se ve,
	// como el de los objetos del suelo, desde 90 m.
	HintDistance = 5000.f;
	// La playa mide 0,8 km: se ve (y su columna de luz) desde lejos; lo de serie son 150 m.
	SetNetCullDistanceSquared(FMath::Square(40000.f));

	const FVector ChestScale3D(ChestScale);
	// Mallas sin colisión (la pone ChestBlock), con las medidas del cofre del lobby y escaladas.
	ChestBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBody"));
	ChestBody->SetupAttachment(SceneRoot);
	ChestBody->SetRelativeScale3D(ChestScale3D);
	ChestBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChestBody->SetCanEverAffectNavigation(false);

	ChestTreasure = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestTreasure"));
	ChestTreasure->SetupAttachment(SceneRoot);
	ChestTreasure->SetRelativeScale3D(ChestScale3D);
	ChestTreasure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChestTreasure->SetCanEverAffectNavigation(false);
	ChestTreasure->SetCastShadow(false);

	ChestLid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestLid"));
	ChestLid->SetupAttachment(SceneRoot);
	ChestLid->SetRelativeLocation(FVector(-ChestHalfD * ChestScale, 0.0, ChestBodyH * ChestScale));
	ChestLid->SetRelativeScale3D(ChestScale3D);
	ChestLid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChestLid->SetCanEverAffectNavigation(false);

	// Caja de colisión del cofre cerrado.
	ChestBlock = CreateDefaultSubobject<UBoxComponent>(TEXT("ChestBlock"));
	ChestBlock->SetupAttachment(SceneRoot);
	ChestBlock->InitBoxExtent(FVector((ChestHalfD + 4.0) * ChestScale, (ChestHalfW + 4.0) * ChestScale, ChestTopZ * ChestScale * 0.5));
	ChestBlock->SetRelativeLocation(FVector(0.0, 0.0, ChestTopZ * ChestScale * 0.5));
	ChestBlock->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	ChestBlock->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	ChestBlock->SetCanEverAffectNavigation(false);
	ChestBlock->SetHiddenInGame(true);

	// Brillo dorado de dentro, sin sombras.
	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(SceneRoot);
	GlowLight->SetRelativeLocation(FVector(0.0, 0.0, (ChestBodyH + 6.0) * ChestScale));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(0.f);
	GlowLight->SetAttenuationRadius(static_cast<float>(420.0 * ChestScale));
	GlowLight->SetLightColor(FLinearColor(1.f, 0.74f, 0.32f));
	GlowLight->SetCastShadows(false);
	GlowLight->SetVisibility(false);
}

void ATN_BeachChestSpot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildChestMeshes();
}

void ATN_BeachChestSpot::BeginPlay()
{
	using namespace TNBeachChestDetail;
	if (HasAuthority())
	{
		// Huella redonda con el borde un palmo por delante del frente y color del «polvo»: oro (monedas y destellos).
		SetupSpot(SpotRadius, 0.f, static_cast<float>(ChestTopZ * ChestScale), FLinearColor(1.f, 0.8f, 0.36f));
	}
	Super::BeginPlay();
	// Las mallas no se duplican con el PIE: la copia llega sin ellas.
	if (ChestBody && !ChestBody->GetStaticMesh())
	{
		BuildChestMeshes();
	}
}

void ATN_BeachChestSpot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Al quitar el cofre (ronda nueva), los premios que nadie ha cogido se van con él.
	if (HasAuthority() && EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (AActor* Prize : Prizes)
		{
			if (IsValid(Prize))
			{
				Prize->Destroy();
			}
		}
	}
	Prizes.Reset();
	PrizeLandings.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachChestSpot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachChestSpot, Prizes);
	DOREPLIFETIME(ATN_BeachChestSpot, PrizeLandings);
}

void ATN_BeachChestSpot::BuildChestMeshes()
{
	using namespace TNBeachChestDetail;
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !ChestBody || !ChestTreasure || !ChestLid)
	{
		return;
	}
	// Piezas de arte (Docs/Arte_Assets.md): la tapa gira en su bisagra y el tesoro se ve al abrirse; las mallas de arte van con ellos.
	TNArt::SetMesh(ChestBody, SharedMesh(0), TN_ART("Beach.Chest.Body"));
	TNArt::SetMesh(ChestTreasure, SharedMesh(1), TN_ART("Beach.Chest.Treasure"));
	TNArt::SetMesh(ChestLid, SharedMesh(2), TN_ART("Beach.Chest.Lid"));
}

void ATN_BeachChestSpot::EnsureBeacon()
{
	using namespace TNBeachChestDetail;
	const UWorld* World = GetWorld();
	if (BeaconBeam || !World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UStaticMesh* BeamAsset = TNLootGlow::BeamMesh();
	if (!BeamAsset)
	{
		return;
	}
	UStaticMeshComponent* Beam = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Beam->SetupAttachment(SceneRoot);
	Beam->SetStaticMesh(BeamAsset);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetGenerateOverlapEvents(false);
	Beam->SetCanEverAffectNavigation(false);
	Beam->SetCastShadow(false);
	Beam->SetReceivesDecals(false);
	Beam->SetCullDistance(BeamDrawDistance);
	Beam->SetRelativeLocation(FVector(0.0, 0.0, ChestBodyH * ChestScale));
	Beam->SetRelativeScale3D(FVector(BeamRadiusScale, BeamRadiusScale, BeamHeightScale));
	Beam->RegisterComponent();
	BeaconBeam = Beam;
}

// ── Reglas del cofre ─────────────────────────────────────────────────────────

float ATN_BeachChestSpot::ChestWeight(FName RowName, const FTN_InventoryItem& Row)
{
	switch (Row.UseType)
	{
		// Lo mejor para avanzar: energía sin fin unos segundos (dejar atrás a todas y a la tormenta) o la barra llena.
		case ETN_ItemUseType::SelfStaminaBoost: return 3.f;
		case ETN_ItemUseType::SelfStaminaFull:  return 2.2f;
		// Para quitarse de delante a las demás: la bola derriba y la tinta ciega.
		case ETN_ItemUseType::Throwable:        return 1.6f;
		case ETN_ItemUseType::InkThrower:       return 1.6f;
		// La concha trampa es para las de detrás: ayuda menos a avanzar.
		case ETN_ItemUseType::Conch:            return 0.6f;
		// Nada que no sirva en la carrera: la cabezota (en la playa no protege) y el tótem (no se muere), nunca.
		case ETN_ItemUseType::BigHead:
		case ETN_ItemUseType::Totem:            return 0.f;
		// Un uso nuevo, con el peso de la carrera.
		default:                                return TNBeachLoot::RaceWeight(RowName, Row);
	}
}

float ATN_BeachChestSpot::GetHoldDuration() const
{
	// Siempre los suyos (5,5 s): tn.Search.Seconds es para los rebuscables.
	return SearchSeconds;
}

float ATN_BeachChestSpot::GetLuck() const
{
	// Siempre hay premio: tn.Search.Luck es para los rebuscables.
	return LootChance;
}

float ATN_BeachChestSpot::GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const
{
	return ChestWeight(RowName, Row);
}

FVector ATN_BeachChestSpot::GetMouthPoint() const
{
	using namespace TNBeachChestDetail;
	return GetActorTransform().TransformPosition(FVector(0.0, 0.0, ChestBodyH * ChestScale));
}

FVector ATN_BeachChestSpot::GetLootOrigin(const APawn* /*Pawn*/) const
{
	using namespace TNBeachChestDetail;
	// De dentro del cofre, algo por delante del centro (la tapa se abre hacia atrás).
	return GetActorTransform().TransformPosition(FVector(12.0 * ChestScale, 0.0, (ChestBodyH + 8.0) * ChestScale));
}

FVector ATN_BeachChestSpot::GetRummageOrigin(const APawn* Searcher) const
{
	using namespace TNBeachChestDetail;
	// Del borde de la boca que da a quien lo abre: monedas y chismes que saltan hacia ella.
	const FVector Mouth = GetMouthPoint();
	FVector Toward = Searcher ? (Searcher->GetActorLocation() - Mouth).GetSafeNormal2D() : FVector::ZeroVector;
	if (Toward.IsNearlyZero())
	{
		Toward = GetActorForwardVector();
	}
	return Mouth + Toward * (25.0 * ChestScale) + FVector(0.0, 0.0, 4.0 * ChestScale);
}

double ATN_BeachChestSpot::BodyReach(const FVector& Dir) const
{
	using namespace TNBeachChestDetail;
	const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Dir.GetSafeNormal2D());
	const double Ax = FMath::Max(FMath::Abs(Local.X), 1e-3);
	const double Ay = FMath::Max(FMath::Abs(Local.Y), 1e-3);
	return FMath::Min((ChestHalfD + 6.0) * ChestScale / Ax, (ChestHalfW + 6.0) * ChestScale / Ay);
}

FVector ATN_BeachChestSpot::GroundNear(const FVector& Target, const APawn* Ignore) const
{
	using namespace TNBeachChestDetail;
	const FVector Base = GetActorLocation();
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FVector(Target.X, Target.Y, Base.Z);
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_BeachChestLanding), false, this);
	if (Ignore)
	{
		Query.AddIgnoredActor(Ignore);
	}
	const FVector2D Offset(Target.X - Base.X, Target.Y - Base.Y);
	const double Top = Base.Z + ChestTopZ * ChestScale + 150.0;
	// El suelo de verdad cerca de la cota del cofre; si no (el borde de lo alto de una fortaleza, una roca alta), más cerca.
	for (const double Pull : { 1.0, 0.78, 0.6 })
	{
		const FVector2D At = FVector2D(Base.X, Base.Y) + Offset * Pull;
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, FVector(At.X, At.Y, Top), FVector(At.X, At.Y, Base.Z - 600.0),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query) && FMath::Abs(Hit.ImpactPoint.Z - Base.Z) < GroundTolerance)
		{
			return Hit.ImpactPoint;
		}
	}
	return FVector(Target.X, Target.Y, Base.Z);
}

FVector ATN_BeachChestSpot::FindLanding(const APawn* Pawn, const FVector& /*From*/) const
{
	using namespace TNBeachChestDetail;
	// El objeto de siempre: por delante del cofre, hacia quien lo abre y algo de lado.
	const FVector Base = GetActorLocation();
	FVector Out = Pawn ? (Pawn->GetActorLocation() - Base).GetSafeNormal2D() : FVector::ZeroVector;
	if (Out.IsNearlyZero())
	{
		Out = GetActorForwardVector().GetSafeNormal2D();
	}
	if (Out.IsNearlyZero())
	{
		Out = FVector::ForwardVector;
	}
	const FVector Side(-Out.Y, Out.X, 0.0);
	const FVector Target = Base + Out * (BodyReach(Out) + FMath::FRandRange(ItemRingMin, ItemRingMax)) + Side * FMath::FRandRange(-60.f, 60.f);
	return GroundNear(Target, Pawn) + FVector(0.0, 0.0, 5.0);
}

void ATN_BeachChestSpot::SpawnPrizes(const APawn* Opener)
{
	using namespace TNBeachChestDetail;
	UWorld* World = GetWorld();
	if (!World || Prizes.Num() > 0)
	{
		return;
	}
	const FVector Base = GetActorLocation();
	FVector Front = Opener ? (Opener->GetActorLocation() - Base).GetSafeNormal2D() : FVector::ZeroVector;
	if (Front.IsNearlyZero())
	{
		Front = GetActorForwardVector().GetSafeNormal2D();
	}
	const double FrontYaw = FMath::RadiansToDegrees(FMath::Atan2(Front.Y, Front.X));

	// Conchas: cuatro de 25, una de 50 y otra de 50 o, a veces, una reina de 100, en orden al azar alrededor.
	TArray<int32> Values(ShellValues, static_cast<int32>(UE_ARRAY_COUNT(ShellValues)));
	Values.Add(FMath::FRand() < GrandShellChance ? 100 : 50);
	for (int32 i = Values.Num() - 1; i > 0; --i)
	{
		Values.Swap(i, FMath::RandRange(0, i));
	}
	UClass* ShellClass = UTN_GameplayAssetSettings::GetScorePickupClass();
	const UDataTable* Table = GetLootTable();

	// En corona alrededor del cofre, dejando libre el frente de quien lo ha abierto (ahí cae el objeto de siempre): el
	// objeto de más, al lado de ese hueco; las conchas, repartidas por el resto.
	const int32 Slots = BonusItems + Values.Num();
	const double StepDeg = (360.0 - 2.0 * FrontGapDeg) / FMath::Max(1, Slots - 1);
	int32 Shells = 0;
	int32 Points = 0;
	FString ItemNames;
	for (int32 k = 0; k < Slots; ++k)
	{
		const double Yaw = FMath::DegreesToRadians(FrontYaw + FrontGapDeg + StepDeg * k + FMath::FRandRange(-8.f, 8.f));
		const FVector Dir(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0);
		const bool bItem = k < BonusItems;
		const double Ring = bItem ? FMath::FRandRange(ItemRingMin, ItemRingMax) : FMath::FRandRange(ShellRingMin, ShellRingMax);
		const FVector Ground = GroundNear(Base + Dir * (BodyReach(Dir) + Ring), Opener);
		AActor* Prize = nullptr;
		FVector Landing = Ground;
		if (bItem)
		{
			FTN_InventoryItem Item;
			// El objeto de más, sorteado según el puesto de quien abre el cofre (pesos del cofre: lo mejor para avanzar).
			if (TNRaceItems::RollLoot(Opener, ETNRaceLootSource::Chest, Table, Item) && Item.PickupActorClass)
			{
				Landing = Ground + FVector(0.0, 0.0, 5.0);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(Item.PickupActorClass, Landing,
					FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0), Params);
				if (Pickup)
				{
					Pickup->InitializeFromInventoryItem(Item);
					Prize = Pickup;
					ItemNames += (ItemNames.IsEmpty() ? TEXT("") : TEXT(", ")) + Item.ItemId.ToString();
				}
			}
		}
		else
		{
			// La concha de siempre (el Blueprint, con su valor puesto antes de aparecer; sin él, la clase nativa).
			const int32 Value = Values[k - BonusItems];
			Landing = Ground + FVector(0.0, 0.0, TNScoreShells::Hover);
			const FTransform Where(FRotator(0.0, GetActorRotation().Yaw, 0.0), Landing);
			ATN_ScorePickup* Shell = World->SpawnActorDeferred<ATN_ScorePickup>(ShellClass, Where, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Shell)
			{
				Shell->SetScoreValue(Value);
				Shell->FinishSpawning(Where);
				Prize = Shell;
				++Shells;
				Points += Value;
			}
		}
		if (Prize)
		{
			Prizes.Add(Prize);
			PrizeLandings.Add(FVector_NetQuantize10(Landing));
		}
	}
	ForceNetUpdate();
	const FTNSearchSpotState& State = GetSearchState();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] cofre %s abierto por %s: %s y, de más, %s y %d conchas (%d puntos)."), *GetName(), *GetNameSafe(Opener),
		State.LootPickup ? *GetNameSafe(State.LootPickup) : TEXT("sin objeto"), ItemNames.IsEmpty() ? TEXT("ningún objeto") : *ItemNames, Shells, Points);
}

// ── Estado replicado: tapa, premios, luz y sonidos ───────────────────────────

void ATN_BeachChestSpot::OnSearchStateChanged(const FTNSearchSpotState& OldState)
{
	using namespace TNBeachChestDetail;
	const FTNSearchSpotState& State = GetSearchState();
	const bool bNewOutcome = State.Outcome != ETNSearchOutcome::None && State.SearchCount != OldState.SearchCount;
	// Servidor: al abrirse, el objeto de más y las conchas (despierto: se replican con el resultado).
	if (bNewOutcome && HasAuthority())
	{
		SpawnPrizes(OldState.Searcher.Get());
	}
	// La tapa, los premios y la luz se mueven con cada cambio: tick a cada fotograma hasta que se quede quieto.
	bPrizesSettled = false;
	SetActorTickInterval(0.f);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (State.Searcher && !OldState.Searcher)
	{
		// Alguien empieza a abrirlo: la tapa cruje al entreabrirse.
		PlaySearchSound(ETNSearchSound::LidCreak, FMath::FRandRange(0.8f, 0.92f), 1.f, GetMouthPoint());
		CreakClock = FMath::FRandRange(0.9f, 1.4f);
	}
	if (bNewOutcome)
	{
		if (ServerNow() - static_cast<double>(State.OutcomeTime) < FreshSeconds)
		{
			// ¡Se abre! La tapa salta hacia atrás (y rebota hasta quedarse abierta) con un chorro de chispas doradas.
			LidSpeed += LidPopKick;
			PlaySearchSound(ETNSearchSound::LidCreak, 0.7f, 1.f, GetMouthPoint());
			EmitSparkles(GetMouthPoint() + FVector(0.0, 0.0, 30.0), 18, FVector::UpVector, 3.f);
		}
		else
		{
			// Llega tarde (esta máquina lo ve ya abierto): abierto del todo, sin animación.
			LidAngle = LidOpenDeg;
			LidSpeed = 0.f;
		}
	}
}

void ATN_BeachChestSpot::OnRep_Prizes()
{
	// Llegan premios (o la red resuelve alguno que faltaba): a moverlos o, si ya han caído, a ponerlos en su sitio.
	bPrizesSettled = false;
	SetActorTickInterval(0.f);
}

bool ATN_BeachChestSpot::WantsFrameTick() const
{
	using namespace TNBeachChestDetail;
	const bool bLidMoving = FMath::Abs(LidAngle - LidTarget) > 0.05f || FMath::Abs(LidSpeed) > 0.05f;
	if (bLidMoving || bPrizeHopsActive || (bChestNearView && !IsSearched()))
	{
		return true;
	}
	if (IsSearched())
	{
		// Recién abierto: los saltos, el fogonazo y la columna que se va.
		const double Since = ServerNow() - static_cast<double>(GetSearchState().OutcomeTime);
		return Since < FMath::Max3(PrizeWindow(Prizes.Num()), FlashSeconds, BeamFadeSeconds);
	}
	return false;
}

void ATN_BeachChestSpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickPrizeHops();
	if (GetNetMode() != NM_DedicatedServer)
	{
		TickChest(DeltaSeconds);
	}
}

void ATN_BeachChestSpot::TickChest(float DeltaSeconds)
{
	using namespace TNBeachChestDetail;
	const FTNSearchSpotState& State = GetSearchState();
	const double Now = ServerNow();
	const float Dt = FMath::Min(DeltaSeconds, 0.25f);
	ChestClock += Dt;
	ThumpCooldown = FMath::Max(0.f, ThumpCooldown - Dt);
	const bool bSearching = State.Searcher != nullptr;
	const bool bOpened = IsSearched();
	const double SinceOutcome = bOpened ? Now - static_cast<double>(State.OutcomeTime) : 1.0e6;

	// Con la cámara cerca, la luz late a cada fotograma (lejos basta el tick lento).
	bChestNearView = TNLocalViews::ClosestCameraDistance(GetWorld(), GetActorLocation()) < NearViewDistance;

	// Adónde va la tapa según el estado replicado (igual en todas las máquinas): entreabierta a tirones mientras se abre
	// (más cuanto más se lleva), abierta del todo en cuanto se vacía (y así se queda) y, si no, cerrada.
	float Progress = 0.f;
	float Target = 0.f;
	if (bSearching)
	{
		const double Hold = FMath::Max(0.2, static_cast<double>(GetHoldDuration()));
		Progress = FMath::Clamp(static_cast<float>((Now - static_cast<double>(State.SearchStart)) / Hold), 0.f, 1.f);
		Target = FMath::Lerp(LidPryStartDeg, LidPryEndDeg, Progress);
	}
	else if (bOpened)
	{
		Target = LidOpenDeg;
	}
	LidTarget = Target;

	// Muelle con algo de rebote, en pasos cortos (estable aunque un fotograma tarde).
	float Remaining = Dt;
	while (Remaining > 0.f)
	{
		const float Step = FMath::Min(Remaining, 1.f / 90.f);
		Remaining -= Step;
		LidSpeed += (LidStiffness * (Target - LidAngle) - LidDamping * LidSpeed) * Step;
		LidAngle += LidSpeed * Step;
		if (LidAngle > LidMaxDeg)
		{
			LidAngle = LidMaxDeg;
			LidSpeed = FMath::Min(LidSpeed, 0.f);
		}
		if (LidAngle < 0.f)
		{
			// Cae sobre la caja (se ha soltado E antes de tiempo): «¡clonc!» si viene con fuerza y un rebote corto.
			if (LidSpeed < -LidThumpSpeed && ThumpCooldown <= 0.f)
			{
				PlaySearchSound(ETNSearchSound::LidThump, FMath::FRandRange(0.8f, 0.9f), FMath::Clamp(-LidSpeed / 400.f, 0.35f, 1.f), GetMouthPoint());
				ThumpCooldown = 0.3f;
			}
			LidAngle = 0.f;
			LidSpeed = -LidSpeed * 0.18f;
			if (LidSpeed < 8.f)
			{
				LidSpeed = 0.f;
			}
		}
	}
	// Mientras se abre, la tapa pesa y se resiste: tiembla (más al principio).
	float Shown = LidAngle;
	if (bSearching && LidAngle > 4.f)
	{
		Shown += (2.2f + 2.f * (1.f - Progress)) * FMath::Sin(ChestClock * 27.f) * (0.55f + 0.45f * FMath::Sin(ChestClock * 4.3f));
	}
	if (ChestLid)
	{
		ChestLid->SetRelativeRotation(FRotator(Shown, 0.f, 0.f));
	}

	// Vacío en cuanto saltan los premios.
	if (ChestTreasure)
	{
		const bool bFull = !bOpened || SinceOutcome < TreasureHideDelay;
		if (ChestTreasure->IsVisible() != bFull)
		{
			ChestTreasure->SetVisibility(bFull);
		}
	}

	// Luz de dentro: por abrir, late por la rendija y sube con la tapa; al vaciarse, un fogonazo y queda apagada.
	if (GlowLight)
	{
		float Lumens = SpentLumens;
		if (!bOpened)
		{
			const float Opening = FMath::Clamp(Shown / 40.f, 0.f, 1.f);
			const float Pulse = 0.7f + 0.3f * FMath::Sin(ChestClock * 2.4f);
			const float Flicker = 1.f + 0.08f * FMath::Sin(ChestClock * 13.f) + 0.05f * FMath::Sin(ChestClock * 31.f + 1.3f);
			Lumens = IdleLumens * Pulse + SearchLumens * Opening * Flicker;
		}
		else if (SinceOutcome < FlashSeconds)
		{
			Lumens = FMath::Lerp(FlashLumens, SpentLumens, static_cast<float>(SinceOutcome / FlashSeconds));
		}
		const bool bLit = Lumens > 5.f;
		if (GlowLight->IsVisible() != bLit)
		{
			GlowLight->SetVisibility(bLit);
		}
		if (bLit)
		{
			GlowLight->SetIntensity(Lumens);
		}
	}

	// Columna de luz: mientras está por abrir; al abrirse, se estrecha y se va.
	const float BeamK = !bOpened ? 1.f : FMath::Clamp(1.f - static_cast<float>(SinceOutcome / BeamFadeSeconds), 0.f, 1.f);
	if (BeamK > 0.f)
	{
		EnsureBeacon();
	}
	if (BeaconBeam)
	{
		const bool bBeam = BeamK > 0.01f;
		if (BeaconBeam->IsVisible() != bBeam)
		{
			BeaconBeam->SetVisibility(bBeam);
		}
		if (bBeam && bOpened)
		{
			BeaconBeam->SetRelativeScale3D(FVector(BeamRadiusScale * BeamK, BeamRadiusScale * BeamK, BeamHeightScale));
		}
	}

	// Destellos que suben de dentro con la tapa abierta, mientras se abre y al salir los premios.
	if (Shown > 12.f && (bSearching || SinceOutcome < FreshSeconds))
	{
		GlintClock -= Dt;
		if (GlintClock <= 0.f)
		{
			GlintClock = FMath::FRandRange(0.1f, 0.22f);
			const FVector Glint = GetActorTransform().TransformPosition(FVector(FMath::FRandRange(-30.f, 30.f) * ChestScale,
				FMath::FRandRange(-55.f, 55.f) * ChestScale, (ChestBodyH - 6.0) * ChestScale));
			EmitSparkles(Glint, 2, FVector::UpVector, 1.6f);
		}
	}

	// Crujidos de vez en cuando mientras se fuerza la tapa.
	if (bSearching)
	{
		CreakClock -= Dt;
		if (CreakClock <= 0.f)
		{
			CreakClock = FMath::FRandRange(0.8f, 1.4f);
			PlaySearchSound(ETNSearchSound::LidCreak, FMath::FRandRange(0.75f, 1.f), FMath::FRandRange(0.4f, 0.6f), GetMouthPoint());
		}
	}
}

void ATN_BeachChestSpot::TickPrizeHops()
{
	using namespace TNBeachChestDetail;
	bPrizeHopsActive = false;
	const int32 Count = FMath::Min3(Prizes.Num(), PrizeLandings.Num(), 32);
	if (!IsSearched() || Count == 0)
	{
		return;
	}
	const double Since = ServerNow() - static_cast<double>(GetSearchState().OutcomeTime);
	if (Since >= PrizeWindow(Count))
	{
		// Ya han caído todos: quien llega tarde (o recibe un premio a medio salto) los ve en su sitio.
		if (!bPrizesSettled)
		{
			bPrizesSettled = true;
			for (int32 k = 0; k < Count; ++k)
			{
				AActor* Prize = Prizes[k];
				const uint32 Bit = 1u << k;
				if (IsValid(Prize) && (PrizeLandedMask & Bit) == 0)
				{
					Prize->SetActorLocation(PrizeLandings[k]);
					Prize->SetActorScale3D(FVector::OneVector);
					PrizeLandedMask |= Bit;
				}
			}
		}
		return;
	}
	const bool bScreen = GetNetMode() != NM_DedicatedServer;
	const FVector From = GetMouthPoint() + FVector(0.0, 0.0, 30.0);
	for (int32 k = 0; k < Count; ++k)
	{
		AActor* Prize = Prizes[k];
		const uint32 Bit = 1u << k;
		if (!IsValid(Prize) || (PrizeLandedMask & Bit) != 0)
		{
			continue;
		}
		const FVector To = PrizeLandings[k];
		const double T = Since - (PrizeDelay + PrizeStagger * k);
		if (T < 0.0)
		{
			// Aún dentro del cofre, pequeño en la boca.
			Prize->SetActorLocation(From);
			Prize->SetActorScale3D(FVector(PrizeStartScale));
			bPrizeHopsActive = true;
			continue;
		}
		const double Alpha = FMath::Clamp(T / PrizeHopSeconds, 0.0, 1.0);
		if (Alpha >= 1.0)
		{
			// Cae en su sitio (el mismo que en el servidor), a tamaño real y con unas chispitas.
			Prize->SetActorLocation(To);
			Prize->SetActorScale3D(FVector::OneVector);
			PrizeLandedMask |= Bit;
			if (bScreen)
			{
				EmitSparkles(To, 3, FVector::UpVector, 1.2f);
				if (k < 2)
				{
					PlaySearchSound(ETNSearchSound::Rummage, 1.5f, 0.4f, To);
				}
			}
			continue;
		}
		// Parábola alta desde la boca, creciendo al asomar.
		const FVector Position = FMath::Lerp(From, To, Alpha) + FVector(0.0, 0.0, 4.0 * PrizeApex * Alpha * (1.0 - Alpha));
		const double Grow = FMath::Min(1.0, Alpha / 0.3);
		Prize->SetActorLocation(Position);
		Prize->SetActorScale3D(FVector(FMath::Lerp(PrizeStartScale, 1.0, 1.0 - FMath::Square(1.0 - Grow))));
		bPrizeHopsActive = true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Consola: TN.Beach.Chest (un cofre delante de tu tortuga)
// ─────────────────────────────────────────────────────────────────────────────

namespace TNBeachChestDetail
{
	/** El mundo con autoridad del mismo proceso (el propio si no es un cliente; en PIE, el del servidor del mismo mapa). */
	UWorld* FindAuthorityWorld(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		if (InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		if (!GEngine)
		{
			return nullptr;
		}
		const FString MapName = UWorld::RemovePIEPrefix(InWorld->GetMapName());
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate != InWorld && Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Client
				&& UWorld::RemovePIEPrefix(Candidate->GetMapName()) == MapName)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	void RunChest(const TArray<FString>& /*Args*/, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Chest: solo en el servidor (escríbelo en la ventana del anfitrión)."));
			return;
		}
		// Delante de la tortuga local de la ventana donde se escribe (la misma posición en el mundo del servidor).
		const APlayerController* PC = InWorld->GetFirstPlayerController();
		const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
		FVector From = FVector::ZeroVector;
		FRotator Facing = FRotator::ZeroRotator;
		if (Viewer)
		{
			From = Viewer->GetActorLocation();
			Facing = FRotator(0.0, Viewer->GetActorRotation().Yaw, 0.0);
		}
		else if (PC)
		{
			FRotator ViewRot;
			PC->GetPlayerViewPoint(From, ViewRot);
			Facing = FRotator(0.0, ViewRot.Yaw, 0.0);
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Chest: sin jugador local para saber dónde ponerlo."));
			return;
		}
		FVector At = From + Facing.Vector() * (TNBeach::FootprintRadius(ETNBeachElement::TreasureChest) + 250.0);
		FHitResult Hit;
		FCollisionObjectQueryParams Floors;
		Floors.AddObjectTypesToQuery(ECC_WorldStatic);
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(TNBeachChestCommand), false);
		At.Z = AuthWorld->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 3000.0), At - FVector(0.0, 0.0, 6000.0), Floors, Query)
			? Hit.ImpactPoint.Z : From.Z - 90.0;

		FTNBeachElementSpec ChestSpec;
		ChestSpec.Element = ETNBeachElement::TreasureChest;
		ChestSpec.Seed = FMath::Rand();
		// Con el frente (su +X) hacia la tortuga.
		ATN_BeachElement* Spawned = ATN_BeachElement::SpawnElement(AuthWorld, FTransform(FRotator(0.0, Facing.Yaw + 180.0, 0.0), At), ChestSpec);
		if (!Spawned)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Chest: no se ha podido crear el cofre."));
			return;
		}
		// La etiqueta de TN.Beach.Place: «TN.Beach.Place clear» también lo quita.
		Spawned->Tags.AddUnique(FName(TEXT("TNBeachDebug")));
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Chest: cofre %s en %s."), *Spawned->GetName(), *At.ToString());
	}

	static FAutoConsoleCommandWithWorldAndArgs ChestCommand(
		TEXT("TN.Beach.Chest"),
		TEXT("Pone un cofre de la playa delante de tu tortuga, con el frente hacia ella (en el anfitrión). «TN.Beach.Place clear» lo quita."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunChest),
		ECVF_Cheat);
}
