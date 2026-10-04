#include "World/Beach/TN_BeachTrench.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachCreatureRules.h"

namespace TNBeachTrenchDetail
{
	/**
	 * Prisma convexo: la sección Sec (cuatro puntos en orden de contorno) extruida por Ext. Caras con la normal hacia fuera
	 * y, con Hulls, su casco de colisión.
	 */
	void AddExtruded(TNBeachTrapKit::FBuffers& B, TNBeachTrapKit::FHulls* Hulls, const FVector (&Sec)[4], const FVector& Ext,
		const FLinearColor& Top, const FLinearColor& Side)
	{
		const FVector Center = (Sec[0] + Sec[1] + Sec[2] + Sec[3]) * 0.25;
		const FVector Axis = Ext.GetSafeNormal();
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector& P0 = Sec[i];
			const FVector& P1 = Sec[(i + 1) % 4];
			FVector Out = (P0 + P1) * 0.5 - Center;
			Out -= Axis * FVector::DotProduct(Out, Axis);
			const FLinearColor& Color = Out.Z > FMath::Abs(Out.X) + FMath::Abs(Out.Y) ? Top : Side;
			B.AddQuad(P0, P1, P1 + Ext, P0 + Ext, Out, Color);
		}
		B.AddQuad(Sec[0], Sec[1], Sec[2], Sec[3], -Axis, Side);
		B.AddQuad(Sec[0] + Ext, Sec[1] + Ext, Sec[2] + Ext, Sec[3] + Ext, Axis, Side);
		if (Hulls)
		{
			TArray<FVector> Hull;
			for (const FVector& P : Sec)
			{
				Hull.Add(P);
				Hull.Add(P + Ext);
			}
			Hulls->Add(Hull);
		}
	}
}

ATN_BeachTrench::ATN_BeachTrench()
{
	PrimaryActorTick.bCanEverTick = true;

	TrenchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrenchMesh"));
	TrenchMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(TrenchMesh);

	TrenchCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TrenchCollision"));
	TrenchCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(TrenchCollision, false);
}

void ATN_BeachTrench::ApplySpec()
{
	using TNBeachTrenchDetail::AddExtruded;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const double K = FMath::Clamp(Fit / 700.0, 0.6, 1.3);
	// Lo de fuera (caballón y faldón) cabe en la huella: PitHalf.X + BermTop + BermFoot <= Fit.
	BermTop = static_cast<float>(40.0 * K);
	BermFoot = static_cast<float>(FMath::Max(200.0, 230.0 * K));
	PitHalf = FVector2D(FMath::Max(220.0, Fit - BermTop - BermFoot), FMath::Max(150.0, 0.6 * (Fit - BermTop - BermFoot)));
	RimHeight = 130.f;
	RampLength = static_cast<float>(FMath::Min(PitHalf.X, 210.0));

	const FLinearColor Top = TNBeachTrapKit::SandTop();
	const FLinearColor Side = TNBeachTrapKit::SandSide();
	const double H = RimHeight;
	const double PX = PitHalf.X;
	const double PY = PitHalf.Y;
	const double T = BermTop;
	const double F = BermFoot;
	const double L = PX + T + F;
	TNBeachTrapKit::FBuffers B;
	TNBeachTrapKit::FHulls Hulls;
	// Caballones de los lados (±Y), de punta a punta: pared vertical por dentro y faldón por fuera.
	for (const double S : { 1.0, -1.0 })
	{
		const FVector Sec[4] = { FVector(-L, S * PY, 0.0), FVector(-L, S * PY, H), FVector(-L, S * (PY + T), H), FVector(-L, S * (PY + T + F), 0.0) };
		AddExtruded(B, &Hulls, Sec, FVector(2.0 * L, 0.0, 0.0), Top, Side);
	}
	// Fondo (-X): pared vertical.
	{
		const FVector Sec[4] = { FVector(-PX, -PY, 0.0), FVector(-PX, -PY, H), FVector(-PX - T, -PY, H), FVector(-PX - T - F, -PY, 0.0) };
		AddExtruded(B, &Hulls, Sec, FVector(0.0, 2.0 * PY, 0.0), Top, Side);
	}
	// Salida (+X): rampa por dentro.
	{
		const FVector Sec[4] = { FVector(PX - RampLength, -PY, 0.0), FVector(PX, -PY, H), FVector(PX + T, -PY, H), FVector(PX + T + F, -PY, 0.0) };
		AddExtruded(B, &Hulls, Sec, FVector(0.0, 2.0 * PY, 0.0), Top, TNBeachTrapKit::SandMark());
	}
	// Fondo del hoyo, arena mojada y removida.
	TNPlaygroundKit::AddAxisBox(B, FVector(-RampLength * 0.5, 0.0, 1.5), FVector(PX - RampLength * 0.5, PY, 1.5), TNBeachTrapKit::SandWet());
	TNBeachTrapKit::SetMesh(TrenchMesh, this, B);
	TrenchCollision->SetCollisionConvexMeshes(Hulls);
}

void ATN_BeachTrench::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	TSet<TWeakObjectPtr<ACharacter>> Now;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!TNBeachTrapKit::SimulatesMovement(Turtle))
		{
			continue;
		}
		const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Turtle->GetActorLocation());
		const FVector Feet = Local - FVector(0.0, 0.0, Turtle->GetSimpleCollisionHalfHeight());
		// La rampa no cuenta como hoyo: por ahí se sale andando.
		if (Feet.X > PitHalf.X - RampLength || !TNBeachCreatureRules::Trench::IsInPit(Feet, PitHalf, RimHeight))
		{
			continue;
		}
		UTN_StaminaComponent* Stamina = Turtle->FindComponentByClass<UTN_StaminaComponent>();
		const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		if (!Stamina || !Move)
		{
			continue;
		}
		Stamina->SetJumpLimit(LimitSource(), TNBeachCreatureRules::Trench::MaxJumpSpeedInPit(RimHeight, Move->GetGravityZ()));
		Now.Add(Turtle);
	}
	for (const TWeakObjectPtr<ACharacter>& Was : Limited)
	{
		if (!Now.Contains(Was))
		{
			ClearJumpLimit(Was.Get());
		}
	}
	Limited = MoveTemp(Now);
}

void ATN_BeachTrench::ClearJumpLimit(ACharacter* Turtle) const
{
	if (UTN_StaminaComponent* Stamina = Turtle ? Turtle->FindComponentByClass<UTN_StaminaComponent>() : nullptr)
	{
		Stamina->ClearJumpLimit(LimitSource());
	}
}

void ATN_BeachTrench::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<ACharacter>& Was : Limited)
	{
		ClearJumpLimit(Was.Get());
	}
	Limited.Reset();
	Super::EndPlay(EndPlayReason);
}
