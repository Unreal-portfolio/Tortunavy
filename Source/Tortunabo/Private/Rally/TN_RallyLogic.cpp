#include "Rally/TN_RallyLogic.h"

#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogTNRally);

namespace TNRally
{
	namespace
	{
		/** Distancia (cm) por debajo de la cual dos puntos del manifest son el mismo sitio. */
		constexpr double RallySamePointCm = 100.0;
		/** Distancia en planta (cm) por debajo de la cual el primer checkpoint ya es la salida. */
		constexpr double RallyGateMergeCm = 500.0;

		bool RallyReadVector(const TArray<TSharedPtr<FJsonValue>>& Values, FVector& Out)
		{
			if (Values.Num() < 3)
			{
				return false;
			}
			Out = FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
			return true;
		}

		double RallyYawOf(const FVector& Direction)
		{
			return FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		}
	}

	FString VariantManifestPath(FName Variant)
	{
		return FPaths::ProjectDir() / TEXT("Scripts/terrain_volumes/Variants") / Variant.ToString() / TEXT("manifest.json");
	}

	bool ParseTrackManifest(const FString& JsonText, FTrackSource& Out, FString& OutError)
	{
		Out = FTrackSource();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("el manifest no es JSON válido");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Checkpoints = nullptr;
		if (Root->TryGetArrayField(TEXT("checkpoints_uu"), Checkpoints) && Checkpoints)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Checkpoints)
			{
				const TArray<TSharedPtr<FJsonValue>>* Entry = nullptr;
				FGateDef Gate;
				if (!Value.IsValid() || !Value->TryGetArray(Entry) || !Entry || !RallyReadVector(*Entry, Gate.Location))
				{
					OutError = TEXT("checkpoints_uu con una entrada mal formada");
					return false;
				}
				Gate.bHasYaw = Entry->Num() >= 4;
				Gate.YawDeg = Gate.bHasYaw ? (*Entry)[3]->AsNumber() : 0.0;
				Out.Checkpoints.Add(Gate);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Start = nullptr;
		if (Root->TryGetArrayField(TEXT("start_uu"), Start) && Start)
		{
			Out.bHasStart = RallyReadVector(*Start, Out.Start);
		}
		const TArray<TSharedPtr<FJsonValue>>* End = nullptr;
		if (Root->TryGetArrayField(TEXT("end_uu"), End) && End)
		{
			Out.bHasEnd = RallyReadVector(*End, Out.End);
		}
		Out.bHasWater = Root->TryGetNumberField(TEXT("water_uu"), Out.WaterZ);

		const TArray<TSharedPtr<FJsonValue>>* Road = nullptr;
		if (Root->TryGetArrayField(TEXT("road_uu"), Road) && Road)
		{
			Out.Road.Reserve(Road->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Road)
			{
				const TArray<TSharedPtr<FJsonValue>>* Entry = nullptr;
				FVector Point;
				if (!Value.IsValid() || !Value->TryGetArray(Entry) || !Entry || !RallyReadVector(*Entry, Point))
				{
					OutError = TEXT("road_uu con un punto mal formado");
					return false;
				}
				Out.Road.Add(Point);
			}
		}
		Out.bHasClosed = Root->TryGetBoolField(TEXT("closed"), Out.bClosed);
		int32 Laps = 0;
		if (Root->TryGetNumberField(TEXT("laps"), Laps))
		{
			Out.Laps = FMath::Max(0, Laps);
		}
		Out.bHasStartYaw = Root->TryGetNumberField(TEXT("start_yaw"), Out.StartYawDeg);
		double RoadWidthM = 0.0;
		if (Root->TryGetNumberField(TEXT("road_width_m"), RoadWidthM))
		{
			Out.RoadWidthCm = FMath::Max(0.0, RoadWidthM * 100.0);
		}

		if (!TNRallyCircuit::ReadCircuitFields(*Root, Out.Road.Num(), Out.RoadBankDeg, Out.Elements, OutError))
		{
			return false;
		}

		if (Out.Checkpoints.Num() == 0 && !Out.bHasStart)
		{
			OutError = TEXT("el manifest no trae checkpoints_uu ni start_uu");
			return false;
		}
		return true;
	}

