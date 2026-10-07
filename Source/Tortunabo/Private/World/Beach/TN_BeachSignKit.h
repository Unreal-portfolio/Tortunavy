#pragma once

#include "CoreMinimal.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Multiplayer/TN_LocalViews.h"
#include "TN_BeachBoostKit.h"
#include "TN_BeachTrapKit.h"

/**
 * Cartel de madera clavado en la arena junto a los trampolines (ATN_BeachTrampoline, también los de la cima de las
 * fortalezas) para que se sepan de lejos: tabla de tres tablones en dos postes con el icono pintado (flecha que baja y
 * rebota hacia arriba) y el rótulo en una franja oscura (TextRender: «¡BOING!»). Los potenciados, tabla dorada con el icono azul marino y una
 * estrella dorada encima. Sin colisión: no estorba.
 *
 * Espacio del cartel (cm): origen al pie de los postes, en la arena; la cara pintada mira a -X (a quien llega). Cada
 * lanzador lo pone por el lado por el que se llega (-X de su marco), a un lado (SideOf) y girado hacia el centro.
 */
namespace TNBeachSignKit
{
	using FBuffers = TNBeachTrapKit::FBuffers;

	constexpr double PostH = 150.0;
	constexpr double BoardW = 320.0;
	constexpr double BoardH = 180.0;
	constexpr double BoardT = 12.0;
	/** Tamaño de las letras del rótulo (cm) y ancho que no pasa (se encoge si hace falta). */
	constexpr float TextSize = 58.f;
	constexpr double TextMaxWidth = 290.0;
	/** A menos de esto (cm) una tortuga local hace rebotar el cartel; a menos de GlowRadius, el rótulo brilla. */
	constexpr double BounceRadius = 900.0;
	constexpr double GlowRadius = 1500.0;

	/** Centro del rótulo (espacio del cartel), delante de la franja oscura. */
	inline FVector TextSpot() { return FVector(-0.5 * BoardT - 5.0, 0.0, PostH + 44.0); }

	/** Lado (+1 = +Y del marco del lanzador, -1 = -Y) del cartel, por la semilla del Spec (la fortaleza pone el cofre al otro). */
	inline double SideOf(int32 SpecSeed)
	{
		return (TNBeachTrapKit::SeedOf(SpecSeed, 61u) & 1u) ? 1.0 : -1.0;
	}

	/** Trazo grueso por una polilínea (Y, Z) pintado en la cara -X de la tabla (X = Face). */
	inline void AddStroke(FBuffers& B, const TArray<FVector2D>& Path, double HalfWidth, double Face, const FLinearColor& Color)
	{
		for (int32 i = 0; i + 1 < Path.Num(); ++i)
		{
			const FVector2D D = (Path[i + 1] - Path[i]).GetSafeNormal();
			const FVector2D N(-D.Y, D.X);
			// Un poco más largo por cada punta: sin rendijas en los codos.
			const FVector2D A = Path[i] - D * (HalfWidth * 0.5);
			const FVector2D C = Path[i + 1] + D * (HalfWidth * 0.5);
			B.AddQuad(FVector(Face, A.X + N.X * HalfWidth, A.Y + N.Y * HalfWidth), FVector(Face, C.X + N.X * HalfWidth, C.Y + N.Y * HalfWidth),
				FVector(Face, C.X - N.X * HalfWidth, C.Y - N.Y * HalfWidth), FVector(Face, A.X - N.X * HalfWidth, A.Y - N.Y * HalfWidth), -FVector::ForwardVector, Color);
		}
	}

	/** Punta de flecha en Tip (Y, Z) apuntando hacia Dir, pintada en la cara -X. */
	inline void AddArrowHead(FBuffers& B, const FVector2D& Tip, const FVector2D& Dir, double Size, double Face, const FLinearColor& Color)
	{
		const FVector2D D = Dir.GetSafeNormal();
		const FVector2D N(-D.Y, D.X);
		const FVector2D Back = Tip - D * Size;
		const FVector2D L = Back + N * (0.62 * Size);
		const FVector2D R = Back - N * (0.62 * Size);
		B.AddTri(FVector(Face, Tip.X, Tip.Y), FVector(Face, L.X, L.Y), FVector(Face, R.X, R.Y), -FVector::ForwardVector, Color);
	}

