#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Multiplayer/TN_LocalViews.h"
#include "UI/Race/TN_RaceCueSynthComponent.h"
#include "TN_BeachTrapKit.h"

/**
 * Piezas comunes de lo «potenciado» y de las fortalezas de arena (ATN_BeachFortress y los lanzadores de su cima,
 * ATN_BeachTrampoline con TNBeach::FlagBoosted): estrella dorada de cinco puntas, bandera y
 * estandarte de Tortunavy (azul marino con la estrella dorada, como la escarapela de la tropa), guirnaldas de banderines
 * y la fanfarria que suena al salir disparada desde un lanzador potenciado. Geometría sobre el kit del parque del lobby
 * (TNPlaygroundKit: color lineal con el brillo en el alfa para M_CosmeticVertexColor).
 */
namespace TNBeachBoostKit
{
	using FBuffers = TNBeachTrapKit::FBuffers;

	inline FLinearColor Navy() { return TNPlaygroundKit::Rgb(0x12305A, 0.1f); }
	inline FLinearColor NavyDark() { return TNPlaygroundKit::Rgb(0x0C2142, 0.1f); }
	inline FLinearColor Gold() { return TNPlaygroundKit::Rgb(0xFFCB3D, 0.65f); }
	inline FLinearColor GoldDeep() { return TNPlaygroundKit::Rgb(0xD9A441, 0.7f); }
	inline FLinearColor PoleWood() { return TNPlaygroundKit::Rgb(0x8C6A45, 0.05f); }

