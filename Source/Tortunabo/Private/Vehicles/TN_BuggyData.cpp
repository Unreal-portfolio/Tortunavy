#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyMath.h"

float UTN_BuggyData::EvaluateBoostRamp(float Progress01) const
{
	const float Progress = FMath::Clamp(Progress01, 0.f, 1.f);
	const FRichCurve* Curve = BoostRampCurve.GetRichCurveConst();
	if (Curve && Curve->GetNumKeys() > 0)
	{
		return Progress <= 0.f ? 0.f : FMath::Clamp(Curve->Eval(Progress), 0.f, 1.f);
	}
	return TNBuggy::BoostRampStrength(Progress, BoostRampExponent);
}