	/**
	 * El cartel entero (espacio del cartel): dos postes clavados en un montoncito de arena, tabla de tres tablones con
	 * marco, franja oscura para el rótulo e icono pintado encima. Potenciado: tabla dorada, icono azul marino, franja azul
	 * marino y una estrella dorada de pie sobre la tabla.
	 */
	inline void BuildSign(FBuffers& B, bool bBoosted, uint32 Seed)
	{
		const double Hw = 0.5 * BoardW;
		const double Top = PostH + BoardH;
		const double Face = -0.5 * BoardT - 1.0;
		const FLinearColor PostWood = TNBeachTrapKit::WoodTone(static_cast<int32>(Seed % 3u) + 1);
		// Postes con la punta clavada y su montoncito de arena.
		for (const double Sy : { -1.0, 1.0 })
		{
			const double Y = Sy * (Hw - 40.0);
			TNPlaygroundKit::AddAxisBox(B, FVector(BoardT * 0.5 + 7.0, Y, 0.5 * (Top + 20.0) - 20.0), FVector(7.0, 9.0, 0.5 * (Top + 20.0)), PostWood);
			TNPlaygroundKit::AddFrustum(B, FVector(BoardT * 0.5 + 7.0, Y, -5.0), FVector(BoardT * 0.5 + 7.0, Y, 16.0), 34.0, 16.0, 8, TNBeachTrapKit::SandMark(),
				TNBeachTrapKit::SandTop(), false, true);
		}
		// Tres tablones (con juntas) y el marco.
		const double PlankH = BoardH / 3.0;
		for (int32 i = 0; i < 3; ++i)
		{
			const FLinearColor Plank = bBoosted ? TNPlaygroundKit::Shade(TNBeachBoostKit::Gold(), i == 1 ? 1.0 : 0.94)
				: TNPlaygroundKit::Shade(TNBeachTrapKit::WoodTone(i + static_cast<int32>(Seed % 2u)), 1.12);
			TNPlaygroundKit::AddAxisBox(B, FVector(0.0, 0.0, PostH + (i + 0.5) * PlankH), FVector(0.5 * BoardT, Hw - 2.0, 0.5 * PlankH - 1.5), Plank);
		}
		const FLinearColor Frame = bBoosted ? TNBeachBoostKit::Navy() : TNPlaygroundKit::Rgb(0x7A5232, 0.05f);
		for (const double Z : { PostH + 4.0, Top - 4.0 })
		{
			TNPlaygroundKit::AddAxisBox(B, FVector(-1.0, 0.0, Z), FVector(0.5 * BoardT + 2.0, Hw + 4.0, 6.0), Frame);
		}
		for (const double Sy : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddAxisBox(B, FVector(-1.0, Sy * (Hw + 1.0), PostH + 0.5 * BoardH), FVector(0.5 * BoardT + 2.0, 6.0, 0.5 * BoardH + 4.0), Frame);
		}
		// Franja oscura del rótulo.
		const FLinearColor Band = bBoosted ? TNBeachBoostKit::NavyDark() : TNPlaygroundKit::Rgb(0x3E2A1C, 0.05f);
		TNPlaygroundKit::AddAxisBox(B, FVector(Face - 1.0, 0.0, PostH + 44.0), FVector(1.5, Hw - 16.0, 32.0), Band);

		// Icono pintado en la parte de arriba (Y, Z del cartel), entre la franja (hasta PostH + 76) y el marco de arriba
		// (desde Top - 10), por delante de los tablones y por detrás de la franja y el marco.
		const double Cz = PostH + 124.0;
		const double IFace = Face - 1.5;
		const double W = 13.0;
		// Cúpula de trampolín abajo y una flecha que baja por la izquierda, toca la cúpula y sube por la derecha.
		const FLinearColor Dome = bBoosted ? TNBeachBoostKit::Navy() : TNPlaygroundKit::Rgb(0x2EC4B6, 0.1f);
		const FLinearColor Arrow = bBoosted ? TNBeachBoostKit::Navy() : TNPlaygroundKit::Rgb(0xE63946, 0.1f);
		TArray<FVector2D> DomePath;
		for (int32 k = 0; k <= 8; ++k)
		{
			const double A = TNPlaygroundKit::KitPi * k / 8.0;
			DomePath.Add(FVector2D(-40.0 * FMath::Cos(A), Cz - 34.0 + 12.0 * FMath::Sin(A)));
		}
		AddStroke(B, DomePath, 6.0, IFace, Dome);
		TArray<FVector2D> Path;
		for (int32 k = 0; k <= 12; ++k)
		{
			const double U = -1.0 + 2.0 * k / 12.0;
			// Parábola con el vértice sobre la cúpula; la rama de la derecha, más alta (sale con más fuerza).
			const double Y = 64.0 * U;
			const double Z = Cz - 18.0 + (U < 0.0 ? 34.0 : 40.0) * U * U;
			Path.Add(FVector2D(Y, Z));
		}
		AddStroke(B, Path, 0.5 * W, IFace, Arrow);
		AddArrowHead(B, Path.Last() + FVector2D(4.0, 10.0), Path.Last() - Path[Path.Num() - 2], 30.0, IFace, Arrow);

		if (bBoosted)
		{
			// Estrella dorada de pie sobre la tabla (por las dos caras) y dos estrellitas azul marino en las esquinas.
			const FVector StarAt(0.0, 0.0, Top + 52.0);
			TNBeachBoostKit::AddStar(B, StarAt + FVector(-2.0, 0.0, 0.0), -FVector::ForwardVector, FVector::UpVector, 48.0, TNBeachBoostKit::Gold());
			TNBeachBoostKit::AddStar(B, StarAt + FVector(2.0, 0.0, 0.0), FVector::ForwardVector, FVector::UpVector, 48.0, TNBeachBoostKit::Gold());
			TNPlaygroundKit::AddAxisBox(B, FVector(0.0, 0.0, Top + 8.0), FVector(4.0, 4.0, 10.0), TNBeachBoostKit::GoldDeep());
			for (const double Sy : { -1.0, 1.0 })
			{
				TNBeachBoostKit::AddStar(B, FVector(IFace, Sy * (Hw - 34.0), Top - 30.0), -FVector::ForwardVector, FVector::UpVector, 17.0, TNBeachBoostKit::Navy());
			}
		}
	}

