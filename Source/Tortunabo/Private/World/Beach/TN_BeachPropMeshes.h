#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "World/Beach/TN_BeachTypes.h"
#include "../ProcMap/TN_ProcMapMeshKit.h"

/**
 * Mallas del decorado gigante de la playa (ATN_BeachDecor, Docs/Modo_Carrera.md, «Decorado gigante»): low-poly de caras
 * planas con color de vértice (M_CosmeticVertexColor: el alfa es el brillo, 0 mate y 1 metal), todo a TNBeach::Scale
 * veces su tamaño real y dentro de la huella TNBeach::FootprintRadius del elemento. Cada receta llena un FParts: la malla
 * fija (con su colisión simple: cajas, esferas y cápsulas), la parte que se mueve (si la hay) y cómo se coloca cada
 * ejemplar (hundimiento, inclinación, giro). Local: base en el origen, Z arriba, cm de juego. Sin dependencias del motor
 * más allá de CoreMinimal: la malla estática y la colisión las monta TN_BeachDecor.cpp.
 */
namespace TNBeachProp
{
	using namespace TNProcMesh;

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Medidas, azar estable y colores
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Centímetros reales a centímetros de juego (TNBeach::Scale): Cm(15) es un coco de 15 cm. */
	inline constexpr double Cm(double RealCm) { return RealCm * TNBeach::Scale; }

	/** Mezcla estable de dos enteros (igual en todas las máquinas). */
	inline uint32 HashMix(uint32 A, uint32 B)
	{
		uint32 H = A * 0x9E3779B1u ^ (B + 0x7F4A7C15u + (A << 6) + (A >> 2));
		H ^= H >> 16;
		H *= 0x85EBCA6Bu;
		H ^= H >> 13;
		H *= 0xC2B2AE35u;
		H ^= H >> 16;
		return H;
	}

	/** Número estable en [0, 1) por semilla e índice. */
	inline double Rnd(uint32 Seed, int32 Index)
	{
		return static_cast<double>(HashMix(Seed, static_cast<uint32>(Index)) & 0xFFFFFFu) / 16777216.0;
	}

	/** Número estable en [Lo, Hi) por semilla e índice. */
	inline double RndIn(uint32 Seed, int32 Index, double Lo, double Hi)
	{
		return Lo + (Hi - Lo) * Rnd(Seed, Index);
	}