	/** Ejes de una cara de normal Normal: OutU hacia Up (proyectado en la cara) y OutS a su lado. */
	inline void FaceAxes(const FVector& Normal, const FVector& Up, FVector& OutN, FVector& OutU, FVector& OutS)
	{
		OutN = Normal.GetSafeNormal();
		OutU = (Up - OutN * FVector::DotProduct(Up, OutN)).GetSafeNormal();
		if (OutU.IsNearlyZero())
		{
			OutU = FVector::CrossProduct(OutN, FMath::Abs(OutN.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		}
		OutS = FVector::CrossProduct(OutN, OutU);
	}

	/** Estrella de cinco puntas plana de radio Radius (dentro, 0,42) en Center, de cara a Normal y con una punta hacia Up. */
	inline void AddStar(FBuffers& B, const FVector& Center, const FVector& Normal, const FVector& Up, double Radius, const FLinearColor& Color)
	{
		FVector Nn;
		FVector Uu;
		FVector Ss;
		FaceAxes(Normal, Up, Nn, Uu, Ss);
		for (int32 k = 0; k < 10; ++k)
		{
			const double A0 = TNPlaygroundKit::KitTwoPi * k / 10.0;
			const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / 10.0;
			const double R0 = (k % 2) ? Radius * 0.42 : Radius;
			const double R1 = (k % 2) ? Radius : Radius * 0.42;
			B.AddTri(Center, Center + (Uu * FMath::Cos(A0) + Ss * FMath::Sin(A0)) * R0, Center + (Uu * FMath::Cos(A1) + Ss * FMath::Sin(A1)) * R1, Nn,
				(k % 2) ? Color : TNPlaygroundKit::Shade(Color, 0.88));
		}
	}

	/**
	 * Bandera de Tortunavy en su mástil: palo de madera desde Foot con Height de alto, pomo dorado y una tela azul marino de
	 * Width x FlagHeight que ondea hacia Dir (en planta), con una franja dorada y la estrella por las dos caras.
	 */
	inline void AddNavyFlag(FBuffers& B, const FVector& Foot, double Height, const FVector& Dir, double Width, double FlagHeight, uint32 Seed)
	{
		const FVector Top = Foot + FVector(0.0, 0.0, Height);
		const double PoleR = FMath::Clamp(Height * 0.016, 5.0, 12.0);
		TNPlaygroundKit::AddRod(B, Foot - FVector(0.0, 0.0, 20.0), Top, PoleR, 8, PoleWood(), FVector::ForwardVector);
		TNPlaygroundKit::AddBall(B, Top + FVector(0.0, 0.0, PoleR * 1.2), PoleR * 1.8, 8, Gold());
		FVector Along = Dir.GetSafeNormal2D();
		if (Along.IsNearlyZero())
		{
			Along = FVector::ForwardVector;
		}
		const FVector Side(-Along.Y, Along.X, 0.0);
		const FVector ClothTop = Top - FVector(0.0, 0.0, PoleR * 0.6);
		const double Phase = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(3, 7, Seed);
		// Punto de la tela: U a lo largo (0 en el mástil), V de abajo (0) a arriba (1); ondea hacia el lado y cae un poco.
		const auto ClothAt = [&ClothTop, &Along, &Side, Width, FlagHeight, Phase](double U, double V)
		{
			const double Ripple = 0.08 * Width * FMath::Sin(TNPlaygroundKit::KitPi * 1.6 * U + Phase) * U;
			return ClothTop + Along * (Width * U) + Side * Ripple - FVector(0.0, 0.0, FlagHeight * (1.0 - V) + 0.1 * FlagHeight * U);
		};
		// Tela en cuatro paños (dos caras) y la franja dorada de abajo.
		constexpr int32 Panels = 4;
		for (int32 i = 0; i < Panels; ++i)
		{
			const double U0 = static_cast<double>(i) / Panels;
			const double U1 = static_cast<double>(i + 1) / Panels;
			const FLinearColor Cloth = (i % 2) ? Navy() : TNPlaygroundKit::Shade(Navy(), 0.9);
			for (const double Face : { 1.0, -1.0 })
			{
				B.AddQuad(ClothAt(U0, 0.14), ClothAt(U1, 0.14), ClothAt(U1, 1.0), ClothAt(U0, 1.0), Side * Face, Cloth);
				B.AddQuad(ClothAt(U0, 0.0), ClothAt(U1, 0.0), ClothAt(U1, 0.14), ClothAt(U0, 0.14), Side * Face, Gold());
			}
		}
		const FVector StarAt = ClothAt(0.4, 0.55);
		for (const double Face : { 1.0, -1.0 })
		{
			AddStar(B, StarAt + Side * (Face * 2.5), Side * Face, FVector::UpVector, FlagHeight * 0.3, Gold());
		}
	}

	/**
	 * Estandarte de Tortunavy colgado de una pared (Normal hacia fuera) con el borde de arriba centrado en TopCenter: barra
	 * de madera con pomos dorados, paño azul marino terminado en cola de golondrina, franjas y estrella doradas.
	 */
	inline void AddBanner(FBuffers& B, const FVector& TopCenter, const FVector& Normal, double Width, double Height)
	{
		FVector Nn;
		FVector Uu;
		FVector Ss;
		FaceAxes(Normal, FVector::UpVector, Nn, Uu, Ss);
		const FVector Base = TopCenter + Nn * 6.0;
		const double Hw = 0.5 * Width;
		TNPlaygroundKit::AddRod(B, Base - Ss * (Hw + 18.0) + Nn * 4.0, Base + Ss * (Hw + 18.0) + Nn * 4.0, 5.0, 6, PoleWood(), Nn);
		for (const double Sgn : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddBall(B, Base + Ss * (Sgn * (Hw + 20.0)) + Nn * 4.0, 8.0, 6, Gold());
		}
		const double Tail = 0.18 * Height;
		const FVector TL = Base - Ss * Hw;
		const FVector TR = Base + Ss * Hw;
		const FVector BL = TL - Uu * Height;
		const FVector BR = TR - Uu * Height;
		const FVector Notch = Base - Uu * (Height - Tail);
		B.AddQuad(TL, TR, TR - Uu * (Height - Tail), TL - Uu * (Height - Tail), Nn, Navy());
		B.AddTri(TL - Uu * (Height - Tail), Notch, BL, Nn, Navy());
		B.AddTri(Notch, TR - Uu * (Height - Tail), BR, Nn, Navy());
		// Franjas doradas (arriba y abajo) y la estrella.
		B.AddQuad(TL - Uu * 10.0 + Nn * 1.0, TR - Uu * 10.0 + Nn * 1.0, TR - Uu * 22.0 + Nn * 1.0, TL - Uu * 22.0 + Nn * 1.0, Nn, Gold());
		const double Low = Height - Tail - 26.0;
		B.AddQuad(TL - Uu * Low + Nn * 1.0, TR - Uu * Low + Nn * 1.0, TR - Uu * (Low + 12.0) + Nn * 1.0, TL - Uu * (Low + 12.0) + Nn * 1.0, Nn, Gold());
		AddStar(B, Base - Uu * (0.44 * Height) + Nn * 2.0, Nn, Uu, 0.3 * Width, Gold());
	}

	/** Guirnalda de banderines de colores de juguete colgada de From a To con una comba de Sag (triángulos de dos caras). */
	inline void AddBunting(FBuffers& B, const FVector& From, const FVector& To, double Sag, double PennantSize, uint32 Seed)
	{
		const FVector Span = To - From;
		const double Len = Span.Size();
		if (Len < 1.0)
		{
			return;
		}
		const FVector Along = Span / Len;
		FVector Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
		if (Side.IsNearlyZero())
		{
			Side = FVector::RightVector;
		}
		const auto StringAt = [&From, &Span, Sag](double T) { return From + Span * T - FVector(0.0, 0.0, 4.0 * Sag * T * (1.0 - T)); };
		constexpr int32 RopeSegs = 8;
		for (int32 s = 0; s < RopeSegs; ++s)
		{
			TNPlaygroundKit::AddRod(B, StringAt(static_cast<double>(s) / RopeSegs), StringAt(static_cast<double>(s + 1) / RopeSegs), 1.8, 4,
				TNPlaygroundKit::Rgb(0xF2E6C8), FVector::UpVector);
		}
		const int32 Count = FMath::Max(2, FMath::FloorToInt32(Len / (PennantSize * 1.25)));
		for (int32 i = 0; i < Count; ++i)
		{
			const double T0 = (i + 0.15) / Count;
			const double T1 = (i + 0.85) / Count;
			const FVector A = StringAt(T0);
			const FVector C = StringAt(T1);
			const FVector Tip = StringAt(0.5 * (T0 + T1)) - FVector(0.0, 0.0, PennantSize);
			const FLinearColor Col = TNPlaygroundKit::ToyColor(static_cast<int32>((Seed + static_cast<uint32>(i) * 3u) % 7u), 0.2f);
			B.AddTri(A, C, Tip, Side, Col);
			B.AddTri(A, C, Tip, -Side, TNPlaygroundKit::Shade(Col, 0.85));
		}
	}

	/**
	 * Fanfarria de un lanzador potenciado en esta máquina: la de los títulos de la carrera (UTN_RaceCueSynthComponent,
	 * sintetizada, en 2D en el mando local) con el volumen según lo lejos que quede la cámara local de At; nada más allá de
	 * Radius (cm). No suena en un servidor dedicado.
	 */
	inline void PlayFanfareNear(const UObject* WorldContext, const FVector& At, float Radius, float Pitch)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer || Radius <= 1.f)
		{
			return;
		}
		// La cámara local más cercana (con la pantalla partida, la de quien lo tiene más cerca; suena una vez).
		APlayerController* Viewer = nullptr;
		FVector ViewAt;
		if (!TNLocalViews::ClosestCamera(World, At, ViewAt, nullptr, &Viewer) || !Viewer)
		{
			return;
		}
		const float Dist = static_cast<float>(FVector::Dist(ViewAt, At));
		if (Dist >= Radius)
		{
			return;
		}
		const float Near = 1.f - Dist / Radius;
		if (UTN_RaceCueSynthComponent* Cue = UTN_RaceCueSynthComponent::Attach2D(Viewer))
		{
			Cue->Play(ETNRaceCue::Fanfare, Pitch, 0.3f + 0.7f * Near * Near);
		}
	}
}
