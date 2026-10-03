#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "VR/TN_VRMode.h"

/**
 * Cuentas del modo VR sin mundo ni actores (las usa ATN_VRRig y las prueba Tortunabo.VR.*): el rayo del puntero contra el
 * panel de la interfaz, el HUD que sigue a la cabeza con retraso, el giro por pasos y la navegación de los menús con los
 * mandos.
 */
namespace TNVRMath
{
	/**
	 * Rayo contra un panel plano (UWidgetComponent en el plano YZ de su componente, mirando a +X, centrado). Size es el
	 * tamaño de dibujo del panel (píxeles = unidades locales); Transform, la del componente (con su escala).
	 * @param OutHit  Punto del mundo tocado.
	 * @param OutUV   Posición en el panel (0..1 de izquierda a derecha y de arriba abajo, vista desde delante).
	 * @return true si el rayo va hacia el panel y lo toca dentro de sus bordes.
	 */
	inline bool RayPanelHit(const FVector& RayOrigin, const FVector& RayDir, const FTransform& Transform, const FVector2D& Size,
		FVector& OutHit, FVector2D& OutUV)
	{
		const FVector LocalOrigin = Transform.InverseTransformPosition(RayOrigin);
		const FVector LocalDir = Transform.InverseTransformVector(RayDir);
		if (FMath::Abs(LocalDir.X) < UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const double T = -LocalOrigin.X / LocalDir.X;
		if (T <= 0.0)
		{
			return false;
		}
		const FVector LocalHit = LocalOrigin + LocalDir * T;
		if (FMath::Abs(LocalHit.Y) > Size.X * 0.5 || FMath::Abs(LocalHit.Z) > Size.Y * 0.5)
		{
			return false;
		}
		OutHit = Transform.TransformPosition(LocalHit);
		// Visto desde +X (desde delante), la derecha de la pantalla es -Y y arriba es +Z.
		OutUV = FVector2D(0.5 - LocalHit.Y / Size.X, 0.5 - LocalHit.Z / Size.Y);
		return true;
	}

	/**
	 * Rumbo del HUD que sigue a la cabeza con retraso: quieto mientras la cabeza no se aparte más de StartAngle; entonces
	 * va hacia ella a Speed grados por segundo hasta quedar a menos de StopAngle.
	 */
	inline float LazyFollowYaw(float CurrentYaw, float TargetYaw, float DeltaTime, bool& bFollowing, float StartAngle = 24.f,
		float StopAngle = 2.f, float Speed = 160.f)
	{
		const float Delta = static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(TargetYaw - CurrentYaw)));
		if (!bFollowing && FMath::Abs(Delta) > StartAngle)
		{
			bFollowing = true;
		}
		if (!bFollowing)
		{
			return CurrentYaw;
		}
		// Más deprisa cuanto más lejos (llega sin frenazo y sin quedarse atrás al girar rápido).
		const float Step = FMath::Max(Speed, FMath::Abs(Delta) * 4.f) * DeltaTime;
		const float Moved = FMath::Abs(Delta) <= Step ? Delta : FMath::Sign(Delta) * Step;
		if (FMath::Abs(Delta - Moved) < StopAngle)
		{
			bFollowing = false;
		}
		return static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(CurrentYaw + Moved)));
	}

	/**
	 * Giro por pasos con el eje X de un stick: un paso (-1 izquierda, +1 derecha) al pasar de OnThreshold; hasta que el
	 * stick vuelve por debajo de OffThreshold no hay otro.
	 */
	inline int32 SnapTurnStep(float Axis, bool& bLatched, float OnThreshold = 0.7f, float OffThreshold = 0.35f)
	{
		if (bLatched)
		{
			if (FMath::Abs(Axis) < OffThreshold)
			{
				bLatched = false;
			}
			return 0;
		}
		if (FMath::Abs(Axis) >= OnThreshold)
		{
			bLatched = true;
			return Axis > 0.f ? 1 : -1;
		}
		return 0;
	}

	/** Distancia del panel: la que se quiere, o antes de lo que haya en medio (con un margen), nunca menos de MinDistance. */
	inline float PanelDistance(float Desired, bool bBlocked, float BlockedAt, float MinDistance = 40.f, float Margin = 8.f)
	{
		if (!bBlocked)
		{
			return Desired;
		}
		return FMath::Clamp(BlockedAt - Margin, MinDistance, Desired);
	}

	/**
	 * Panel curvo (Docs/Modo_VR.md): un trozo de cilindro vertical que rodea los ojos. En el espacio local del panel (el del
	 * UWidgetComponent: unidades = píxeles de dibujo, +X hacia los ojos) el centro del panel está en el origen y el eje del
	 * cilindro pasa por (Radius, 0, *): los bordes se acercan a los ojos. Radius = ancho de dibujo / arco en radianes.
	 */
	inline double CurvedPanelRadius(const FVector2D& Size, float ArcDeg)
	{
		return Size.X / FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(ArcDeg), 5.0, 180.0));
	}

	/** Punto local del panel curvo para una posición del panel (0..1, como OutUV de RayPanelHit). */
	inline FVector CurvedPanelPoint(const FVector2D& UV, const FVector2D& Size, float ArcDeg)
	{
		const double Arc = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(ArcDeg), 5.0, 180.0));
		const double Radius = CurvedPanelRadius(Size, ArcDeg);
		// A la izquierda (vista desde delante, +Y) el ángulo es positivo; U = 0 en el borde izquierdo.
		const double Angle = (0.5 - UV.X) * Arc;
		return FVector(Radius - Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), (0.5 - UV.Y) * Size.Y);
	}

	/**
	 * Rayo contra el panel curvo de CurvedPanelPoint (Transform, la del componente con su escala).
	 * @param OutHit  Punto del mundo tocado en la superficie curva.
	 * @param OutUV   Posición en el panel (0..1 de izquierda a derecha y de arriba abajo, vista desde delante).
	 */
	inline bool RayCurvedPanelHit(const FVector& RayOrigin, const FVector& RayDir, const FTransform& Transform, const FVector2D& Size,
		float ArcDeg, FVector& OutHit, FVector2D& OutUV)
	{
		const double Arc = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(ArcDeg), 5.0, 180.0));
		const double Radius = CurvedPanelRadius(Size, ArcDeg);
		const FVector O = Transform.InverseTransformPosition(RayOrigin);
		const FVector D = Transform.InverseTransformVector(RayDir);
		// Cilindro vertical con el eje en (Radius, 0): |(O + tD - C).XY| = Radius.
		const double Ox = O.X - Radius;
		const double A = D.X * D.X + D.Y * D.Y;
		if (A < UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const double B = 2.0 * (Ox * D.X + O.Y * D.Y);
		const double C = Ox * Ox + O.Y * O.Y - Radius * Radius;
		const double Disc = B * B - 4.0 * A * C;
		if (Disc < 0.0)
		{
			return false;
		}
		const double Root = FMath::Sqrt(Disc);
		const double Ts[2] = { (-B - Root) / (2.0 * A), (-B + Root) / (2.0 * A) };
		for (const double T : Ts)
		{
			if (T <= 0.0)
			{
				continue;
			}
			const FVector P = O + D * T;
			// Ángulo desde el eje hacia el panel (el panel está en la cara -X del cilindro).
			const double Angle = FMath::Atan2(P.Y, Radius - P.X);
			if (Radius - P.X <= 0.0 || FMath::Abs(Angle) > Arc * 0.5 || FMath::Abs(P.Z) > Size.Y * 0.5)
			{
				continue;
			}
			OutHit = Transform.TransformPosition(P);
			OutUV = FVector2D(0.5 - Angle / Arc, 0.5 - P.Z / Size.Y);
			return true;
		}
		return false;
	}

	/** Escala del panel curvo para que su cilindro tenga Distance de radio (el eje pasa por los ojos). */
	inline float CurvedPanelScale(float Distance, float ArcDeg, float DrawWidth)
	{
		return static_cast<float>(Distance * FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(ArcDeg), 5.0, 180.0)) / FMath::Max(1.0, static_cast<double>(DrawWidth)));
	}

	/** Valor del gatillo o del agarre de los Touch a partir del cual cuenta como pulsado, y por debajo del cual, suelto. */
	constexpr float AnalogPressThreshold = 0.55f;
	constexpr float AnalogReleaseThreshold = 0.35f;

	/**
	 * Botón analógico (gatillo o agarre de los Touch) con histéresis: +1 al pasar de OnThreshold, -1 al volver por debajo
	 * de OffThreshold, 0 si no cambia. Con OpenXR, los Touch solo dan el valor del gatillo y del agarre (no el «clic»).
	 */
	inline int32 AnalogButton(float Value, bool& bHeld, float OnThreshold = AnalogPressThreshold, float OffThreshold = AnalogReleaseThreshold)
	{
		if (!bHeld && Value >= OnThreshold)
		{
			bHeld = true;
			return 1;
		}
		if (bHeld && Value < OffThreshold)
		{
			bHeld = false;
			return -1;
		}
		return 0;
	}

	/** Velocidad con la que sale lo que se suelta de la mano: la de la mano, con tope (cm/s). */
	inline FVector ThrowVelocity(const FVector& HandVelocity, float MaxSpeed = 1600.f)
	{
		return HandVelocity.GetClampedToMaxSize(MaxSpeed);
	}

	/**
	 * Velocidad de la mano respecto del origen de la vista (el del seguimiento de las gafas, que va con el cuerpo), en los
	 * ejes del mundo de ahora (cm/s). Andar, saltar o girar con el stick mueven el origen y la mano a la vez: no cuentan;
	 * solo cuenta lo que se mueve la mano de verdad. Origin: la transformación del origen en cada fotograma.
	 */
	inline FVector RelativeHandVelocity(const FTransform& PrevOrigin, const FVector& PrevHand, const FTransform& NowOrigin, const FVector& NowHand,
		float DeltaTime)
	{
		if (DeltaTime <= UE_KINDA_SMALL_NUMBER)
		{
			return FVector::ZeroVector;
		}
		const FVector PrevLocal = PrevOrigin.InverseTransformPosition(PrevHand);
		const FVector NowLocal = NowOrigin.InverseTransformPosition(NowHand);
		return NowOrigin.TransformVector((NowLocal - PrevLocal) / DeltaTime);
	}

	/** ¿Soltar con esta velocidad de la mano (respecto del cuerpo) es un gesto de lanzar? */
	inline bool IsThrowSwing(const FVector& HandRelativeVelocity, float MinSpeed)
	{
		return HandRelativeVelocity.SizeSquared() >= FMath::Square(MinSpeed);
	}

	/**
	 * Velocidad con la que sale un objeto con física al soltarlo: la de la mano respecto del cuerpo más, si bAddBody, la del
	 * cuerpo (lo que llevas andando sigue andando contigo), con tope.
	 */
	inline FVector ReleaseVelocity(const FVector& HandRelativeVelocity, const FVector& BodyVelocity, bool bAddBody, float MaxSpeed = 1600.f)
	{
		return ThrowVelocity(bAddBody ? HandRelativeVelocity + BodyVelocity : HandRelativeVelocity, MaxSpeed);
	}

	/**
	 * ¿Cabe en la imagen el panel curvo de ArcRad radianes, mirado de frente desde el eje del cilindro? TanHalfH y TanHalfV:
	 * tangentes de la mitad del campo de visión horizontal y vertical; DrawAspect: alto / ancho del dibujo del panel;
	 * DropFraction: cuánto va el centro del panel por debajo de los ojos (en distancias). Las esquinas de abajo son lo que
	 * antes se sale: están a la distancia del panel, pero de lado, y en la imagen se ven más abajo (se dividen por el coseno).
	 */
	inline bool CurvedPanelFits(double ArcRad, double TanHalfH, double TanHalfV, double DrawAspect, double DropFraction)
	{
		const double Half = ArcRad * 0.5;
		if (Half >= UE_DOUBLE_HALF_PI * 0.95)
		{
			return false;
		}
		const double Cos = FMath::Cos(Half);
		const double HalfHeight = 0.5 * ArcRad * DrawAspect;
		const double Bottom = (DropFraction + HalfHeight) / Cos;
		const double Top = FMath::Abs(HalfHeight - DropFraction) / Cos;
		return FMath::Tan(Half) <= TanHalfH && FMath::Max(Bottom, Top) <= TanHalfV;
	}

	/**
	 * Arco (grados) del menú curvo sin gafas (modo simulado): el pedido, pero sin pasar de lo que cabe en la imagen con el
	 * campo de visión horizontal de la cámara (HorizontalFov), la proporción de la ventana (ancho / alto) y un margen (la
	 * fracción de la imagen que puede ocupar). El panel curvo tiene el eje en los ojos (ATN_VRRig::PlacePanel).
	 */
	inline float SimulatedMenuArc(float WantedArc, float HorizontalFov, float AspectRatio, float DropFraction = 0.f, float Margin = 0.85f,
		float DrawAspect = 1080.f / 1920.f)
	{
		const double TanH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(HorizontalFov), 20.0, 170.0) * 0.5)) * Margin;
		const double TanV = TanH / FMath::Max(0.1, static_cast<double>(AspectRatio));
		const double Wanted = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(WantedArc), 5.0, 180.0));
		if (CurvedPanelFits(Wanted, TanH, TanV, DrawAspect, DropFraction))
		{
			return static_cast<float>(FMath::RadiansToDegrees(Wanted));
		}
		// El mayor que cabe (entre 5° y el pedido), por bisección.
		double Lo = FMath::DegreesToRadians(5.0);
		double Hi = Wanted;
		for (int32 Step = 0; Step < 30; ++Step)
		{
			const double Mid = (Lo + Hi) * 0.5;
			if (CurvedPanelFits(Mid, TanH, TanV, DrawAspect, DropFraction))
			{
				Lo = Mid;
			}
			else
			{
				Hi = Mid;
			}
		}
		// Una centésima de grado por dentro: al pasar a float el valor del borde puede redondear hacia fuera y dejar de caber.
		return static_cast<float>(FMath::RadiansToDegrees(Lo) - 0.01);
	}

	/** Escala del panel para que ocupe HorizontalFovDeg grados de ancho a Distance: el ancho de dibujo pasa a centímetros. */
	inline float PanelScale(float Distance, float HorizontalFovDeg, float DrawWidth)
	{
		const float Width = 2.f * Distance * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDeg, 10.f, 150.f) * 0.5f));
		return Width / FMath::Max(1.f, DrawWidth);
	}

	/** Dirección de un stick para moverse por un menú: 0 ninguna, 1 arriba, 2 abajo, 3 izquierda, 4 derecha. */
	inline int32 StickDirection(const FVector2D& Stick, float Threshold = 0.6f)
	{
		if (Stick.Size() < Threshold)
		{
			return 0;
		}
		if (FMath::Abs(Stick.Y) >= FMath::Abs(Stick.X))
		{
			return Stick.Y > 0.f ? 1 : 2;
		}
		return Stick.X > 0.f ? 4 : 3;
	}

	/** La tecla de mando que entienden los menús para una dirección de StickDirection. */
	inline FKey DirectionKey(int32 Direction)
	{
		switch (Direction)
		{
			case 1: return EKeys::Gamepad_DPad_Up;
			case 2: return EKeys::Gamepad_DPad_Down;
			case 3: return EKeys::Gamepad_DPad_Left;
			case 4: return EKeys::Gamepad_DPad_Right;
			default: return EKeys::Invalid;
		}
	}

	/**
	 * Botón de los mandos VR → la tecla de mando que ya entienden todos los menús (tienda, probador, general, pausa,
	 * salas, campeón): A/X aceptar, B/Y atrás, gatillos de agarre cambiar de pestaña, menú = Start. Los gatillos son el
	 * clic del puntero (no pasan por aquí). EKeys::Invalid si no tiene equivalente.
	 */
	inline FKey MenuKeyFor(const FKey& Key)
	{
		if (Key == FTNVRKeys::A || Key == FTNVRKeys::X) { return EKeys::Gamepad_FaceButton_Bottom; }
		if (Key == FTNVRKeys::B || Key == FTNVRKeys::Y) { return EKeys::Gamepad_FaceButton_Right; }
		if (Key == FTNVRKeys::LeftGrip) { return EKeys::Gamepad_LeftShoulder; }
		if (Key == FTNVRKeys::RightGrip) { return EKeys::Gamepad_RightShoulder; }
		if (Key == FTNVRKeys::Menu) { return EKeys::Gamepad_Special_Right; }
		if (Key == FTNVRKeys::LeftStickUp || Key == FTNVRKeys::RightStickUp) { return EKeys::Gamepad_DPad_Up; }
		if (Key == FTNVRKeys::LeftStickDown || Key == FTNVRKeys::RightStickDown) { return EKeys::Gamepad_DPad_Down; }
		if (Key == FTNVRKeys::LeftStickLeft || Key == FTNVRKeys::RightStickLeft) { return EKeys::Gamepad_DPad_Left; }
		if (Key == FTNVRKeys::LeftStickRight || Key == FTNVRKeys::RightStickRight) { return EKeys::Gamepad_DPad_Right; }
		return EKeys::Invalid;
	}
}