	/** Decodificación sRGB → lineal de un canal. */
	inline float BeachSrgbDecode(float Channel)
	{
		const float V = FMath::Clamp(Channel, 0.f, 1.f);
		return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f);
	}

	/**
	 * Color sRGB 0xRRGGBB para MakeStaticMesh con M_CosmeticVertexColor (se decodifica aquí y otra vez allí, como
	 * TNCastleKit::Pal, para que se vea como en las mallas procedurales). Shine es el alfa: 0 mate, 1 metal.
	 */
	inline FLinearColor Hex(uint32 Rgb, float Shine = 0.f)
	{
		return FLinearColor(BeachSrgbDecode(static_cast<float>((Rgb >> 16) & 255u) / 255.f),
			BeachSrgbDecode(static_cast<float>((Rgb >> 8) & 255u) / 255.f), BeachSrgbDecode(static_cast<float>(Rgb & 255u) / 255.f), Shine);
	}

	/** Oscurece (o aclara) un color sin tocar su brillo. */
	inline FLinearColor Shade(const FLinearColor& Color, float Factor)
	{
		return FLinearColor(Color.R * Factor, Color.G * Factor, Color.B * Factor, Color.A);
	}

	/** Mezcla de dos colores (también el brillo). */
	inline FLinearColor Blend(const FLinearColor& From, const FLinearColor& To, float T)
	{
		const float U = FMath::Clamp(T, 0.f, 1.f);
		return FLinearColor(From.R + (To.R - From.R) * U, From.G + (To.G - From.G) * U, From.B + (To.B - From.B) * U, From.A + (To.A - From.A) * U);
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Piezas: malla fija, parte animada, colisión simple y colocación de cada ejemplar
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Movimiento leve de la parte animada (local en cada máquina, ver ATN_BeachDecor). */
	enum class EAnim : uint8
	{
		None,
		/** Respira (escala que sube y baja) y tiembla de vez en cuando: la medusa. */
		Breathe,
		/** Se abre y se cierra de vez en cuando girando por la bisagra: la valva de la almeja. AnimRate = segundos del ciclo. */
		Clam,
		/** Ondea deprisa (giro y abombado): tela de la vela, banderitas. */
		Flutter,
		/** Se mece despacio con el viento: la red. */
		Sway
	};

	/** Cómo se anima y se coloca cada ejemplar (lo que se guarda en la caché junto a las mallas). */
	struct FPropInfo
	{
		EAnim Anim = EAnim::None;
		/** Pivote de la parte animada (coordenadas de la malla fija) y eje de giro (Clam, Flutter, Sway). */
		FVector AnimPivot = FVector::ZeroVector;
		FVector AnimAxis = FVector(0.0, 1.0, 0.0);
		/** Grados de giro (Clam, Flutter, Sway) o escala relativa (Breathe). */
		float AnimAmp = 0.f;
		/** Hz (Breathe, Flutter, Sway) o segundos de cada ciclo (Clam). */
		float AnimRate = 0.f;
		/** Hundimiento en la arena de cada ejemplar (cm de juego con SizeScale = 1), al azar entre los dos. */
		float SinkMin = 0.f;
		float SinkMax = 0.f;
		/** Inclinación máxima al azar de cada ejemplar (grados). */
		float TiltMax = 0.f;
		/** Giro libre (si no, solo YawJitter grados alrededor del +X del actor: la silla mira hacia donde la pongan). */
		bool bFreeYaw = true;
		float YawJitter = 0.f;
		/** Tapa la cámara (rocas, castillos, troncos): el resto la deja pasar para que no dé tirones. */
		bool bBlocksCamera = false;
		bool bCastShadow = true;
	};

	struct FColBox
	{
		FColBox(const FVector& InCenter, const FQuat& InRot, const FVector& InHalf) : Center(InCenter), Rot(InRot), Half(InHalf) {}
		FVector Center;
		FQuat Rot;
		FVector Half;
	};

	struct FColSphere
	{
		FColSphere(const FVector& InCenter, double InRadius) : Center(InCenter), Radius(InRadius) {}
		FVector Center;
		double Radius;
	};

	struct FColCapsule
	{
		FColCapsule(const FVector& InA, const FVector& InB, double InRadius) : A(InA), B(InB), Radius(InRadius) {}
		FVector A;
		FVector B;
		double Radius;
	};

	inline FQuat YawQ(double Deg)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(Deg));
	}

	/** Giro que lleva el eje Z al eje Dir (para tumbar cuerpos de revolución). */
	inline FQuat AxisZTo(const FVector& Dir)
	{
		return FQuat::FindBetweenNormals(FVector::UpVector, Dir.GetSafeNormal());
	}

	/** Lo que sale de una receta. */
	struct FParts
	{
		/** Malla fija (lleva la colisión). */
		FTNProcMeshBuffers Body;
		/** Parte que se mueve, sin colisión, en coordenadas de su pivote (ver AnimAround). */
		FTNProcMeshBuffers Moving;
		FPropInfo Info;
		TArray<FColBox> Boxes;
		TArray<FColSphere> Spheres;
		TArray<FColCapsule> Capsules;

		bool HasCollision() const { return Boxes.Num() + Spheres.Num() + Capsules.Num() > 0; }

		void ColBox(const FVector& Center, const FQuat& Rot, const FVector& Half) { Boxes.Emplace(Center, Rot, Half); }
		void ColBoxYaw(const FVector& Center, double YawDeg, const FVector& Half) { Boxes.Emplace(Center, YawQ(YawDeg), Half); }
		void ColSphere(const FVector& Center, double Radius) { Spheres.Emplace(Center, Radius); }
		void ColCapsule(const FVector& From, const FVector& To, double Radius) { Capsules.Emplace(From, To, Radius); }

		/**
		 * Prisma recto de 2N lados de apotema Apothem entre Z0 y Z1 (torres, rocas, cubos): la unión de N cajas finas de
		 * Apothem × Apothem·tan(90°/N) giradas 180°/N cada una cubre justo el polígono. YawDeg gira la primera cara.
		 */
		void ColPrism(const FVector& Center, double Apothem, double Z0, double Z1, double YawDeg = 0.0, int32 N = 4)
		{
			const double HalfSide = Apothem * FMath::Tan(UE_DOUBLE_PI / (2.0 * N));
			for (int32 k = 0; k < N; ++k)
			{
				ColBoxYaw(FVector(Center.X, Center.Y, (Z0 + Z1) * 0.5), YawDeg + 180.0 * k / N, FVector(Apothem, HalfSide, (Z1 - Z0) * 0.5));
			}
		}

		/** Pared en anillo (N cajas tangentes de grosor Thick por fuera del radio Radius): bordes de cuencos y vasos de pie. */
		void ColRing(const FVector& Center, double Radius, double Thick, double Z0, double Z1, int32 N = 8)
		{
			const double HalfLen = (Radius + Thick) * FMath::Tan(UE_DOUBLE_PI / N);
			for (int32 k = 0; k < N; ++k)
			{
				const double Deg = 360.0 * k / N;
				const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Deg)), FMath::Sin(FMath::DegreesToRadians(Deg)), 0.0);
				ColBoxYaw(FVector(Center.X, Center.Y, (Z0 + Z1) * 0.5) + Dir * (Radius + Thick * 0.5), Deg, FVector(Thick * 0.5, HalfLen, (Z1 - Z0) * 0.5));
			}
		}

		/**
		 * Tubo hueco de From a To: N cajas de pared de grosor Thick por dentro del radio Radius (vasos tumbados, troncos
		 * huecos). Con el eje en horizontal, una de las cajas queda justo abajo: el suelo de dentro es plano.
		 */
		void ColTube(const FVector& From, const FVector& To, double Radius, double Thick, int32 N = 8)
		{
			const FVector Ax = (To - From).GetSafeNormal();
			if (Ax.IsNearlyZero()) { return; }
			const double Len = FVector::Dist(From, To);
			const FVector U0 = FVector::CrossProduct(Ax, FMath::Abs(Ax.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
			const FVector V0 = FVector::CrossProduct(Ax, U0);
			const double Rm = Radius - Thick * 0.5;
			const double HalfT = Radius * FMath::Tan(UE_DOUBLE_PI / N) * 1.04;
			for (int32 k = 0; k < N; ++k)
			{
				const double Ang = TNProcMap::TwoPi * k / N;
				const FVector Radial = U0 * FMath::Cos(Ang) + V0 * FMath::Sin(Ang);
				ColBox((From + To) * 0.5 + Radial * Rm, FRotationMatrix::MakeFromXY(Ax, Radial).ToQuat(), FVector(Len * 0.5, Thick * 0.5, HalfT));
			}
		}

		/** Esfera cuyo casquete sobre Base mide BaseR de radio en el suelo y H de alto (medusas, cantos rodados, montículos). */
		void ColCap(const FVector& Base, double BaseR, double H)
		{
			const double Rs = (BaseR * BaseR + H * H) / (2.0 * FMath::Max(1.0, H));
			ColSphere(Base + FVector(0.0, 0.0, H - Rs), Rs);
		}

		/** Pasa la parte animada a coordenadas de su pivote Pivot (en la malla fija). */
		void AnimAround(const FVector& Pivot)
		{
			Info.AnimPivot = Pivot;
			for (FVector& V : Moving.Verts) { V -= Pivot; }
		}
	};

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Primitivas
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Marco local: origen y giro (su Z es el eje de los cuerpos de revolución). */
	struct FBeachFrame
	{
		FBeachFrame() = default;
		FBeachFrame(const FVector& InOrigin, const FQuat& InRot) : O(InOrigin), Q(InRot) {}
		FVector O = FVector::ZeroVector;
		FQuat Q = FQuat::Identity;
		FVector P(const FVector& Local) const { return O + Q.RotateVector(Local); }
		FVector D(const FVector& Local) const { return Q.RotateVector(Local); }
	};

	/** Caja orientada por Rot (semilados Half), con las caras de los lados algo más oscuras. */
	inline void AddOBox(FTNProcMeshBuffers& M, const FVector& Center, const FQuat& Rot, const FVector& Half, const FLinearColor& Color)
	{
		const FVector Ax = Rot.GetAxisX();
		const FVector Ay = Rot.GetAxisY();
		const FVector Az = Rot.GetAxisZ();
		auto Corner = [&](double Sx, double Sy, double Sz) { return Center + Ax * (Sx * Half.X) + Ay * (Sy * Half.Y) + Az * (Sz * Half.Z); };
		M.AddQuad(Corner(-1, -1, 1), Corner(1, -1, 1), Corner(1, 1, 1), Corner(-1, 1, 1), Az, Color);
		M.AddQuad(Corner(-1, -1, -1), Corner(-1, 1, -1), Corner(1, 1, -1), Corner(1, -1, -1), -Az, Shade(Color, 0.8f));
		M.AddQuad(Corner(1, -1, -1), Corner(1, 1, -1), Corner(1, 1, 1), Corner(1, -1, 1), Ax, Shade(Color, 0.92f));
		M.AddQuad(Corner(-1, -1, -1), Corner(-1, -1, 1), Corner(-1, 1, 1), Corner(-1, 1, -1), -Ax, Shade(Color, 0.92f));
		M.AddQuad(Corner(-1, 1, -1), Corner(-1, 1, 1), Corner(1, 1, 1), Corner(1, 1, -1), Ay, Shade(Color, 0.86f));
		M.AddQuad(Corner(-1, -1, -1), Corner(1, -1, -1), Corner(1, -1, 1), Corner(-1, -1, 1), -Ay, Shade(Color, 0.86f));
	}

	/** Caja girada YawDeg alrededor de Z. */
	inline void AddYawBox(FTNProcMeshBuffers& M, const FVector& Center, double YawDeg, const FVector& Half, const FLinearColor& Color)
	{
		AddOBox(M, Center, YawQ(YawDeg), Half, Color);
	}

	/**
	 * Cuerpo de revolución en el marco F. Profile son puntos (radio, altura) recorridos con el sólido a la izquierda: de
	 * abajo arriba por fuera, hacia dentro por arriba y de arriba abajo por dentro (un radio 0 cierra el polo). Seg lados;
	 * ColorAt(Tramo, Lado) colorea cada cara; Jitter mueve cada vértice en radio (rocas, cocos); SquashY aplasta la
	 * sección (elipse).
	 */
	inline void AddRevolve(FTNProcMeshBuffers& M, const FBeachFrame& F, const TArray<FVector2D>& Profile, int32 Seg,
		TFunctionRef<FLinearColor(int32 Ring, int32 Side)> ColorAt, double Jitter = 0.0, uint32 NoiseSeed = 0u, double SquashY = 1.0)
	{
		const int32 NumRings = Profile.Num();
		if (NumRings < 2 || Seg < 3) { return; }
		TArray<FVector> Pts;
		Pts.SetNumUninitialized(NumRings * Seg);
		for (int32 r = 0; r < NumRings; ++r)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const double Ang = TNProcMap::TwoPi * k / Seg;
				const double Wobble = (Jitter > 0.0 && Profile[r].X > 1.0) ? 1.0 + Jitter * TNProcHashNoise(r, k, NoiseSeed) : 1.0;
				const double Rad = Profile[r].X * Wobble;
				Pts[r * Seg + k] = F.P(FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad * SquashY, Profile[r].Y));
			}
		}
		for (int32 r = 0; r + 1 < NumRings; ++r)
		{
			const double Dr = Profile[r + 1].X - Profile[r].X;
			const double Dz = Profile[r + 1].Y - Profile[r].Y;
			if (FMath::Abs(Dr) + FMath::Abs(Dz) < 1e-3) { continue; }
			for (int32 k = 0; k < Seg; ++k)
			{
				const int32 K1 = (k + 1) % Seg;
				const double Am = TNProcMap::TwoPi * (k + 0.5) / Seg;
				// Normal del perfil (dz, -dr) llevada al lado k: hacia fuera del sólido.
				const FVector Hint = F.D(FVector(FMath::Cos(Am) * Dz, FMath::Sin(Am) * Dz, -Dr));
				M.AddQuad(Pts[r * Seg + k], Pts[r * Seg + K1], Pts[(r + 1) * Seg + K1], Pts[(r + 1) * Seg + k], Hint, ColorAt(r, k));
			}
		}
	}

	inline void AddRevolve(FTNProcMeshBuffers& M, const FBeachFrame& F, const TArray<FVector2D>& Profile, int32 Seg, const FLinearColor& Color,
		double Jitter = 0.0, uint32 NoiseSeed = 0u, double SquashY = 1.0)
	{
		AddRevolve(M, F, Profile, Seg, [&Color](int32, int32) { return Color; }, Jitter, NoiseSeed, SquashY);
	}

	/** Perfil de un elipsoide de semiejes Rxy (radio) y Rz (alto) centrado en el origen, con Rings tramos. */
	inline TArray<FVector2D> EllipsoidProfile(double Rxy, double Rz, int32 Rings)
	{
		TArray<FVector2D> Prof;
		for (int32 i = 0; i <= Rings; ++i)
		{
			const double T = UE_DOUBLE_PI * i / Rings;
			Prof.Add(FVector2D(i == Rings ? 0.0 : Rxy * FMath::Sin(T), -Rz * FMath::Cos(T)));
		}
		return Prof;
	}

	/** Elipsoide de semiejes (Rx, Ry, Rz) en el origen del marco F. */
	inline void AddBlob(FTNProcMeshBuffers& M, const FBeachFrame& F, double Rx, double Ry, double Rz, int32 Seg, int32 Rings, const FLinearColor& Color,
		double Jitter = 0.0, uint32 NoiseSeed = 0u)
	{
		AddRevolve(M, F, EllipsoidProfile(Rx, Rz, Rings), Seg, Color, Jitter, NoiseSeed, Ry / FMath::Max(1.0, Rx));
	}

	/**
	 * Perfil de un casquete esférico de radio BaseR en el suelo y H de alto (Rings tramos), con un faldón que baja Skirt
	 * por debajo del suelo (para que no quede hueco en las dunas). Es el mismo casquete que FParts::ColCap.
	 */
	inline TArray<FVector2D> CapProfile(double BaseR, double H, int32 Rings, double Skirt = 0.0)
	{
		TArray<FVector2D> Prof;
		const double Rs = (BaseR * BaseR + H * H) / (2.0 * FMath::Max(1.0, H));
		const double Zc = H - Rs;
		const double T0 = FMath::Atan2(BaseR, -Zc);
		if (Skirt > 0.0) { Prof.Add(FVector2D(BaseR * 1.02, -Skirt)); }
		for (int32 i = 0; i <= Rings; ++i)
		{
			const double T = T0 * (1.0 - static_cast<double>(i) / Rings);
			Prof.Add(FVector2D(i == Rings ? 0.0 : Rs * FMath::Sin(T), Zc + Rs * FMath::Cos(T)));
		}
		return Prof;
	}

	/** Sección circular de radio 1 (Seg lados) para AddSweep; SquashB aplasta el segundo eje. */
	inline TArray<FVector2D> CircleSection(int32 Seg, double SquashB = 1.0)
	{
		TArray<FVector2D> Sec;
		for (int32 k = 0; k < Seg; ++k)
		{
			const double Ang = TNProcMap::TwoPi * (k + 0.5) / Seg;
			Sec.Add(FVector2D(FMath::Cos(Ang), FMath::Sin(Ang) * SquashB));
		}
		return Sec;
	}

	/** Sección rectangular (semilados HalfN en la normal y HalfB en la binormal). */
	inline TArray<FVector2D> RectSection(double HalfN, double HalfB)
	{
		return { FVector2D(HalfN, HalfB), FVector2D(HalfN, -HalfB), FVector2D(-HalfN, -HalfB), FVector2D(-HalfN, HalfB) };
	}

	/**
	 * Barrido de una sección convexa (puntos (N, B), en orden) a lo largo de Path, escalada ScaleAt(i) en cada punto.
	 * Up fija la normal (Up sin su parte en la tangente, para anillos tumbados); si es cero, marco transportado. bCaps
	 * cierra los extremos de un camino abierto. ColorAt(Tramo, Lado) colorea cada cara.
	 */
	inline void AddSweep(FTNProcMeshBuffers& M, const TArray<FVector>& Path, const TArray<FVector2D>& Section, bool bClosedPath,
		TFunctionRef<double(int32 Station)> ScaleAt, TFunctionRef<FLinearColor(int32 Station, int32 Side)> ColorAt, bool bCaps,
		const FVector& Up = FVector::ZeroVector)
	{
		const int32 N = Path.Num();
		const int32 S = Section.Num();
		if (N < 2 || S < 3) { return; }
		TArray<FVector> Tan;
		TArray<FVector> Nor;
		Tan.SetNum(N);
		Nor.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Prev = bClosedPath ? (i + N - 1) % N : FMath::Max(0, i - 1);
			const int32 Next = bClosedPath ? (i + 1) % N : FMath::Min(N - 1, i + 1);
			const FVector T = (Path[Next] - Path[Prev]).GetSafeNormal();
			Tan[i] = T.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : T;
		}
		const bool bFixedUp = !Up.IsNearlyZero();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Base = bFixedUp ? Up : (i == 0 ? (FMath::Abs(Tan[0].Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector) : Nor[i - 1]);
			FVector Nn = Base - Tan[i] * FVector::DotProduct(Base, Tan[i]);
			if (Nn.SizeSquared() < 1e-6)
			{
				Nn = FVector::CrossProduct(Tan[i], FMath::Abs(Tan[i].Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector);
			}
			Nor[i] = Nn.GetSafeNormal();
		}
		TArray<FVector> Pts;
		Pts.SetNumUninitialized(N * S);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Bn = FVector::CrossProduct(Tan[i], Nor[i]);
			const double Sc = ScaleAt(i);
			for (int32 s = 0; s < S; ++s)
			{
				Pts[i * S + s] = Path[i] + Nor[i] * (Section[s].X * Sc) + Bn * (Section[s].Y * Sc);
			}
		}
		const int32 Spans = bClosedPath ? N : N - 1;
		for (int32 i = 0; i < Spans; ++i)
		{
			const int32 I1 = (i + 1) % N;
			const FVector Mid = (Path[i] + Path[I1]) * 0.5;
			for (int32 s = 0; s < S; ++s)
			{
				const int32 S1 = (s + 1) % S;
				const FVector Q = (Pts[i * S + s] + Pts[i * S + S1] + Pts[I1 * S + S1] + Pts[I1 * S + s]) * 0.25;
				M.AddQuad(Pts[i * S + s], Pts[i * S + S1], Pts[I1 * S + S1], Pts[I1 * S + s], Q - Mid, ColorAt(i, s));
			}
		}
		if (bCaps && !bClosedPath)
		{
			for (int32 s = 0; s < S; ++s)
			{
				const int32 S1 = (s + 1) % S;
				M.AddTri(Path[0], Pts[s], Pts[S1], -Tan[0], Shade(ColorAt(0, s), 0.9f));
				M.AddTri(Path[N - 1], Pts[(N - 1) * S + s], Pts[(N - 1) * S + S1], Tan[N - 1], Shade(ColorAt(N - 2, s), 0.9f));
			}
		}
	}

	/** Tubo de radio fijo (Seg lados) a lo largo de Path, de un color. */
	inline void AddTube(FTNProcMeshBuffers& M, const TArray<FVector>& Path, double Radius, int32 Seg, const FLinearColor& Color, bool bCaps = true)
	{
		AddSweep(M, Path, CircleSection(Seg), false, [Radius](int32) { return Radius; }, [&Color](int32, int32) { return Color; }, bCaps);
	}

	/** Tubo que se estrecha de RadiusA a RadiusB a lo largo de Path. */
	inline void AddTaperTube(FTNProcMeshBuffers& M, const TArray<FVector>& Path, double RadiusA, double RadiusB, int32 Seg, const FLinearColor& Color, bool bCaps = true)
	{
		const int32 Last = FMath::Max(1, Path.Num() - 1);
		AddSweep(M, Path, CircleSection(Seg), false, [RadiusA, RadiusB, Last](int32 i) { return FMath::Lerp(RadiusA, RadiusB, static_cast<double>(i) / Last); },
			[&Color](int32, int32) { return Color; }, bCaps);
	}

	/**
	 * Lámina con grosor (dos caras separadas Thick, sin cantos): rejilla de Nu × Nv celdas de la superficie Pos(u, v) con
	 * u y v en [0, 1]; la cara de arriba mira hacia dPos/du × dPos/dv. ColorAt(i, j) colorea cada celda por arriba (por
	 * abajo, algo más oscura) y Keep(i, j) = false deja un agujero (tela rasgada).
	 */
	inline void AddSheet(FTNProcMeshBuffers& M, int32 Nu, int32 Nv, TFunctionRef<FVector(double, double)> Pos, double Thick,
		TFunctionRef<FLinearColor(int32 I, int32 J)> ColorAt, TFunctionRef<bool(int32 I, int32 J)> Keep)
	{
		if (Nu < 1 || Nv < 1) { return; }
		const int32 W = Nu + 1;
		TArray<FVector> Pt;
		TArray<FVector> Nr;
		Pt.SetNumUninitialized(W * (Nv + 1));
		Nr.SetNumUninitialized(W * (Nv + 1));
		constexpr double Hd = 1e-3;
		for (int32 j = 0; j <= Nv; ++j)
		{
			for (int32 i = 0; i <= Nu; ++i)
			{
				const double U = static_cast<double>(i) / Nu;
				const double V = static_cast<double>(j) / Nv;
				const FVector Du = Pos(FMath::Min(1.0, U + Hd), V) - Pos(FMath::Max(0.0, U - Hd), V);
				const FVector Dv = Pos(U, FMath::Min(1.0, V + Hd)) - Pos(U, FMath::Max(0.0, V - Hd));
				const FVector Nn = FVector::CrossProduct(Du, Dv).GetSafeNormal();
				Pt[j * W + i] = Pos(U, V);
				Nr[j * W + i] = Nn.IsNearlyZero() ? FVector::UpVector : Nn;
			}
		}
		const double Ht = Thick * 0.5;
		for (int32 j = 0; j < Nv; ++j)
		{
			for (int32 i = 0; i < Nu; ++i)
			{
				if (!Keep(i, j)) { continue; }
				const int32 A = j * W + i;
				const int32 B = j * W + i + 1;
				const int32 C = (j + 1) * W + i + 1;
				const int32 D = (j + 1) * W + i;
				const FVector Hint = (Nr[A] + Nr[B] + Nr[C] + Nr[D]).GetSafeNormal();
				const FLinearColor Col = ColorAt(i, j);
				M.AddQuad(Pt[A] + Nr[A] * Ht, Pt[B] + Nr[B] * Ht, Pt[C] + Nr[C] * Ht, Pt[D] + Nr[D] * Ht, Hint, Col);
				M.AddQuad(Pt[A] - Nr[A] * Ht, Pt[B] - Nr[B] * Ht, Pt[C] - Nr[C] * Ht, Pt[D] - Nr[D] * Ht, -Hint, Shade(Col, 0.78f));
			}
		}
	}

	inline void AddSheet(FTNProcMeshBuffers& M, int32 Nu, int32 Nv, TFunctionRef<FVector(double, double)> Pos, double Thick,
		TFunctionRef<FLinearColor(int32 I, int32 J)> ColorAt)
	{
		AddSheet(M, Nu, Nv, Pos, Thick, ColorAt, [](int32, int32) { return true; });
	}

	/** Prisma recto de un polígono en planta (en orden, estrellado respecto a su centro) de Z0 a Z1 en el marco F. */
	inline void AddPrismPoly(FTNProcMeshBuffers& M, const FBeachFrame& F, const TArray<FVector2D>& Poly, double Z0, double Z1, const FLinearColor& Top,
		const FLinearColor& Side, bool bBottom = false)
	{
		const int32 N = Poly.Num();
		if (N < 3) { return; }
		FVector2D C = FVector2D::ZeroVector;
		for (const FVector2D& V : Poly) { C += V; }
		C /= static_cast<double>(N);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[(i + 1) % N];
			const FVector2D Out = (A + B) * 0.5 - C;
			M.AddQuad(F.P(FVector(A.X, A.Y, Z0)), F.P(FVector(B.X, B.Y, Z0)), F.P(FVector(B.X, B.Y, Z1)), F.P(FVector(A.X, A.Y, Z1)), F.D(FVector(Out.X, Out.Y, 0.0)), Side);
			M.AddTri(F.P(FVector(C.X, C.Y, Z1)), F.P(FVector(A.X, A.Y, Z1)), F.P(FVector(B.X, B.Y, Z1)), F.D(FVector::UpVector), Top);
			if (bBottom) { M.AddTri(F.P(FVector(C.X, C.Y, Z0)), F.P(FVector(A.X, A.Y, Z0)), F.P(FVector(B.X, B.Y, Z0)), F.D(-FVector::UpVector), Shade(Side, 0.8f)); }
		}
	}

	/** Contorno de una elipse irregular de semiejes (Ax, Ay) en N puntos (losas, suelas, tablas). */
	inline TArray<FVector2D> WobblyEllipse(double Ax, double Ay, int32 N, double Jitter, uint32 Seed)
	{
		TArray<FVector2D> Poly;
		for (int32 i = 0; i < N; ++i)
		{
			const double Ang = TNProcMap::TwoPi * i / N;
			const double Wob = 1.0 + Jitter * (2.0 * Rnd(Seed, i) - 1.0);
			Poly.Add(FVector2D(FMath::Cos(Ang) * Ax * Wob, FMath::Sin(Ang) * Ay * Wob));
		}
		return Poly;
	}

	/** Bellotas (conos bajos de 5 lados) pegadas a una superficie: percebes y lapas de las rocas y la madera. */
	inline void AddBarnacle(FTNProcMeshBuffers& M, const FVector& At, const FVector& Normal, double Radius, const FLinearColor& Color)
	{
		const FBeachFrame F(At, AxisZTo(Normal));
		AddRevolve(M, F, { FVector2D(0.0, -Radius * 0.3), FVector2D(Radius, 0.0), FVector2D(Radius * 0.45, Radius * 0.9), FVector2D(0.0, Radius * 0.6) }, 5, Color);
	}

	/** Bultito de tres caras (verrugas de la estrella, granos de arena pegados). */
	inline void AddBump(FTNProcMeshBuffers& M, const FVector& At, const FVector& Normal, double Radius, const FLinearColor& Color)
	{
		const FVector Nn = Normal.GetSafeNormal();
		const FVector U = FVector::CrossProduct(Nn, FMath::Abs(Nn.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector V = FVector::CrossProduct(Nn, U);
		const FVector Tip = At + Nn * Radius;
		FVector Base[3];
		for (int32 k = 0; k < 3; ++k)
		{
			const double Ang = TNProcMap::TwoPi * k / 3.0;
			Base[k] = At + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * Radius - Nn * (Radius * 0.3);
		}
		for (int32 k = 0; k < 3; ++k)
		{
			const FVector& A = Base[k];
			const FVector& B = Base[(k + 1) % 3];
			M.AddTri(A, B, Tip, (A + B) * 0.5 - At + Nn * (Radius * 0.5), Color);
		}
	}

	/** Contorno de estadio (rectángulo con los extremos redondos) de semilargo HalfLen y semiancho HalfWid. */
	inline TArray<FVector2D> Stadium(double HalfLen, double HalfWid, int32 PerEnd = 5)
	{
		TArray<FVector2D> Poly;
		const double Straight = FMath::Max(0.0, HalfLen - HalfWid);
		for (int32 End = 0; End < 2; ++End)
		{
			const double Sx = End == 0 ? 1.0 : -1.0;
			for (int32 i = 0; i <= PerEnd; ++i)
			{
				const double Ang = -UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * i / PerEnd;
				Poly.Add(FVector2D(Sx * (Straight + FMath::Cos(Ang) * HalfWid), Sx * FMath::Sin(Ang) * HalfWid));
			}
		}
		return Poly;
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Naturaleza: cocos, medusas, almejas, conchas, estrellas, rocas, troncos, maderas, sepias, plumas y cáscaras
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Medio coco boca arriba en el marco F: cuenco de cáscara parda con la carne blanca por dentro. */
	inline void AddCoconutHalf(FTNProcMeshBuffers& M, const FBeachFrame& F, double Rad, const FLinearColor& Husk, const FLinearColor& Flesh)
	{
		const double Th = Rad * 0.16;
		TArray<FVector2D> Prof;
		for (int32 i = 0; i <= 4; ++i)
		{
			const double T = UE_DOUBLE_HALF_PI * i / 4.0;
			Prof.Add(FVector2D(Rad * FMath::Sin(T), Rad * (1.0 - FMath::Cos(T))));
		}
		for (int32 i = 4; i >= 0; --i)
		{
			const double T = UE_DOUBLE_HALF_PI * i / 4.0;
			Prof.Add(FVector2D(i == 0 ? 0.0 : (Rad - Th) * FMath::Sin(T), Th + (Rad - Th) * (1.0 - FMath::Cos(T))));
		}
		AddRevolve(M, F, Prof, 9, [&](int32 Ring, int32) { return Ring < 4 ? Husk : Flesh; });
	}

	/** Coco: pardo y peludo tumbado (0), verde con su cáscara (1), partido en dos (2) o germinando (3). */
	inline void BuildCoconut(FParts& P, int32 Variant, uint32 Seed)
	{
		const FLinearColor Brown = Hex(0x6B4526);
		const FLinearColor BrownDark = Hex(0x4A2E18);
		const FLinearColor Fiber = Hex(0x8C6A45);
		const FLinearColor Green = Hex(0x6E9A34);
		const FLinearColor GreenDark = Hex(0x4E7A22);
		const FLinearColor Flesh = Hex(0xF4F0E4);
		P.Info.SinkMin = static_cast<float>(Cm(0.3));
		P.Info.SinkMax = static_cast<float>(Cm(2.5));
		P.Info.TiltMax = 6.f;
		if (Variant == 2)
		{
			// Partido en dos: una mitad boca arriba y la otra boca abajo, algo separadas.
			const double Rad = Cm(5.2);
			const FVector CupA(-Cm(3.0), -Cm(1.2), 0.0);
			const FVector CupB(Cm(3.2), Cm(1.4), 0.0);
			AddCoconutHalf(P.Body, FBeachFrame(CupA, YawQ(RndIn(Seed, 1, 0.0, 360.0))), Rad, Brown, Flesh);
			AddCoconutHalf(P.Body, FBeachFrame(CupB + FVector(0.0, 0.0, Rad), FQuat(FVector(1.0, 0.0, 0.0), UE_DOUBLE_PI)), Rad, Brown, Flesh);
			P.ColPrism(CupA, Rad * 0.9, 0.0, Rad, 22.5);
			P.ColCap(CupB, Rad * 0.95, Rad * 0.95);
			return;
		}
		const bool bGreen = Variant == 1;
		const double Len = bGreen ? Cm(16.0) : Cm(14.0);
		const double Rad = bGreen ? Cm(6.6) : Cm(5.8);
		const double ShiftX = Variant == 3 ? Cm(2.0) : 0.0;
		const FBeachFrame F(FVector(ShiftX, 0.0, Rad * 0.9), AxisZTo(FVector(1.0, 0.0, 0.0)));
		AddRevolve(P.Body, F, EllipsoidProfile(Rad, Len * 0.5, 7), bGreen ? 7 : 10, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (bGreen) { return (Side % 2) ? Green : GreenDark; }
			return ((Side + Ring) % 3 == 0) ? BrownDark : ((Side % 2) ? Brown : Fiber);
		}, bGreen ? 0.08 : 0.07, Seed, bGreen ? 0.88 : 0.96);
		if (bGreen)
		{
			// Cáliz del rabillo.
			AddBlob(P.Body, FBeachFrame(F.P(FVector(0.0, 0.0, Len * 0.5 - Cm(0.2))), F.Q), Cm(1.4), Cm(1.4), Cm(0.5), 6, 2, Hex(0x8A7040));
		}
		else
		{
			// Los tres ojos en el extremo.
			for (int32 e = 0; e < 3; ++e)
			{
				const double Ang = TNProcMap::TwoPi * e / 3.0 + 0.5;
				const FVector Eye = F.P(FVector(FMath::Cos(Ang) * Rad * 0.3, FMath::Sin(Ang) * Rad * 0.3, -Len * 0.5 + Cm(0.35)));
				AddBlob(P.Body, FBeachFrame(Eye, F.Q), Cm(0.6), Cm(0.6), Cm(0.35), 5, 2, BrownDark);
			}
		}
		if (Variant == 3)
		{
			// Germinando: brote verde que sale por los ojos, con tres hojas plegadas.
			const FVector StemA = F.P(FVector(0.0, Rad * 0.35, -Len * 0.5 + Cm(1.0)));
			const FVector StemB = StemA + FVector(-Cm(0.8), 0.0, Cm(4.0));
			AddTaperTube(P.Body, { StemA, (StemA + StemB) * 0.5 + FVector(-Cm(0.3), 0.0, 0.0), StemB }, Cm(0.45), Cm(0.3), 5, GreenDark);
			for (int32 L = 0; L < 3; ++L)
			{
				const double Ang = TNProcMap::TwoPi * L / 3.0 + RndIn(Seed, 40 + L, -0.3, 0.3);
				const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
				const FVector Across(-Dir.Y, Dir.X, 0.0);
				const double LeafLen = Cm(RndIn(Seed, 50 + L, 3.2, 4.2));
				AddSheet(P.Body, 5, 1, [&](double U, double V)
				{
					const double Wd = Cm(0.9) * FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(U * 0.95 + 0.05, 0.0, 1.0));
					return StemB + Dir * (LeafLen * U * 0.6) + FVector(0.0, 0.0, LeafLen * (0.9 * U - 0.25 * U * U)) + Across * (Wd * (V - 0.5) * 2.0);
				}, 6.0, [&](int32 I, int32) { return (I % 2) ? Green : Hex(0x86B444); });
			}
		}
		const double Reach = FMath::Max(0.0, Len * 0.5 - Rad);
		P.ColCapsule(F.P(FVector(0.0, 0.0, -Reach)), F.P(FVector(0.0, 0.0, Reach)), Rad * 0.9);
	}

	/** Medusa varada: campana que respira (parte animada), brazos orales y filamentos por la arena. */
	inline void BuildJellyfish(FParts& P, int32 Variant, uint32 Seed)
	{
		// Aurelia (lila con cuatro herraduras), aguamala (blanca con borde violeta), acalefo (crema con rayas pardas) y
		// clavel (rosa con verrugas).
		const uint32 BellHex[4] = { 0xC4DCF0, 0xE8EEF4, 0xF2E3C8, 0xF4BCD2 };
		const uint32 RimHex[4] = { 0x9A8ED2, 0x6F74C8, 0xB0703E, 0xC45C8C };
		const uint32 MarkHex[4] = { 0x8E5CC0, 0xA9B4E4, 0x9A4E2A, 0xA83E70 };
		const uint32 ArmHex[4] = { 0xCDBDEB, 0xD5DCF2, 0xE8C8A0, 0xEA9CBC };
		const int32 Kind = FMath::Clamp(Variant, 0, 3);
		const FLinearColor Bell = Hex(BellHex[Kind], 0.2f);
		const FLinearColor Rim = Hex(RimHex[Kind], 0.2f);
		const FLinearColor Mark = Hex(MarkHex[Kind], 0.2f);
		const FLinearColor ArmC = Hex(ArmHex[Kind], 0.15f);
		const double BaseR = Cm(18.5);
		const double Height = Cm(6.5);
		// Campana: labio algo abierto a ras de suelo y cúpula aplastada.
		const double Fr[7] = { 0.95, 0.85, 0.7, 0.5, 0.28, 0.12, 0.0 };
		TArray<FVector2D> Prof = { FVector2D(BaseR * 1.04, -Cm(0.9)), FVector2D(BaseR, Cm(0.4)) };
		for (const double Fk : Fr) { Prof.Add(FVector2D(BaseR * Fk, Height * FMath::Pow(1.0 - Fk * Fk, 0.65))); }
		AddRevolve(P.Moving, FBeachFrame(), Prof, 16, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring == 0) { return Rim; }
			if (Ring == 1) { return Blend(Rim, Bell, 0.45f); }
			switch (Kind)
			{
			case 0: return (Ring >= 3 && Ring <= 4 && (Side % 4 == 1 || Side % 4 == 2)) ? Mark : Bell;
			case 1: return Ring == 2 ? Mark : Bell;
			case 2: return (Ring >= 2 && Side % 2 == 0) ? Mark : Bell;
			default: return Rnd(Seed, Ring * 31 + Side) < 0.2 ? Mark : Bell;
			}
		}, 0.03, Seed);
		P.Info.Anim = EAnim::Breathe;
		P.Info.AnimAmp = 0.035f;
		P.Info.AnimRate = static_cast<float>(RndIn(Seed, 7, 0.22, 0.34));
		P.AnimAround(FVector::ZeroVector);
		// Brazos orales: tiras rizadas que salen de debajo de la campana.
		const int32 NumArms = 4 + static_cast<int32>(Rnd(Seed, 9) * 3.0);
		for (int32 a = 0; a < NumArms; ++a)
		{
			const double Ang0 = TNProcMap::TwoPi * (a + RndIn(Seed, 10 + a, -0.2, 0.2)) / NumArms;
			const double Reach = BaseR + Cm(RndIn(Seed, 20 + a, 2.0, 4.8));
			TArray<FVector> Path;
			for (int32 s = 0; s <= 6; ++s)
			{
				const double T = s / 6.0;
				const double Rr = FMath::Lerp(BaseR * 0.55, Reach, T);
				const double Ang = Ang0 + 0.25 * FMath::Sin(T * 5.0 + a) * T;
				Path.Add(FVector(FMath::Cos(Ang) * Rr, FMath::Sin(Ang) * Rr, FMath::Lerp(Cm(1.4), Cm(0.4), T)));
			}
			AddSweep(P.Body, Path, CircleSection(5, 0.55), false, [](int32 i) { return FMath::Lerp(Cm(1.8), Cm(0.4), i / 6.0); },
				[&](int32 i, int32 Side) { return ((i + Side) % 2) ? ArmC : Shade(ArmC, 0.85f); }, true);
		}
		// Filamentos finos.
		const int32 NumTent = 5 + static_cast<int32>(Rnd(Seed, 11) * 4.0);
		for (int32 t = 0; t < NumTent; ++t)
		{
			const double Ang0 = TNProcMap::TwoPi * Rnd(Seed, 60 + t);
			const double Reach = BaseR + Cm(RndIn(Seed, 70 + t, 1.0, 4.5));
			TArray<FVector> Path;
			for (int32 s = 0; s <= 5; ++s)
			{
				const double T = s / 5.0;
				const double Rr = FMath::Lerp(BaseR * 0.9, Reach, T);
				const double Ang = Ang0 + 0.18 * FMath::Sin(T * 7.0 + t * 1.3);
				Path.Add(FVector(FMath::Cos(Ang) * Rr, FMath::Sin(Ang) * Rr, 6.0));
			}
			AddTube(P.Body, Path, 5.0, 3, Shade(ArmC, 0.9f), false);
		}
		P.ColCap(FVector::ZeroVector, BaseR * 0.97, Height * 0.95);
		P.Info.SinkMax = static_cast<float>(Cm(0.8));
		P.Info.TiltMax = 3.f;
	}

	/** Almeja: valva de abajo medio enterrada y valva de arriba (parte animada) que se abre por la bisagra de atrás. */
	inline void BuildClam(FParts& P, int32 Variant, uint32 Seed)
	{
		const uint32 ShellHex[4] = { 0xE8D6B0, 0xD9C6E6, 0xF0B48A, 0xB9C4CE };
		const uint32 BandHex[4] = { 0x9C6B3E, 0x8E6AB0, 0xD27A4E, 0x6E7E8E };
		const int32 Kind = FMath::Clamp(Variant, 0, 3);
		const FLinearColor ShellC = Hex(ShellHex[Kind], 0.1f);
		const FLinearColor Band = Hex(BandHex[Kind], 0.1f);
		const FLinearColor Nacre = Hex(0xF4E8F0, 0.35f);
		const double HalfL = Cm(2.5);
		const double Squash = 0.8;
		const double Seam = Cm(1.0);
		const double Dome = Cm(1.3);
		// Valva de arriba: cara interior plana (nácar) y cúpula con anillos de crecimiento (y rayos en la lila).
		const TArray<FVector2D> TopProf = { FVector2D(0.0, 2.0), FVector2D(HalfL * 0.97, 2.0), FVector2D(HalfL, 6.0), FVector2D(HalfL * 0.94, Dome * 0.35),
			FVector2D(HalfL * 0.78, Dome * 0.68), FVector2D(HalfL * 0.52, Dome * 0.9), FVector2D(HalfL * 0.22, Dome), FVector2D(0.0, Dome * 1.02) };
		AddRevolve(P.Moving, FBeachFrame(FVector(Cm(0.2), 0.0, Seam), FQuat::Identity), TopProf, 14, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring == 0) { return Nacre; }
			const bool bBand = (Ring % 2 == 0) || (Kind == 1 && Side % 3 == 0);
			return bBand ? Band : ShellC;
		}, 0.0, 0u, Squash);
		// Umbo: el pico de la bisagra.
		AddBlob(P.Moving, FBeachFrame(FVector(-HalfL * 0.8, 0.0, Seam + Dome * 0.55), FQuat::Identity), Cm(0.5), Cm(0.4), Cm(0.35), 6, 3, Shade(ShellC, 0.9f));
		// Valva de abajo, del revés y medio enterrada, con la carne y (en una variante) una perla.
		const TArray<FVector2D> BotProf = { FVector2D(0.0, -Dome), FVector2D(HalfL * 0.22, -Dome * 0.98), FVector2D(HalfL * 0.52, -Dome * 0.88),
			FVector2D(HalfL * 0.78, -Dome * 0.66), FVector2D(HalfL * 0.94, -Dome * 0.33), FVector2D(HalfL, -4.0), FVector2D(HalfL * 0.97, 0.0), FVector2D(0.0, 0.0) };
		AddRevolve(P.Body, FBeachFrame(FVector(Cm(0.2), 0.0, Seam), FQuat::Identity), BotProf, 14, [&](int32 Ring, int32) { return Ring == 6 ? Nacre : Shade(Band, 0.9f); },
			0.0, 0u, Squash);
		AddBlob(P.Body, FBeachFrame(FVector(Cm(0.4), 0.0, Seam + 2.0), FQuat::Identity), Cm(1.6), Cm(1.2), Cm(0.35), 8, 3, Hex(0xF2B49A, 0.3f));
		if (Kind == 3)
		{
			AddBlob(P.Body, FBeachFrame(FVector(Cm(0.9), Cm(0.3), Seam + Cm(0.45)), FQuat::Identity), Cm(0.4), Cm(0.4), Cm(0.4), 8, 4, Hex(0xF8F6F2, 0.7f));
		}
		P.Info.Anim = EAnim::Clam;
		P.Info.AnimAxis = FVector(0.0, -1.0, 0.0);
		P.Info.AnimAmp = static_cast<float>(RndIn(Seed, 5, 22.0, 30.0));
		P.Info.AnimRate = static_cast<float>(RndIn(Seed, 6, 7.0, 12.0));
		P.AnimAround(FVector(-HalfL * 0.95 + Cm(0.2), 0.0, Seam));
		P.ColBox(FVector(Cm(0.2), 0.0, (Seam + Dome) * 0.5), FQuat::Identity, FVector(HalfL, HalfL * Squash, (Seam + Dome) * 0.5));
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 4.f;
	}

	/** Concha de adorno (no las de puntos): caracola (0), berberecho (1), porcelana moteada (2) o vieira pálida (3). */
	inline void BuildDecorShell(FParts& P, int32 Variant, uint32 Seed)
	{
		P.Info.SinkMax = static_cast<float>(Cm(0.6));
		P.Info.TiltMax = 6.f;
		switch (Variant % 4)
		{
		case 0:
		{
			// Caracola tumbada: vueltas abultadas hasta la boca, con el labio rosado.
			const double Len = Cm(9.0);
			const double Rmax = Cm(2.1);
			TArray<FVector2D> Prof = { FVector2D(0.0, 0.0) };
			for (int32 i = 1; i <= 10; ++i)
			{
				const double T = i / 10.0;
				Prof.Add(FVector2D(Rmax * FMath::Pow(T, 0.85) * (1.0 + 0.16 * FMath::Abs(FMath::Sin(UE_DOUBLE_PI * 4.5 * T))), Len * T * 0.9));
			}
			Prof.Add(FVector2D(Rmax * 1.12, Len * 0.96));
			Prof.Add(FVector2D(Rmax * 0.55, Len));
			Prof.Add(FVector2D(0.0, Len * 0.97));
			const FLinearColor Cream = Hex(0xEAD9B8);
			const FLinearColor Brown = Hex(0x9A6A3E);
			const FLinearColor Lip = Hex(0xF2B6A0, 0.3f);
			AddRevolve(P.Body, FBeachFrame(FVector(-Len * 0.5, 0.0, Rmax * 0.85), AxisZTo(FVector(1.0, 0.0, 0.08))), Prof, 8, [&](int32 Ring, int32 Side) -> FLinearColor
			{
				if (Ring >= 10) { return Lip; }
				return ((Ring + Side / 2) % 3 == 0) ? Brown : Cream;
			}, 0.04, Seed);
			P.ColBox(FVector(0.0, 0.0, Rmax * 0.85), FQuat::Identity, FVector(Len * 0.46, Rmax * 0.9, Rmax * 0.85));
			break;
		}
		case 1:
		{
			// Berberecho: cúpula de costillas blancas y rosadas con el pico de la bisagra.
			const double BaseR = Cm(2.6);
			const double H = Cm(2.0);
			const FLinearColor RibA = Hex(0xF3E6D6);
			const FLinearColor RibB = Hex(0xD9A88A);
			AddRevolve(P.Body, FBeachFrame(FVector(0.0, 0.0, -Cm(0.2)), FQuat::Identity), CapProfile(BaseR, H, 4, Cm(0.3)), 18,
				[&](int32, int32 Side) { return (Side % 2) ? RibA : RibB; }, 0.02, Seed, 0.92);
			AddBlob(P.Body, FBeachFrame(FVector(-BaseR * 0.55, 0.0, H * 0.75), FQuat::Identity), Cm(0.6), Cm(0.5), Cm(0.5), 6, 3, RibB);
			P.ColBox(FVector(0.0, 0.0, H * 0.45), FQuat::Identity, FVector(BaseR * 0.9, BaseR * 0.85, H * 0.45));
			break;
		}
		case 2:
		{
			// Porcelana: huevo brillante moteado, con la tripa clara.
			const double Rx = Cm(2.6);
			const double Ry = Cm(1.7);
			const double Rz = Cm(1.4);
			const FLinearColor Shell = Hex(0xB5835A, 0.45f);
			const FLinearColor Spot = Hex(0xF4EBDD, 0.45f);
			const FLinearColor Belly = Hex(0xF2E8DA, 0.45f);
			AddRevolve(P.Body, FBeachFrame(FVector(0.0, 0.0, Rz * 0.8), AxisZTo(FVector(1.0, 0.0, 0.0))), EllipsoidProfile(Rz, Rx, 6), 10, [&](int32 Ring, int32 Side) -> FLinearColor
			{
				// Con el eje tumbado en X, el lado k mira hacia -Z cuando su coseno es positivo.
				if (FMath::Cos(TNProcMap::TwoPi * (Side + 0.5) / 10.0) > 0.35) { return Belly; }
				return Rnd(Seed, Ring * 17 + Side) < 0.3 ? Spot : Shell;
			}, 0.0, 0u, Ry / Rz);
			P.ColBox(FVector(0.0, 0.0, Rz * 0.8), FQuat::Identity, FVector(Rx * 0.9, Ry * 0.9, Rz * 0.8));
			break;
		}
		default:
		{
			// Vieira pálida, tumbada boca abajo: abanico de costillas con las orejetas.
			const double R = Cm(4.6);
			const FVector Hinge(-R * 0.45, 0.0, 6.0);
			const FLinearColor Pale = Hex(0xE3D2EE);
			const FLinearColor RibC = Hex(0xF6EEF8);
			const FLinearColor Edge = Hex(0xC9A8D8);
			AddSheet(P.Body, 10, 4, [&](double U, double V)
			{
				// U va de +70° a -70° para que la cara de arriba mire hacia arriba.
				const double Ang = FMath::DegreesToRadians(70.0 - 140.0 * U);
				const double Rr = R * (0.08 + 0.92 * V);
				const double Rib = 4.0 * FMath::Abs(FMath::Sin(U * UE_DOUBLE_PI * 5.0)) * V;
				return Hinge + FVector(FMath::Cos(Ang) * Rr, FMath::Sin(Ang) * Rr, Cm(1.1) * FMath::Sin(UE_DOUBLE_PI * (0.15 + 0.85 * V)) * (1.0 - 0.3 * V) + Rib);
			}, 10.0, [&](int32 I, int32 J) { return J == 3 ? Edge : ((I % 2) ? Pale : RibC); });
			for (const double Sy : { -1.0, 1.0 })
			{
				const FVector E0 = Hinge + FVector(0.0, 0.0, 4.0);
				const FVector E1 = Hinge + FVector(-Cm(0.4), Sy * Cm(0.9), 3.0);
				const FVector E2 = Hinge + FVector(Cm(0.6), Sy * Cm(1.1), 5.0);
				P.Body.AddTri(E0, E1, E2, FVector::UpVector, Edge);
				P.Body.AddTri(E0, E1, E2, -FVector::UpVector, Shade(Edge, 0.7f));
			}
			P.ColBox(FVector(Cm(0.4), 0.0, Cm(0.7)), FQuat::Identity, FVector(R * 0.5, R * 0.85, Cm(0.7)));
			break;
		}
		}
	}

	/** Estrella de mar de cinco brazos con cresta y verrugas; a veces un brazo levantado o uno que vuelve a crecer. */
	inline void BuildStarfish(FParts& P, int32 Variant, uint32 Seed)
	{
		const uint32 BodyHex[4] = { 0xE8742E, 0xC8372D, 0x8A4FA0, 0x3E7CC0 };
		const uint32 DotHex[4] = { 0xF7C27A, 0xF09080, 0xD6B0E6, 0xA9D0F2 };
		const int32 Kind = Variant % 4;
		const FLinearColor Col = Hex(BodyHex[Kind]);
		const FLinearColor Dot = Hex(DotHex[Kind]);
		const double Reach = Cm(7.2);
		const double Spin = RndIn(Seed, 1, 0.0, TNProcMap::TwoPi);
		const int32 Curled = (Kind == 1 || Kind == 3) ? static_cast<int32>(Rnd(Seed, 2) * 5.0) : -1;
		const int32 Short = Kind == 2 ? static_cast<int32>(Rnd(Seed, 3) * 5.0) : -1;
		for (int32 a = 0; a < 5; ++a)
		{
			const double Ang = Spin + TNProcMap::TwoPi * a / 5.0;
			const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
			const FVector Across(-Dir.Y, Dir.X, 0.0);
			const double ArmLen = Reach * (a == Short ? 0.55 : RndIn(Seed, 10 + a, 0.9, 1.0));
			const double Rs[4] = { Cm(1.4), ArmLen * 0.42, ArmLen * 0.72, ArmLen };
			const double Ws[4] = { Cm(1.7), Cm(1.35), Cm(0.85), Cm(0.2) };
			const double Hs[4] = { Cm(1.8), Cm(1.35), Cm(0.85), Cm(0.35) };
			FVector Lft[4];
			FVector Rgt[4];
			FVector Top[4];
			for (int32 s = 0; s < 4; ++s)
			{
				const double Lift = (a == Curled) ? Cm(2.6) * FMath::Square(s / 3.0) : 0.0;
				const FVector C = Dir * Rs[s] + FVector(0.0, 0.0, Lift);
				Lft[s] = C + Across * Ws[s] + FVector(0.0, 0.0, 3.0);
				Rgt[s] = C - Across * Ws[s] + FVector(0.0, 0.0, 3.0);
				Top[s] = C + FVector(0.0, 0.0, Hs[s]);
			}
			const FVector Drop(0.0, 0.0, 18.0);
			for (int32 s = 0; s < 3; ++s)
			{
				P.Body.AddQuad(Lft[s], Lft[s + 1], Top[s + 1], Top[s], Across + FVector::UpVector, Col);
				P.Body.AddQuad(Top[s], Top[s + 1], Rgt[s + 1], Rgt[s], -Across + FVector::UpVector, Shade(Col, 0.88f));
				// Faldón hasta la arena.
				P.Body.AddQuad(Lft[s], Lft[s + 1], Lft[s + 1] - Drop, Lft[s] - Drop, Across, Shade(Col, 0.75f));
				P.Body.AddQuad(Rgt[s], Rgt[s + 1], Rgt[s + 1] - Drop, Rgt[s] - Drop, -Across, Shade(Col, 0.75f));
				AddBump(P.Body, (Top[s] + Top[s + 1]) * 0.5, FVector::UpVector + Across * 0.2, Cm(0.4), Dot);
			}
			P.Body.AddTri(Lft[3], Rgt[3], Top[3], Dir, Shade(Col, 0.9f));
			P.ColBoxYaw(Dir * (ArmLen * 0.5) + FVector(0.0, 0.0, Cm(0.5)), FMath::RadiansToDegrees(Ang), FVector(ArmLen * 0.45, Cm(1.1), Cm(0.5)));
		}
		AddRevolve(P.Body, FBeachFrame(), CapProfile(Cm(2.3), Cm(2.0), 3, 10.0), 10, [&](int32 Ring, int32 Side) { return (Ring == 3 && Side % 2 == 0) ? Dot : Col; });
		P.ColCap(FVector::ZeroVector, Cm(2.2), Cm(1.9));
		P.Info.SinkMax = static_cast<float>(Cm(0.3));
		P.Info.TiltMax = 3.f;
	}

	/** Paleta de una roca: capas claras y oscuras alternas, algas y percebes. */
	struct FRockLook
	{
		FLinearColor Light;
		FLinearColor Dark;
		FLinearColor Algae;
		FLinearColor Barnacle;
	};

	/** Arenisca (0), granito (1) o pizarra oscura (2). */
	inline FRockLook RockLookOf(int32 Kind)
	{
		switch (((Kind % 3) + 3) % 3)
		{
		case 0: return { Hex(0xC9A677), Hex(0xAE8A5C), Hex(0x6F8F3A), Hex(0xEDE6D6) };
		case 1: return { Hex(0x9C9892), Hex(0x827E78), Hex(0x5E8A44), Hex(0xE8E4DA) };
		default: return { Hex(0x7A716A), Hex(0x625A54), Hex(0x4F7A3A), Hex(0xDCD6CA) };
		}
	}

	/**
	 * Roca de estratos: Layers capas octogonales de StepH de alto (se sube saltando de una a otra), cada una más pequeña y
	 * algo corrida, con su colisión (un prisma de cuatro cajas por capa, alineado con las caras). Apothem0 es la apotema
	 * de la capa de abajo.
	 */
	inline void AddStrataRock(FParts& P, const FVector& Center, double Apothem0, int32 Layers, double StepH, uint32 RockSeed, const FRockLook& Look)
	{
		FVector C = Center;
		for (int32 l = 0; l < Layers; ++l)
		{
			const double Frac = Layers > 1 ? static_cast<double>(l) / (Layers - 1) : 0.0;
			const double Apo = Apothem0 * FMath::Lerp(1.0, 0.36, Frac);
			if (l > 0)
			{
				const double Drift = RndIn(RockSeed, 30 + l, 0.0, TNProcMap::TwoPi);
				C += FVector(FMath::Cos(Drift), FMath::Sin(Drift), 0.0) * (Apothem0 * 0.08);
			}
			const double Z0 = l == 0 ? -StepH * 0.6 : l * StepH - StepH * 0.3;
			const double Z1 = (l + 1) * StepH;
			const double Yaw = RndIn(RockSeed, 40 + l, 0.0, 45.0);
			const double Rv = Apo / FMath::Cos(UE_DOUBLE_PI / 8.0);
			const FLinearColor Band = (l % 2) ? Look.Dark : Look.Light;
			const TArray<FVector2D> Prof = { FVector2D(Rv * 1.02, Z0), FVector2D(Rv, Z1 - StepH * 0.22), FVector2D(Rv * 0.9, Z1), FVector2D(0.0, Z1 + 2.0) };
			AddRevolve(P.Body, FBeachFrame(FVector(C.X, C.Y, 0.0), YawQ(Yaw)), Prof, 8, [&](int32 Ring, int32 Side) -> FLinearColor
			{
				if (Ring == 2 && Rnd(RockSeed, 100 + l * 16 + Side) < 0.35) { return Look.Algae; }
				return Ring == 0 ? Band : Shade(Band, 1.08f);
			}, 0.05, RockSeed + static_cast<uint32>(l) * 13u);
			P.ColPrism(FVector(C.X, C.Y, 0.0), Apo * 0.98, FMath::Max(Z0, -StepH * 0.2), Z1, Yaw + 22.5);
			// Un percebe en el costado.
			const double Ab = RndIn(RockSeed, 200 + l, 0.0, TNProcMap::TwoPi);
			const FVector Out(FMath::Cos(Ab), FMath::Sin(Ab), 0.0);
			const double Zb = FMath::Lerp(FMath::Max(Z0, 0.0) + StepH * 0.3, Z1 - StepH * 0.3, Rnd(RockSeed, 210 + l));
			AddBarnacle(P.Body, FVector(C.X, C.Y, Zb) + Out * (Apo * 0.97), Out, Cm(0.5), Look.Barnacle);
		}
	}

	/** Canto rodado: casquete irregular con algas abajo y percebes, y su esfera de colisión si bCollide. */
	inline void AddBoulder(FParts& P, const FVector& Base, double BaseR, double H, uint32 RockSeed, const FRockLook& Look, bool bCollide = true)
	{
		AddRevolve(P.Body, FBeachFrame(Base, YawQ(RndIn(RockSeed, 1, 0.0, 360.0))), CapProfile(BaseR, H, 4, Cm(1.0)), 10, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring <= 1) { return Rnd(RockSeed, 50 + Side) < 0.5 ? Look.Algae : Look.Dark; }
			return (Side + Ring) % 4 == 0 ? Look.Dark : Look.Light;
		}, 0.07, RockSeed);
		if (bCollide) { P.ColCap(Base, BaseR * 0.96, H * 0.96); }
		const double Rs = (BaseR * BaseR + H * H) / (2.0 * FMath::Max(1.0, H));
		const FVector Center = Base + FVector(0.0, 0.0, H - Rs);
		for (int32 b = 0; b < 2; ++b)
		{
			const double Ang = RndIn(RockSeed, 60 + b, 0.0, TNProcMap::TwoPi);
			const double Rr = BaseR * RndIn(RockSeed, 70 + b, 0.55, 0.85);
			const FVector At = Base + FVector(FMath::Cos(Ang) * Rr, FMath::Sin(Ang) * Rr, H - Rs + FMath::Sqrt(FMath::Max(0.0, Rs * Rs - Rr * Rr)));
			AddBarnacle(P.Body, At, At - Center, Cm(0.6), Look.Barnacle);
		}
	}

	/** Roca: de estratos (0), losa inclinada que hace de rampa (1), canto rodado (2) o mesa baja con charquito (3). */
	inline void BuildRock(FParts& P, int32 Variant, uint32 Seed)
	{
		const FRockLook Look = RockLookOf(Variant / 4 + Variant);
		P.Info.bBlocksCamera = true;
		P.Info.SinkMax = static_cast<float>(Cm(0.8));
		P.Info.TiltMax = 2.f;
		switch (Variant % 4)
		{
		case 0:
			AddStrataRock(P, FVector::ZeroVector, Cm(20.0), 4, 80.0, Seed, Look);
			break;
		case 1:
		{
			// Losa de 20° que sale de la arena: se sube andando y se salta desde el borde alto (~3,2 m).
			const FQuat Tilt(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(-20.0));
			const FBeachFrame F(FVector(0.0, 0.0, 90.0), Tilt);
			AddPrismPoly(P.Body, F, WobblyEllipse(Cm(17.0), Cm(11.5), 9, 0.08, Seed), -70.0, 70.0, Look.Light, Look.Dark, true);
			P.ColBox(F.O, Tilt, FVector(Cm(17.0) * 0.9, Cm(11.5) * 0.9, 70.0));
			for (int32 b = 0; b < 3; ++b)
			{
				const FVector Local(RndIn(Seed, 80 + b, -Cm(10.0), Cm(10.0)), RndIn(Seed, 90 + b, -Cm(6.0), Cm(6.0)), 70.0);
				AddBarnacle(P.Body, F.P(Local), F.D(FVector::UpVector), Cm(0.6), Look.Barnacle);
			}
			break;
		}
		case 2:
			AddBoulder(P, FVector::ZeroVector, Cm(18.0), Cm(11.5), Seed, Look);
			break;
		default:
			AddStrataRock(P, FVector::ZeroVector, Cm(20.0), 1, 85.0, Seed, Look);
			AddRevolve(P.Body, FBeachFrame(FVector(Cm(3.0), -Cm(2.0), 88.0), FQuat::Identity), { FVector2D(Cm(5.0), 0.0), FVector2D(0.0, 1.0) }, 9, Hex(0x5FA8C8, 0.6f), 0.12, Seed);
			break;
		}
	}

	/** Grupo de rocas: una grande de estratos, dos medianas pegadas que hacen de escalones, cantos y guijarros. */
	inline void BuildRockCluster(FParts& P, int32 Variant, uint32 Seed)
	{
		const FRockLook Look = RockLookOf(Variant);
		P.Info.bBlocksCamera = true;
		P.Info.SinkMax = static_cast<float>(Cm(0.6));
		const double BigAng = RndIn(Seed, 1, 0.0, TNProcMap::TwoPi);
		const FVector BigC = FVector(FMath::Cos(BigAng), FMath::Sin(BigAng), 0.0) * Cm(8.0);
		const double BigApo = Cm(18.0);
		AddStrataRock(P, BigC, BigApo, 5 + (Variant % 2), 80.0, Seed, Look);
		for (int32 m = 0; m < 2; ++m)
		{
			const double Ang = BigAng + UE_DOUBLE_PI + (m == 0 ? -0.7 : 0.8) + RndIn(Seed, 10 + m, -0.2, 0.2);
			const double Apo = Cm(RndIn(Seed, 20 + m, 9.0, 11.5));
			const FVector C = BigC + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * ((BigApo + Apo) * 0.82);
			AddStrataRock(P, C, Apo, 2 + m, 80.0, Seed + 101u * static_cast<uint32>(m + 1), Look);
		}
		for (int32 b = 0; b < 3; ++b)
		{
			const double Ang = BigAng + UE_DOUBLE_HALF_PI * (b == 0 ? 1.0 : (b == 1 ? -1.0 : 2.0)) + RndIn(Seed, 30 + b, -0.3, 0.3);
			const double R = Cm(RndIn(Seed, 40 + b, 7.0, 12.0));
			const FVector C = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * Cm(RndIn(Seed, 50 + b, 26.0, 36.0));
			AddBoulder(P, C, R, R * RndIn(Seed, 60 + b, 0.55, 0.8), Seed + 777u * static_cast<uint32>(b + 1), Look);
		}
		for (int32 n = 0; n < 7; ++n)
		{
			const double Ang = RndIn(Seed, 70 + n, 0.0, TNProcMap::TwoPi);
			const FVector C = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * Cm(RndIn(Seed, 80 + n, 20.0, 46.0));
			const double R = Cm(RndIn(Seed, 90 + n, 1.2, 2.8));
			AddBlob(P.Body, FBeachFrame(C + FVector(0.0, 0.0, R * 0.3), YawQ(RndIn(Seed, 95 + n, 0.0, 360.0))), R, R * 0.8, R * 0.6, 6, 3,
				(n % 2) ? Look.Light : Look.Dark, 0.1, Seed + static_cast<uint32>(n));
		}
	}

	/** Rama que se estrecha de R0 a R1 por Path, con nudos más oscuros; cápsulas de colisión cada dos tramos si bCollide. */
	inline void AddBranch(FParts& P, const TArray<FVector>& Path, double R0, double R1, const FLinearColor& Col, bool bCollide)
	{
		const int32 Last = FMath::Max(1, Path.Num() - 1);
		AddSweep(P.Body, Path, CircleSection(7), false, [R0, R1, Last](int32 i) { return FMath::Lerp(R0, R1, static_cast<double>(i) / Last); },
			[&Col](int32 i, int32 Side) { return ((i + Side) % 5 == 0) ? Shade(Col, 0.85f) : Col; }, true);
		if (!bCollide) { return; }
		for (int32 i = 0; i + 1 < Path.Num(); i += 2)
		{
			const int32 j = FMath::Min(i + 2, Path.Num() - 1);
			const double Rr = FMath::Lerp(R0, R1, (i + j) * 0.5 / Last);
			P.ColCapsule(Path[i], Path[j], Rr * 0.9);
		}
	}

	/** Camino de Steps tramos de A a B que serpentea Amp a los lados (ramas, cuerdas). */
	inline TArray<FVector> WigglePath(const FVector& A, const FVector& B, int32 Steps, double Amp, int32 Salt)
	{
		TArray<FVector> Path;
		const FVector D = B - A;
		const FVector Across = FVector(-D.Y, D.X, 0.0).GetSafeNormal();
		for (int32 s = 0; s <= Steps; ++s)
		{
			const double T = static_cast<double>(s) / Steps;
			const double W = FMath::Sin(T * UE_DOUBLE_TWO_PI + Salt) * Amp * FMath::Sin(T * UE_DOUBLE_PI);
			Path.Add(A + D * T + Across * W + FVector(0.0, 0.0, Amp * 0.15 * FMath::Sin(T * 7.0 + Salt)));
		}
		return Path;
	}

	/** Madera a la deriva, blanqueada: rama con horquilla (0), raíz (1), dos palos cruzados (2) o rama larga en S (3). */
	inline void BuildDriftwood(FParts& P, int32 Variant, uint32 Seed)
	{
		const uint32 WoodHex[4] = { 0xB9B2A6, 0xA7A49D, 0xC7B18E, 0x9C8C78 };
		const FLinearColor Wood = Hex(WoodHex[Variant % 4]);
		P.Info.SinkMin = static_cast<float>(Cm(0.4));
		P.Info.SinkMax = static_cast<float>(Cm(1.8));
		P.Info.TiltMax = 3.f;
		switch (Variant % 4)
		{
		case 0:
		{
			const double R = Cm(3.0);
			const TArray<FVector> Main = WigglePath(FVector(-Cm(26.0), 0.0, R), FVector(Cm(26.0), Cm(3.0), R * 0.8), 7, Cm(2.0), 1);
			AddBranch(P, Main, R, R * 0.55, Wood, true);
			const FVector Fork = Main[4];
			AddBranch(P, WigglePath(Fork, Fork + FVector(Cm(9.0), Cm(9.0), Cm(1.0)), 4, Cm(0.8), 2), R * 0.55, R * 0.25, Shade(Wood, 1.05f), true);
			break;
		}
		case 1:
		{
			const double R = Cm(5.0);
			const TArray<FVector> Trunk = WigglePath(FVector(-Cm(12.0), 0.0, R * 0.9), FVector(Cm(10.0), 0.0, R * 0.8), 4, Cm(1.0), 3);
			AddBranch(P, Trunk, R, R * 0.8, Wood, true);
			// Raíces: un abanico de tentáculos que salen del extremo gordo.
			for (int32 k = 0; k < 5; ++k)
			{
				const double Ang = UE_DOUBLE_PI * (0.6 + 0.8 * k / 4.0);
				const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
				const FVector Start = Trunk[0] + Dir * (R * 0.5);
				AddBranch(P, WigglePath(Start, Start + Dir * Cm(RndIn(Seed, 10 + k, 9.0, 13.0)) + FVector(0.0, 0.0, -R * 0.6), 4, Cm(1.2), 4 + k),
					R * 0.35, R * 0.1, Shade(Wood, 0.95f), k % 2 == 0);
			}
			break;
		}
		case 2:
		{
			// Uno apoyado en el otro: se sube al de abajo y por el de arriba.
			const double R = Cm(2.4);
			AddBranch(P, WigglePath(FVector(-Cm(22.0), -Cm(6.0), R * 0.7), FVector(Cm(22.0), Cm(6.0), R * 0.7), 6, Cm(1.0), 5), R, R * 0.7, Wood, true);
			AddBranch(P, WigglePath(FVector(-Cm(12.0), Cm(16.0), R * 0.6), FVector(Cm(10.0), -Cm(18.0), R * 4.15), 5, Cm(0.8), 6), R * 0.9, R * 0.6,
				Shade(Wood, 1.06f), true);
			break;
		}
		default:
		{
			const double R = Cm(2.2);
			AddBranch(P, WigglePath(FVector(-Cm(28.0), -Cm(4.0), R * 0.8), FVector(Cm(28.0), Cm(4.0), R * 0.8), 9, Cm(4.5), 7), R, R * 0.4, Wood, true);
			break;
		}
		}
	}

	/** Tapa de un tronco cortado (anillos de crecimiento) en At mirando hacia Out. */
	inline void AddLogEnd(FTNProcMeshBuffers& M, const FVector& At, const FVector& Out, double Rad, const FLinearColor& Cut, const FLinearColor& CutRing)
	{
		AddRevolve(M, FBeachFrame(At, AxisZTo(Out)), { FVector2D(Rad * 1.01, 0.0), FVector2D(Rad * 0.7, 2.0), FVector2D(Rad * 0.4, 2.0), FVector2D(0.0, 3.0) }, 10,
			[&](int32 Ring, int32) { return (Ring % 2) ? CutRing : Cut; });
	}

	/**
	 * Tronco con musgo: apoyado en una piedra, en rampa (0), medio enterrado (1), hueco para atravesarlo (2) o con
	 * repisas de hongos que hacen de escalones (3).
	 */
	inline void BuildMossyLog(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const FLinearColor Bark = Hex(Kind == 1 ? 0x8F8A7E : 0x5B4331);
		const FLinearColor BarkDark = Shade(Bark, 0.8f);
		const FLinearColor Moss = Hex(0x5E8C34);
		const FLinearColor MossLight = Hex(0x86B04A);
		const FLinearColor Cut = Hex(0xC9A46C);
		const FLinearColor CutRing = Hex(0x9C7A4A);
		P.Info.bBlocksCamera = true;
		P.Info.SinkMax = static_cast<float>(Cm(0.6));
		P.Info.TiltMax = 1.5f;
		const double HalfLen = Cm(42.0);
		// Musgo por arriba: con el eje en horizontal y Up fijo, los lados de coseno alto miran al cielo.
		auto BarkColor = [&](int32 Station, int32 Side) -> FLinearColor
		{
			const double Ca = FMath::Cos(TNProcMap::TwoPi * (Side + 0.5) / 10.0);
			if (Ca > 0.35 && Rnd(Seed, Station * 10 + Side) < 0.75) { return ((Station + Side) % 2) ? Moss : MossLight; }
			return ((Station + Side) % 3 == 0) ? BarkDark : Bark;
		};
		if (Kind == 2)
		{
			// Hueco: pared de corteza con el interior oscuro; se entra por los extremos.
			const double Ro = Cm(6.8);
			const double Ri = Cm(4.6);
			const double Zc = Cm(5.4);
			TArray<FVector2D> Prof;
			for (int32 s = 0; s <= 4; ++s) { Prof.Add(FVector2D(Ro, FMath::Lerp(-HalfLen, HalfLen, s / 4.0))); }
			for (int32 s = 4; s >= 0; --s) { Prof.Add(FVector2D(Ri, FMath::Lerp(-HalfLen, HalfLen, s / 4.0))); }
			Prof.Add(FVector2D(Ro, -HalfLen));
			const FBeachFrame F(FVector(0.0, 0.0, Zc), AxisZTo(FVector(1.0, 0.0, 0.0)));
			// Tramos: 0-3 corteza, 4 y 9 las bocas (anillos del corte), 5-8 el interior.
			AddRevolve(P.Body, F, Prof, 10, [&](int32 Ring, int32 Side) -> FLinearColor
			{
				if (Ring == 4 || Ring == 9) { return (Side % 2) ? CutRing : Cut; }
				if (Ring > 4) { return Hex(0x3A2A1C); }
				// Con el eje en X, el lado k mira hacia arriba cuando su coseno es negativo.
				const double Ca = FMath::Cos(TNProcMap::TwoPi * (Side + 0.5) / 10.0);
				if (Ca < -0.35 && Rnd(Seed, Ring * 10 + Side) < 0.75) { return (Ring + Side) % 2 ? Moss : MossLight; }
				return (Ring + Side) % 3 == 0 ? BarkDark : Bark;
			}, 0.04, Seed);
			P.ColTube(FVector(-HalfLen, 0.0, Zc), FVector(HalfLen, 0.0, Zc), Ro, Ro - Ri);
			return;
		}
		const double R = Cm(6.0);
		FVector A(-HalfLen, 0.0, -Cm(2.0));
		FVector B(HalfLen, 0.0, -Cm(2.0));
		if (Kind == 0)
		{
			// El extremo bajo, enterrado (se sube de un salto); el alto, apoyado en una piedra.
			const double RockX = Cm(28.0);
			const double RockH = Cm(6.8);
			AddBoulder(P, FVector(RockX, 0.0, 0.0), Cm(8.0), RockH, Seed + 5u, RockLookOf(1));
			const double ZLow = 90.0 - R;
			const double ZAtRock = RockH + R;
			const double Slope = (ZAtRock - ZLow) / (RockX + HalfLen);
			A = FVector(-HalfLen, 0.0, ZLow);
			B = FVector(HalfLen, 0.0, ZAtRock + (HalfLen - RockX) * Slope);
		}
		else if (Kind == 3)
		{
			A.Z = Cm(3.6);
			B.Z = Cm(3.6);
		}
		TArray<FVector> Path;
		for (int32 s = 0; s <= 6; ++s)
		{
			const double T = s / 6.0;
			Path.Add(FMath::Lerp(A, B, T) + FVector(0.0, Cm(1.5) * FMath::Sin(T * UE_DOUBLE_PI), 0.0));
		}
		AddSweep(P.Body, Path, CircleSection(10), false, [R](int32) { return R; }, BarkColor, false, FVector::UpVector);
		const FVector Axis = (B - A).GetSafeNormal();
		AddLogEnd(P.Body, Path[0], -Axis, R, Cut, CutRing);
		AddLogEnd(P.Body, Path.Last(), Axis, R, Cut, CutRing);
		P.ColCapsule(A + Axis * (R * 0.9), B - Axis * (R * 0.9), R * 0.97);
		// Muñones de ramas y setitas naranjas en el musgo.
		for (int32 b = 0; b < 2; ++b)
		{
			const FVector At = FMath::Lerp(A, B, b == 0 ? 0.32 : 0.7) + FVector(0.0, 0.0, R * 0.7);
			const FVector Out = FVector(RndIn(Seed, 120 + b, -0.4, 0.4), b == 0 ? 0.7 : -0.7, 0.8).GetSafeNormal();
			AddTaperTube(P.Body, { At, At + Out * Cm(4.5) }, R * 0.32, R * 0.22, 6, BarkDark);
		}
		for (int32 m = 0; m < 3; ++m)
		{
			const FVector At = FMath::Lerp(A, B, RndIn(Seed, 130 + m, 0.15, 0.85)) + FVector(0.0, RndIn(Seed, 140 + m, -R * 0.4, R * 0.4), R * 0.92);
			AddTube(P.Body, { At, At + FVector(0.0, 0.0, Cm(1.0)) }, Cm(0.25), 5, Hex(0xF0E2C8));
			AddRevolve(P.Body, FBeachFrame(At + FVector(0.0, 0.0, Cm(1.0)), FQuat::Identity), CapProfile(Cm(0.9), Cm(0.6), 2), 6, Hex(0xE8712A, 0.15f));
		}
		if (Kind == 3)
		{
			// Repisas de hongo por un costado: del suelo a 0,9 m, de ahí a 1,8 m y al lomo (2,7 m).
			for (int32 f = 0; f < 3; ++f)
			{
				const double Fx = Cm(-14.0 + 12.0 * f);
				const double Fz = 90.0 * (1 + f % 2);
				const double Dz = Fz - A.Z;
				const double Fy = -FMath::Sqrt(FMath::Max(0.0, R * R - Dz * Dz)) + 20.0;
				const TArray<FVector2D> Shelf = { FVector2D(-110.0, 0.0), FVector2D(-90.0, -60.0), FVector2D(-40.0, -105.0), FVector2D(40.0, -110.0), FVector2D(95.0, -70.0), FVector2D(110.0, 0.0) };
				AddPrismPoly(P.Body, FBeachFrame(FVector(Fx, Fy, 0.0), FQuat::Identity), Shelf, Fz - 30.0, Fz, Hex(0xE0B060), Hex(0xB9823E), true);
				P.ColBox(FVector(Fx, Fy - 55.0, Fz - 15.0), FQuat::Identity, FVector(105.0, 55.0, 15.0));
			}
		}
	}

	/** Hueso de sepia: óvalo blanco abombado con el borde duro y la punta. */
	inline void BuildCuttlebone(FParts& P, int32 Variant, uint32 Seed)
	{
		const FLinearColor Bone = Hex(Variant % 2 ? 0xF4F0E6 : 0xEFE6D2);
		const FLinearColor Edge = Hex(0xE2D6BE);
		const FLinearColor Line = Hex(0xE9E2D2);
		const double Rx = Cm(7.5) * (Variant == 2 ? 0.7 : 1.0);
		const double Ry = Cm(3.0);
		const double H = Cm(1.4);
		AddRevolve(P.Body, FBeachFrame(FVector(0.0, 0.0, -5.0), FQuat::Identity), CapProfile(Rx, H, 4, 10.0), 14, [&](int32 Ring, int32) -> FLinearColor
		{
			return Ring <= 1 ? Edge : ((Ring % 2) ? Line : Bone);
		}, 0.02, Seed, Ry / Rx);
		AddBlob(P.Body, FBeachFrame(FVector(-Rx * 0.97, 0.0, Cm(0.4)), AxisZTo(FVector(-1.0, 0.0, 0.2))), Cm(0.35), Cm(0.35), Cm(0.9), 5, 2, Edge);
		P.ColBox(FVector(0.0, 0.0, H * 0.4), FQuat::Identity, FVector(Rx * 0.85, Ry * 0.85, H * 0.4));
		P.Info.SinkMax = static_cast<float>(Cm(0.5));
		P.Info.TiltMax = 5.f;
	}

	/** Pluma de gaviota: cañón curvado y dos barbas (blanca o gris, con la punta negra en algunas). */
	inline void BuildGullFeather(FParts& P, int32 Variant, uint32 Seed)
	{
		const FLinearColor Vane = Hex(Variant % 2 ? 0xDADDE0 : 0xF2F2EE);
		const FLinearColor Tip = Hex(Variant == 2 ? 0x8E949C : 0x2B2B2E);
		const FLinearColor Shaft = Hex(0xF4F0E0);
		const double Len = Cm(15.0);
		const double Curl = Cm(RndIn(Seed, 1, 0.8, 1.6));
		auto ShaftAt = [&](double U) { return FVector(-Len * 0.5 + Len * U, Cm(0.8) * FMath::Sin(U * UE_DOUBLE_PI), 8.0 + Curl * U * U); };
		TArray<FVector> Path;
		for (int32 s = 0; s <= 8; ++s) { Path.Add(ShaftAt(s / 8.0)); }
		AddTaperTube(P.Body, Path, 9.0, 3.0, 4, Shaft, false);
		const bool bBlackTip = Variant != 1;
		for (const double Sy : { 1.0, -1.0 })
		{
			AddSheet(P.Body, 8, 1, [&](double U, double V)
			{
				const double Uu = 0.12 + 0.88 * U;
				const double Wd = Cm(1.6) * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Min(1.0, Uu * 1.1)), 0.6) * (Sy > 0.0 ? 1.0 : 0.75);
				// Por el lado -Y se recorre del borde al cañón para que la cara de arriba mire hacia arriba.
				const double Along = Sy > 0.0 ? V : 1.0 - V;
				return ShaftAt(Uu) + FVector(0.0, Sy * Wd * Along, Cm(0.3) * Along);
			}, 3.0, [&](int32 I, int32) { return (bBlackTip && I >= 6) ? Tip : Vane; });
		}
		P.Info.SinkMax = 4.f;
		P.Info.TiltMax = 4.f;
	}

	/** Cáscaras de pipas (rayadas), de pistachos y de cacahuetes, unas boca arriba y otras boca abajo. */
	inline void BuildSnackShells(FParts& P, int32 Variant, uint32 Seed)
	{
		const FLinearColor PipaBlack = Hex(0x2E2A26);
		const FLinearColor PipaWhite = Hex(0xE8E2D2);
		const FLinearColor Pista = Hex(0xE3CFA8);
		const FLinearColor PistaIn = Hex(0xD9B89A);
		const FLinearColor Peanut = Hex(0xCFA877);
		const int32 Count = 8 + static_cast<int32>(Rnd(Seed, 1) * 6.0);
		for (int32 n = 0; n < Count; ++n)
		{
			const double Ang = RndIn(Seed, 10 + n, 0.0, TNProcMap::TwoPi);
			const double Dist = Cm(RndIn(Seed, 30 + n, 0.0, 8.5));
			const FBeachFrame F(FVector(FMath::Cos(Ang) * Dist, FMath::Sin(Ang) * Dist, 0.0), YawQ(RndIn(Seed, 50 + n, 0.0, 360.0)));
			const bool bOpenUp = Rnd(Seed, 70 + n) < 0.5;
			const int32 Type = (Variant + n) % 3;
			const double L = Type == 0 ? Cm(1.1) : (Type == 1 ? Cm(1.0) : Cm(1.5));
			const double W = Type == 0 ? Cm(0.45) : (Type == 1 ? Cm(0.75) : Cm(0.6));
			const double H = Type == 0 ? Cm(0.25) : Cm(0.4);
			const FLinearColor Outer = Type == 1 ? Pista : Peanut;
			const FLinearColor Inner = Type == 1 ? PistaIn : Shade(Peanut, 1.1f);
			if (bOpenUp)
			{
				// Boca arriba: cuenco con la cara de dentro a la vista.
				const TArray<FVector2D> Prof = { FVector2D(0.0, 0.0), FVector2D(L * 0.7, H * 0.25), FVector2D(L, H), FVector2D(0.0, H * 0.7) };
				AddRevolve(P.Body, F, Prof, 6, [&](int32 Ring, int32 Side) -> FLinearColor
				{
					if (Ring == 2) { return Type == 0 ? PipaWhite : Inner; }
					return Type == 0 ? ((Side % 2) ? PipaBlack : PipaWhite) : Outer;
				}, 0.0, 0u, W / L);
			}
			else
			{
				AddRevolve(P.Body, F, CapProfile(L, H, 2), 6, [&](int32, int32 Side) -> FLinearColor
				{
					return Type == 0 ? ((Side % 2) ? PipaBlack : PipaWhite) : Outer;
				}, 0.05, Seed + static_cast<uint32>(n), W / L);
			}
		}
		P.Info.bCastShadow = false;
		P.Info.SinkMax = 3.f;
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Basura y cosas de la playa
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Aro (toro de sección redonda) de radio Radius y grosor Thick en el plano de normal Normal. */
	inline void AddLoop(FTNProcMeshBuffers& M, const FVector& Center, const FVector& Normal, double Radius, double Thick, int32 Seg, const FLinearColor& Color)
	{
		const FVector Nn = Normal.GetSafeNormal();
		const FVector U = FVector::CrossProduct(Nn, FMath::Abs(Nn.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector V = FVector::CrossProduct(Nn, U);
		TArray<FVector> Path;
		for (int32 k = 0; k < Seg; ++k)
		{
			const double Ang = TNProcMap::TwoPi * k / Seg;
			Path.Add(Center + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * Radius);
		}
		AddSweep(M, Path, CircleSection(5), true, [Thick](int32) { return Thick; }, [&Color](int32, int32) { return Color; }, false, Nn);
	}

	/** Anillas de un pack de seis latas (tumbadas, algo retorcidas), casi todas cortadas; sin colisión. */
	inline void BuildSixPackRings(FParts& P, int32 Variant, uint32 Seed)
	{
		const uint32 PlasticHex[4] = { 0xEEF2F2, 0xE4DDB0, 0xDDEBEF, 0xF0EFE8 };
		const FLinearColor Plastic = Hex(PlasticHex[Variant % 4], 0.25f);
		const double RingR = Cm(3.4);
		const double HalfThick = 5.0;
		const TArray<FVector2D> Sec = RectSection(HalfThick, Cm(0.4));
		for (int32 ix = -1; ix <= 1; ++ix)
		{
			for (int32 iy = 0; iy < 2; ++iy)
			{
				const int32 Idx = (ix + 1) * 2 + iy;
				const FVector C(ix * Cm(6.9), (iy - 0.5) * Cm(7.2), 0.0);
				const bool bCut = Rnd(Seed, Idx) < (Variant == 1 ? 0.35 : 0.7);
				const double Gap = bCut ? FMath::DegreesToRadians(RndIn(Seed, 10 + Idx, 25.0, 45.0)) : 0.0;
				const double Start = RndIn(Seed, 20 + Idx, 0.0, TNProcMap::TwoPi);
				const double Lift = Variant == 3 ? RndIn(Seed, 30 + Idx, 10.0, 80.0) : RndIn(Seed, 30 + Idx, 0.0, 30.0);
				const int32 Steps = 14;
				TArray<FVector> Path;
				for (int32 k = 0; k < (bCut ? Steps + 1 : Steps); ++k)
				{
					const double Ang = Start + (TNProcMap::TwoPi - Gap) * k / Steps;
					Path.Add(C + FVector(FMath::Cos(Ang) * RingR, FMath::Sin(Ang) * RingR, HalfThick + 2.0 + Lift * (0.5 + 0.5 * FMath::Sin(Ang * 2.0 + Idx))));
				}
				AddSweep(P.Body, Path, Sec, !bCut, [](int32) { return 1.0; }, [&Plastic](int32, int32) { return Plastic; }, bCut, FVector::UpVector);
			}
		}
		P.Info.bCastShadow = false;
		P.Info.SinkMax = 3.f;
		P.Info.TiltMax = 2.f;
	}

	/** Sujetador rojo: dos copas (del revés o una boca arriba), tirantes, espalda con corchetes y un lacito. */
	inline void BuildRedBra(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const FLinearColor Red = Hex(0xD62839, 0.15f);
		const FLinearColor RedDark = Hex(0xA3172B, 0.15f);
		const FLinearColor White = Hex(0xF8F4F0, 0.1f);
		const FLinearColor Lining = Hex(0xF2A6B4);
		const FLinearColor Metal = Hex(0xD8D8D8, 0.9f);
		const FLinearColor BowC = Kind == 2 ? White : Hex(0xF4A0B8);
		const double CupR = Cm(7.0);
		const double CupH = Cm(5.2);
		const double CupY = Cm(7.4);
		for (const double Sy : { -1.0, 1.0 })
		{
			const FVector C(0.0, Sy * CupY, 0.0);
			if (!(Kind == 3 && Sy > 0.0))
			{
				AddRevolve(P.Body, FBeachFrame(C, YawQ(Sy * 12.0)), CapProfile(CupR, CupH, 5, 8.0), 14, [&](int32 Ring, int32 Side) -> FLinearColor
				{
					if (Ring <= 1) { return Kind == 2 ? White : RedDark; }
					if (Kind == 1 && (Ring * 5 + Side * 3) % 7 == 0) { return White; }
					return (Ring == 3 && Side % 7 == 0) ? RedDark : Red;
				});
				P.ColCap(C, CupR * 0.96, CupH * 0.95);
			}
			else
			{
				// Copa boca arriba: cuenco con el forro rosa.
				TArray<FVector2D> Prof;
				for (int32 i = 0; i <= 4; ++i)
				{
					const double T = UE_DOUBLE_HALF_PI * i / 4.0;
					Prof.Add(FVector2D(CupR * FMath::Sin(T), CupH * (1.0 - FMath::Cos(T))));
				}
				for (int32 i = 4; i >= 0; --i)
				{
					const double T = UE_DOUBLE_HALF_PI * i / 4.0;
					Prof.Add(FVector2D(i == 0 ? 0.0 : (CupR - 10.0) * FMath::Sin(T), 10.0 + (CupH - 10.0) * (1.0 - FMath::Cos(T))));
				}
				AddRevolve(P.Body, FBeachFrame(C, FQuat::Identity), Prof, 14, [&](int32 Ring, int32) -> FLinearColor
				{
					return Ring < 4 ? Red : (Ring == 4 ? White : Lining);
				});
				P.ColRing(C, CupR - 20.0, 20.0, 0.0, CupH);
			}
		}
		// Puente entre las copas y lazo.
		AddYawBox(P.Body, FVector(Cm(1.6), 0.0, Cm(0.9)), 0.0, FVector(Cm(1.0), Cm(0.8), Cm(0.5)), RedDark);
		const FVector Bow(Cm(2.4), 0.0, Cm(1.7));
		for (const double Sy : { -1.0, 1.0 })
		{
			const FVector B1 = Bow + FVector(Cm(0.5), Sy * Cm(1.2), Cm(0.6));
			const FVector B2 = Bow + FVector(-Cm(0.4), Sy * Cm(1.1), -Cm(0.4));
			P.Body.AddTri(Bow, B1, B2, FVector(0.3, 0.0, 1.0), BowC);
			P.Body.AddTri(Bow, B1, B2, FVector(-0.3, 0.0, -1.0), Shade(BowC, 0.8f));
		}
		// Espalda: dos bandas que se curvan hacia atrás, con corchetes; tirantes sueltos hacia delante.
		const TArray<FVector2D> BandSec = RectSection(9.0, Cm(1.6));
		const TArray<FVector2D> StrapSec = RectSection(7.0, Cm(0.75));
		for (const double Sy : { -1.0, 1.0 })
		{
			const TArray<FVector> Wing = { FVector(-Cm(1.2), Sy * Cm(13.4), 16.0), FVector(-Cm(5.4), Sy * Cm(15.2), 14.0), FVector(-Cm(10.4), Sy * Cm(14.2), 12.0),
				FVector(-Cm(13.4), Sy * Cm(10.6), 12.0), FVector(-Cm(14.2), Sy * Cm(7.0), 12.0) };
			AddSweep(P.Body, Wing, BandSec, false, [](int32) { return 1.0; }, [&](int32 i, int32) { return (Kind == 2 && i == 0) ? White : Red; }, true, FVector::UpVector);
			AddYawBox(P.Body, Wing.Last() + FVector(0.0, 0.0, 8.0), 0.0, FVector(Cm(0.5), Cm(1.2), 6.0), Metal);
			const bool bLoop = Kind == 2 && Sy > 0.0;
			const TArray<FVector> Strap = { FVector(Cm(2.0), Sy * Cm(6.2), CupH * 0.8), FVector(Cm(6.0), Sy * Cm(6.8), bLoop ? Cm(7.0) : Cm(2.0)),
				FVector(Cm(10.5), Sy * Cm(6.0), bLoop ? Cm(6.0) : 10.0), FVector(Cm(13.2), Sy * Cm(8.0), 10.0), FVector(Cm(13.0), Sy * Cm(11.0), 10.0) };
			AddSweep(P.Body, Strap, StrapSec, false, [](int32) { return 1.0; }, [&Red](int32, int32) { return Red; }, true);
			AddYawBox(P.Body, Strap[2] + FVector(0.0, 0.0, 10.0), 20.0, FVector(Cm(0.5), Cm(0.9), 5.0), Metal);
		}
		P.Info.SinkMax = static_cast<float>(Cm(0.5));
		P.Info.TiltMax = 3.f;
	}

	/** Vaso de plástico: tumbado para meterse dentro (0 rojo de fiesta, 1 transparente), de pie medio enterrado (2) o aplastado (3). */
	inline void BuildPlasticCup(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 OutHex[4] = { 0xD8282E, 0xCFE6EE, 0xF4F4F2, 0x3E7CC8 };
		const FLinearColor Outside = Hex(OutHex[Kind], Kind == 1 ? 0.45f : 0.2f);
		const FLinearColor Inside = Kind == 0 ? Hex(0xF4F2EE, 0.2f) : Shade(Outside, 0.92f);
		const FLinearColor RimC = Kind == 0 ? Hex(0xF4F2EE, 0.2f) : Shade(Outside, 1.08f);
		const double Rb = Cm(2.6);
		const double Rt = Cm(3.75);
		const double H = Cm(9.5);
		const double Wall = 8.0;
		const TArray<FVector2D> Prof = { FVector2D(0.0, 0.0), FVector2D(Rb, 0.0), FVector2D(Rb + (Rt - Rb) * 0.5, H * 0.5), FVector2D(Rt, H), FVector2D(Rt + 6.0, H + 4.0),
			FVector2D(Rt - Wall, H + 4.0), FVector2D(Rt - Wall, H - 2.0), FVector2D(Rb - Wall, Wall), FVector2D(0.0, Wall) };
		// Tramos: 0 culo, 1-2 pared por fuera (con estrías en el rojo), 3-5 el borde enrollado, 6-7 por dentro.
		auto ColorAt = [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring >= 3 && Ring <= 5) { return RimC; }
			if (Ring >= 6) { return Inside; }
			if (Kind == 0 && Ring == 2 && Side % 3 == 0) { return Shade(Outside, 0.9f); }
			return Outside;
		};
		P.Info.SinkMax = static_cast<float>(Cm(0.5));
		P.Info.TiltMax = 3.f;
		switch (Kind)
		{
		case 0:
		case 1:
		{
			// Tumbado con la generatriz de abajo a ras de suelo: el eje sube hacia la boca.
			const FVector Base(-H * 0.5, 0.0, Rb);
			const FVector Dir = FVector(H, 0.0, Rt - Rb).GetSafeNormal();
			AddRevolve(P.Body, FBeachFrame(Base, AxisZTo(Dir)), Prof, 12, ColorAt, Kind == 1 ? 0.03 : 0.0, Seed);
			P.ColTube(Base, Base + Dir * H, (Rb + Rt) * 0.5, 14.0);
			P.ColBox(Base + Dir * 6.0, FRotationMatrix::MakeFromXY(Dir, FVector(0.0, 1.0, 0.0)).ToQuat(), FVector(6.0, Rb * 0.9, Rb * 0.9));
			break;
		}
		case 2:
			AddRevolve(P.Body, FBeachFrame(), Prof, 12, ColorAt);
			P.ColRing(FVector::ZeroVector, (Rb + Rt) * 0.5 - 12.0, 12.0, 0.0, H);
			P.Info.SinkMin = static_cast<float>(Cm(5.0));
			P.Info.SinkMax = static_cast<float>(Cm(5.6));
			P.Info.TiltMax = 6.f;
			break;
		default:
			// Aplastado: tumbado y chafado en vertical (SquashY va a Z con el giro de 90° sobre el eje).
			AddRevolve(P.Body, FBeachFrame(FVector(-H * 0.5, 0.0, Rt * 0.32), AxisZTo(FVector(1.0, 0.0, 0.0)) * FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI)), Prof, 12,
				ColorAt, 0.12, Seed, 0.3);
			P.ColBox(FVector(0.0, 0.0, Rt * 0.3), FQuat::Identity, FVector(H * 0.5, Rt * 0.95, Rt * 0.3));
			break;
		}
	}

	/** Botella: tumbada con etiqueta (0), clavada boca abajo (1), tumbada con un mensaje (2) o clavada de culo con su chapa (3). */
	inline void BuildBottle(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 GlassHex[4] = { 0x2F7D4A, 0x6B3A16, 0xBFE0E6, 0x3E8F5A };
		const FLinearColor Glass = Hex(GlassHex[Kind], 0.5f);
		const FLinearColor Label = Hex(Kind == 1 ? 0xE9C46A : 0xF2E6C9, 0.05f);
		const FLinearColor LabelBand = Hex(Kind == 1 ? 0xB0302A : 0xC0392B, 0.05f);
		const FLinearColor Mouth = Shade(Glass, 0.35f);
		const FLinearColor CapC = Hex(0xC9C9C9, 0.9f);
		const double R = Cm(3.0);
		const double Body = Cm(15.5);
		const double Neck = Cm(1.25);
		const double Total = Cm(25.0);
		const TArray<FVector2D> Prof = { FVector2D(0.0, 8.0), FVector2D(R * 0.9, 0.0), FVector2D(R, 12.0), FVector2D(R, Body * 0.35), FVector2D(R, Body * 0.72),
			FVector2D(R, Body), FVector2D(R * 0.82, Body + Cm(1.4)), FVector2D(Neck * 1.15, Body + Cm(4.0)), FVector2D(Neck, Body + Cm(5.0)), FVector2D(Neck, Total - Cm(1.2)),
			FVector2D(Neck * 1.25, Total - Cm(1.0)), FVector2D(Neck * 1.25, Total), FVector2D(Neck * 0.8, Total), FVector2D(Neck * 0.75, Total - Cm(2.0)), FVector2D(0.0, Total - Cm(2.0)) };
		// Tramos: 0-1 culo, 2-4 cuerpo (3 la etiqueta), 5-8 hombro y cuello, 9-11 gollete, 12-13 la boca por dentro.
		auto ColorAt = [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring >= 12) { return Mouth; }
			if (Ring == 3 && Kind != 2) { return (Side % 6 == 0) ? LabelBand : Label; }
			return Glass;
		};
		const TArray<FVector2D> CapProf = { FVector2D(Neck * 1.35, -Cm(0.9)), FVector2D(Neck * 1.35, Cm(0.2)), FVector2D(0.0, Cm(0.3)) };
		switch (Kind)
		{
		case 1:
		{
			const FVector Dir(FMath::Sin(FMath::DegreesToRadians(15.0)), 0.0, -FMath::Cos(FMath::DegreesToRadians(15.0)));
			const FVector Base(-Dir.X * Cm(6.5), 0.0, Cm(13.0));
			AddRevolve(P.Body, FBeachFrame(Base, AxisZTo(Dir)), Prof, 12, ColorAt);
			P.ColCapsule(Base + Dir * R, Base + Dir * Body, R * 0.95);
			P.Info.TiltMax = 2.f;
			break;
		}
		case 3:
		{
			const double Lean = FMath::DegreesToRadians(35.0);
			const FVector Dir(FMath::Sin(Lean), 0.0, FMath::Cos(Lean));
			const FVector Base = -Dir * Cm(7.0) - FVector(Dir.X * Cm(9.0), 0.0, 0.0);
			AddRevolve(P.Body, FBeachFrame(Base, AxisZTo(Dir)), Prof, 12, ColorAt);
			AddRevolve(P.Body, FBeachFrame(Base + Dir * Total, AxisZTo(Dir)), CapProf, 10, CapC);
			P.ColCapsule(Base + Dir * Cm(7.0), Base + Dir * Body, R * 0.95);
			P.ColCapsule(Base + Dir * Body, Base + Dir * (Total - Neck), Neck * 1.2);
			P.Info.TiltMax = 2.f;
			break;
		}
		default:
		{
			const FBeachFrame F(FVector(-Total * 0.5, 0.0, R * 0.95), AxisZTo(FVector(1.0, 0.0, 0.0)));
			AddRevolve(P.Body, F, Prof, 12, ColorAt);
			if (Kind == 2)
			{
				// Mensaje: un rollo de papel atado que asoma por el cuello.
				AddTube(P.Body, { F.P(FVector(0.0, 0.0, Total - Cm(4.0))), F.P(FVector(0.0, 0.0, Total + Cm(1.2))) }, Neck * 0.6, 8, Hex(0xF2E6C9));
				AddTube(P.Body, { F.P(FVector(0.0, 0.0, Total + Cm(0.2))), F.P(FVector(0.0, 0.0, Total + Cm(0.6))) }, Neck * 0.66, 8, Hex(0xC0392B));
			}
			else
			{
				AddRevolve(P.Body, FBeachFrame(F.P(FVector(0.0, 0.0, Total)), F.Q), CapProf, 10, CapC);
			}
			P.ColCapsule(F.P(FVector(0.0, 0.0, R)), F.P(FVector(0.0, 0.0, Body - R * 0.5)), R * 0.95);
			P.ColCapsule(F.P(FVector(0.0, 0.0, Body)), F.P(FVector(0.0, 0.0, Total - Neck)), Neck * 1.2);
			P.Info.SinkMax = static_cast<float>(Cm(0.8));
			P.Info.TiltMax = 3.f;
			break;
		}
		}
	}

	/** Chupachups: de bola tumbado (0 con el envoltorio abierto, 1 chupado con arena) o piruleta en espiral tumbada (2) o clavada (3). */
	inline void BuildLollipop(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const FLinearColor Stick = Hex(0xF6F3EA, 0.1f);
		const double StickR = Cm(0.3);
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 4.f;
		if (Kind <= 1)
		{
			const FLinearColor Candy = Hex(Kind == 0 ? 0xC8102E : 0xF28C1E, 0.45f);
			const double BallR = Cm(2.1);
			const FVector Ball(-Cm(4.0), 0.0, BallR);
			AddBlob(P.Body, FBeachFrame(Ball, YawQ(RndIn(Seed, 1, 0.0, 360.0))), BallR, BallR, BallR * (Kind == 1 ? 0.8 : 1.0), 10, 6, Candy, 0.03, Seed);
			AddTube(P.Body, { Ball + FVector(BallR * 0.7, 0.0, -BallR * 0.3), FVector(Cm(6.0), Cm(0.6), StickR) }, StickR, 6, Stick);
			P.ColSphere(Ball, BallR * 0.95);
			if (Kind == 0)
			{
				// Envoltorio: pétalos arrugados hacia atrás, amarillos y rojos.
				const FVector Knot = Ball + FVector(BallR * 0.85, 0.0, -BallR * 0.15);
				for (int32 k = 0; k < 7; ++k)
				{
					const double Ang = TNProcMap::TwoPi * k / 7.0;
					const FVector Dir(0.0, FMath::Cos(Ang), FMath::Sin(Ang));
					const FVector Tip = Knot + Dir * (BallR * 1.25) - FVector(BallR * (0.3 + 0.5 * (1.0 - FMath::Abs(Dir.Z))), 0.0, 0.0);
					const FVector Wide = FVector::CrossProduct(FVector(1.0, 0.0, 0.0), Dir) * (BallR * 0.35);
					const FLinearColor Wrap = Hex((k % 2) ? 0xF5C518 : 0xE8412C, 0.5f);
					P.Body.AddTri(Knot, Tip + Wide, Tip - Wide, Dir, Wrap);
					P.Body.AddTri(Knot, Tip + Wide, Tip - Wide, -Dir, Shade(Wrap, 0.8f));
				}
			}
			else
			{
				// Granos de arena pegados.
				for (int32 g = 0; g < 7; ++g)
				{
					const double A1 = RndIn(Seed, 20 + g, 0.0, TNProcMap::TwoPi);
					const double A2 = RndIn(Seed, 30 + g, 0.2, 1.4);
					const FVector Nn(FMath::Cos(A1) * FMath::Sin(A2), FMath::Sin(A1) * FMath::Sin(A2), FMath::Cos(A2));
					AddBump(P.Body, Ball + Nn * (BallR * 0.97), Nn, Cm(0.25), Hex(0xE3C99A));
				}
			}
			return;
		}
		const FLinearColor SwirlA = Hex(Kind == 2 ? 0xE8412C : 0x7B3FB8, 0.45f);
		const FLinearColor SwirlB = Hex(Kind == 2 ? 0xF6F2EA : 0xF5C518, 0.45f);
		const double DiscR = Cm(3.2);
		const double DiscT = Cm(0.45);
		const TArray<FVector2D> Disc = { FVector2D(0.0, -DiscT), FVector2D(DiscR * 0.97, -DiscT), FVector2D(DiscR, 0.0), FVector2D(DiscR * 0.97, DiscT),
			FVector2D(DiscR * 0.72, DiscT * 1.05), FVector2D(DiscR * 0.46, DiscT * 1.1), FVector2D(DiscR * 0.22, DiscT * 1.15), FVector2D(0.0, DiscT * 1.2) };
		auto Swirl = [&](int32 Ring, int32 Side) -> FLinearColor { return ((Side + Ring * 3) % 4 < 2) ? SwirlA : SwirlB; };
		if (Kind == 2)
		{
			const FVector C(-Cm(2.5), 0.0, DiscT + 4.0);
			AddRevolve(P.Body, FBeachFrame(C, FQuat::Identity), Disc, 16, Swirl);
			AddTube(P.Body, { C + FVector(DiscR * 0.9, 0.0, 0.0), FVector(Cm(8.0), Cm(1.0), StickR) }, StickR, 6, Stick);
			P.ColBox(C, FQuat::Identity, FVector(DiscR * 0.85, DiscR * 0.85, DiscT));
			return;
		}
		// Clavada de pie: el palo en la arena y el disco en lo alto, de canto.
		const FVector Top(0.0, 0.0, Cm(8.0));
		AddTube(P.Body, { FVector(0.0, 0.0, -Cm(2.0)), Top }, StickR, 6, Stick);
		const FVector C = Top + FVector(0.0, 0.0, DiscR * 0.95);
		AddRevolve(P.Body, FBeachFrame(C, AxisZTo(FVector(0.0, 1.0, 0.0))), Disc, 16, Swirl);
		P.ColCapsule(FVector::ZeroVector, Top, StickR * 1.5);
		P.ColBox(C, FQuat::Identity, FVector(DiscR * 0.85, DiscT, DiscR * 0.85));
		P.Info.SinkMax = 0.f;
	}

	/**
	 * Tajada de sandía comida, tumbada en el plano XY del marco F con el grosor Thick hacia +Z: arco de corteza verde,
	 * franja blanca y lo que queda de carne mordisqueada (FleshT de grueso; 0 = comida hasta lo blanco), con pepitas. Span
	 * en radianes; colisión: cuatro cajas por el arco.
	 */
	inline void AddMelonSlice(FParts& P, const FBeachFrame& F, double Span, double Thick, double FleshT, uint32 SliceSeed)
	{
		const FLinearColor Skin = Hex(0x2F7A34);
		const FLinearColor SkinStripe = Hex(0x4E9A40);
		const FLinearColor White = Hex(0xE9F0C8);
		const FLinearColor Flesh = Hex(0xF0506A, 0.2f);
		const FLinearColor Pip = Hex(0x1E1A18, 0.3f);
		const double R = Cm(12.0);
		const double SkinT = Cm(0.9);
		const double WhiteT = Cm(0.9);
		const bool bFlesh = FleshT > 1.0;
		const int32 Steps = 14;
		const double Mid = R - (SkinT + WhiteT + FleshT) * 0.5;
		const double Phase = static_cast<double>(SliceSeed % 7u);
		auto Bite = [Phase](double Th) { return 0.35 + 0.65 * FMath::Square(0.5 + 0.5 * FMath::Cos(Th * 23.0 + Phase)); };
		auto At = [&F, Mid](double Th, double Rad, double Z) { return F.P(FVector(FMath::Cos(Th) * Rad - Mid, FMath::Sin(Th) * Rad, Z)); };
		const FVector Up = F.D(FVector::UpVector);
		const double R1 = R - SkinT;
		const double R2 = R1 - WhiteT;
		for (int32 i = 0; i < Steps; ++i)
		{
			const double T0 = -Span * 0.5 + Span * i / Steps;
			const double T1 = -Span * 0.5 + Span * (i + 1) / Steps;
			const double R3a = R2 - FleshT * Bite(T0);
			const double R3b = R2 - FleshT * Bite(T1);
			P.Body.AddQuad(At(T0, R1, Thick), At(T1, R1, Thick), At(T1, R, Thick), At(T0, R, Thick), Up, (i % 3 == 0) ? SkinStripe : Skin);
			P.Body.AddQuad(At(T0, R2, Thick), At(T1, R2, Thick), At(T1, R1, Thick), At(T0, R1, Thick), Up, White);
			if (bFlesh) { P.Body.AddQuad(At(T0, R3a, Thick), At(T1, R3b, Thick), At(T1, R2, Thick), At(T0, R2, Thick), Up, Flesh); }
			// Corteza por fuera (baja un poco por debajo) y el mordisco por dentro.
			const double Tm = (T0 + T1) * 0.5;
			const FVector Out = F.D(FVector(FMath::Cos(Tm), FMath::Sin(Tm), 0.0));
			P.Body.AddQuad(At(T0, R, -12.0), At(T1, R, -12.0), At(T1, R, Thick), At(T0, R, Thick), Out, (i % 2) ? SkinStripe : Skin);
			const double Rin0 = bFlesh ? R3a : R2;
			const double Rin1 = bFlesh ? R3b : R2;
			P.Body.AddQuad(At(T0, Rin0, -12.0), At(T1, Rin1, -12.0), At(T1, Rin1, Thick), At(T0, Rin0, Thick), -Out, bFlesh ? Shade(Flesh, 0.85f) : White);
		}
		for (int32 End = 0; End < 2; ++End)
		{
			const double Th = End == 0 ? -Span * 0.5 : Span * 0.5;
			const double Rin = R2 - FleshT * Bite(Th);
			const FVector Tan = F.D(FVector(-FMath::Sin(Th), FMath::Cos(Th), 0.0)) * (End == 0 ? -1.0 : 1.0);
			P.Body.AddQuad(At(Th, Rin, -12.0), At(Th, R, -12.0), At(Th, R, Thick), At(Th, Rin, Thick), Tan, White);
		}
		if (bFlesh)
		{
			for (int32 s = 0; s < 8; ++s)
			{
				const double Th = RndIn(SliceSeed, 40 + s, -Span * 0.4, Span * 0.4);
				const double Rad = R2 - FleshT * RndIn(SliceSeed, 50 + s, 0.15, 0.3);
				const FVector C = At(Th, Rad, Thick + 1.5);
				const FVector Rd = F.D(FVector(FMath::Cos(Th), FMath::Sin(Th), 0.0));
				const FVector Tn = F.D(FVector(-FMath::Sin(Th), FMath::Cos(Th), 0.0));
				P.Body.AddTri(C + Rd * Cm(0.35), C - Rd * Cm(0.25) + Tn * Cm(0.2), C - Rd * Cm(0.25) - Tn * Cm(0.2), Up, Pip);
			}
		}
		const double RinC = R2 - FleshT * 0.6;
		for (int32 b = 0; b < 4; ++b)
		{
			const double Tm = -Span * 0.5 + Span * (b + 0.5) / 4.0;
			P.ColBox(At(Tm, (R + RinC) * 0.5, Thick * 0.5), F.Q * YawQ(FMath::RadiansToDegrees(Tm)), FVector((R - RinC) * 0.5, R * FMath::Sin(Span / 8.0) * 1.02, Thick * 0.5));
		}
	}

	/** Corteza de sandía roída: con carne (0), comida hasta lo blanco (1), de pie como un barquito (2) o dos trozos (3). */
	inline void BuildWatermelonRind(FParts& P, int32 Variant, uint32 Seed)
	{
		P.Info.SinkMax = static_cast<float>(Cm(0.6));
		P.Info.TiltMax = 3.f;
		switch (Variant % 4)
		{
		case 0:
			AddMelonSlice(P, FBeachFrame(), FMath::DegreesToRadians(150.0), Cm(3.4), Cm(1.8), Seed);
			break;
		case 1:
			AddMelonSlice(P, FBeachFrame(), FMath::DegreesToRadians(140.0), Cm(3.0), 0.0, Seed);
			break;
		case 2:
		{
			// Local X al suelo (-Z), local Y a +X: la corteza abajo y la carne arriba.
			const FQuat Q = FRotationMatrix::MakeFromXY(FVector(0.0, 0.0, -1.0), FVector(1.0, 0.0, 0.0)).ToQuat();
			AddMelonSlice(P, FBeachFrame(FVector(0.0, Cm(1.6), Cm(1.5)), Q), FMath::DegreesToRadians(150.0), Cm(3.2), Cm(1.2), Seed);
			P.Info.TiltMax = 8.f;
			break;
		}
		default:
			AddMelonSlice(P, FBeachFrame(FVector(-Cm(4.0), -Cm(3.5), 0.0), YawQ(30.0)), FMath::DegreesToRadians(75.0), Cm(2.6), Cm(1.0), Seed);
			AddMelonSlice(P, FBeachFrame(FVector(Cm(5.0), Cm(3.0), 0.0), YawQ(200.0)), FMath::DegreesToRadians(65.0), Cm(2.4), Cm(0.6), Seed + 3u);
			break;
		}
	}

	/** Pajita de rayas: recta (0), doblada por el acordeón (1), de papel reblandecida (2) o clavada en la arena (3). */
	inline void BuildStraw(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 BaseHex[4] = { 0xF4F2EE, 0xF4F2EE, 0x86B84E, 0xF4F2EE };
		const uint32 StripeHex[4] = { 0xD8282E, 0x2E6FD0, 0x5E9A36, 0xE87A1E };
		const FLinearColor Base = Hex(BaseHex[Kind], Kind == 2 ? 0.f : 0.25f);
		const FLinearColor Stripe = Hex(StripeHex[Kind], Kind == 2 ? 0.f : 0.25f);
		const double R = 12.0;
		const double Len = Cm(20.0);
		TArray<FVector> Path;
		int32 BendFrom = -1;
		int32 BendTo = -1;
		switch (Kind)
		{
		case 1:
		{
			const double Straight = Len * 0.62;
			for (int32 s = 0; s <= 12; ++s) { Path.Add(FVector(-Len * 0.45 + Straight * s / 12.0, 0.0, R)); }
			BendFrom = Path.Num();
			const FVector Pivot = Path.Last() + FVector(0.0, Cm(2.0), 0.0);
			for (int32 s = 1; s <= 5; ++s)
			{
				const double Ang = -UE_DOUBLE_HALF_PI + (UE_DOUBLE_PI / 3.0) * s / 5.0;
				Path.Add(Pivot + FVector(FMath::Cos(Ang) * Cm(2.0), FMath::Sin(Ang) * Cm(2.0), 0.0));
			}
			BendTo = Path.Num();
			const FVector Dir = (Path.Last() - Path[Path.Num() - 2]).GetSafeNormal();
			const FVector From = Path.Last();
			for (int32 s = 1; s <= 6; ++s) { Path.Add(From + Dir * (Len * 0.3 * s / 6.0) + FVector(0.0, 0.0, 4.0 * s)); }
			break;
		}
		case 3:
		{
			const FVector Dir(FMath::Sin(0.15), 0.0, FMath::Cos(0.15));
			for (int32 s = 0; s <= 10; ++s) { Path.Add(FVector(0.0, 0.0, -Cm(4.0)) + Dir * (Len * s / 10.0)); }
			P.ColCapsule(Path[2], Path.Last(), R * 1.3);
			break;
		}
		default:
		{
			for (int32 s = 0; s <= 16; ++s)
			{
				const double T = s / 16.0;
				Path.Add(FVector(-Len * 0.5 + Len * T, Cm(0.6) * FMath::Sin(T * 3.0 + Kind), R + (Kind == 2 ? -3.0 : 0.0)));
			}
			break;
		}
		}
		AddSweep(P.Body, Path, CircleSection(6, Kind == 2 ? 0.55 : 1.0), false,
			[BendFrom, BendTo, R](int32 i) { return (i >= BendFrom && i < BendTo && i % 2 == 0) ? R * 1.25 : R; },
			[&](int32 i, int32 Side) -> FLinearColor
			{
				if (Kind == 2) { return ((i / 3) % 2) ? Base : Shade(Base, 0.9f); }
				return ((i + Side) % 3 == 0) ? Stripe : Base;
			}, true);
		P.Info.bCastShadow = Kind == 3;
		P.Info.SinkMax = Kind == 3 ? 0.f : 3.f;
	}

	/** Lata: tumbada (0 roja, 1 azul), aplastada tumbada (2) o chafada de pie medio enterrada (3). */
	inline void BuildSodaCan(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 PaintHex[4] = { 0xD4202A, 0x2A5CB8, 0x3F9B3A, 0xF2C81E };
		const uint32 BandHex[4] = { 0xF4F4F4, 0xC8D0DA, 0xF2E24A, 0x2A2A2A };
		const FLinearColor Paint = Hex(PaintHex[Kind], 0.55f);
		const FLinearColor Band = Hex(BandHex[Kind], 0.55f);
		const FLinearColor Alu = Hex(0xCDD1D6, 0.9f);
		const double R = Cm(3.3);
		const double H = Cm(12.2);
		const double Crush = Kind == 3 ? 0.55 : 1.0;
		const double Pinch = Kind == 2 ? 0.72 : 1.0;
		const TArray<FVector2D> Prof = { FVector2D(0.0, 14.0 * Crush), FVector2D(R * 0.8, 0.0), FVector2D(R, 10.0 * Crush), FVector2D(R, H * 0.2 * Crush),
			FVector2D(R * Pinch, H * 0.5 * Crush), FVector2D(R, H * 0.8 * Crush), FVector2D(R, (H - 12.0) * Crush), FVector2D(R * 0.88, (H - 2.0) * Crush),
			FVector2D(R * 0.86, (H + 3.0) * Crush), FVector2D(R * 0.78, (H + 1.0) * Crush), FVector2D(0.0, (H - 4.0) * Crush) };
		// Tramos: 0-1 fondo de aluminio, 2-5 la lata pintada (3 y 4 con la franja), 6-9 cuello, borde y tapa.
		auto ColorAt = [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring <= 1 || Ring >= 6) { return Alu; }
			if ((Ring == 3 || Ring == 4) && (Side / 2) % 2 == 0) { return Band; }
			return Paint;
		};
		P.Info.TiltMax = 4.f;
		switch (Kind)
		{
		case 2:
			AddRevolve(P.Body, FBeachFrame(FVector(-H * 0.5, 0.0, R * 0.42), AxisZTo(FVector(1.0, 0.0, 0.0)) * FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI)), Prof, 12,
				ColorAt, 0.12, Seed, 0.42);
			P.ColBox(FVector(0.0, 0.0, R * 0.42), FQuat::Identity, FVector(H * 0.5, R * 0.95, R * 0.42));
			P.Info.SinkMax = static_cast<float>(Cm(0.3));
			break;
		case 3:
			AddRevolve(P.Body, FBeachFrame(), Prof, 12, ColorAt, 0.12, Seed);
			P.ColPrism(FVector::ZeroVector, R * 0.95, 0.0, H * Crush, 22.5);
			P.Info.SinkMin = static_cast<float>(Cm(1.0));
			P.Info.SinkMax = static_cast<float>(Cm(2.5));
			break;
		default:
		{
			const FBeachFrame F(FVector(-H * 0.5, 0.0, R), AxisZTo(FVector(1.0, 0.0, 0.0)));
			AddRevolve(P.Body, F, Prof, 12, ColorAt);
			AddOBox(P.Body, F.P(FVector(R * 0.3, 0.0, H - 2.0)), F.Q, FVector(Cm(0.5), Cm(0.35), 3.0), Alu);
			P.ColCapsule(F.P(FVector(0.0, 0.0, R)), F.P(FVector(0.0, 0.0, H - R)), R * 0.95);
			P.Info.SinkMax = static_cast<float>(Cm(0.6));
			break;
		}
		}
	}

	/** Dos a cuatro chapas de botella, alguna boca arriba (con el corcho de dentro) y algo dobladas. */
	inline void BuildBottleCaps(FParts& P, int32 Variant, uint32 Seed)
	{
		const uint32 TopHex[5] = { 0xC8202A, 0xD9A520, 0xB8BEC6, 0x2E7D3A, 0x2A5CB8 };
		const FLinearColor Liner = Hex(0xE9E4D8, 0.1f);
		const int32 Count = 2 + Variant % 3;
		const double R = Cm(1.6);
		const double H = Cm(0.6);
		const TArray<FVector2D> Prof = { FVector2D(0.0, 3.0), FVector2D(R * 0.95, 3.0), FVector2D(R * 1.08, 0.0), FVector2D(R, H * 0.8), FVector2D(R * 0.9, H), FVector2D(0.0, H) };
		for (int32 c = 0; c < Count; ++c)
		{
			const FLinearColor Top = Hex(TopHex[(Variant + c * 2) % 5], 0.85f);
			const double Ang = TNProcMap::TwoPi * c / Count + RndIn(Seed, c, -0.4, 0.4);
			const FVector At = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * Cm(RndIn(Seed, 10 + c, 1.4, 2.6));
			const bool bUp = Rnd(Seed, 20 + c) < 0.4;
			const FQuat Tilt(FVector(FMath::Cos(Ang * 2.0), FMath::Sin(Ang * 2.0), 0.0), FMath::DegreesToRadians(RndIn(Seed, 30 + c, 0.0, 14.0)));
			// Tramos: 0 el corcho de dentro, 1-2 la falda rizada, 3-4 la cara de arriba.
			const FBeachFrame F = bUp ? FBeachFrame(At + FVector(0.0, 0.0, H), Tilt * FQuat(FVector(1.0, 0.0, 0.0), UE_DOUBLE_PI)) : FBeachFrame(At, Tilt);
			AddRevolve(P.Body, F, Prof, 14, [&](int32 Ring, int32 Side) -> FLinearColor
			{
				if (Ring == 0) { return Liner; }
				if (Ring <= 2) { return (Side % 2) ? Top : Shade(Top, 0.8f); }
				return Ring == 4 ? Shade(Top, 1.1f) : Top;
			}, 0.04, Seed + static_cast<uint32>(c));
		}
		P.Info.bCastShadow = false;
		P.Info.SinkMax = 3.f;
	}

	/** Contorno de la suela de una chancla de largo Len (dedos hacia +X) y ancho Wid, con la cintura en el puente. */
	inline TArray<FVector2D> SoleOutline(double Len, double Wid)
	{
		TArray<FVector2D> Poly;
		for (int32 i = 0; i < 18; ++i)
		{
			const double Ang = TNProcMap::TwoPi * i / 18.0;
			const double Cx = FMath::Cos(Ang);
			const double Sn = FMath::Sin(Ang);
			const double Widen = Cx > 0.0 ? 1.0 + 0.12 * Cx : 0.88;
			const double Waist = 1.0 - 0.18 * FMath::Exp(-FMath::Square((Cx + 0.1) / 0.35));
			Poly.Add(FVector2D(Cx * Len * 0.5, Sn * Wid * 0.5 * Widen * Waist));
		}
		return Poly;
	}

	/** Una chancla en At girada Yaw: suela de dos capas y tira en Y (del revés, con los surcos a la vista; o con una tira suelta). */
	inline void AddFlipFlop(FParts& P, const FVector& At, double Yaw, bool bUpsideDown, bool bBrokenStrap, const FLinearColor& SoleBot, const FLinearColor& SoleTop,
		const FLinearColor& StrapC)
	{
		const double Len = Cm(26.0);
		const double Wid = Cm(9.5);
		const double T1 = Cm(0.8);
		const double T2 = Cm(1.5);
		const FBeachFrame F(At, YawQ(Yaw));
		const TArray<FVector2D> Outline = SoleOutline(Len, Wid);
		const FLinearColor Lower = bUpsideDown ? SoleTop : SoleBot;
		const FLinearColor Upper = bUpsideDown ? SoleBot : SoleTop;
		AddPrismPoly(P.Body, F, Outline, -6.0, T1, Lower, Lower);
		AddPrismPoly(P.Body, F, Outline, T1, T2, Upper, Shade(Upper, 0.9f));
		P.ColBox(F.P(FVector(0.0, 0.0, T2 * 0.5 - 3.0)), F.Q, FVector(Len * 0.46, Wid * 0.42, T2 * 0.5 + 3.0));
		if (bUpsideDown)
		{
			for (int32 g = 0; g < 5; ++g)
			{
				AddOBox(P.Body, F.P(FVector(-Len * 0.36 + Len * 0.18 * g, 0.0, T2 + 1.0)), F.Q, FVector(Cm(0.35), Wid * 0.3, 2.0), Shade(SoleBot, 0.6f));
			}
			return;
		}
		AddTube(P.Body, { F.P(FVector(Len * 0.28, 0.0, T2)), F.P(FVector(Len * 0.28, 0.0, T2 + Cm(1.6))) }, Cm(0.35), 6, StrapC);
		for (const double Sy : { -1.0, 1.0 })
		{
			const bool bLoose = bBrokenStrap && Sy > 0.0;
			TArray<FVector> Path;
			for (int32 s = 0; s <= 6; ++s)
			{
				const double T = s / 6.0;
				const FVector Local = bLoose
					? FVector(FMath::Lerp(Len * 0.28, Len * 0.1, T), Sy * FMath::Lerp(0.0, Wid * 0.95, T), FMath::Lerp(T2 + Cm(1.6), 8.0, FMath::Sqrt(T)))
					: FVector(FMath::Lerp(Len * 0.28, -Len * 0.04, T), Sy * Wid * 0.46 * FMath::Sin(T * UE_DOUBLE_HALF_PI),
						T2 + Cm(1.6) * FMath::Cos(T * UE_DOUBLE_HALF_PI) + Cm(0.9) * FMath::Sin(T * UE_DOUBLE_PI));
				Path.Add(F.P(Local));
			}
			AddSweep(P.Body, Path, RectSection(Cm(0.3), Cm(0.9)), false, [](int32) { return 1.0; }, [&StrapC](int32, int32) { return StrapC; }, true);
			if (!bLoose)
			{
				P.ColCapsule(Path[0], Path[3], Cm(0.7));
				P.ColCapsule(Path[3], Path[6], Cm(0.7));
			}
		}
	}

	/** Chancla sola (0), el par (1), una del revés (2) o con la tira rota (3). */
	inline void BuildFlipFlop(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 BotHex[4] = { 0x2A5CB8, 0xE85C9A, 0xF2C81E, 0x2A2A2E };
		const uint32 TopHex[4] = { 0xF4E3C6, 0xF8D8E6, 0xF4F0DA, 0x4A8FD0 };
		const uint32 StrapHex[4] = { 0xF4F4F4, 0xD8282E, 0x3F9B3A, 0xF2C81E };
		const FLinearColor SoleBot = Hex(BotHex[Kind], 0.1f);
		const FLinearColor SoleTop = Hex(TopHex[Kind], 0.05f);
		const FLinearColor StrapC = Hex(StrapHex[Kind], 0.2f);
		switch (Kind)
		{
		case 1:
			AddFlipFlop(P, FVector(Cm(0.5), -Cm(5.6), 0.0), RndIn(Seed, 1, -8.0, 2.0), false, false, SoleBot, SoleTop, StrapC);
			AddFlipFlop(P, FVector(-Cm(0.8), Cm(5.6), 0.0), RndIn(Seed, 2, 2.0, 8.0), false, false, SoleBot, SoleTop, StrapC);
			break;
		case 2:
			AddFlipFlop(P, FVector::ZeroVector, 0.0, true, false, SoleBot, SoleTop, StrapC);
			break;
		case 3:
			AddFlipFlop(P, FVector::ZeroVector, 0.0, false, true, SoleBot, SoleTop, StrapC);
			break;
		default:
			AddFlipFlop(P, FVector::ZeroVector, 0.0, false, false, SoleBot, SoleTop, StrapC);
			break;
		}
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 3.f;
	}

	/** Brick de zumo: tumbado con la pajita puesta (0), de pie (1), aplastado (2) o con la pajita envuelta pegada (3). */
	inline void BuildJuiceBox(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 MainHex[4] = { 0xF28C1E, 0xE84A6A, 0x6DBB3A, 0xF2C81E };
		const FLinearColor Main = Hex(MainHex[Kind], 0.15f);
		const FLinearColor Panel = Hex(0xF6F2E8, 0.15f);
		const FLinearColor Fruit = Hex(Kind == 2 ? 0xC8202A : (Kind == 1 ? 0xD8283E : 0xF28C1E), 0.15f);
		const FLinearColor Leaf = Hex(0x3F9B3A, 0.1f);
		const FLinearColor StrawC = Hex(0xF4F4F4, 0.2f);
		const double Lx = Cm(10.5);
		const double Ly = Cm(6.3);
		const double Lz = Cm(4.0) * (Kind == 2 ? 0.35 : 1.0);
		const TArray<FVector2D> DiscProf = { FVector2D(Cm(1.3), 0.0), FVector2D(0.0, 1.0) };
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 4.f;
		if (Kind == 1)
		{
			const FVector C(0.0, 0.0, Lx * 0.5);
			AddOBox(P.Body, C, FQuat::Identity, FVector(Lz * 0.5, Ly * 0.5, Lx * 0.5), Main);
			AddOBox(P.Body, C + FVector(Lz * 0.5 + 1.0, 0.0, 0.0), FQuat::Identity, FVector(1.0, Ly * 0.36, Lx * 0.3), Panel);
			AddRevolve(P.Body, FBeachFrame(C + FVector(Lz * 0.5 + 3.0, 0.0, 0.0), AxisZTo(FVector(1.0, 0.0, 0.0))), DiscProf, 10, Fruit);
			AddOBox(P.Body, C + FVector(0.0, 0.0, Lx * 0.5 + Cm(0.3)), FQuat::Identity, FVector(Lz * 0.2, Ly * 0.5, Cm(0.3)), Shade(Main, 0.9f));
			const FVector S0 = C + FVector(0.0, Ly * 0.25, Lx * 0.5 - Cm(1.0));
			AddTube(P.Body, { S0, S0 + FVector(0.0, 0.0, Cm(4.5)), S0 + FVector(Cm(1.2), 0.0, Cm(5.6)) }, Cm(0.25), 6, StrawC);
			P.ColBox(C, FQuat::Identity, FVector(Lz * 0.5, Ly * 0.5, Lx * 0.5));
			return;
		}
		const FVector C(0.0, 0.0, Lz * 0.5);
		AddOBox(P.Body, C, FQuat::Identity, FVector(Lx * 0.5, Ly * 0.5, Lz * 0.5), Main);
		AddOBox(P.Body, C + FVector(-Lx * 0.08, 0.0, Lz * 0.5 + 1.0), FQuat::Identity, FVector(Lx * 0.3, Ly * 0.36, 1.0), Panel);
		AddRevolve(P.Body, FBeachFrame(C + FVector(-Lx * 0.08, 0.0, Lz * 0.5 + 3.0), FQuat::Identity), DiscProf, 10, Fruit);
		AddOBox(P.Body, C + FVector(-Lx * 0.08 + Cm(0.6), Cm(0.9), Lz * 0.5 + 4.0), YawQ(30.0), FVector(Cm(0.6), Cm(0.3), 1.0), Leaf);
		// Sellado del extremo de arriba (+X).
		AddOBox(P.Body, C + FVector(Lx * 0.5 + Cm(0.3), 0.0, 0.0), FQuat::Identity, FVector(Cm(0.3), Ly * 0.5, Lz * 0.2), Shade(Main, 0.9f));
		if (Kind == 0)
		{
			const FVector S0 = C + FVector(Lx * 0.5 - Cm(0.5), Ly * 0.22, Lz * 0.2);
			AddTube(P.Body, { S0, S0 + FVector(Cm(1.8), 0.0, Cm(0.6)), S0 + FVector(Cm(2.4), 0.0, Cm(1.8)) }, Cm(0.25), 6, StrawC);
		}
		else if (Kind == 3)
		{
			const FVector S0 = C + FVector(-Lx * 0.4, Ly * 0.5 + Cm(0.3), 0.0);
			AddTube(P.Body, { S0, S0 + FVector(Lx * 0.75, 0.0, 0.0) }, Cm(0.35), 6, Hex(0xE8EEF2, 0.5f));
		}
		P.ColBox(C, FQuat::Identity, FVector(Lx * 0.5, Ly * 0.5, Lz * 0.5));
	}

	/** Boya: bola naranja con su cabo (0), baliza de rayas tumbada (1), defensa blanca (2) o bola amarilla con algas (3). */
	inline void BuildBuoy(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const FLinearColor RopeC = Hex(0xC8A86E);
		const FLinearColor Algae = Hex(0x5E8A44);
		const FLinearColor Barn = Hex(0xE8E4DA);
		const FLinearColor Iron = Hex(0x3A3A3A, 0.6f);
		P.Info.TiltMax = 5.f;
		if (Kind == 1 || Kind == 2)
		{
			const double Len = Kind == 1 ? Cm(28.0) : Cm(27.0);
			const double R = Kind == 1 ? Cm(5.0) : Cm(6.0);
			const FLinearColor ColA = Hex(Kind == 1 ? 0xD8282E : 0xF4F4F0, 0.3f);
			const FLinearColor ColB = Hex(Kind == 1 ? 0xF4F4F0 : 0xD9DCDF, 0.3f);
			TArray<FVector2D> Prof;
			for (int32 i = 0; i <= 12; ++i)
			{
				const double Z = Len * i / 12.0;
				const double Over = FMath::Max(0.0, FMath::Abs(Z - Len * 0.5) - (Len * 0.5 - R));
				const double Rad = (i == 0 || i == 12) ? 0.0 : R * FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Square(Over / R)));
				Prof.Add(FVector2D((Kind == 2 && i % 2 == 0) ? Rad * 1.06 : Rad, Z));
			}
			const FBeachFrame F(FVector(-Len * 0.5, 0.0, R * 0.85), AxisZTo(FVector(1.0, 0.0, 0.0)));
			AddRevolve(P.Body, F, Prof, 12, [&](int32 Ring, int32) -> FLinearColor { return ((Ring / (Kind == 1 ? 2 : 1)) % 2) ? ColB : ColA; }, 0.02, Seed);
			if (Kind == 1)
			{
				// Palo con banderín en un extremo.
				const FVector Tip = F.P(FVector(0.0, 0.0, Len));
				const FVector Fl = Tip + FVector(Cm(1.0), 0.0, Cm(6.0));
				AddTube(P.Body, { Tip - F.D(FVector(0.0, 0.0, Cm(1.0))), Fl }, Cm(0.5), 6, Iron);
				const FVector F1 = Fl + FVector(-Cm(0.8), 0.0, -Cm(3.0));
				const FVector F2 = Fl + FVector(Cm(2.6), 0.0, -Cm(1.4));
				P.Body.AddTri(Fl, F1, F2, FVector(0.0, 1.0, 0.0), Hex(0xF2C81E));
				P.Body.AddTri(Fl, F1, F2, FVector(0.0, -1.0, 0.0), Hex(0xF2C81E));
			}
			else
			{
				// Ojales de cabo en los dos extremos.
				AddLoop(P.Body, F.P(FVector(0.0, 0.0, -Cm(0.7))), FVector(0.0, 1.0, 0.0), Cm(0.9), Cm(0.3), 8, RopeC);
				AddLoop(P.Body, F.P(FVector(0.0, 0.0, Len + Cm(0.7))), FVector(0.0, 1.0, 0.0), Cm(0.9), Cm(0.3), 8, RopeC);
			}
			P.ColCapsule(F.P(FVector(0.0, 0.0, R)), F.P(FVector(0.0, 0.0, Len - R)), R * 0.95);
			P.Info.SinkMax = static_cast<float>(Cm(1.2));
			return;
		}
		const double R = Cm(12.0);
		const FLinearColor Ball = Hex(Kind == 0 ? 0xF06A25 : 0xF2C81E, 0.3f);
		const FVector C(0.0, 0.0, R * 0.85);
		AddRevolve(P.Body, FBeachFrame(C, FQuat::Identity), EllipsoidProfile(R, R, 8), 14, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring == 4) { return Shade(Ball, 0.8f); }
			if (Kind == 3 && Ring <= 2 && Rnd(Seed, Ring * 14 + Side) < 0.7) { return Algae; }
			return Ball;
		});
		// Ojal de arriba y el cabo que baja por un lado hasta la arena.
		AddLoop(P.Body, C + FVector(0.0, 0.0, R + Cm(1.0)), FVector(0.0, 1.0, 0.0), Cm(1.0), Cm(0.35), 8, Iron);
		TArray<FVector> Rope;
		for (int32 s = 0; s <= 6; ++s)
		{
			const double Phi = FMath::DegreesToRadians(8.0 + 100.0 * s / 6.0);
			Rope.Add(C + FVector(FMath::Sin(Phi) * R * 1.04, 0.0, FMath::Cos(Phi) * R * 1.04));
		}
		const FVector Hang = Rope.Last();
		for (int32 s = 1; s <= 3; ++s) { Rope.Add(FVector(Hang.X + Cm(2.0) * s, Cm(1.5) * FMath::Sin(s * 1.3), Cm(0.6))); }
		AddTube(P.Body, Rope, Cm(0.6), 5, RopeC, true);
		if (Kind == 3)
		{
			for (int32 b = 0; b < 4; ++b)
			{
				const double Ang = RndIn(Seed, 40 + b, 0.0, TNProcMap::TwoPi);
				const FVector Nn(FMath::Cos(Ang) * 0.8, FMath::Sin(Ang) * 0.8, -0.3);
				AddBarnacle(P.Body, C + Nn.GetSafeNormal() * R, Nn, Cm(0.8), Barn);
			}
		}
		P.ColSphere(C, R * 0.97);
		P.Info.SinkMax = static_cast<float>(Cm(2.0));
	}

	/** Bote de crema solar: tumbado con un sol dibujado y un pegote de crema (0-2) o clavado de pie con el tapón en la arena (3). */
	inline void BuildSunscreen(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 BodyHex[4] = { 0xF28C1E, 0x2A7FD0, 0xF2D21E, 0xF4F4F0 };
		const uint32 CapHex[4] = { 0xF4F4F0, 0xF4F4F0, 0xE8412C, 0x2EB0A8 };
		const FLinearColor BodyC = Hex(BodyHex[Kind], 0.35f);
		const FLinearColor CapC = Hex(CapHex[Kind], 0.3f);
		const FLinearColor Sun = Hex(0xF5C518, 0.2f);
		const FLinearColor Cream = Hex(0xFAF6EE, 0.3f);
		const double R = Cm(2.8);
		const double Len = Cm(15.0);
		const double CapLen = Cm(2.2);
		const double Squash = 0.55;
		const TArray<FVector2D> Prof = { FVector2D(0.0, 0.0), FVector2D(R * 0.75, 0.0), FVector2D(R, Cm(1.0)), FVector2D(R, Len * 0.5), FVector2D(R * 0.95, Len * 0.85),
			FVector2D(R * 0.8, Len), FVector2D(R * 0.72, Len), FVector2D(R * 0.72, Len + CapLen * 0.9), FVector2D(R * 0.6, Len + CapLen), FVector2D(0.0, Len + CapLen) };
		// Tramos: 0-4 el bote, 5-8 el tapón.
		auto ColorAt = [&](int32 Ring, int32) -> FLinearColor { return Ring >= 5 ? CapC : BodyC; };
		if (Kind == 3)
		{
			const double Up = Len + CapLen - Cm(3.0);
			AddRevolve(P.Body, FBeachFrame(FVector(0.0, 0.0, Up), FQuat(FVector(1.0, 0.0, 0.0), UE_DOUBLE_PI)), Prof, 12, ColorAt, 0.0, 0u, Squash);
			P.ColBox(FVector(0.0, 0.0, Up * 0.5), FQuat::Identity, FVector(R * 0.9, R * Squash * 0.9, Up * 0.5));
			P.Info.TiltMax = 6.f;
			return;
		}
		// Tumbado con la cara plana hacia arriba (SquashY va a Z con el giro de 90° sobre el eje).
		AddRevolve(P.Body, FBeachFrame(FVector(-(Len + CapLen) * 0.5, 0.0, R * Squash), AxisZTo(FVector(1.0, 0.0, 0.0)) * FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI)),
			Prof, 12, ColorAt, 0.0, 0u, Squash);
		const FVector SunC(-Cm(1.5), 0.0, R * Squash * 2.0 + 1.0);
		AddRevolve(P.Body, FBeachFrame(SunC, FQuat::Identity), { FVector2D(Cm(1.2), 0.0), FVector2D(0.0, 1.0) }, 10, Sun);
		for (int32 k = 0; k < 8; ++k)
		{
			const double Ang = TNProcMap::TwoPi * k / 8.0;
			const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
			const FVector Wide(-Dir.Y * Cm(0.35), Dir.X * Cm(0.35), 0.0);
			const FVector Lift(0.0, 0.0, 1.0);
			P.Body.AddTri(SunC + Dir * Cm(2.2) + Lift, SunC + Dir * Cm(1.4) + Wide + Lift, SunC + Dir * Cm(1.4) - Wide + Lift, FVector::UpVector, Sun);
		}
		if (Kind != 1)
		{
			for (int32 b = 0; b < 3; ++b)
			{
				const FVector At(Cm(5.5) + Cm(1.1) * b, (R + Cm(1.6)) * (b % 2 ? -1.0 : 1.0), 0.0);
				AddBlob(P.Body, FBeachFrame(At, FQuat::Identity), Cm(0.9 - 0.2 * b), Cm(0.8 - 0.2 * b), Cm(0.4), 7, 3, Cream);
			}
		}
		P.ColBox(FVector(0.0, 0.0, R * Squash), FQuat::Identity, FVector((Len + CapLen) * 0.5, R * 0.95, R * Squash));
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 3.f;
	}

	/** Palitos de helado (uno a tres, cruzados), alguno con restos de helado derretido. */
	inline void BuildPopsicleSticks(FParts& P, int32 Variant, uint32 Seed)
	{
		const FLinearColor Wood = Hex(Variant % 2 ? 0xE3C792 : 0xD9B77E);
		const int32 Count = 1 + Variant % 3;
		const double HalfLen = Cm(5.7);
		const TArray<FVector2D> Outline = Stadium(HalfLen, Cm(0.5), 4);
		for (int32 n = 0; n < Count; ++n)
		{
			const FVector At = n == 0 ? FVector::ZeroVector : FVector(RndIn(Seed, 10 + n, -Cm(1.8), Cm(1.8)), RndIn(Seed, 20 + n, -Cm(2.2), Cm(2.2)), 12.0 * n);
			const FBeachFrame F(At, YawQ(RndIn(Seed, 30 + n, 0.0, 180.0)) * FQuat(FVector(1.0, 0.0, 0.0), FMath::DegreesToRadians(RndIn(Seed, 40 + n, -4.0, 4.0))));
			AddPrismPoly(P.Body, F, Outline, -2.0, 12.0, Shade(Wood, 1.0f + 0.06f * static_cast<float>(n)), Shade(Wood, 0.85f));
			if (Variant != 1 && n == 0)
			{
				AddBlob(P.Body, FBeachFrame(F.P(FVector(HalfLen * 0.55, 0.0, 12.0)), F.Q), Cm(1.4), Cm(0.8), Cm(0.25), 8, 3, Hex(Variant == 2 ? 0x6B3E26 : 0xF29AB8, 0.35f));
			}
		}
		P.Info.bCastShadow = false;
		P.Info.SinkMax = 3.f;
	}

	/** Trozo de cuerda: suelta en S (0), en lazada (1), con un nudo (2) o un cabo corto (3), deshilachada por las puntas. */
	inline void BuildRopePiece(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 AHex[4] = { 0xC8A86E, 0x2E6FB0, 0x3F8A4A, 0xE8712A };
		const uint32 BHex[4] = { 0x9C7E4A, 0x7FB2E6, 0x2A6A36, 0xF2B06A };
		const FLinearColor ColA = Hex(AHex[Kind]);
		const FLinearColor ColB = Hex(BHex[Kind]);
		const double R = Cm(0.8);
		TArray<FVector> Path;
		switch (Kind)
		{
		case 1:
		{
			const FVector Loop(-Cm(4.0), 0.0, 0.0);
			for (int32 s = 0; s <= 14; ++s)
			{
				const double Ang = -UE_DOUBLE_HALF_PI + TNProcMap::TwoPi * 0.85 * s / 14.0;
				const double Rad = Cm(8.0) * (1.0 - 0.1 * s / 14.0);
				Path.Add(Loop + FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, R * 0.8 + (s > 10 ? R * 1.6 * (s - 10) / 4.0 : 0.0)));
			}
			const FVector From = Path.Last();
			for (int32 s = 1; s <= 5; ++s) { Path.Add(From + FVector(Cm(3.2) * s, Cm(1.0) * FMath::Sin(s * 1.1), -R * 0.3 * s)); }
			break;
		}
		case 2:
		{
			for (int32 s = 0; s <= 8; ++s) { Path.Add(FVector(-Cm(18.0) + Cm(2.0) * s, Cm(1.2) * FMath::Sin(s * 0.8), R * 0.8)); }
			const FVector K0 = Path.Last();
			for (int32 s = 1; s <= 8; ++s)
			{
				const double Ang = TNProcMap::TwoPi * s / 8.0;
				Path.Add(K0 + FVector(Cm(1.6) * FMath::Sin(Ang), Cm(1.6) * (1.0 - FMath::Cos(Ang)), R * 1.2 * FMath::Sin(Ang * 0.5)));
			}
			const FVector K1 = Path.Last();
			for (int32 s = 1; s <= 8; ++s) { Path.Add(K1 + FVector(Cm(2.0) * s, Cm(1.2) * FMath::Sin(s * 0.9 + 1.0), 0.0)); }
			break;
		}
		default:
		{
			const double HalfL = Kind == 3 ? Cm(10.0) : Cm(19.0);
			const int32 Steps = Kind == 3 ? 8 : 14;
			for (int32 s = 0; s <= Steps; ++s)
			{
				const double T = static_cast<double>(s) / Steps;
				Path.Add(FVector(-HalfL + 2.0 * HalfL * T, Cm(5.0) * FMath::Sin(T * UE_DOUBLE_TWO_PI + Kind), R * 0.8));
			}
			break;
		}
		}
		// Cordones retorcidos: franjas en diagonal.
		AddSweep(P.Body, Path, CircleSection(6), false, [R](int32) { return R; }, [&](int32 i, int32 Side) -> FLinearColor { return ((i * 2 + Side) % 6 < 3) ? ColA : ColB; }, true);
		for (int32 End = 0; End < 2; ++End)
		{
			const FVector Tip = End == 0 ? Path[0] : Path.Last();
			const FVector Dir = (End == 0 ? Path[0] - Path[1] : Path.Last() - Path[Path.Num() - 2]).GetSafeNormal();
			const FVector Across = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
			for (int32 f = 0; f < 4; ++f)
			{
				AddTube(P.Body, { Tip, Tip + (Dir + Across * ((f - 1.5) * 0.35)).GetSafeNormal() * Cm(2.0) + FVector(0.0, 0.0, -R * 0.5) }, 5.0, 3, Shade(ColA, 1.1f), false);
			}
		}
		P.Info.SinkMax = 4.f;
	}

	/** Contorno de un cristal de gafas (rectángulo redondeado o corazón) en el plano YZ, centrado en Center. */
	inline TArray<FVector> LensOutline(const FVector& Center, double HalfW, double HalfH, bool bHeart, int32 N = 16)
	{
		TArray<FVector> Pts;
		for (int32 k = 0; k < N; ++k)
		{
			const double T = TNProcMap::TwoPi * k / N;
			double Ly = 0.0;
			double Lz = 0.0;
			if (bHeart)
			{
				const double S = FMath::Sin(T);
				Ly = HalfW * S * S * S;
				Lz = HalfH * (13.0 * FMath::Cos(T) - 5.0 * FMath::Cos(2.0 * T) - 2.0 * FMath::Cos(3.0 * T) - FMath::Cos(4.0 * T)) / 16.0;
			}
			else
			{
				// Superelipse de exponente 4: esquinas redondeadas.
				const double Ct = FMath::Cos(T);
				const double St = FMath::Sin(T);
				const double Rr = 1.0 / FMath::Pow(FMath::Pow(FMath::Abs(Ct), 4.0) + FMath::Pow(FMath::Abs(St), 4.0), 0.25);
				Ly = HalfW * Ct * Rr;
				Lz = HalfH * St * Rr;
			}
			Pts.Add(Center + FVector(0.0, Ly, Lz));
		}
		return Pts;
	}

	/** Gafas de sol: negras (0), rojas de espejo plegadas (1), de carey sin un cristal (2) o de corazón (3). */
	inline void BuildSunglasses(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 FrameHex[4] = { 0x1E1E22, 0xD02030, 0x6E4526, 0xF4F4F4 };
		const uint32 LensHex[4] = { 0x1A2A30, 0x3A7FD0, 0x5A3A20, 0xE87AA8 };
		const FLinearColor FrameC = Hex(FrameHex[Kind], 0.4f);
		const FLinearColor LensC = Hex(LensHex[Kind], Kind == 1 ? 0.95f : 0.75f);
		const FLinearColor Spot = Hex(0xB07A3E, 0.4f);
		const double FrontX = Cm(4.5);
		const double LensHW = Cm(2.5);
		const double LensHH = Cm(2.2);
		const double LensY = Cm(3.6);
		const double LensZ = Cm(0.4) + LensHH;
		for (const double Sy : { -1.0, 1.0 })
		{
			const FVector C(FrontX, Sy * LensY, LensZ);
			const TArray<FVector> Rim = LensOutline(C, LensHW, LensHH, Kind == 3);
			AddSweep(P.Body, Rim, RectSection(9.0, 11.0), true, [](int32) { return 1.0; }, [&](int32 i, int32) { return (Kind == 2 && i % 3 == 0) ? Spot : FrameC; }, false,
				FVector(1.0, 0.0, 0.0));
			if (!(Kind == 2 && Sy > 0.0))
			{
				for (int32 k = 0; k < Rim.Num(); ++k)
				{
					const FVector Front(2.0, 0.0, 0.0);
					const FVector& A = Rim[k];
					const FVector& B = Rim[(k + 1) % Rim.Num()];
					P.Body.AddTri(C + Front, A + Front, B + Front, FVector(1.0, 0.0, 0.0), LensC);
					P.Body.AddTri(C - Front, A - Front, B - Front, FVector(-1.0, 0.0, 0.0), Shade(LensC, 0.7f));
				}
			}
			// Patillas hacia atrás (o plegadas detrás de los cristales).
			const FVector Hinge(FrontX - 6.0, Sy * (LensY + LensHW + 6.0), LensZ + LensHH * 0.6);
			const TArray<FVector> Arm = Kind == 1
				? TArray<FVector>{ Hinge, Hinge + FVector(-Cm(1.0), -Sy * Cm(1.0), 0.0), Hinge + FVector(-Cm(1.4), -Sy * Cm(6.5), -Cm(0.6)) }
				: TArray<FVector>{ Hinge, Hinge + FVector(-Cm(5.0), -Sy * Cm(0.4), -Cm(0.3)), Hinge + FVector(-Cm(10.5), -Sy * Cm(0.9), -Cm(1.6)),
					Hinge + FVector(-Cm(12.0), -Sy * Cm(1.0), -Cm(2.5)) };
			AddSweep(P.Body, Arm, RectSection(8.0, 12.0), false, [](int32) { return 1.0; }, [&FrameC](int32, int32) { return FrameC; }, true);
			P.ColCapsule(Arm[0], Arm.Last(), 14.0);
		}
		AddTube(P.Body, { FVector(FrontX, -LensY + LensHW * 0.8, LensZ + LensHH * 0.5), FVector(FrontX + 8.0, 0.0, LensZ + LensHH * 0.7),
			FVector(FrontX, LensY - LensHW * 0.8, LensZ + LensHH * 0.5) }, 8.0, 5, FrameC);
		if (Kind == 2)
		{
			// El cristal que se ha salido, tumbado en la arena.
			AddRevolve(P.Body, FBeachFrame(FVector(FrontX - Cm(3.0), LensY + Cm(3.4), 6.0), FQuat::Identity),
				{ FVector2D(LensHW, 0.0), FVector2D(LensHW * 0.95, 4.0), FVector2D(0.0, 5.0) }, 12, LensC, 0.0, 0u, LensHH / LensHW);
		}
		P.ColBox(FVector(FrontX, 0.0, LensZ), FQuat::Identity, FVector(12.0, LensY + LensHW, LensHH + 10.0));
		P.Info.SinkMax = 4.f;
		P.Info.TiltMax = 6.f;
	}

	/** Cubito de juguete lleno de arena con su asa, y un flan de estrella (0), de tortuga (1), un molde de pez (2) o un rastrillo (3). */
	inline void BuildToyBucket(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 PlasticHex[4] = { 0xE0342E, 0x2E6FD0, 0xF2C81E, 0x3F9B3A };
		const uint32 ToyHex[4] = { 0x2EB0A8, 0xF28C1E, 0x9B5DE5, 0xF49AC0 };
		const FLinearColor Plastic = Hex(PlasticHex[Kind], 0.3f);
		const FLinearColor Toy = Hex(ToyHex[Kind], 0.3f);
		const FLinearColor SandC = Hex(0xE3C48A);
		const FLinearColor SandDark = Hex(0xCBA66A);
		const FVector BucketC(-Cm(3.0), -Cm(2.0), 0.0);
		const double Rb = Cm(3.8);
		const double Rt = Cm(4.6);
		const double H = Cm(9.0);
		const TArray<FVector2D> Prof = { FVector2D(0.0, 0.0), FVector2D(Rb, 0.0), FVector2D(Rb + (Rt - Rb) * 0.5, H * 0.5), FVector2D(Rt, H), FVector2D(Rt + 8.0, H + 6.0),
			FVector2D(Rt - 10.0, H + 6.0), FVector2D(Rt - 10.0, H - 12.0), FVector2D(Rt - 14.0, H - 30.0), FVector2D(Rt * 0.5, H - 18.0), FVector2D(0.0, H - 12.0) };
		// Tramos: 0-2 el cubo por fuera, 3-4 el borde, 5-6 por dentro, 7-8 la arena.
		AddRevolve(P.Body, FBeachFrame(BucketC, YawQ(RndIn(Seed, 1, 0.0, 360.0))), Prof, 12, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring >= 7) { return (Side % 3 == 0) ? SandDark : SandC; }
			if (Ring == 1 && Side % 2 == 0) { return Shade(Plastic, 0.85f); }
			return Ring >= 5 ? Shade(Plastic, 0.8f) : Plastic;
		});
		TArray<FVector> Handle;
		for (int32 s = 0; s <= 8; ++s)
		{
			const double Ang = UE_DOUBLE_PI * s / 8.0;
			Handle.Add(BucketC + FVector(FMath::Cos(Ang) * (Rt + 6.0), -FMath::Sin(Ang) * Cm(3.0), H * 0.92 - FMath::Sin(Ang) * Cm(1.5)));
		}
		AddTube(P.Body, Handle, 7.0, 4, Plastic, true);
		P.ColPrism(BucketC, (Rb + Rt) * 0.5 * 0.95, 0.0, H + 6.0, 22.5);
		switch (Kind)
		{
		case 0:
		{
			const FVector C(Cm(5.5), Cm(4.0), 0.0);
			TArray<FVector2D> Star;
			for (int32 k = 0; k < 10; ++k)
			{
				const double Ang = TNProcMap::TwoPi * k / 10.0;
				const double Rr = (k % 2) ? Cm(1.6) : Cm(3.6);
				Star.Add(FVector2D(FMath::Cos(Ang) * Rr, FMath::Sin(Ang) * Rr));
			}
			AddPrismPoly(P.Body, FBeachFrame(C, YawQ(RndIn(Seed, 2, 0.0, 72.0))), Star, -10.0, Cm(3.0), SandC, SandDark);
			P.ColPrism(C, Cm(2.2), 0.0, Cm(3.0));
			break;
		}
		case 1:
		{
			// Flan con forma de tortuga: caparazón, cabeza y cuatro aletas.
			const FVector C(Cm(5.5), Cm(4.0), 0.0);
			AddRevolve(P.Body, FBeachFrame(C, FQuat::Identity), CapProfile(Cm(3.0), Cm(2.4), 3, 10.0), 10, [&](int32 Ring, int32 Side) { return ((Ring + Side) % 3 == 0) ? SandDark : SandC; });
			AddBlob(P.Body, FBeachFrame(C + FVector(Cm(3.4), 0.0, Cm(0.8)), FQuat::Identity), Cm(1.0), Cm(0.8), Cm(0.8), 7, 3, SandC);
			for (int32 k = 0; k < 4; ++k)
			{
				const double Ang = FMath::DegreesToRadians(45.0 + 90.0 * k);
				AddBlob(P.Body, FBeachFrame(C + FVector(FMath::Cos(Ang) * Cm(2.8), FMath::Sin(Ang) * Cm(2.8), Cm(0.3)), YawQ(FMath::RadiansToDegrees(Ang))), Cm(1.1), Cm(0.6), Cm(0.35), 6, 2, SandC);
			}
			P.ColCap(C, Cm(2.9), Cm(2.3));
			break;
		}
		case 2:
		{
			const FVector C(Cm(5.0), Cm(4.5), 0.0);
			AddBlob(P.Body, FBeachFrame(C + FVector(0.0, 0.0, -Cm(0.3)), YawQ(20.0)), Cm(3.2), Cm(1.9), Cm(2.0), 10, 4, Toy);
			const FVector Tail = C + YawQ(20.0).RotateVector(FVector(-Cm(3.6), 0.0, 0.0));
			AddPrismPoly(P.Body, FBeachFrame(Tail, YawQ(20.0)), { FVector2D(Cm(0.6), 0.0), FVector2D(-Cm(1.6), Cm(1.6)), FVector2D(-Cm(1.2), 0.0), FVector2D(-Cm(1.6), -Cm(1.6)) },
				0.0, Cm(1.2), Toy, Shade(Toy, 0.85f));
			P.ColCap(C, Cm(2.6), Cm(1.6));
			break;
		}
		default:
		{
			// Rastrillo tumbado.
			const FVector A(Cm(1.0), Cm(7.0), 12.0);
			const FVector B(Cm(12.0), Cm(4.0), 12.0);
			AddTube(P.Body, { A, B }, 11.0, 6, Toy);
			const FVector Dir = (B - A).GetSafeNormal();
			const FVector Across(-Dir.Y, Dir.X, 0.0);
			const FQuat Q = FRotationMatrix::MakeFromXY(Dir, Across).ToQuat();
			AddOBox(P.Body, B + Dir * Cm(0.4), Q, FVector(Cm(0.4), Cm(3.0), Cm(0.4)), Toy);
			for (int32 t = 0; t < 7; ++t)
			{
				AddOBox(P.Body, B + Dir * Cm(1.2) + Across * Cm(-2.6 + 0.866 * t), Q, FVector(Cm(0.7), Cm(0.18), Cm(0.3)), Toy);
			}
			break;
		}
		}
		P.Info.SinkMax = static_cast<float>(Cm(0.6));
		P.Info.TiltMax = 3.f;
	}

	/** Pelota hinchable de gajos: clásica (0), medio deshinchada (1), de colores pastel (2) o azul y blanca (3). */
	inline void BuildBeachBall(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 Classic[6] = { 0xE0342E, 0xF2C81E, 0x2E6FD0, 0xF6F2EA, 0x3F9B3A, 0xF28C1E };
		const uint32 Pastel[6] = { 0xF49AC0, 0xA8E0D8, 0xF7E08A, 0xF6F2EA, 0xB8A8E8, 0xF8C09A };
		const double R = Cm(15.0);
		const double Flat = Kind == 1 ? 0.55 : 0.85;
		const double Sag = Kind == 1 ? 0.14 : 0.06;
		TArray<FVector2D> Prof;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double T = UE_DOUBLE_PI * i / 8.0;
			const double Low = FMath::Max(0.0, -FMath::Cos(T));
			Prof.Add(FVector2D(i == 8 ? 0.0 : R * FMath::Sin(T) * (1.0 + Sag * Low), -R * Flat * FMath::Cos(T)));
		}
		const FVector C(0.0, 0.0, R * Flat);
		AddRevolve(P.Body, FBeachFrame(C, YawQ(RndIn(Seed, 1, 0.0, 360.0))), Prof, 12, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring == 0 || Ring == 7) { return Hex(0xF6F2EA, 0.4f); }
			const int32 Gore = FMath::Clamp(Side / 2, 0, 5);
			if (Kind == 3) { return Hex((Gore % 2) ? 0xF6F2EA : 0x2E6FD0, 0.4f); }
			return Hex(Kind == 2 ? Pastel[Gore] : Classic[Gore], 0.4f);
		}, Kind == 1 ? 0.08 : 0.03, Seed);
		AddBlob(P.Body, FBeachFrame(C + FVector(R * 0.98, 0.0, R * Flat * 0.2), AxisZTo(FVector(1.0, 0.0, 0.2))), Cm(0.6), Cm(0.6), Cm(0.5), 6, 2, Hex(0xF6F2EA, 0.4f));
		if (Kind == 1) { P.ColCap(FVector::ZeroVector, R, 2.0 * R * Flat); }
		else { P.ColSphere(C, R * 0.9); }
		P.Info.SinkMax = static_cast<float>(Cm(1.0));
		P.Info.TiltMax = 4.f;
	}

	/** Disco volador: boca abajo, para subirse (0 rojo, 1 amarillo con anillos), boca arriba como un cuenco (2) o de canto medio enterrado (3). */
	inline void BuildFrisbee(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 DiscHex[4] = { 0xE0342E, 0xF2C81E, 0x2E6FD0, 0x3F9B3A };
		const FLinearColor Disc = Hex(DiscHex[Kind], 0.35f);
		const FLinearColor PrintC = Hex(0xF6F2EA, 0.35f);
		const double R = Cm(13.5);
		const double H = Cm(3.2);
		const TArray<FVector2D> Prof = { FVector2D(0.0, H * 0.25), FVector2D(R - 30.0, H * 0.2), FVector2D(R - 22.0, 0.0), FVector2D(R, 18.0), FVector2D(R, H - 18.0),
			FVector2D(R * 0.93, H), FVector2D(R * 0.72, H + 8.0), FVector2D(R * 0.45, H + 12.0), FVector2D(0.0, H + 14.0) };
		// Tramos: 0-1 por debajo, 2-4 el canto, 5-7 la cara de arriba (6 con el anillo impreso).
		auto ColorAt = [&](int32 Ring, int32) -> FLinearColor
		{
			if (Ring <= 1) { return Shade(Disc, 0.75f); }
			if (Kind == 1 && Ring == 6) { return PrintC; }
			return Ring == 3 ? Shade(Disc, 0.9f) : Disc;
		};
		const double Top = H + 14.0;
		switch (Kind)
		{
		case 2:
		{
			// Boca arriba: el cuenco tiene el fondo a ~0,85 m y un reborde de 20 cm.
			AddRevolve(P.Body, FBeachFrame(FVector(0.0, 0.0, Top), FQuat(FVector(1.0, 0.0, 0.0), UE_DOUBLE_PI)), Prof, 16, ColorAt);
			const double Floor = Top - H * 0.22;
			P.ColPrism(FVector::ZeroVector, R * 0.9, 0.0, Floor, 0.0, 8);
			P.ColRing(FVector::ZeroVector, R * 0.88 - 30.0, 30.0, Floor, Top);
			break;
		}
		case 3:
			AddRevolve(P.Body, FBeachFrame(FVector(0.0, Top * 0.5, R * 0.45), AxisZTo(FVector(0.0, -1.0, 0.0))), Prof, 16, ColorAt);
			P.ColBox(FVector(0.0, 0.0, R * 0.45), FQuat::Identity, FVector(R * 0.95, Top * 0.5, R * 0.95));
			break;
		default:
			AddRevolve(P.Body, FBeachFrame(), Prof, 16, ColorAt);
			P.ColPrism(FVector::ZeroVector, R * 0.93, 0.0, Top - 4.0, 0.0, 8);
			break;
		}
		P.Info.SinkMax = Kind == 3 ? 0.f : 3.f;
		P.Info.TiltMax = 3.f;
	}

	/** Patito de goma: de pie (0 amarillo, 2 rosa, 3 azul) o tumbado de lado y descolorido por el sol (1). */
	inline void BuildRubberDuck(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 BodyHex[4] = { 0xF6D02A, 0xEEDC8A, 0xF49AC0, 0x4A8FD0 };
		const FLinearColor BodyC = Hex(BodyHex[Kind], 0.35f);
		const FLinearColor Beak = Hex(0xF28C1E, 0.35f);
		const FLinearColor Eye = Hex(0x141414, 0.5f);
		const FLinearColor EyeW = Hex(0xF6F6F6, 0.4f);
		const FBeachFrame F = Kind == 1 ? FBeachFrame(FVector(0.0, Cm(3.0), Cm(2.9)), FQuat(FVector(1.0, 0.0, 0.0), FMath::DegreesToRadians(80.0)))
			: FBeachFrame(FVector(-Cm(1.0), 0.0, 0.0), FQuat::Identity);
		AddBlob(P.Body, FBeachFrame(F.P(FVector(0.0, 0.0, Cm(2.6))), F.Q), Cm(4.5), Cm(3.4), Cm(2.6), 10, 5, BodyC);
		AddRevolve(P.Body, FBeachFrame(F.P(FVector(-Cm(3.8), 0.0, Cm(3.4))), F.Q * FQuat(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(-50.0))),
			{ FVector2D(Cm(1.6), 0.0), FVector2D(Cm(0.8), Cm(1.6)), FVector2D(0.0, Cm(2.2)) }, 6, BodyC);
		const FVector Head = F.P(FVector(Cm(2.4), 0.0, Cm(6.4)));
		AddBlob(P.Body, FBeachFrame(Head, F.Q), Cm(2.4), Cm(2.3), Cm(2.3), 10, 5, BodyC);
		AddBlob(P.Body, FBeachFrame(F.P(FVector(Cm(4.7), 0.0, Cm(6.0))), F.Q), Cm(1.5), Cm(1.1), Cm(0.5), 8, 3, Beak);
		for (const double Sy : { -1.0, 1.0 })
		{
			AddBlob(P.Body, FBeachFrame(F.P(FVector(Cm(3.9), Sy * Cm(1.2), Cm(7.2))), F.Q), Cm(0.5), Cm(0.5), Cm(0.6), 6, 3, EyeW);
			AddBlob(P.Body, FBeachFrame(F.P(FVector(Cm(4.25), Sy * Cm(1.25), Cm(7.3))), F.Q), Cm(0.28), Cm(0.28), Cm(0.35), 5, 2, Eye);
			AddBlob(P.Body, FBeachFrame(F.P(FVector(-Cm(0.5), Sy * Cm(3.1), Cm(3.0))), F.Q * YawQ(Sy * 10.0)), Cm(2.2), Cm(0.6), Cm(1.3), 7, 3, Shade(BodyC, 0.92f));
		}
		P.ColCapsule(F.P(FVector(-Cm(2.0), 0.0, Cm(2.6))), F.P(FVector(Cm(2.0), 0.0, Cm(2.6))), Cm(2.4));
		P.ColSphere(Head, Cm(2.2));
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 5.f;
	}

	/** Toalla tendida con pliegues que se suben andando (1 a 3), flecos en las puntas y, en una variante, un extremo enrollado. */
	inline void BuildBeachTowel(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 AHex[4] = { 0xE0342E, 0xF2C81E, 0x2EB0A8, 0xE85C9A };
		const uint32 BHex[4] = { 0xF6F2EA, 0xF28C1E, 0xF6F2EA, 0x9B5DE5 };
		const uint32 CHex[4] = { 0x2E6FD0, 0xF6F2EA, 0x2E6FD0, 0xF6F2EA };
		const FLinearColor ColA = Hex(AHex[Kind]);
		const FLinearColor ColB = Hex(BHex[Kind]);
		const FLinearColor ColC = Hex(CHex[Kind]);
		const double Lx = Cm(100.0);
		const double Ly = Cm(60.0);
		const double Thick = 22.0;
		const bool bRolled = Kind == 3;
		const double RollR = Cm(5.0);
		const double FlatEnd = bRolled ? Lx * 0.5 - RollR * 1.1 : Lx * 0.5;
		// Pliegues: tiendas de menos de 32° a lo ancho de la toalla (la colisión son sus dos caras). Lo bastante altas para que
		// la tortuga de pie (cápsula de 34 cm de radio y 176 de alto) pase por debajo: parecen un túnel y lo son.
		struct FFold
		{
			double X = 0.0;
			double Half = 1.0;
			double H = 0.0;
		};
		TArray<FFold> Folds;
		const int32 NumFolds = 1 + (Kind % 3);
		for (int32 f = 0; f < NumFolds; ++f)
		{
			FFold Fold;
			Fold.X = FMath::Lerp(-Lx * 0.3, FlatEnd - Lx * 0.2, NumFolds == 1 ? 0.5 : static_cast<double>(f) / (NumFolds - 1)) + Cm(RndIn(Seed, f, -5.0, 5.0));
			Fold.Half = Cm(RndIn(Seed, 10 + f, 13.0, 15.0));
			Fold.H = FMath::Min(Cm(RndIn(Seed, 20 + f, 7.8, 9.1)), Fold.Half * 0.6);
			Folds.Add(Fold);
		}
		auto FoldHeight = [&Folds](double X)
		{
			double Z = 0.0;
			for (const FFold& Each : Folds) { Z = FMath::Max(Z, Each.H * (1.0 - FMath::Abs(X - Each.X) / Each.Half)); }
			return Z;
		};
		const int32 Nu = 28;
		const int32 Stripes = 7;
		AddSheet(P.Body, Nu, 6, [&](double U, double V)
		{
			const double X = FMath::Lerp(-Lx * 0.5, FlatEnd, U);
			return FVector(X, FMath::Lerp(-Ly * 0.5, Ly * 0.5, V), Thick * 0.5 + 2.0 + FoldHeight(X));
		}, Thick, [&](int32 I, int32) -> FLinearColor
		{
			const int32 Band = I * Stripes / Nu;
			return Band % 3 == 0 ? ColA : (Band % 3 == 1 ? ColB : ColC);
		});
		// Flecos.
		for (int32 End = 0; End < (bRolled ? 1 : 2); ++End)
		{
			const double X = End == 0 ? -Lx * 0.5 : FlatEnd;
			const double Out = End == 0 ? -1.0 : 1.0;
			const double Z = Thick * 0.5 + 2.0 + FoldHeight(X);
			for (int32 t = 0; t < 14; ++t)
			{
				const double Y = -Ly * 0.5 + Ly * (t + 0.5) / 14.0;
				const double Wag = Cm(0.4) * FMath::Sin(t * 1.7);
				const FVector A(X, Y - 7.0, Z);
				const FVector B(X, Y + 7.0, Z);
				const FVector C(X + Out * Cm(2.2), Y + 7.0 + Wag, 3.0);
				const FVector D(X + Out * Cm(2.2), Y - 7.0 + Wag, 3.0);
				P.Body.AddQuad(A, B, C, D, FVector::UpVector, (t % 2) ? ColA : ColB);
				P.Body.AddQuad(A, B, C, D, -FVector::UpVector, Shade(ColA, 0.7f));
			}
		}
		if (bRolled)
		{
			const TArray<FVector2D> Prof = { FVector2D(0.0, 0.0), FVector2D(RollR * 0.33, 0.0), FVector2D(RollR * 0.66, 0.0), FVector2D(RollR, 0.0), FVector2D(RollR, Ly),
				FVector2D(RollR * 0.66, Ly), FVector2D(RollR * 0.33, Ly), FVector2D(0.0, Ly) };
			const FVector RollC(FlatEnd + RollR * 0.9, -Ly * 0.5, RollR * 0.95);
			AddRevolve(P.Body, FBeachFrame(RollC, AxisZTo(FVector(0.0, 1.0, 0.0))), Prof, 12, [&](int32 Ring, int32 Side) -> FLinearColor
			{
				if (Ring == 3) { return (Side % 3 == 0) ? ColA : ((Side % 3 == 1) ? ColB : ColC); }
				return (Ring % 2) ? ColA : ColB;
			});
			P.ColCapsule(RollC + FVector(0.0, RollR, 0.0), RollC + FVector(0.0, Ly - RollR, 0.0), RollR * 0.95);
		}
		// Colisión: la toalla plana y las dos caras de cada pliegue.
		P.ColBox(FVector((FlatEnd - Lx * 0.5) * 0.5, 0.0, (Thick + 2.0) * 0.5), FQuat::Identity, FVector((FlatEnd + Lx * 0.5) * 0.5, Ly * 0.5, (Thick + 2.0) * 0.5));
		const double Surface = Thick + 2.0;
		for (const FFold& Each : Folds)
		{
			const double Slope = FMath::Atan2(Each.H, Each.Half);
			const double HalfLen = FMath::Sqrt(Each.Half * Each.Half + Each.H * Each.H) * 0.5;
			for (const double Sx : { -1.0, 1.0 })
			{
				const FQuat Q(FVector(0.0, 1.0, 0.0), Sx < 0.0 ? -Slope : Slope);
				const FVector Nrm = Q.RotateVector(FVector::UpVector);
				P.ColBox(FVector(Each.X + Sx * Each.Half * 0.5, 0.0, Surface + Each.H * 0.5) - Nrm * 10.0, Q, FVector(HalfLen, Ly * 0.5, 10.0));
			}
		}
		P.Info.SinkMax = 3.f;
		P.Info.TiltMax = 1.f;
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Maderas, redes, velas, sombrillas y sillas (se trepan y se saltan desde lo alto)
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Tablón de largo Len, ancho Wd y grueso Th en Center (orientado por Rot): tramos de pintura desconchada, punta astillada y clavos. */
	inline void AddPlank(FParts& P, const FVector& Center, const FQuat& Rot, double Len, double Wd, double Th, const FLinearColor& Wood, const FLinearColor& Paint,
		double PaintAmount, uint32 PlankSeed, bool bCollide = true)
	{
		constexpr int32 Segs = 5;
		const FVector Ax = Rot.GetAxisX();
		const FVector Ay = Rot.GetAxisY();
		const FVector Az = Rot.GetAxisZ();
		for (int32 s = 0; s < Segs; ++s)
		{
			const double X0 = -Len * 0.5 + Len * s / Segs;
			const bool bPainted = Rnd(PlankSeed, s) < PaintAmount;
			const FLinearColor Col = bPainted ? Paint : Shade(Wood, static_cast<float>(0.9 + 0.2 * Rnd(PlankSeed, 10 + s)));
			AddOBox(P.Body, Center + Ax * (X0 + Len / Segs * 0.5), Rot, FVector(Len / Segs * 0.5 + 0.5, Wd * 0.5, Th * 0.5), Col);
		}
		const FVector End = Center + Ax * (Len * 0.5);
		for (int32 k = 0; k < 3; ++k)
		{
			const double Off = (k - 1) * Wd * 0.3;
			P.Body.AddTri(End + Ay * (Off - Wd * 0.15) + Az * (Th * 0.4), End + Ay * (Off + Wd * 0.15) + Az * (Th * 0.4),
				End + Ax * Cm(RndIn(PlankSeed, 30 + k, 1.0, 2.5)) + Ay * Off, Az, Shade(Wood, 0.95f));
		}
		for (int32 n = 0; n < 2; ++n)
		{
			const FVector At = Center + Ax * (Len * (n == 0 ? -0.4 : 0.4)) + Ay * (Wd * RndIn(PlankSeed, 20 + n, -0.25, 0.25)) + Az * (Th * 0.5 + 10.0);
			AddOBox(P.Body, At, Rot, FVector(6.0, 6.0, 12.0), Hex(0x8A4B2A, 0.5f));
		}
		if (bCollide) { P.ColBox(Center, Rot, FVector(Len * 0.5, Wd * 0.5, Th * 0.5)); }
	}

	/** Tablones viejos de barca o de obra: dos cruzados, un cajón y un tablón apoyado en él que hace de rampa (y otro suelto). */
	inline void BuildOldPlanks(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 PaintHex[4] = { 0xA89B86, 0x3D6FA8, 0x2F8F8A, 0x8A3B2A };
		const FLinearColor Wood = Hex(0x9C8467);
		const FLinearColor Paint = Hex(PaintHex[Kind]);
		const FLinearColor Trim = Hex(0xEDE8DC);
		const double PaintAmt = Kind == 0 ? 0.0 : 0.55;
		const double Len = Cm(50.0);
		const double Wd = Cm(9.0);
		const double Th = Cm(2.0);
		const double Yaw0 = RndIn(Seed, 1, -20.0, 20.0);
		AddPlank(P, FVector(0.0, -Cm(4.0), Th * 0.5), YawQ(Yaw0), Len * 0.95, Wd, Th, Wood, Paint, PaintAmt, Seed + 1u);
		AddPlank(P, FVector(Cm(2.0), Cm(5.0), Th * 1.5), YawQ(Yaw0 + RndIn(Seed, 2, 35.0, 70.0)), Len * 0.8, Wd, Th, Wood, Kind == 1 ? Trim : Paint, PaintAmt, Seed + 2u);
		// Cajón viejo y un tablón apoyado en él: rampa de ~19° hasta su tapa (3,6 m).
		const double Box = Cm(6.0);
		const FVector CrateC(Cm(14.0), -Cm(6.0), Box);
		const FQuat CrateQ = YawQ(8.0);
		AddOBox(P.Body, CrateC, CrateQ, FVector(Box, Box, Box), Shade(Wood, 0.85f));
		for (int32 s = 0; s < 3; ++s)
		{
			AddOBox(P.Body, CrateC + FVector(0.0, 0.0, -Box * 0.66 + Box * 0.66 * s), CrateQ, FVector(Box + 4.0, Box + 4.0, 10.0), Shade(Wood, 0.7f));
		}
		P.ColBox(CrateC, CrateQ, FVector(Box, Box, Box));
		const FVector Low(-Cm(22.0), -Cm(6.0), Th * 0.5);
		const FVector High(CrateC.X - Box * 0.3, -Cm(6.0), Box * 2.0 + Th * 0.5);
		AddPlank(P, (Low + High) * 0.5, FRotationMatrix::MakeFromXZ(High - Low, FVector::UpVector).ToQuat(), FVector::Dist(Low, High) + Cm(4.0), Wd, Th, Wood, Paint,
			PaintAmt, Seed + 3u);
		if (Kind >= 2)
		{
			AddPlank(P, FVector(-Cm(12.0), Cm(12.0), Th * 0.5), YawQ(Yaw0 + 95.0), Len * 0.5, Wd * 0.8, Th, Wood, Paint, PaintAmt, Seed + 4u);
		}
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
		P.Info.TiltMax = 1.5f;
	}

	/** Hebra de red: cinta fina de dos caras de A a B. */
	inline void AddStrand(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, double HalfW, const FLinearColor& Color)
	{
		const FVector D = B - A;
		FVector Across = FVector::CrossProduct(D, FVector::UpVector).GetSafeNormal();
		if (Across.IsNearlyZero()) { Across = FVector(0.0, 1.0, 0.0); }
		Across *= HalfW;
		const FVector Nrm = FVector::CrossProduct(Across, D).GetSafeNormal();
		M.AddQuad(A - Across, A + Across, B + Across, B - Across, Nrm, Color);
		M.AddQuad(A - Across, A + Across, B + Across, B - Across, -Nrm, Shade(Color, 0.7f));
	}

	/**
	 * Red de pesca echada sobre una piedra: montón enredado que se sube andando (menos de 40°, 2,2 m), malla y cabo de
	 * borde con flotadores por encima y un paño colgado de un palo que se mece con el viento (parte animada).
	 */
	inline void BuildFishingNet(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 NetHex[4] = { 0x3D8F5A, 0x2F6DA3, 0xE0662A, 0x4FA8A0 };
		const FLinearColor Net = Hex(NetHex[Kind]);
		const FLinearColor NetDark = Shade(Net, 0.55f);
		const FLinearColor RopeC = Hex(0xC8A86E);
		const FLinearColor FloatC = Hex(Kind == 2 ? 0xF2F2EE : 0xF06A25, 0.25f);
		const FLinearColor Cork = Hex(0xB88A56);
		const FLinearColor Stick = Hex(0x8C7355);
		const double MoundR = Cm(23.0);
		const double MoundH = Cm(7.9);
		AddRevolve(P.Body, FBeachFrame(), CapProfile(MoundR, MoundH, 4, 20.0), 12, [&](int32 Ring, int32 Side) { return ((Ring + Side) % 3 == 0) ? Net : NetDark; }, 0.06, Seed);
		P.ColCap(FVector::ZeroVector, MoundR, MoundH);
		const double Rs = (MoundR * MoundR + MoundH * MoundH) / (2.0 * MoundH);
		const double Phase = static_cast<double>(Seed % 7u);
		auto Surface = [&](double Rad, double Ang)
		{
			const double OnMound = Rad < MoundR ? (MoundH - Rs) + FMath::Sqrt(FMath::Max(0.0, Rs * Rs - Rad * Rad)) : 0.0;
			const double Folds = Rad > MoundR * 0.8 ? Cm(0.9) * FMath::Max(0.0, FMath::Sin(Ang * 3.0 + Rad / 150.0 + Phase)) : 0.0;
			return FMath::Max(OnMound, 0.0) + Folds + 12.0;
		};
		auto EdgeR = [Phase](double Ang) { return Cm(36.0) + Cm(5.0) * FMath::Sin(Ang * 3.0 + Phase); };
		constexpr int32 Radials = 24;
		constexpr int32 Rings = 8;
		const double Cell = Cm(5.0);
		auto NetPt = [&](int32 i, int32 k)
		{
			const double Ang = TNProcMap::TwoPi * k / Radials + 0.08 * FMath::Sin(i * 1.7 + k);
			const double Rad = FMath::Min(Cell * i, EdgeR(Ang));
			return FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, Surface(Rad, Ang));
		};
		int32 LastRing[Radials];
		for (int32 k = 0; k < Radials; ++k)
		{
			LastRing[k] = FMath::Min(Rings, static_cast<int32>(EdgeR(TNProcMap::TwoPi * k / Radials) / Cell) + 1);
		}
		TArray<FVector> Edge;
		for (int32 k = 0; k < Radials; ++k)
		{
			const int32 K1 = (k + 1) % Radials;
			for (int32 i = 1; i < LastRing[k]; ++i) { AddStrand(P.Body, NetPt(i, k), NetPt(i + 1, k), 8.0, Net); }
			const int32 Top = FMath::Min(LastRing[k], LastRing[K1]);
			for (int32 i = 1; i < Top; ++i) { AddStrand(P.Body, NetPt(i, k), NetPt(i, K1), 8.0, Net); }
			Edge.Add(NetPt(LastRing[k], k));
		}
		// Cabo del borde con flotadores (bolas o corchos).
		AddSweep(P.Body, Edge, CircleSection(4), true, [](int32) { return 14.0; }, [&RopeC](int32, int32) { return RopeC; }, false);
		for (int32 k = 0; k < Radials; k += 3)
		{
			const FVector At = Edge[k] + FVector(0.0, 0.0, Cm(1.2));
			if (Kind == 1) { AddYawBox(P.Body, At, 360.0 * k / Radials, FVector(Cm(1.2), Cm(2.0), Cm(1.0)), Cork); }
			else { AddBlob(P.Body, FBeachFrame(At, FQuat::Identity), Cm(2.0), Cm(2.0), Cm(1.8), 7, 4, FloatC); }
		}
		// Palo clavado en el montón y un paño de red colgado de él (se mece).
		const double StickAng = RndIn(Seed, 3, 0.0, TNProcMap::TwoPi);
		const FVector Radial(FMath::Cos(StickAng), FMath::Sin(StickAng), 0.0);
		const FVector Tang(-Radial.Y, Radial.X, 0.0);
		const FVector StickBase = Radial * Cm(17.0) + FVector(0.0, 0.0, Surface(Cm(17.0), StickAng) - 40.0);
		const FVector StickTop = StickBase + FVector::UpVector * Cm(19.0) + Radial * Cm(3.0);
		AddTaperTube(P.Body, { StickBase - FVector(0.0, 0.0, 60.0), StickTop }, Cm(1.1), Cm(0.8), 6, Stick);
		P.ColCapsule(StickBase, StickTop, Cm(1.1));
		auto FlapPt = [&](int32 a, int32 b)
		{
			const double U = a / 3.0;
			const double V = b / 3.0;
			return StickTop + Tang * (Cm(15.0) * (U - 0.5) * (1.0 - 0.3 * V)) + Radial * (Cm(9.0) * V + Cm(2.0) * FMath::Sin(V * 3.0 + U * 2.0)) - FVector(0.0, 0.0, Cm(14.0) * V);
		};
		for (int32 a = 0; a <= 3; ++a)
		{
			for (int32 b = 0; b < 3; ++b) { AddStrand(P.Moving, FlapPt(a, b), FlapPt(a, b + 1), 7.0, Net); }
		}
		for (int32 b = 1; b <= 3; ++b)
		{
			for (int32 a = 0; a < 3; ++a) { AddStrand(P.Moving, FlapPt(a, b), FlapPt(a + 1, b), 7.0, Net); }
		}
		AddTube(P.Moving, { FlapPt(0, 0), FlapPt(3, 0) }, 12.0, 4, RopeC);
		AddBlob(P.Moving, FBeachFrame(FlapPt(3, 3) - FVector(0.0, 0.0, Cm(1.5)), FQuat::Identity), Cm(1.6), Cm(1.6), Cm(1.5), 6, 3, FloatC);
		P.Info.Anim = EAnim::Sway;
		P.Info.AnimAxis = Tang;
		P.Info.AnimAmp = 7.f;
		P.Info.AnimRate = static_cast<float>(RndIn(Seed, 4, 0.25, 0.4));
		P.AnimAround(StickTop);
		P.Info.SinkMax = static_cast<float>(Cm(0.5));
		P.Info.TiltMax = 1.f;
	}

	/**
	 * Restos de la vela de un barco que naufragó hace mucho: mástil roto tumbado (enterrado por un extremo y apoyado en un
	 * cajón por el otro), la vela echada por encima como una tienda (se sube andando por la tela y se salta desde el
	 * mástil, a ~8 m; debajo queda hueco para cobijarse), cuerdas, un rollo de cabo y un jirón que ondea (parte animada).
	 */
	inline void BuildShipSailWreck(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 ClothHex[4] = { 0xE8DCC0, 0xD8C49A, 0xC9C3B6, 0xD9B77A };
		const uint32 StripeHex[4] = { 0xB8423A, 0x2F5D8A, 0x5E5A52, 0x2E2A26 };
		const FLinearColor Cloth = Hex(ClothHex[Kind]);
		const FLinearColor Stripe = Hex(StripeHex[Kind]);
		const FLinearColor Patch = Shade(Cloth, 0.8f);
		const FLinearColor MastC = Hex(0x7A6048);
		const FLinearColor MastDark = Hex(0x5A4636);
		const FLinearColor RopeC = Hex(0xB89F6E);
		const FLinearColor Barn = Hex(0xE6E0D2);
		const FLinearColor Iron = Hex(0x4A4440, 0.5f);
		const double MR = Cm(5.0);
		const double XLow = -Cm(92.0);
		const double XProp = Cm(52.0);
		const double XEnd = Cm(80.0);
		const double ZLow = 40.0;
		const double CrateH = Cm(15.0);
		const double ZProp = CrateH + MR;
		auto MastZ = [&](double X) { return ZLow + (X - XLow) * (ZProp - ZLow) / (XProp - XLow); };
		TArray<FVector> MastPath;
		for (int32 s = 0; s <= 8; ++s)
		{
			const double X = FMath::Lerp(XLow, XEnd, s / 8.0);
			MastPath.Add(FVector(X, Cm(1.0) * FMath::Sin(s * 0.7), MastZ(X)));
		}
		AddSweep(P.Body, MastPath, CircleSection(10), false, [MR](int32) { return MR; }, [&](int32 i, int32 Side) { return ((i + Side) % 4 == 0) ? MastDark : MastC; }, true,
			FVector::UpVector);
		const FVector MastDir = (MastPath.Last() - MastPath[0]).GetSafeNormal();
		P.ColCapsule(MastPath[0] + MastDir * (MR * 0.9), MastPath.Last() - MastDir * (MR * 0.9), MR * 0.97);
		AddRevolve(P.Body, FBeachFrame(MastPath.Last(), AxisZTo(MastDir)), { FVector2D(MR * 0.95, 0.0), FVector2D(MR * 0.6, Cm(3.0)), FVector2D(0.0, Cm(5.5)) }, 8,
			Shade(MastC, 1.15f), 0.35, Seed);
		for (const double Xb : { -Cm(40.0), Cm(20.0) })
		{
			const FVector At(Xb, 0.0, MastZ(Xb));
			AddTube(P.Body, { At - MastDir * Cm(0.8), At + MastDir * Cm(0.8) }, MR * 1.08, 10, Iron);
		}
		for (int32 b = 0; b < 5; ++b)
		{
			const double Xb = FMath::Lerp(XLow + Cm(4.0), XLow + Cm(30.0), b / 4.0);
			const double Ang = RndIn(Seed, 10 + b, -1.2, 1.2);
			const FVector Nrm(0.0, FMath::Sin(Ang), FMath::Cos(Ang));
			AddBarnacle(P.Body, FVector(Xb, 0.0, MastZ(Xb)) + Nrm * MR, Nrm, Cm(0.8), Barn);
		}
		// Cajón donde se apoya el mástil.
		const FVector CrateC(XProp, 0.0, CrateH * 0.5);
		const FQuat CrateQ = YawQ(RndIn(Seed, 20, -15.0, 15.0));
		const FVector CrateHalf(CrateH * 0.5, CrateH * 0.55, CrateH * 0.5);
		AddOBox(P.Body, CrateC, CrateQ, CrateHalf, Shade(MastC, 1.1f));
		for (int32 s = 0; s < 3; ++s)
		{
			AddOBox(P.Body, CrateC + FVector(0.0, 0.0, -CrateH * 0.33 + CrateH * 0.33 * s), CrateQ, CrateHalf + FVector(5.0, 5.0, 12.0 - CrateHalf.Z), MastDark);
		}
		P.ColBox(CrateC, CrateQ, CrateHalf);
		// La vela, a los dos lados del mástil: de la cresta a la arena, algo combada, con franja, remiendos y rotos.
		const double TentX0 = XLow + Cm(10.0);
		const double TentX1 = XProp - Cm(4.0);
		const double Spread = Cm(46.0);
		auto ClothPos = [&](double SideSign, double U, double V)
		{
			// Por el lado -Y se recorre U al revés para que la cara de arriba mire hacia fuera.
			const double X = SideSign > 0.0 ? FMath::Lerp(TentX0, TentX1, U) : FMath::Lerp(TentX1, TentX0, U);
			const double Ridge = MastZ(X) + MR + 10.0;
			const double EdgeY = SideSign * (Spread + Cm(3.0) * FMath::Sin(X / 900.0 + SideSign));
			const double Z = FMath::Lerp(Ridge, -15.0, V) - 0.08 * (Ridge + 15.0) * FMath::Sin(UE_DOUBLE_PI * V);
			return FVector(X, FMath::Lerp(SideSign * MR * 0.7, EdgeY, V), Z);
		};
		for (const double Sy : { -1.0, 1.0 })
		{
			const uint32 SideSeed = Seed + (Sy > 0.0 ? 11u : 23u);
			AddSheet(P.Body, 8, 5, [&](double U, double V) { return ClothPos(Sy, U, V); }, 10.0, [&](int32 I, int32 J) -> FLinearColor
			{
				if (J == 2) { return Stripe; }
				return Rnd(SideSeed, I * 7 + J) < 0.12 ? Patch : Cloth;
			}, [&](int32 I, int32 J) { return !(J >= 1 && J <= 3 && Rnd(SideSeed, 500 + I * 7 + J) < 0.07); });
			// Colisión: 4 × 2 cajas finas por lado, siguiendo la comba de la tela.
			for (int32 a = 0; a < 4; ++a)
			{
				for (int32 h = 0; h < 2; ++h)
				{
					const FVector P00 = ClothPos(Sy, a / 4.0, h * 0.5);
					const FVector P10 = ClothPos(Sy, (a + 1) / 4.0, h * 0.5);
					const FVector P01 = ClothPos(Sy, a / 4.0, (h + 1) * 0.5);
					const FVector P11 = ClothPos(Sy, (a + 1) / 4.0, (h + 1) * 0.5);
					const FVector AlongU = ((P10 - P00) + (P11 - P01)) * 0.5;
					const FVector AlongV = ((P01 - P00) + (P11 - P10)) * 0.5;
					FVector Nrm = FVector::CrossProduct(AlongU, AlongV).GetSafeNormal();
					if (Nrm.Z < 0.0) { Nrm = -Nrm; }
					P.ColBox((P00 + P10 + P01 + P11) * 0.25 - Nrm * 20.0, FRotationMatrix::MakeFromXZ(AlongU, Nrm).ToQuat(),
						FVector(AlongU.Size() * 0.5 + 10.0, AlongV.Size() * 0.5 + 10.0, 20.0));
				}
			}
		}
		// Cuerdas por encima de la tela hasta la arena.
		for (int32 r = 0; r < 4; ++r)
		{
			const double SideSign = (r % 2) ? 1.0 : -1.0;
			const double Frac = RndIn(Seed, 30 + r, 0.15, 0.85);
			TArray<FVector> Line;
			for (int32 s = 0; s <= 5; ++s) { Line.Add(ClothPos(SideSign, SideSign > 0.0 ? Frac : 1.0 - Frac, s / 5.0) + FVector(0.0, 0.0, 16.0)); }
			const FVector Out(0.0, SideSign, 0.0);
			const FVector Mid = Line.Last() + Out * Cm(5.0) + FVector(0.0, 0.0, -10.0);
			const FVector Tail = Mid + Out * Cm(5.0) + FVector(Cm(2.0), 0.0, 0.0);
			Line.Add(Mid);
			Line.Add(Tail);
			AddTube(P.Body, Line, Cm(0.55), 4, RopeC, true);
		}
		// Rollo de cabo en la arena.
		const FVector CoilC(Cm(66.0), Cm(22.0) * ((Kind % 2) ? 1.0 : -1.0), 0.0);
		TArray<FVector> Coil;
		for (int32 s = 0; s <= 30; ++s)
		{
			const double Ang = TNProcMap::TwoPi * 2.5 * s / 30.0;
			const double Rad = Cm(5.5) - Cm(2.5) * s / 30.0;
			Coil.Add(CoilC + FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, Cm(0.6) + 3.0 * s));
		}
		AddTube(P.Body, Coil, Cm(0.6), 4, RopeC, true);
		// Jirón de vela colgado de la punta del mástil: ondea (gira por el eje del mástil y se abomba).
		const double FlapSide = (Kind % 2) ? -1.0 : 1.0;
		const double FX0 = Cm(62.0);
		const double FX1 = Cm(76.0);
		AddSheet(P.Moving, 4, 3, [&](double U, double V)
		{
			const double X = FlapSide > 0.0 ? FMath::Lerp(FX0, FX1, U) : FMath::Lerp(FX1, FX0, U);
			const double Rag = V > 0.95 ? Cm(1.5) * FMath::Sin(U * 17.0) : 0.0;
			return FVector(X, FlapSide * (MR + Cm(4.0) * V), MastZ(X) + MR * 0.3 - Cm(16.0) * V - Rag);
		}, 8.0, [&](int32 I, int32 J) -> FLinearColor { return (J == 1) ? Stripe : (((I + J) % 3 == 0) ? Patch : Cloth); });
		P.Info.Anim = EAnim::Flutter;
		P.Info.AnimAxis = FVector(1.0, 0.0, 0.0);
		P.Info.AnimAmp = 10.f;
		P.Info.AnimRate = static_cast<float>(RndIn(Seed, 50, 0.7, 1.1));
		P.AnimAround(FVector((FX0 + FX1) * 0.5, 0.0, MastZ((FX0 + FX1) * 0.5)));
		P.Info.SinkMax = static_cast<float>(Cm(0.5));
	}

	/**
	 * Sombrilla clavada del día anterior, algo torcida: mástil de dos tramos con la rótula, lona de ocho gajos con su
	 * faldón y montoncito de arena al pie. Colisión en el mástil y en la lona (dos cajas finas por gajo): se puede subir a
	 * la lona desde algo alto y cobijarse debajo.
	 */
	inline void BuildPlantedUmbrella(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 AHex[4] = { 0xE0342E, 0x2E6FD0, 0xF2C81E, 0x2EB0A8 };
		const uint32 BHex[4] = { 0xF6F2EA, 0xF6F2EA, 0xF28C1E, 0xF6F2EA };
		const FLinearColor ColA = Hex(AHex[Kind], 0.1f);
		const FLinearColor ColB = Hex(BHex[Kind], 0.1f);
		const FLinearColor PoleC = Hex(0xD8DCE0, 0.8f);
		const FLinearColor PoleDark = Hex(0x9EA4AA, 0.8f);
		const FLinearColor SandC = Hex(0xE6CC94);
		const double Lean = FMath::DegreesToRadians(RndIn(Seed, 1, 3.0, 7.0));
		const FVector Axis(FMath::Sin(Lean), 0.0, FMath::Cos(Lean));
		const double ApexH = Cm(125.0);
		const double CanR = Cm(46.0);
		const double CanDrop = CanR * FMath::Tan(FMath::DegreesToRadians(22.0));
		// El pie se corre hacia atrás para que la lona inclinada quede centrada en la huella.
		const FVector Base(-FMath::Sin(Lean) * ApexH * 0.45, 0.0, 0.0);
		const FBeachFrame F(Base, AxisZTo(Axis));
		const double Joint = Cm(60.0);
		AddTube(P.Body, { F.P(FVector(0.0, 0.0, -Cm(12.0))), F.P(FVector(0.0, 0.0, Joint)) }, Cm(1.5), 8, PoleC);
		AddTube(P.Body, { F.P(FVector(0.0, 0.0, Joint)), F.P(FVector(0.0, 0.0, ApexH)) }, Cm(1.2), 8, PoleC);
		AddBlob(P.Body, FBeachFrame(F.P(FVector(0.0, 0.0, Joint)), F.Q), Cm(2.0), Cm(2.0), Cm(2.4), 8, 3, PoleDark);
		AddRevolve(P.Body, FBeachFrame(Base, FQuat::Identity), CapProfile(Cm(11.0), Cm(2.6), 3, 20.0), 10, SandC, 0.1, Seed);
		P.ColCap(Base, Cm(11.0), Cm(2.6));
		P.ColCapsule(F.P(FVector(0.0, 0.0, -40.0)), F.P(FVector(0.0, 0.0, ApexH - Cm(4.0))), Cm(1.5));
		constexpr int32 Panels = 8;
		const double Rr[4] = { 0.0, 0.35, 0.7, 1.0 };
		auto CanopyPt = [&](double Ang, double Frac, double Off)
		{
			return F.P(FVector(FMath::Cos(Ang) * CanR * Frac, FMath::Sin(Ang) * CanR * Frac, ApexH - CanDrop * FMath::Pow(Frac, 1.35) + Off));
		};
		const FVector CanUp = F.D(FVector::UpVector);
		for (int32 p = 0; p < Panels; ++p)
		{
			const double A0 = TNProcMap::TwoPi * p / Panels;
			const double A1 = TNProcMap::TwoPi * (p + 1) / Panels;
			const double Am = (A0 + A1) * 0.5;
			const FLinearColor Col = (p % 2) ? ColB : ColA;
			for (int32 r = 0; r < 3; ++r)
			{
				P.Body.AddQuad(CanopyPt(A0, Rr[r], 4.0), CanopyPt(A1, Rr[r], 4.0), CanopyPt(A1, Rr[r + 1], 4.0), CanopyPt(A0, Rr[r + 1], 4.0), CanUp, Col);
				P.Body.AddQuad(CanopyPt(A0, Rr[r], -4.0), CanopyPt(A1, Rr[r], -4.0), CanopyPt(A1, Rr[r + 1], -4.0), CanopyPt(A0, Rr[r + 1], -4.0), -CanUp, Shade(Col, 0.7f));
			}
			// Faldón: una onda que cuelga del borde de cada gajo.
			const FVector Out = F.D(FVector(FMath::Cos(Am), FMath::Sin(Am), 0.0));
			const FVector E0 = CanopyPt(A0, 1.0, 0.0);
			const FVector E1 = CanopyPt(A1, 1.0, 0.0);
			const FVector Hang = CanopyPt(Am, 0.97, -Cm(3.5));
			P.Body.AddTri(E0, E1, Hang, Out, Col);
			P.Body.AddTri(E0, E1, Hang, -Out, Shade(Col, 0.7f));
			// Varilla por debajo.
			AddTube(P.Body, { F.P(FVector(0.0, 0.0, ApexH - Cm(22.0))), CanopyPt(A0, 0.62, -8.0) }, 7.0, 3, PoleDark, false);
			// Colisión: dos cajas finas por gajo (mitad de dentro y de fuera), con la cara de arriba en la lona.
			for (int32 h = 0; h < 2; ++h)
			{
				const double Ra = h == 0 ? 0.0 : 0.5;
				const double Rb = h == 0 ? 0.5 : 1.0;
				const FVector Pa = CanopyPt(Am, Ra, 0.0);
				const FVector Pb = CanopyPt(Am, Rb, 0.0);
				const FVector Dir = (Pb - Pa).GetSafeNormal();
				FVector Nrm = FVector::CrossProduct(Dir, F.D(FVector(-FMath::Sin(Am), FMath::Cos(Am), 0.0))).GetSafeNormal();
				if (Nrm.Z < 0.0) { Nrm = -Nrm; }
				const double HalfW = CanR * FMath::Sin(UE_DOUBLE_PI / Panels) * Rb * 1.02;
				P.ColBox((Pa + Pb) * 0.5 - Nrm * 15.0, FRotationMatrix::MakeFromXZ(Dir, Nrm).ToQuat(), FVector(FVector::Dist(Pa, Pb) * 0.5, HalfW, 15.0));
			}
		}
		AddRevolve(P.Body, FBeachFrame(F.P(FVector(0.0, 0.0, ApexH)), F.Q), { FVector2D(Cm(1.2), 0.0), FVector2D(Cm(0.9), Cm(1.5)), FVector2D(0.0, Cm(2.6)) }, 8, PoleDark);
		P.Info.SinkMax = static_cast<float>(Cm(1.0));
	}

	/**
	 * Silla de playa baja (mira hacia +X del actor) con una toalla echada del asiento a la arena: la toalla es una rampa
	 * (~33°) hasta el asiento (5,6 m), del que se salta; debajo del asiento hay sitio para cobijarse. Colisión en patas,
	 * travesaño de atrás, asiento, respaldo, reposabrazos y toalla.
	 */
	inline void BuildBeachChair(FParts& P, int32 Variant, uint32 Seed)
	{
		const int32 Kind = Variant % 4;
		const uint32 FabAHex[4] = { 0x2E6FD0, 0xE0342E, 0x3F9B3A, 0xF28C1E };
		const uint32 FabBHex[4] = { 0xF6F2EA, 0xF6F2EA, 0xF2D21E, 0xF49AC0 };
		const uint32 TowAHex[4] = { 0xF2D21E, 0x2EB0A8, 0xE0342E, 0x2E6FD0 };
		const uint32 TowBHex[4] = { 0xF6F2EA, 0xF28C1E, 0xF6F2EA, 0xF2D21E };
		const FLinearColor FabA = Hex(FabAHex[Kind], 0.05f);
		const FLinearColor FabB = Hex(FabBHex[Kind], 0.05f);
		const FLinearColor TubeC = Kind == 2 ? Hex(0xF2F2F0, 0.25f) : Hex(0xC9CDD2, 0.85f);
		const FLinearColor ArmC = Hex(0xA0703E);
		const FLinearColor TowA = Hex(TowAHex[Kind]);
		const FLinearColor TowB = Hex(TowBHex[Kind]);
		// Medidas reales: 50 cm de ancho, asiento a 20/15 cm, reposabrazos a 32 cm y respaldo hasta 68 cm; corrida para centrarla.
		const double Ox = -Cm(5.4);
		const double Hw = Cm(25.0);
		const double TubeR = Cm(1.25);
		const double FrontX = Cm(12.5) + Ox;
		const double RearX = -Cm(26.8) + Ox;
		const double BackTopX = -Cm(37.5) + Ox;
		const double SeatFrontZ = Cm(20.0);
		const double SeatRearZ = Cm(15.0);
		const double ArmZ = Cm(32.0);
		const double BackTopZ = Cm(68.0);
		for (const double Sy : { -1.0, 1.0 })
		{
			const double Y = Sy * Hw;
			const FVector FrontFoot(FrontX, Y, -40.0);
			const FVector FrontTop(FrontX - 30.0, Y, ArmZ);
			const FVector RearFoot(RearX + 60.0, Y, -40.0);
			const FVector RearSeat(RearX, Y, SeatRearZ);
			const FVector BackTop(BackTopX, Y, BackTopZ);
			AddTube(P.Body, { FrontFoot, FrontTop }, TubeR, 8, TubeC);
			AddTube(P.Body, { RearFoot, RearSeat, BackTop }, TubeR, 8, TubeC);
			AddTube(P.Body, { FrontTop, FVector(RearX - 40.0, Y, ArmZ + 60.0) }, TubeR * 0.8, 6, TubeC);
			const FVector ArmA(FrontX + 10.0, Y, ArmZ + 45.0);
			const FVector ArmB(RearX - 20.0, Y, ArmZ + 100.0);
			const FQuat ArmQ = FRotationMatrix::MakeFromXZ(ArmB - ArmA, FVector::UpVector).ToQuat();
			const FVector ArmHalf(FVector::Dist(ArmA, ArmB) * 0.5, Cm(2.5), Cm(0.9));
			AddOBox(P.Body, (ArmA + ArmB) * 0.5, ArmQ, ArmHalf, ArmC);
			P.ColBox((ArmA + ArmB) * 0.5, ArmQ, ArmHalf);
			P.ColCapsule(FVector(FrontFoot.X, Y, 0.0), FrontTop, TubeR);
			P.ColCapsule(FVector(RearFoot.X, Y, 0.0), RearSeat, TubeR);
			P.ColCapsule(RearSeat, BackTop, TubeR);
		}
		AddTube(P.Body, { FVector(FrontX - 10.0, -Hw, SeatFrontZ), FVector(FrontX - 10.0, Hw, SeatFrontZ) }, TubeR, 8, TubeC);
		AddTube(P.Body, { FVector(RearX, -Hw, SeatRearZ), FVector(RearX, Hw, SeatRearZ) }, TubeR, 8, TubeC);
		AddTube(P.Body, { FVector(BackTopX, -Hw, BackTopZ), FVector(BackTopX, Hw, BackTopZ) }, TubeR, 8, TubeC);
		AddTube(P.Body, { FVector(RearX + 60.0, -Hw, 50.0), FVector(RearX + 60.0, Hw, 50.0) }, TubeR * 0.8, 6, TubeC);
		P.ColCapsule(FVector(RearX + 60.0, -Hw, 50.0), FVector(RearX + 60.0, Hw, 50.0), TubeR * 0.8);
		// Lona del asiento (combada) y del respaldo, a rayas.
		AddSheet(P.Body, 6, 5, [&](double U, double V)
		{
			const double X = FMath::Lerp(RearX, FrontX - 10.0, U);
			const double Z = FMath::Lerp(SeatRearZ, SeatFrontZ, U) - Cm(3.0) * FMath::Sin(UE_DOUBLE_PI * U) * (0.6 + 0.4 * FMath::Sin(UE_DOUBLE_PI * V));
			return FVector(X, FMath::Lerp(-Hw + 20.0, Hw - 20.0, V), Z);
		}, 10.0, [&](int32, int32 J) { return (J % 2) ? FabB : FabA; });
		const FVector BackLo(RearX, 0.0, SeatRearZ);
		const FVector BackHi(BackTopX, 0.0, BackTopZ);
		AddSheet(P.Body, 5, 6, [&](double U, double V)
		{
			return FMath::Lerp(BackLo, BackHi, V) + FVector(-Cm(2.0) * FMath::Sin(UE_DOUBLE_PI * V), FMath::Lerp(-Hw + 20.0, Hw - 20.0, U), 0.0);
		}, 10.0, [&](int32 I, int32) { return (I % 2) ? FabB : FabA; });
		const FVector SeatA(RearX, 0.0, SeatRearZ - Cm(2.0));
		const FVector SeatB(FrontX - 10.0, 0.0, SeatFrontZ - Cm(1.0));
		const FQuat SeatQ = FRotationMatrix::MakeFromXZ(SeatB - SeatA, FVector::UpVector).ToQuat();
		P.ColBox((SeatA + SeatB) * 0.5 - SeatQ.GetAxisZ() * 12.0, SeatQ, FVector(FVector::Dist(SeatA, SeatB) * 0.5, Hw - 20.0, 12.0));
		const FQuat BackQ = FRotationMatrix::MakeFromXZ(BackHi - BackLo, FVector(1.0, 0.0, 0.0)).ToQuat();
		P.ColBox((BackLo + BackHi) * 0.5 - BackQ.GetAxisZ() * 12.0 - FVector(Cm(1.0), 0.0, 0.0), BackQ, FVector(FVector::Dist(BackLo, BackHi) * 0.5, Hw - 20.0, 12.0));
		// Toalla del asiento a la arena: la rampa para subir.
		const double TowelHalfW = Cm(15.0);
		const FVector TopEdge(FrontX - Cm(6.0), 0.0, SeatFrontZ - Cm(1.5) + 14.0);
		const FVector Lip(FrontX + 10.0, 0.0, SeatFrontZ + TubeR + 8.0);
		const FVector Foot(FrontX + Cm(33.0), 0.0, 8.0);
		const FVector Tail(Foot.X + Cm(4.5), 0.0, 4.0);
		const TArray<FVector> TowelLine = { TopEdge, Lip, FMath::Lerp(Lip, Foot, 0.5) - FVector(0.0, 0.0, Cm(1.0)), Foot, Tail };
		AddSheet(P.Body, 8, 4, [&](double U, double V)
		{
			const double Fp = U * (TowelLine.Num() - 1);
			const int32 Idx = FMath::Min(static_cast<int32>(Fp), TowelLine.Num() - 2);
			return FMath::Lerp(TowelLine[Idx], TowelLine[Idx + 1], Fp - Idx) + FVector(0.0, FMath::Lerp(-TowelHalfW, TowelHalfW, V), 0.0);
		}, 12.0, [&](int32 I, int32) { return (I % 2) ? TowB : TowA; });
		const FVector RampDir = (Foot - Lip).GetSafeNormal();
		FVector RampN = FVector::CrossProduct(RampDir, FVector(0.0, 1.0, 0.0));
		if (RampN.Z < 0.0) { RampN = -RampN; }
		P.ColBox((Lip + Foot) * 0.5 - RampN * 12.0, FRotationMatrix::MakeFromXZ(RampDir, RampN).ToQuat(), FVector(FVector::Dist(Lip, Foot) * 0.5, TowelHalfW, 14.0));
		P.Info.bFreeYaw = false;
		P.Info.YawJitter = 15.f;
		P.Info.SinkMax = static_cast<float>(Cm(0.8));
		P.Info.TiltMax = 1.5f;
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Castillos de arena (escalones de 80 cm: se sube de salto en salto; las almenas no tienen colisión)
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Arena mojada de los castillos (algo más oscura que la playa seca). */
	struct FSandLook
	{
		FLinearColor Sand;
		FLinearColor Dark;
		FLinearColor Light;
	};

	inline FSandLook SandLookOf()
	{
		return { Hex(0xE3C48A), Hex(0xCBA66A), Hex(0xEFD9A8) };
	}

	/** Conchita de adorno pegada a una pared (abanico de cinco gajos mirando hacia Normal). */
	inline void AddShellDeco(FTNProcMeshBuffers& M, const FVector& At, const FVector& Normal, double Size, const FLinearColor& Color)
	{
		const FVector Nn = Normal.GetSafeNormal();
		const FVector Across = FVector::CrossProduct(Nn, FVector::UpVector).GetSafeNormal();
		const FVector UpDir = FVector::CrossProduct(Across, Nn);
		for (int32 f = 0; f < 5; ++f)
		{
			const double Fa = FMath::DegreesToRadians(-60.0 + f * 30.0);
			const double Fb = FMath::DegreesToRadians(-60.0 + (f + 1) * 30.0);
			const FVector Ea = At + (Across * FMath::Sin(Fa) + UpDir * FMath::Cos(Fa)) * Size + Nn * 3.0;
			const FVector Eb = At + (Across * FMath::Sin(Fb) + UpDir * FMath::Cos(Fb)) * Size + Nn * 3.0;
			M.AddTri(At - UpDir * (Size * 0.4) + Nn * 3.0, Ea, Eb, Nn, (f % 2) ? Color : Shade(Color, 0.85f));
		}
	}

	/**
	 * Torre de cubo de radio R en C, de Z0 a Z1 (con marcas de cubo si es alta), almenas encima (sin colisión) y su
	 * prisma de colisión de 12 lados. Worn (0-1) la desgasta: más irregular y con almenas caídas.
	 */
	inline void AddSandTower(FParts& P, const FVector& C, double R, double Z0, double Z1, bool bMerlons, double Worn, uint32 TowerSeed, const FSandLook& Look)
	{
		const double H = Z1 - Z0;
		const double Rt = R * 0.9;
		const bool bRidged = H > 140.0;
		TArray<FVector2D> Prof = { FVector2D(R * 1.03, Z0 - 20.0) };
		if (bRidged)
		{
			for (const double K : { 0.33, 0.66 })
			{
				const double Rk = FMath::Lerp(R, Rt, K);
				Prof.Add(FVector2D(Rk, Z0 + H * K - 8.0));
				Prof.Add(FVector2D(Rk + 9.0, Z0 + H * K));
				Prof.Add(FVector2D(Rk, Z0 + H * K + 8.0));
			}
		}
		Prof.Add(FVector2D(Rt, Z1));
		Prof.Add(FVector2D(Rt * 0.85, Z1 + 3.0));
		Prof.Add(FVector2D(0.0, Z1 + 5.0));
		const int32 NumSeg = Prof.Num() - 1;
		const double Yaw = RndIn(TowerSeed, 1, 0.0, 30.0);
		// Tramos: las dos últimas son la azotea; con marcas, 1-2 y 4-5 son los rebordes del cubo.
		AddRevolve(P.Body, FBeachFrame(FVector(C.X, C.Y, 0.0), YawQ(Yaw)), Prof, 12, [&](int32 Ring, int32 Side) -> FLinearColor
		{
			if (Ring >= NumSeg - 2) { return Look.Light; }
			if (bRidged && (Ring == 1 || Ring == 2 || Ring == 4 || Ring == 5)) { return Look.Dark; }
			return (Side % 4 == 0) ? Shade(Look.Sand, 0.93f) : Look.Sand;
		}, 0.015 + Worn * 0.08, TowerSeed);
		if (bMerlons)
		{
			const int32 N = FMath::Clamp(static_cast<int32>(Rt / 32.0), 6, 12);
			for (int32 m = 0; m < N; ++m)
			{
				if (Worn > 0.0 && Rnd(TowerSeed, 50 + m) < Worn * 0.6) { continue; }
				const double Ang = TNProcMap::TwoPi * (m + 0.5) / N;
				const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
				AddYawBox(P.Body, FVector(C.X, C.Y, Z1 + 20.0) + Dir * (Rt - 18.0), FMath::RadiansToDegrees(Ang),
					FVector(16.0, FMath::Min(26.0, Rt * FMath::Sin(UE_DOUBLE_PI / N) * 0.6), 22.0), Look.Sand);
			}
		}
		P.ColPrism(FVector(C.X, C.Y, 0.0), (R + Rt) * 0.5 * 0.96, Z0 - 20.0, Z1, Yaw + 15.0, 6);
	}

	/**
	 * Muralla recta de A a B (en planta) de grosor Thick, de Z0 a Z0 + H, con una marca de cubo y una hilera de almenas
	 * (sin colisión) corrida MerlonSide (-1..1) hacia su izquierda; y su caja de colisión.
	 */
	inline void AddSandWall(FParts& P, const FVector2D& A, const FVector2D& B, double Thick, double Z0, double H, double MerlonSide, double Worn, uint32 WallSeed,
		const FSandLook& Look)
	{
		const FVector2D D = B - A;
		const double Len = D.Size();
		if (Len < 1.0) { return; }
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		const FVector2D Mid = (A + B) * 0.5;
		const double Top = Z0 + H;
		const FVector C(Mid.X, Mid.Y, (Z0 - 20.0 + Top) * 0.5);
		const FVector Half(Len * 0.5, Thick * 0.5, (Top - Z0 + 20.0) * 0.5);
		AddYawBox(P.Body, C, Yaw, Half, Look.Sand);
		AddYawBox(P.Body, FVector(Mid.X, Mid.Y, Z0 + H * 0.5), Yaw, FVector(Len * 0.5 - 4.0, Thick * 0.5 + 6.0, 8.0), Look.Dark);
		const FVector2D Dir = D / Len;
		const FVector2D Left(-Dir.Y, Dir.X);
		const int32 N = FMath::Max(1, static_cast<int32>(Len / 160.0));
		for (int32 m = 0; m < N; ++m)
		{
			if (Worn > 0.0 && Rnd(WallSeed, m) < Worn * 0.6) { continue; }
			const FVector2D At = A + Dir * (Len * (m + 0.5) / N) + Left * (MerlonSide * FMath::Max(0.0, Thick * 0.5 - 22.0));
			AddYawBox(P.Body, FVector(At.X, At.Y, Top + 20.0), Yaw, FVector(FMath::Min(34.0, Len / N * 0.3), 18.0, 22.0), Look.Sand);
		}
		P.ColBoxYaw(C, Yaw, Half);
	}

	/** Banderita de palillo clavada en Top: el palo va en la malla fija y la tela, si bMoving, en la parte animada (ondea). */
	inline void AddCastleFlag(FParts& P, const FVector& Top, double StickH, double FlagW, double FlagH, const FLinearColor& FlagC, bool bMoving)
	{
		const FVector StickTop = Top + FVector(0.0, 0.0, StickH);
		AddTube(P.Body, { Top - FVector(0.0, 0.0, 30.0), StickTop }, 9.0, 5, Hex(0xE8D2A0));
		FTNProcMeshBuffers& Dst = bMoving ? P.Moving : P.Body;
		AddSheet(Dst, 4, 2, [&](double U, double V)
		{
			const double Wave = 18.0 * FMath::Sin(UE_DOUBLE_PI * U * 1.5) * U;
			return StickTop + FVector(FlagW * U, Wave, FMath::Lerp(-FlagH, 0.0, V) * (1.0 - U) - FlagH * 0.5 * U);
		}, 4.0, [&FlagC](int32 I, int32) { return (I % 2) ? FlagC : Shade(FlagC, 0.88f); });
		if (bMoving)
		{
			P.Info.Anim = EAnim::Flutter;
			P.Info.AnimAxis = FVector::UpVector;
			P.Info.AnimAmp = 16.f;
			P.Info.AnimRate = 1.2f;
			P.AnimAround(StickTop);
		}
	}

	/**
	 * Castillo de arena pequeño: torre mayor de dos pisos (3 y 4 escalones), dos o tres torrecillas pegadas (2 escalones)
	 * y murallas bajas (1 escalón) que cierran un patio; conchitas y banderita. La variante 3 está medio deshecha.
	 */
	inline void BuildSandCastleSmall(FParts& P, int32 Variant, uint32 Seed)
	{
		const FSandLook Look = SandLookOf();
		const int32 Kind = Variant % 4;
		const double Worn = Kind == 3 ? 0.8 : 0.0;
		const double Step = Kind == 3 ? 68.0 : 80.0;
		const uint32 FlagHex[4] = { 0xE0342E, 0x2E6FD0, 0xF2C81E, 0x3F9B3A };
		const FLinearColor FlagC = Hex(FlagHex[Kind]);
		const FVector Main(Cm(2.0), 0.0, 0.0);
		const double MainR = Cm(6.8);
		AddSandTower(P, Main, MainR, 0.0, Step * 3.0, false, Worn, Seed + 1u, Look);
		AddSandTower(P, Main, Cm(4.3), Step * 3.0, Step * 4.0, Kind != 3, Worn, Seed + 2u, Look);
		const int32 NumSmall = 2 + (Kind % 2);
		const double SmallR = Cm(3.9);
		const double BaseAng = RndIn(Seed, 3, 0.0, TNProcMap::TwoPi);
		const FVector2D MainXY(Main.X, Main.Y);
		TArray<FVector2D> Corners;
		for (int32 s = 0; s < NumSmall; ++s)
		{
			const double Ang = BaseAng + UE_DOUBLE_PI * (0.75 + 0.5 * s / FMath::Max(1, NumSmall - 1));
			const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
			const FVector2D At = MainXY + Dir * ((MainR * 0.95 + SmallR) * 0.98);
			AddSandTower(P, FVector(At.X, At.Y, 0.0), SmallR, 0.0, Step * 2.0, true, Worn, Seed + 10u + static_cast<uint32>(s), Look);
			Corners.Add(MainXY + Dir * Cm(22.0));
			AddSandWall(P, At + Dir * (SmallR * 0.6), Corners.Last(), Cm(3.2), 0.0, Step, 0.0, Worn, Seed + 20u + static_cast<uint32>(s), Look);
		}
		for (int32 s = 0; s + 1 < NumSmall; ++s)
		{
			AddSandWall(P, Corners[s], Corners[s + 1], Cm(3.2), 0.0, Step, 0.0, Worn, Seed + 30u + static_cast<uint32>(s), Look);
		}
		for (int32 c = 0; c < 5; ++c)
		{
			const double Ang = RndIn(Seed, 60 + c, 0.0, TNProcMap::TwoPi);
			const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
			AddShellDeco(P.Body, Main + Dir * (MainR * 0.95 + 3.0) + FVector(0.0, 0.0, RndIn(Seed, 70 + c, 40.0, Step * 2.6)), Dir, Cm(1.1),
				Hex((c % 2) ? 0xF4C6D0 : 0xF6EAD2));
		}
		if (Kind == 3)
		{
			// La banderita se ha caído a la arena.
			const FVector S0(Cm(10.0), Cm(9.0), 8.0);
			const FVector S1 = S0 + FVector(Cm(7.0), Cm(2.0), 0.0);
			AddTube(P.Body, { S0, S1 }, 9.0, 5, Hex(0xE8D2A0));
			P.Body.AddTri(S1, S1 + FVector(-Cm(1.0), Cm(4.0), 0.0), S1 + FVector(-Cm(3.0), Cm(1.5), 0.0), FVector::UpVector, FlagC);
		}
		else
		{
			AddCastleFlag(P, FVector(Main.X, Main.Y, Step * 4.0 + 5.0), Cm(7.0), Cm(4.5), Cm(3.0), FlagC, true);
		}
		P.Info.bBlocksCamera = true;
		P.Info.SinkMax = static_cast<float>(Cm(0.5));
		P.Info.TiltMax = Kind == 3 ? 2.f : 0.f;
	}

	/**
	 * Castillo de arena enorme (hecho entre toda una familia): plataforma con rampa, recinto de murallas con puerta al +X,
	 * cuatro torres de esquina, torreón con escalinata y torre mayor de cubos apilados hasta 8,8 m. Todo a escalones de
	 * 80 cm: del suelo a la plataforma, al patio, a los peldaños, a la muralla, a las torres y a la cima (se salta desde
	 * allí). La bandera de la cima ondea.
	 */
	inline void BuildSandCastleHuge(FParts& P, int32 Variant, uint32 Seed)
	{
		const FSandLook Look = SandLookOf();
		const int32 Kind = Variant % 4;
		const double Step = 80.0;
		const uint32 FlagHex[4] = { 0xE0342E, 0x2E6FD0, 0xF2C81E, 0x9B5DE5 };
		// Plataforma (un escalón) y rampa hacia la puerta.
		const double PlatR = Cm(80.0);
		AddRevolve(P.Body, FBeachFrame(FVector::ZeroVector, YawQ(11.25)),
			{ FVector2D(PlatR * 1.03, -40.0), FVector2D(PlatR, Step - 16.0), FVector2D(PlatR * 0.97, Step), FVector2D(0.0, Step + 1.0) }, 16,
			[&](int32 Ring, int32 Side) -> FLinearColor { return Ring == 0 ? Look.Dark : ((Side % 2) ? Look.Sand : Shade(Look.Sand, 0.96f)); }, 0.02, Seed);
		P.ColPrism(FVector::ZeroVector, PlatR * 0.95, -20.0, Step, 22.5, 8);
		const FVector RampLow(PlatR * 0.97 + Cm(9.0), 0.0, 0.0);
		const FVector RampHigh(PlatR * 0.97 - 20.0, 0.0, Step);
		const FVector RampAlong = (RampHigh - RampLow).GetSafeNormal();
		FVector RampN = FVector::CrossProduct(RampAlong, FVector(0.0, 1.0, 0.0));
		if (RampN.Z < 0.0) { RampN = -RampN; }
		const FQuat RampQ = FRotationMatrix::MakeFromXZ(RampAlong, RampN).ToQuat();
		const FVector RampHalf(FVector::Dist(RampLow, RampHigh) * 0.5, Cm(12.0), 25.0);
		AddOBox(P.Body, (RampLow + RampHigh) * 0.5 - RampN * 25.0, RampQ, RampHalf, Look.Sand);
		P.ColBox((RampLow + RampHigh) * 0.5 - RampN * 25.0, RampQ, RampHalf);
		// Recinto: murallas de 3 escalones (andén a 3,2 m) con la puerta en +X y su dintel.
		const double In = Cm(41.0);
		const double Wt = Cm(7.0);
		const double Wc = In + Wt * 0.5;
		const double Wend = In + Wt;
		const double Gate = Cm(9.0);
		const double WallH = Step * 3.0;
		AddSandWall(P, FVector2D(-Wc, -Wend), FVector2D(-Wc, Wend), Wt, Step, WallH, 1.0, 0.0, Seed + 1u, Look);
		AddSandWall(P, FVector2D(-Wend, -Wc), FVector2D(Wend, -Wc), Wt, Step, WallH, -1.0, 0.0, Seed + 2u, Look);
		AddSandWall(P, FVector2D(-Wend, Wc), FVector2D(Wend, Wc), Wt, Step, WallH, 1.0, 0.0, Seed + 3u, Look);
		AddSandWall(P, FVector2D(Wc, -Wend), FVector2D(Wc, -Gate), Wt, Step, WallH, -1.0, 0.0, Seed + 4u, Look);
		AddSandWall(P, FVector2D(Wc, Gate), FVector2D(Wc, Wend), Wt, Step, WallH, -1.0, 0.0, Seed + 5u, Look);
		AddSandWall(P, FVector2D(Wc, -Gate), FVector2D(Wc, Gate), Wt, Step + 190.0, WallH - 190.0, -1.0, 0.0, Seed + 6u, Look);
		// Torres de esquina: dos pisos (1 y 2 escalones sobre el andén) con banderitas quietas.
		const FVector2D CornerPts[4] = { FVector2D(Wc, Wc), FVector2D(-Wc, Wc), FVector2D(-Wc, -Wc), FVector2D(Wc, -Wc) };
		for (int32 t = 0; t < 4; ++t)
		{
			const FVector C(CornerPts[t].X, CornerPts[t].Y, 0.0);
			AddSandTower(P, C, Cm(11.0), Step, Step * 5.0, false, 0.0, Seed + 40u + static_cast<uint32>(t), Look);
			AddSandTower(P, C, Cm(7.8), Step * 5.0, Step * 6.0, true, 0.0, Seed + 50u + static_cast<uint32>(t), Look);
			AddCastleFlag(P, FVector(C.X, C.Y, Step * 6.0 + 5.0), Cm(8.0), Cm(4.0), Cm(2.6), Hex(FlagHex[(Kind + t + 1) % 4]), false);
		}
		// Torreón central con escalinata hacia la puerta (cuatro peldaños) y aspilleras.
		const double KeepHalf = Cm(19.6);
		const double KeepTop = Step * 6.0;
		const FVector KeepC(0.0, 0.0, (Step - 20.0 + KeepTop) * 0.5);
		const FVector KeepHalfExt(KeepHalf, KeepHalf, (KeepTop - Step + 20.0) * 0.5);
		AddYawBox(P.Body, KeepC, 0.0, KeepHalfExt, Look.Sand);
		AddYawBox(P.Body, FVector(0.0, 0.0, Step + (KeepTop - Step) * 0.5), 0.0, FVector(KeepHalf + 6.0, KeepHalf + 6.0, 9.0), Look.Dark);
		P.ColBoxYaw(KeepC, 0.0, KeepHalfExt);
		const FLinearColor Slit = Hex(0x5A4630);
		for (int32 f = 1; f < 4; ++f)
		{
			const FQuat FaceQ = YawQ(90.0 * f);
			for (const double Wy : { -1.0, 1.0 })
			{
				const FVector At = FaceQ.RotateVector(FVector(KeepHalf + 2.0, Wy * Cm(8.0), 0.0)) + FVector(0.0, 0.0, Step + Cm(8.0));
				AddYawBox(P.Body, At, 90.0 * f, FVector(4.0, Cm(1.2), Cm(2.8)), Slit);
			}
		}
		for (int32 s = 0; s < 4; ++s)
		{
			const double StepTop = Step * (2 + s);
			const double X0 = KeepHalf + Cm(4.3) * (3 - s);
			const FVector StepC(X0 + Cm(4.3) * 0.5, 0.0, (Step - 20.0 + StepTop) * 0.5);
			const FVector StepHalf(Cm(4.3) * 0.5, Cm(9.0), (StepTop - Step + 20.0) * 0.5);
			AddYawBox(P.Body, StepC, 0.0, StepHalf, (s % 2) ? Look.Sand : Shade(Look.Sand, 0.95f));
			P.ColBoxYaw(StepC, 0.0, StepHalf);
		}
		// Dos peldaños del patio al andén de la muralla de -Y.
		for (int32 s = 0; s < 2; ++s)
		{
			const double StepTop = Step * (2 + s);
			const double Y0 = -In + Cm(4.3) * (1 - s);
			const FVector StepC(-Cm(12.5), Y0 + Cm(4.3) * 0.5, (Step - 20.0 + StepTop) * 0.5);
			const FVector StepHalf(Cm(5.4), Cm(4.3) * 0.5, (StepTop - Step + 20.0) * 0.5);
			AddYawBox(P.Body, StepC, 0.0, StepHalf, (s % 2) ? Look.Sand : Shade(Look.Sand, 0.95f));
			P.ColBoxYaw(StepC, 0.0, StepHalf);
		}
		// Torre mayor de cubos apilados sobre el torreón, con la bandera que ondea.
		const double TierR[5] = { Cm(15.0), Cm(12.3), Cm(10.0), Cm(8.0), Cm(6.4) };
		const FVector TowerC(-Cm(3.0), 0.0, 0.0);
		for (int32 t = 0; t < 5; ++t)
		{
			AddSandTower(P, TowerC, TierR[t], KeepTop + Step * t, KeepTop + Step * (t + 1), t == 4, 0.0, Seed + 60u + static_cast<uint32>(t), Look);
		}
		AddCastleFlag(P, FVector(TowerC.X, TowerC.Y, KeepTop + Step * 5.0 + 5.0), Cm(12.0), Cm(7.0), Cm(4.5), Hex(FlagHex[Kind]), true);
		// Conchas pegadas por fuera de las murallas.
		for (int32 c = 0; c < 12; ++c)
		{
			const int32 Face = c % 4;
			const FQuat FaceQ = YawQ(90.0 * Face);
			const FVector Out = FaceQ.RotateVector(FVector(1.0, 0.0, 0.0));
			const double AlongWall = RndIn(Seed, 80 + c, -In * 0.8, In * 0.8);
			if (Face == 0 && FMath::Abs(AlongWall) < Gate + 60.0) { continue; }
			const FVector At = FaceQ.RotateVector(FVector(Wend, AlongWall, 0.0)) + FVector(0.0, 0.0, Step + RndIn(Seed, 90 + c, 40.0, WallH - 50.0));
			AddShellDeco(P.Body, At, Out, Cm(1.6), Hex((c % 3 == 0) ? 0xF4C6D0 : ((c % 3 == 1) ? 0xF6EAD2 : 0xE6D0FF)));
		}
		P.Info.bBlocksCamera = true;
		P.Info.SinkMax = static_cast<float>(Cm(0.4));
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Tramos: pasarela de madera y caminito de palos (piezas que ATN_BeachDecor instancia a lo largo de Extent)
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Pasarela de madera vieja: tramos de cuatro tablas sobre dos largueros, con un par de pilotes en medio (simétricos). */
	namespace BoardwalkKit
	{
		constexpr double SlatPitch = 320.0;
		constexpr int32 SlatsPerModule = 4;
		constexpr double ModuleLen = SlatPitch * SlatsPerModule;
		constexpr double HalfWidth = 650.0;
		constexpr double DeckTop = 110.0;
		/** Tramos rectos: entero (0), sin una tabla (1), con una rota (2), con una suelta levantada (3) y con tablas movidas (4). */
		constexpr int32 NumKinds = 5;
		/** Bajada a la arena (baja hacia +X; la del otro extremo se gira 180°). */
		constexpr int32 RampKind = 5;
	}

	inline void BuildBoardwalkModule(FParts& P, int32 Kind, uint32 Seed)
	{
		using namespace BoardwalkKit;
		const FLinearColor WoodA = Hex(0x9C8467);
		const FLinearColor WoodB = Hex(0x8A7358);
		const FLinearColor WoodC = Hex(0xB09A7A);
		const FLinearColor StringerC = Hex(0x6E5A44);
		const FLinearColor PileC = Hex(0x5E4B38);
		const FLinearColor Nail = Hex(0x8A4B2A, 0.4f);
		const bool bRamp = Kind == RampKind;
		const double SlatW = Cm(9.0);
		const double SlatT = 50.0;
		const double Half = ModuleLen * 0.5;
		auto DeckZ = [&](double X) { return bRamp ? FMath::Lerp(DeckTop, 18.0, (X + Half) / ModuleLen) : DeckTop; };
		const double RampPitch = bRamp ? FMath::Atan2(DeckTop - 18.0, ModuleLen) : 0.0;
		for (const double Sy : { -1.0, 1.0 })
		{
			const FVector A(-Half, Sy * (HalfWidth - 170.0), DeckZ(-Half) - SlatT - 60.0);
			const FVector B(Half, Sy * (HalfWidth - 170.0), DeckZ(Half) - SlatT - 60.0);
			AddOBox(P.Body, (A + B) * 0.5, FRotationMatrix::MakeFromXZ(B - A, FVector::UpVector).ToQuat(), FVector(Half, 60.0, 60.0), StringerC);
			// Pilote corto en medio del tramo, por fuera de las tablas, asomando algo por encima.
			const double PileTop = DeckZ(0.0) + (bRamp ? 10.0 : 45.0);
			const FVector PileCenter(0.0, Sy * (HalfWidth + 5.0), (PileTop - 250.0) * 0.5);
			const FVector PileHalf(45.0, 45.0, (PileTop + 250.0) * 0.5);
			AddYawBox(P.Body, PileCenter, RndIn(Seed, Sy > 0.0 ? 3 : 4, -6.0, 6.0), PileHalf, PileC);
			P.ColBoxYaw(PileCenter, 0.0, PileHalf);
		}
		const int32 Special = static_cast<int32>(Rnd(Seed, 1) * SlatsPerModule);
		for (int32 s = 0; s < SlatsPerModule; ++s)
		{
			if (Kind == 1 && s == Special) { continue; }
			const double X = -Half + SlatPitch * (s + 0.5);
			const FLinearColor Col = (s % 3 == 0) ? WoodA : ((s % 3 == 1) ? WoodB : WoodC);
			const double Yaw = Kind == 4 ? RndIn(Seed, 10 + s, -6.0, 6.0) : RndIn(Seed, 10 + s, -1.5, 1.5);
			const double Shift = (Kind == 4 && s == Special) ? 60.0 : 0.0;
			const bool bBroken = Kind == 2 && s == Special;
			const double HalfLen = bBroken ? HalfWidth * 0.55 : HalfWidth;
			const double CenterY = bBroken ? -HalfWidth * 0.45 : 0.0;
			FQuat Rot = YawQ(Yaw) * FQuat(FVector(0.0, 1.0, 0.0), RampPitch);
			FVector C(X + Shift, CenterY, DeckZ(X) - SlatT * 0.5);
			if (Kind == 3 && s == Special)
			{
				// Suelta: levantada por un extremo (rampita de 9°).
				const double Lift = FMath::DegreesToRadians(9.0);
				Rot = YawQ(Yaw) * FQuat(FVector(1.0, 0.0, 0.0), Lift);
				C.Z += HalfWidth * FMath::Sin(Lift);
			}
			AddOBox(P.Body, C, Rot, FVector(SlatW * 0.5, HalfLen, SlatT * 0.5), Col);
			for (const double Sy : { -1.0, 1.0 })
			{
				const double NailY = Sy * (HalfWidth - 170.0) - CenterY;
				if (FMath::Abs(NailY) < HalfLen) { AddOBox(P.Body, C + Rot.RotateVector(FVector(0.0, NailY, SlatT * 0.5 + 3.0)), Rot, FVector(9.0, 9.0, 4.0), Nail); }
			}
			if (bBroken)
			{
				// Astillas en la rotura.
				const FVector Br = C + Rot.RotateVector(FVector(0.0, HalfLen, SlatT * 0.3));
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector Off = Rot.RotateVector(FVector((k - 1) * SlatW * 0.3, 0.0, 0.0));
					const FVector Wd = Rot.RotateVector(FVector(SlatW * 0.12, 0.0, 0.0));
					P.Body.AddTri(Br + Off - Wd, Br + Off + Wd, Br + Off + Rot.RotateVector(FVector(0.0, RndIn(Seed, 40 + k, 40.0, 90.0), 0.0)), FVector::UpVector, Shade(Col, 1.1f));
				}
			}
			// Colisión algo más ancha que la tabla: los huecos de 7 cm entre tablas no se notan al andar.
			P.ColBox(C, Rot, FVector(SlatPitch * 0.5 - 12.0, HalfLen, SlatT * 0.5));
		}
	}

	/** Caminito de palos con cuerda: palos cada Spacing a los dos lados (Y = ±HalfWidth) y tramos de cuerda de palo a palo. */
	namespace PostPathKit
	{
		constexpr double Spacing = 560.0;
		constexpr double HalfWidth = 270.0;
		constexpr double PostH = 420.0;
		constexpr double RopeZ = 330.0;
		/** Palos: recto (0), roto y más bajo (1) y con vueltas de cuerda (2). */
		constexpr int32 NumPostKinds = 3;
		/** Cuerdas (piezas RopeBase + n): de cáñamo (0), cabo azul (1) y cinta de balizar roja y blanca (2). */
		constexpr int32 RopeBase = 100;
		constexpr int32 NumRopeKinds = 3;
		/** Altura (cm, sin escalar) a la que se ata la cuerda en cada palo. */
		inline double AttachZ(int32 PostKind) { return PostKind == 1 ? 230.0 : RopeZ; }
	}

	inline void BuildPathPost(FParts& P, int32 Kind, uint32 Seed)
	{
		using namespace PostPathKit;
		const FLinearColor Wood = Hex(0x8C7355);
		const FLinearColor TopC = Hex(0xB39B78);
		const FLinearColor RopeC = Hex(0xC8A86E);
		const double R = Cm(0.8);
		const double H = Kind == 1 ? 260.0 : PostH;
		AddTaperTube(P.Body, { FVector(0.0, 0.0, -150.0), FVector(3.0, 2.0, H * 0.5), FVector(0.0, 0.0, H) }, R * 1.1, R * 0.9, 6, Wood, true);
		if (Kind == 1)
		{
			AddRevolve(P.Body, FBeachFrame(FVector(0.0, 0.0, H), FQuat::Identity), { FVector2D(R * 0.95, 0.0), FVector2D(R * 0.5, 30.0), FVector2D(0.0, 55.0) }, 6, TopC, 0.4, Seed);
		}
		else if (Kind == 2)
		{
			AddLoop(P.Body, FVector(0.0, 0.0, RopeZ - 20.0), FVector::UpVector, R + 6.0, 7.0, 8, RopeC);
			AddLoop(P.Body, FVector(0.0, 0.0, RopeZ + 4.0), FVector::UpVector, R + 6.0, 7.0, 8, RopeC);
		}
		AddBlob(P.Body, FBeachFrame(FVector(0.0, 0.0, AttachZ(Kind)), FQuat::Identity), R * 1.3, R * 1.3, 14.0, 6, 2, Shade(RopeC, 0.9f));
		P.ColCapsule(FVector::ZeroVector, FVector(0.0, 0.0, H - R), R * 1.15);
	}

	/** Tramo de cuerda de Spacing a lo largo de +X que cuelga en el medio (la instancia lo estira en X hasta el palo siguiente). */
	inline void BuildPathRope(FParts& P, int32 RopeKind)
	{
		using namespace PostPathKit;
		const double Sag = 55.0;
		TArray<FVector> Path;
		for (int32 s = 0; s <= 8; ++s)
		{
			const double T = s / 8.0;
			Path.Add(FVector(Spacing * T, 0.0, -Sag * 4.0 * T * (1.0 - T)));
		}
		if (RopeKind == 2)
		{
			const FLinearColor Red = Hex(0xE0342E, 0.3f);
			const FLinearColor White = Hex(0xF6F2EA, 0.3f);
			AddSweep(P.Body, Path, RectSection(22.0, 2.5), false, [](int32) { return 1.0; }, [&](int32 i, int32) { return (i % 2) ? White : Red; }, true, FVector::UpVector);
		}
		else
		{
			const FLinearColor ColA = Hex(RopeKind == 0 ? 0xC8A86E : 0x2E6FB0);
			const FLinearColor ColB = Hex(RopeKind == 0 ? 0x9C7E4A : 0x7FB2E6);
			AddSweep(P.Body, Path, CircleSection(4), false, [](int32) { return 9.0; }, [&](int32 i, int32 Side) { return ((i + Side) % 2) ? ColA : ColB; }, true);
		}
		P.Info.bCastShadow = false;
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
	// Reparto por elemento
	// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Decorado militar (sacos terreros ... soldaditos): sus recetas están en TN_BeachMilitaryMeshes.h, incluido al final. */
	inline bool IsMilitaryDecor(ETNBeachElement E)
	{
		return E >= ETNBeachElement::Sandbags && E <= ETNBeachElement::ToySoldiers;
	}
	inline int32 NumMilitaryVariants(ETNBeachElement E);
	inline void BuildMilitaryDecor(FParts& P, ETNBeachElement E, int32 Variant, uint32 Seed);

	/** Mallas distintas (variantes) de cada elemento; la de cada ejemplar sale de su Seed. */
	inline int32 NumVariants(ETNBeachElement E)
	{
		if (IsMilitaryDecor(E)) { return NumMilitaryVariants(E); }
		switch (E)
		{
		case ETNBeachElement::Rock:           return 8;
		case ETNBeachElement::RockCluster:    return 3;
		case ETNBeachElement::Boardwalk:
		case ETNBeachElement::WoodenPostPath: return 1;
		default:                              return 4;
		}
	}

	/** Los que se montan con piezas instanciadas a lo largo de Extent. */
	inline bool IsTiled(ETNBeachElement E)
	{
		return E == ETNBeachElement::Boardwalk || E == ETNBeachElement::WoodenPostPath;
	}

	/** Receta de la variante Variant del elemento E (Seed: la de la malla, igual para todos los ejemplares de esa variante). */
	inline void BuildDecor(FParts& P, ETNBeachElement E, int32 Variant, uint32 Seed)
	{
		switch (E)
		{
		case ETNBeachElement::Coconut:           BuildCoconut(P, Variant, Seed); break;
		case ETNBeachElement::StrandedJellyfish: BuildJellyfish(P, Variant, Seed); break;
		case ETNBeachElement::SixPackRings:      BuildSixPackRings(P, Variant, Seed); break;
		case ETNBeachElement::RedBra:            BuildRedBra(P, Variant, Seed); break;
		case ETNBeachElement::Clam:              BuildClam(P, Variant, Seed); break;
		case ETNBeachElement::DecorShell:        BuildDecorShell(P, Variant, Seed); break;
		case ETNBeachElement::Starfish:          BuildStarfish(P, Variant, Seed); break;
		case ETNBeachElement::Rock:              BuildRock(P, Variant, Seed); break;
		case ETNBeachElement::RockCluster:       BuildRockCluster(P, Variant, Seed); break;
		case ETNBeachElement::ShipSailWreck:     BuildShipSailWreck(P, Variant, Seed); break;
		case ETNBeachElement::MossyLog:          BuildMossyLog(P, Variant, Seed); break;
		case ETNBeachElement::OldPlanks:         BuildOldPlanks(P, Variant, Seed); break;
		case ETNBeachElement::FishingNet:        BuildFishingNet(P, Variant, Seed); break;
		case ETNBeachElement::PlasticCup:        BuildPlasticCup(P, Variant, Seed); break;
		case ETNBeachElement::Bottle:            BuildBottle(P, Variant, Seed); break;
		case ETNBeachElement::Lollipop:          BuildLollipop(P, Variant, Seed); break;
		case ETNBeachElement::WatermelonRind:    BuildWatermelonRind(P, Variant, Seed); break;
		case ETNBeachElement::Straw:             BuildStraw(P, Variant, Seed); break;
		case ETNBeachElement::PlantedUmbrella:   BuildPlantedUmbrella(P, Variant, Seed); break;
		case ETNBeachElement::BeachChair:        BuildBeachChair(P, Variant, Seed); break;
		case ETNBeachElement::SandCastleSmall:   BuildSandCastleSmall(P, Variant, Seed); break;
		case ETNBeachElement::SandCastleHuge:    BuildSandCastleHuge(P, Variant, Seed); break;
		case ETNBeachElement::Driftwood:         BuildDriftwood(P, Variant, Seed); break;
		case ETNBeachElement::SodaCan:           BuildSodaCan(P, Variant, Seed); break;
		case ETNBeachElement::BottleCaps:        BuildBottleCaps(P, Variant, Seed); break;
		case ETNBeachElement::FlipFlop:          BuildFlipFlop(P, Variant, Seed); break;
		case ETNBeachElement::JuiceBox:          BuildJuiceBox(P, Variant, Seed); break;
		case ETNBeachElement::Buoy:              BuildBuoy(P, Variant, Seed); break;
		case ETNBeachElement::BeachTowel:        BuildBeachTowel(P, Variant, Seed); break;
		case ETNBeachElement::SunscreenBottle:   BuildSunscreen(P, Variant, Seed); break;
		case ETNBeachElement::PopsicleSticks:    BuildPopsicleSticks(P, Variant, Seed); break;
		case ETNBeachElement::SnackShells:       BuildSnackShells(P, Variant, Seed); break;
		case ETNBeachElement::RopePiece:         BuildRopePiece(P, Variant, Seed); break;
		case ETNBeachElement::Sunglasses:        BuildSunglasses(P, Variant, Seed); break;
		case ETNBeachElement::ToyBucket:         BuildToyBucket(P, Variant, Seed); break;
		case ETNBeachElement::BeachBall:         BuildBeachBall(P, Variant, Seed); break;
		case ETNBeachElement::Frisbee:           BuildFrisbee(P, Variant, Seed); break;
		case ETNBeachElement::Cuttlebone:        BuildCuttlebone(P, Variant, Seed); break;
		case ETNBeachElement::RubberDuck:        BuildRubberDuck(P, Variant, Seed); break;
		case ETNBeachElement::GullFeather:       BuildGullFeather(P, Variant, Seed); break;
		case ETNBeachElement::Sandbags:
		case ETNBeachElement::AmmoCrate:
		case ETNBeachElement::TankTrap:
		case ETNBeachElement::MilitaryHelmet:
		case ETNBeachElement::CamoNet:
		case ETNBeachElement::Jerrycan:
		case ETNBeachElement::ToySoldiers:       BuildMilitaryDecor(P, E, Variant, Seed); break;
		default:                                 BuildRock(P, Variant, Seed); break;
		}
	}

	/** Pieza Piece de un elemento por tramos (módulo de pasarela; palo o cuerda del caminito). */
	inline void BuildTiledPiece(FParts& P, ETNBeachElement E, int32 Piece, uint32 Seed)
	{
		if (E == ETNBeachElement::Boardwalk)
		{
			BuildBoardwalkModule(P, Piece, Seed);
			return;
		}
		if (Piece >= PostPathKit::RopeBase)
		{
			BuildPathRope(P, Piece - PostPathKit::RopeBase);
			return;
		}
		BuildPathPost(P, Piece, Seed);
	}
}

// Recetas del decorado militar (definen NumMilitaryVariants y BuildMilitaryDecor, declaradas arriba).
#include "TN_BeachMilitaryMeshes.h"