	/** Deja listo el rótulo (centrado, sin colisión ni sombra, de cara a -X del cartel). */
	inline void ConfigureText(UTextRenderComponent* Text)
	{
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Text->SetCastShadow(false);
		Text->SetGenerateOverlapEvents(false);
		// El texto mira a su +X: girado media vuelta, a quien llega.
		Text->SetRelativeLocationAndRotation(TextSpot(), FRotator(0.0, 180.0, 0.0));
	}

	/** Pone el rótulo con su color y lo encoge si no cabe en la franja. */
	inline void SetText(UTextRenderComponent* Text, const FText& Label, const FColor& Color)
	{
		if (!Text)
		{
			return;
		}
		Text->SetText(Label);
		Text->SetTextRenderColor(Color);
		Text->SetWorldSize(TextSize);
		const double Width = Text->GetTextLocalSize().Y;
		if (Width > TextMaxWidth)
		{
			Text->SetWorldSize(static_cast<float>(TextSize * TextMaxWidth / Width));
		}
	}

	/** Distancia en planta (cm) de la tortuga local más cercana a At (con la pantalla partida, cualquiera; enorme si no hay). */
	inline double LocalPawnDistance(const UWorld* World, const FVector& At)
	{
		const APawn* Pawn = TNLocalViews::ClosestLocalPawn(World, At);
		return Pawn ? FVector::Dist2D(Pawn->GetActorLocation(), At) : 1e9;
	}

	/**
	 * Rebote y brillo del cartel cuando se acerca la tortuga local (solo en máquinas con pantalla; el estado lo guarda cada
	 * lanzador): al entrar a menos de BounceRadius da un botecito (se estira y se balancea) y, a menos de GlowRadius, el
	 * rótulo se aclara poco a poco. Devuelve el estirón (0 = quieto) y el balanceo (grados) de ahora.
	 */
	inline void TickSignAnim(float DeltaSeconds, const UWorld* World, const FVector& SignAt, float& InOutAge, bool& bInOutNear, float& InOutGlow,
		float& OutStretch, float& OutSway)
	{
		const double Dist = LocalPawnDistance(World, SignAt);
		const bool bNowNear = Dist < BounceRadius;
		if (bNowNear && !bInOutNear)
		{
			InOutAge = 0.f;
		}
		bInOutNear = bNowNear;
		InOutAge = FMath::Min(InOutAge + DeltaSeconds, 10.f);
		InOutGlow = FMath::FInterpTo(InOutGlow, Dist < GlowRadius ? 1.f : 0.f, DeltaSeconds, 3.f);
		const float Env = FMath::Exp(-InOutAge / 0.28f);
		OutStretch = 0.08f * Env * FMath::Sin(2.f * UE_PI * 3.2f * InOutAge);
		OutSway = 5.f * Env * FMath::Sin(2.f * UE_PI * 2.1f * InOutAge + 0.6f);
	}

	/** Aclara el color del rótulo con el brillo (solo lo toca cuando cambia de verdad; InOutApplied < 0 lo fuerza). */
	inline void ApplySignGlow(UTextRenderComponent* Text, const FColor& Base, float Glow, float& InOutApplied)
	{
		if (!Text || (InOutApplied >= 0.f && FMath::Abs(Glow - InOutApplied) < 0.02f))
		{
			return;
		}
		InOutApplied = Glow;
		const float Pulse = Glow * 0.55f;
		Text->SetTextRenderColor(FColor(
			static_cast<uint8>(FMath::Lerp(static_cast<float>(Base.R), 255.f, Pulse)),
			static_cast<uint8>(FMath::Lerp(static_cast<float>(Base.G), 255.f, Pulse)),
			static_cast<uint8>(FMath::Lerp(static_cast<float>(Base.B), 235.f, Pulse))));
	}

	/** Color del rótulo: crema en los normales y dorado en los potenciados. */
	inline FColor TextColor(bool bBoosted)
	{
		return bBoosted ? FColor(255, 214, 60) : FColor(255, 234, 170);
	}
}
