#include "Vehicles/TN_BuggyWheel.h"
#include "Vehicles/TN_BuggyMath.h"

namespace
{
	/**
	 * Geometría y suspensión comunes: parten del Offroad de TP_VehicleAdv con ajuste arcade (amortiguación 0,25,
	 * recorrido 25 cm y freno de 6000 N·m), como en HellYeah.
	 */
	void ApplySharedWheelSetup(UChaosVehicleWheel& Wheel)
	{
		// Medidas de SM_TN_BuggyTire (caja de 100,85 × 35 cm); lo comprueba Tortunabo.Rally.Buggy.Assets.
		Wheel.WheelRadius = 50.4f;
		Wheel.WheelWidth = 35.f;
		// Más rígido que los 750 de HellYeah (#606): la rueda llega a su agarre con menos deriva y el giro rápido responde.
		Wheel.CorneringStiffness = 1000.f;
		Wheel.SuspensionMaxRaise = 25.f;
		Wheel.SuspensionMaxDrop = 25.f;
		Wheel.SuspensionDampingRatio = 0.25f;
		Wheel.WheelLoadRatio = 1.f;
		Wheel.SpringRate = 100.f;
		Wheel.SpringPreload = 100.f;
		Wheel.SweepShape = ESweepShape::Shapecast;
		Wheel.MaxBrakeTorque = 6000.f;
	}
}

UTN_BuggyWheelFront::UTN_BuggyWheelFront()
{
	ApplySharedWheelSetup(*this);
	AxleType = EAxleType::Front;
	bAffectedBySteering = true;
	// El mismo a cualquier velocidad (#606); ATN_Buggy::ApplyWheelFriction lo cambia por UTN_BuggyData::MaxSteerAngleDeg.
	MaxSteerAngle = TNBuggy::DefaultSteerAngleDeg;
	// ATN_Buggy::ApplyWheelFriction lo sustituye por UTN_BuggyData::FrontFriction en cuanto hay simulación.
	FrictionForceMultiplier = 3.6f;
}

UTN_BuggyWheelRear::UTN_BuggyWheelRear()
{
	ApplySharedWheelSetup(*this);
	AxleType = EAxleType::Rear;
	bAffectedByHandbrake = true;
	bAffectedByEngine = true;
	MaxHandBrakeTorque = 6000.f;
	// Igual que la delantera (UTN_BuggyData::RearFriction, que lo sustituye en cuanto hay simulación).
	FrictionForceMultiplier = 3.6f;
}