	bool IsCircuit(const FTrackSource& Source)
	{
		if (Source.bHasClosed)
		{
			return Source.bClosed;
		}
		return Source.bHasStart && Source.bHasEnd && FVector::Dist(Source.Start, Source.End) < RallySamePointCm;
	}

	TArray<FGateDef> BuildGateList(const FTrackSource& Source, bool& bOutCircuit)
	{
		bOutCircuit = IsCircuit(Source);
		TArray<FGateDef> Gates = Source.Checkpoints;
		if (Source.bHasStart && (Gates.Num() == 0 || FVector::Dist2D(Gates[0].Location, Source.Start) > RallyGateMergeCm))
		{
			FGateDef StartGate;
			StartGate.Location = Source.Start;
			StartGate.bHasYaw = Source.bHasStartYaw;
			StartGate.YawDeg = Source.StartYawDeg;
			Gates.Insert(StartGate, 0);
		}
		if (!bOutCircuit && Source.bHasEnd && (Gates.Num() == 0 || FVector::Dist2D(Gates.Last().Location, Source.End) > RallyGateMergeCm))
		{
			FGateDef EndGate;
			EndGate.Location = Source.End;
			EndGate.bHasYaw = false;
			Gates.Add(EndGate);
		}
		const int32 Num = Gates.Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (Gates[Index].bHasYaw || Num < 2)
			{
				continue;
			}
			FVector Direction;
			if (Index + 1 < Num)
			{
				Direction = Gates[Index + 1].Location - Gates[Index].Location;
			}
			else if (bOutCircuit)
			{
				Direction = Gates[0].Location - Gates[Index].Location;
			}
			else
			{
				Direction = Gates[Index].Location - Gates[Index - 1].Location;
			}
			Gates[Index].YawDeg = RallyYawOf(Direction);
			Gates[Index].bHasYaw = true;
		}
		return Gates;
	}

	int32 FLapRules::GatesToFinish() const
	{
		if (NumGates <= 0)
		{
			return 0;
		}
		return bCircuit ? 1 + NumGates * FMath::Max(1, Laps) : NumGates;
	}

	int32 FLapRules::NextGateIndex(int32 GatesPassed) const
	{
		if (NumGates <= 0)
		{
			return 0;
		}
		return bCircuit ? GatesPassed % NumGates : FMath::Min(GatesPassed, NumGates - 1);
	}

	int32 FLapRules::LapForGates(int32 GatesPassed) const
	{
		if (GatesPassed <= 0 || NumGates <= 0)
		{
			return 0;
		}
		if (!bCircuit)
		{
			return 1;
		}
		return FMath::Min(FMath::Max(1, Laps), (GatesPassed - 1) / NumGates + 1);
	}

	bool FLapRules::IsFinished(int32 GatesPassed) const
	{
		return NumGates > 0 && GatesPassed >= GatesToFinish();
	}

	int32 FLapRules::LastGateIndex(int32 GatesPassed) const
	{
		if (GatesPassed <= 0 || NumGates <= 0)
		{
			return -1;
		}
		return bCircuit ? (GatesPassed - 1) % NumGates : FMath::Min(GatesPassed - 1, NumGates - 1);
	}

	EGateCheck CheckGate(int32 GateIndex, int32 ExpectedGate, bool bForward, double TraveledCm, double SplineCmBetweenGates,
		bool bApplyShortcutRule)
	{
		if (GateIndex != ExpectedGate)
		{
			return EGateCheck::WrongGate;
		}
		if (!bForward)
		{
			return EGateCheck::WrongDirection;
		}
		if (bApplyShortcutRule && SplineCmBetweenGates > 0.0 && TraveledCm < MinTravelFraction * SplineCmBetweenGates)
		{
			return EGateCheck::Shortcut;
		}
		return EGateCheck::Valid;
	}

	bool SegmentCrossesGate(const FVector& Prev, const FVector& Cur, const FTransform& Gate, const FVector& HalfExtent,
		double& OutAlpha, bool& bOutForward)
	{
		const FVector A = Gate.InverseTransformPositionNoScale(Prev);
		const FVector B = Gate.InverseTransformPositionNoScale(Cur);
		const bool bCrosses = (A.X < 0.0 && B.X >= 0.0) || (A.X > 0.0 && B.X <= 0.0);
		if (!bCrosses)
		{
			return false;
		}
		const double Alpha = A.X / (A.X - B.X);
		const FVector Hit = FMath::Lerp(A, B, Alpha);
		if (FMath::Abs(Hit.Y) > HalfExtent.Y || FMath::Abs(Hit.Z) > HalfExtent.Z)
		{
			return false;
		}
		OutAlpha = Alpha;
		bOutForward = B.X > A.X;
		return true;
	}

	TArray<int32> SortStandings(const TArray<FStandingKey>& Keys)
	{
		TArray<int32> Order;
		Order.Reserve(Keys.Num());
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.StableSort([&Keys](int32 IndexA, int32 IndexB)
		{
			const FStandingKey& A = Keys[IndexA];
			const FStandingKey& B = Keys[IndexB];
			if (A.bRetired != B.bRetired) { return !A.bRetired; }
			if (A.bFinished != B.bFinished) { return A.bFinished; }
			if (A.bFinished && A.FinishTime != B.FinishTime) { return A.FinishTime < B.FinishTime; }
			if (A.Lap != B.Lap) { return A.Lap > B.Lap; }
			if (A.GatesPassed != B.GatesPassed) { return A.GatesPassed > B.GatesPassed; }
			if (A.SegmentProgressCm != B.SegmentProgressCm) { return A.SegmentProgressCm > B.SegmentProgressCm; }
			return A.Id < B.Id;
		});
		return Order;
	}

	int32 PointsForPlace(int32 Place, bool bFinished)
	{
		static const int32 RallyPointsTable[] = { 10, 8, 6, 5, 4, 3, 2, 1 };
		if (!bFinished || Place < 1 || Place > UE_ARRAY_COUNT(RallyPointsTable))
		{
			return 0;
		}
		return RallyPointsTable[Place - 1];
	}

	EWrongWayEvent UpdateWrongWay(FWrongWayState& State, double DotVelocityTangent, double SpeedKmh, double DeltaSeconds)
	{
		const bool bWrong = DotVelocityTangent < WrongWayDot && SpeedKmh > WrongWayMinKmh;
		if (!bWrong)
		{
			State.Seconds = 0.0;
			if (State.bWarning)
			{
				State.bWarning = false;
				return EWrongWayEvent::WarningOff;
			}
			return EWrongWayEvent::None;
		}
		State.Seconds += DeltaSeconds;
		if (State.Seconds >= WrongWayTurnSeconds)
		{
			State = FWrongWayState();
			return EWrongWayEvent::TurnAround;
		}
		if (State.Seconds >= WrongWayWarnSeconds && !State.bWarning)
		{
			State.bWarning = true;
			return EWrongWayEvent::WarningOn;
		}
		return EWrongWayEvent::None;
	}

	bool UpdateStuck(FStuckState& State, const FVector& Position, double DeltaSeconds)
	{
		if (!State.bHasAnchor || FVector::Dist(Position, State.Anchor) > StuckRadiusCm)
		{
			State.Anchor = Position;
			State.Seconds = 0.0;
			State.bHasAnchor = true;
			return false;
		}
		State.Seconds += DeltaSeconds;
		if (State.Seconds >= StuckSeconds)
		{
			State = FStuckState();
			return true;
		}
		return false;
	}

	bool UpdateOffTrack(FOffTrackState& State, double DistanceToAxisCm, double DeltaSeconds)
	{
		if (DistanceToAxisCm <= OffTrackDistanceCm)
		{
			State.Seconds = 0.0;
			return false;
		}
		State.Seconds += DeltaSeconds;
		if (State.Seconds >= OffTrackGraceSeconds)
		{
			State.Seconds = 0.0;
			return true;
		}
		return false;
	}

	FAmmoWeights AmmoWeightsForPlace(int32 Place, int32 NumTeams)
	{
		const float T = NumTeams <= 1 ? 0.5f : FMath::Clamp(static_cast<float>(Place - 1) / static_cast<float>(NumTeams - 1), 0.f, 1.f);
		FAmmoWeights Weights;
		Weights.Alga = FMath::Lerp(4.f, 1.f, T);
		Weights.Tinta = FMath::Lerp(3.f, 1.f, T);
		Weights.Burbuja = FMath::Lerp(1.f, 3.f, T);
		Weights.Mortero = FMath::Lerp(1.f, 4.f, T);
		Weights.Ancla = FMath::Lerp(0.5f, 1.5f, T);
		// Conchas de las cajas «?» (#629): la recta, sobre todo delante; la teledirigida, sobre todo detrás.
		Weights.Concha = FMath::Lerp(3.f, 1.5f, T);
		Weights.ConchaGuiada = FMath::Lerp(0.5f, 2.5f, T);
		return Weights;
	}

	ETNRallyAmmo PickAmmo(const FAmmoWeights& Weights, float Roll01)
	{
		const float Total = Weights.Total();
		if (Total <= 0.f)
		{
			return ETNRallyAmmo::Alga;
		}
		float Pick = FMath::Clamp(Roll01, 0.f, 0.99999f) * Total;
		if ((Pick -= Weights.Alga) < 0.f) { return ETNRallyAmmo::Alga; }
		if ((Pick -= Weights.Burbuja) < 0.f) { return ETNRallyAmmo::Burbuja; }
		if ((Pick -= Weights.Mortero) < 0.f) { return ETNRallyAmmo::Mortero; }
		if ((Pick -= Weights.Ancla) < 0.f) { return ETNRallyAmmo::Ancla; }
		if ((Pick -= Weights.Concha) < 0.f) { return ETNRallyAmmo::Concha; }
		if ((Pick -= Weights.ConchaGuiada) < 0.f) { return ETNRallyAmmo::ConchaGuiada; }
		return ETNRallyAmmo::Tinta;
	}

	int32 ChargesFor(ETNRallyAmmo Ammo)
	{
		switch (Ammo)
		{
		case ETNRallyAmmo::Alga:
		case ETNRallyAmmo::Tinta:
		case ETNRallyAmmo::Ancla:
		case ETNRallyAmmo::Concha:
			return 2;
		case ETNRallyAmmo::Burbuja:
		case ETNRallyAmmo::Mortero:
		case ETNRallyAmmo::ConchaGuiada:
			return 1;
		default:
			return 0;
		}
	}

	EBotSpecialShot ShouldBotFireSpecial(ETNRallyAmmo Ammo, float HeldSeconds, float AheadCm, float BehindCm)
	{
		if (Ammo == ETNRallyAmmo::None || Ammo == ETNRallyAmmo::Coco)
		{
			return EBotSpecialShot::Hold;
		}
		const bool bAheadInRange = AheadCm >= 0.f && AheadCm <= BotSpecialRangeCm;
		const bool bBehindInRange = BehindCm >= 0.f && BehindCm <= BotAlgaBehindRangeCm;
		EBotSpecialShot Shot = EBotSpecialShot::Hold;
		switch (Ammo)
		{
		case ETNRallyAmmo::Alga:
			Shot = bBehindInRange ? EBotSpecialShot::AtBehind : (bAheadInRange ? EBotSpecialShot::AtAhead : EBotSpecialShot::Hold);
			break;
		case ETNRallyAmmo::Burbuja:
			Shot = HeldSeconds >= BotBubbleDelaySeconds ? EBotSpecialShot::Free : EBotSpecialShot::Hold;
			break;
		default:
			Shot = bAheadInRange ? EBotSpecialShot::AtAhead : EBotSpecialShot::Hold;
			break;
		}
		if (Shot == EBotSpecialShot::Hold && HeldSeconds >= BotMaxHoldSeconds)
		{
			// Guardada demasiado tiempo: al que haya delante o detrás y, si no hay nadie, hacia delante.
			Shot = AheadCm >= 0.f ? EBotSpecialShot::AtAhead : (BehindCm >= 0.f ? EBotSpecialShot::AtBehind : EBotSpecialShot::Free);
		}
		return Shot;
	}

	double WrapArc(double S, double Length, bool bClosed)
	{
		if (Length <= 0.0)
		{
			return 0.0;
		}
		if (!bClosed)
		{
			return FMath::Clamp(S, 0.0, Length);
		}
		double Wrapped = FMath::Fmod(S, Length);
		if (Wrapped < 0.0)
		{
			Wrapped += Length;
		}
		return Wrapped;
	}

	double ForwardArc(double From, double To, double Length, bool bClosed)
	{
		return bClosed ? WrapArc(To - From, Length, true) : To - From;
	}

	TArray<double> AmmoRowArcs(const TArray<double>& GateArcs, double Length, bool bClosed, double AfterGateCm,
		double NoAmmoBeforeFinishCm)
	{
		TArray<double> Rows;
		const int32 Num = GateArcs.Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			double Arc = -1.0;
			if (Index % 2 == 0 && Index != 0)
			{
				Arc = GateArcs[Index] + AfterGateCm;
			}
			else if (Index % 2 == 1 && (bClosed || Index + 1 < Num))
			{
				Arc = GateArcs[Index] + 0.5 * ForwardArc(GateArcs[Index], GateArcs[(Index + 1) % Num], Length, bClosed);
			}
			if (Arc < 0.0 || (!bClosed && Length - Arc < NoAmmoBeforeFinishCm))
			{
				continue;
			}
			Rows.Add(WrapArc(Arc, Length, bClosed));
		}
		return Rows;
	}

	double FindArcInWindow(TFunctionRef<FVector(double)> PositionAt, double Length, bool bClosed, const FVector& Point,
		double PrevS, double BehindCm, double AheadCm, double StepCm)
	{
		if (Length <= 0.0)
		{
			return 0.0;
		}
		double From = PrevS - BehindCm;
		double To = PrevS + AheadCm;
		if (!bClosed)
		{
			From = FMath::Max(0.0, From);
			To = FMath::Min(Length, To);
		}
		else if (To - From >= Length)
		{
			From = 0.0;
			To = Length;
		}
		const double Step = FMath::Max(StepCm, 1.0);
		auto DistAt = [&](double S) { return FVector::DistSquared(PositionAt(WrapArc(S, Length, bClosed)), Point); };

		double BestS = From;
		double BestD = TNumericLimits<double>::Max();
		for (double S = From; S <= To + KINDA_SMALL_NUMBER; S += Step)
		{
			const double D = DistAt(S);
			if (D < BestD)
			{
				BestD = D;
				BestS = S;
			}
		}
		// Afinado: búsqueda ternaria en el paso alrededor de la mejor muestra (la distancia es unimodal a esa escala).
		double Low = FMath::Max(From, BestS - Step);
		double High = FMath::Min(To, BestS + Step);
		for (int32 Iteration = 0; Iteration < 20; ++Iteration)
		{
			const double M1 = Low + (High - Low) / 3.0;
			const double M2 = High - (High - Low) / 3.0;
			if (DistAt(M1) < DistAt(M2)) { High = M2; } else { Low = M1; }
		}
		return WrapArc(0.5 * (Low + High), Length, bClosed);
	}

	TArray<FVector> DownsampleRoad(const TArray<FVector>& Road, double StepCm, bool bClosed)
	{
		TArray<FVector> Points;
		if (Road.Num() == 0)
		{
			return Points;
		}
		const double Step = FMath::Max(StepCm, 1.0);
		Points.Add(Road[0]);
		double Accumulated = 0.0;
		for (int32 Index = 1; Index < Road.Num(); ++Index)
		{
			Accumulated += FVector::Dist(Road[Index - 1], Road[Index]);
			if (Accumulated >= Step)
			{
				Points.Add(Road[Index]);
				Accumulated = 0.0;
			}
		}
		if (bClosed)
		{
			while (Points.Num() > 2 && FVector::Dist(Points.Last(), Points[0]) < 0.5 * Step)
			{
				Points.Pop();
			}
		}
		else if (Points.Last() != Road.Last())
		{
			// El final exacto de la calzada: si el último punto guardado queda muy cerca, se sustituye.
			if (Points.Num() > 1 && FVector::Dist(Points.Last(), Road.Last()) < 0.5 * Step)
			{
				Points.Last() = Road.Last();
			}
			else
			{
				Points.Add(Road.Last());
			}
		}
		return Points;
	}

	FVector2D GridSlotOffset(int32 Slot)
	{
		const int32 Row = FMath::Max(0, Slot) / 2;
		const bool bLeft = FMath::Max(0, Slot) % 2 == 0;
		return FVector2D(GridFirstRowBackCm + GridRowSpacingCm * Row, bLeft ? -GridHalfSpacingCm : GridHalfSpacingCm);
	}

	float SteerToward(const FVector& Forward, const FVector& ToTarget, float MaxAngleDeg)
	{
		const FVector2D F = FVector2D(Forward).GetSafeNormal();
		const FVector2D T = FVector2D(ToTarget).GetSafeNormal();
		if (F.IsNearlyZero() || T.IsNearlyZero() || MaxAngleDeg <= 0.f)
		{
			return 0.f;
		}
		const double Cross = F.X * T.Y - F.Y * T.X;
		const double Dot = F.X * T.X + F.Y * T.Y;
		const double AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
		return static_cast<float>(FMath::Clamp(AngleDeg / MaxAngleDeg, -1.0, 1.0));
	}

	float CornerSpeedKmh(const FVector& TangentNow, const FVector& TangentAhead, float MaxKmh, float MinKmh)
	{
		const FVector2D A = FVector2D(TangentNow).GetSafeNormal();
		const FVector2D B = FVector2D(TangentAhead).GetSafeNormal();
		const double Dot = FMath::Clamp(A.X * B.X + A.Y * B.Y, -1.0, 1.0);
		const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
		const float T = static_cast<float>(FMath::Clamp(AngleDeg / 90.0, 0.0, 1.0));
		return FMath::Lerp(MaxKmh, MinKmh, T);
	}

	float ApproachSpeedKmh(float CornerKmh, double DistanceCm, double DecelCms2)
	{
		const double CornerCms = FMath::Max(0.0, static_cast<double>(CornerKmh)) / 0.036;
		const double Room = 2.0 * FMath::Max(0.0, DecelCms2) * FMath::Max(0.0, DistanceCm);
		return static_cast<float>(CmsToKmh(FMath::Sqrt(CornerCms * CornerCms + Room)));
	}

	ETeamCleanup DecideTeamCleanup(bool bVehicleValid, bool bOccupied, bool bBeforeStart, bool bRetired)
	{
		if (bVehicleValid && bOccupied)
		{
			return ETeamCleanup::Keep;
		}
		if (bBeforeStart)
		{
			return ETeamCleanup::Remove;
		}
		return bRetired ? ETeamCleanup::Keep : ETeamCleanup::Retire;
	}

	bool AreWeaponsLive(bool bRaceRunning, bool bRetired)
	{
		return bRaceRunning && !bRetired;
	}
}
