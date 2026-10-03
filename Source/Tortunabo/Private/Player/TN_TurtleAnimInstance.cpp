#include "Player/TN_TurtleAnimInstance.h"
#include "Art/TN_TurtleArt.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "World/Beach/TN_BeachRaceGenerator.h"

namespace TNTurtleAnim
{
	/** Ejes del espacio de la malla: mira a +Y, arriba +Z, su izquierda +X. */
	const FVector AxisX(1.0, 0.0, 0.0);
	const FVector AxisY(0.0, 1.0, 0.0);
	const FVector AxisZ(0.0, 0.0, 1.0);

	/**
	 * Velocidad (unidades de la malla por segundo) a la que el clip de andar no patina: el pie apoyado barre 41 u/s en
	 * el clip y la zancada se alarga un 25 % (Amplify). Se multiplica por la escala del componente (2,5 en el personaje).
	 */
	constexpr float WalkNaturalUnits = 51.f;
	/** Largo de la pierna (cadera a punta del pie), en unidades de la malla: da la amplitud del paso de la carrera. */
	constexpr float LegUnits = 24.f;
	constexpr float TwoPiF = 6.2831853f;

	struct FBones
	{
		FCompactPoseBoneIndex Hips = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Spine = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Spine1 = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Spine2 = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Neck = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Head = FCompactPoseBoneIndex(INDEX_NONE);
		/** Clavículas (hombros): las bajan las celebraciones del podio. */
		FCompactPoseBoneIndex LShoulder = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RShoulder = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex LArm = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex LFore = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RArm = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RFore = FCompactPoseBoneIndex(INDEX_NONE);
		/** Manos (muñecas): el IK de los brazos en VR las lleva a los mandos. */
		FCompactPoseBoneIndex LHand = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RHand = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex LUp = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex LLeg = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex LFoot = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RUp = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RLeg = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex RFoot = FCompactPoseBoneIndex(INDEX_NONE);
	};

	FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	}

	FBones ResolveBones(const FBoneContainer& Bones)
	{
		FBones Out;
		Out.Hips = FindBone(Bones, TEXT("Hips"));
		Out.Spine = FindBone(Bones, TEXT("Spine"));
		Out.Spine1 = FindBone(Bones, TEXT("Spine1"));
		Out.Spine2 = FindBone(Bones, TEXT("Spine2"));
		Out.Neck = FindBone(Bones, TEXT("Neck"));
		Out.Head = FindBone(Bones, TEXT("Head"));
		Out.LShoulder = FindBone(Bones, TEXT("LeftShoulder"));
		Out.RShoulder = FindBone(Bones, TEXT("RightShoulder"));
		Out.LArm = FindBone(Bones, TEXT("LeftArm"));
		Out.LFore = FindBone(Bones, TEXT("LeftForeArm"));
		Out.RArm = FindBone(Bones, TEXT("RightArm"));
		Out.RFore = FindBone(Bones, TEXT("RightForeArm"));
		Out.LHand = FindBone(Bones, TEXT("LeftHand"));
		Out.RHand = FindBone(Bones, TEXT("RightHand"));
		Out.LUp = FindBone(Bones, TEXT("LeftUpLeg"));
		Out.LLeg = FindBone(Bones, TEXT("LeftLeg"));
		Out.LFoot = FindBone(Bones, TEXT("LeftFoot"));
		Out.RUp = FindBone(Bones, TEXT("RightUpLeg"));
		Out.RLeg = FindBone(Bones, TEXT("RightLeg"));
		Out.RFoot = FindBone(Bones, TEXT("RightFoot"));
		return Out;
	}

	/** Transformación de un hueso en el espacio de la malla (composición de las locales desde la raíz). */
	FTransform ComponentSpace(const FCompactPose& Pose, FCompactPoseBoneIndex Bone)
	{
		FTransform T = Pose[Bone];
		for (FCompactPoseBoneIndex P = Pose.GetParentBoneIndex(Bone); P.IsValid(); P = Pose.GetParentBoneIndex(P))
		{
			T = T * Pose[P];
		}
		return T;
	}

	/**
	 * IK de dos huesos (brazo y antebrazo): la mano llega a Target (espacio de la malla) o, si no alcanza, se estira
	 * hacia él. El codo se dobla en el plano en el que ya estaba (con el brazo recto, hacia abajo y atrás). Solo gira el
	 * brazo y el antebrazo; la mano sigue al antebrazo. Weight mezcla con la pose que hay.
	 */
	void ReachArm(FCompactPose& Pose, FCompactPoseBoneIndex Upper, FCompactPoseBoneIndex Lower, FCompactPoseBoneIndex Hand,
		const FVector& Target, float Weight)
	{
		if (!Upper.IsValid() || !Lower.IsValid() || !Hand.IsValid() || Weight < 0.01f) { return; }
		const FCompactPoseBoneIndex UpperParent = Pose.GetParentBoneIndex(Upper);
		const FTransform ParentCS = UpperParent.IsValid() ? ComponentSpace(Pose, UpperParent) : FTransform::Identity;
		const FTransform UpperCS = Pose[Upper] * ParentCS;
		const FTransform LowerCS = Pose[Lower] * UpperCS;
		const FTransform HandCS = Pose[Hand] * LowerCS;
		const FVector A = UpperCS.GetLocation();
		const FVector Elbow = LowerCS.GetLocation();
		const FVector Wrist = HandCS.GetLocation();
		const double L1 = (Elbow - A).Size();
		const double L2 = (Wrist - Elbow).Size();
		const FVector ToTarget = Target - A;
		double Reach = ToTarget.Size();
		if (L1 < 1e-3 || L2 < 1e-3 || Reach < 1e-3) { return; }
		const FVector Dir = ToTarget / Reach;
		Reach = FMath::Clamp(Reach, FMath::Abs(L1 - L2) + 0.01, (L1 + L2) * 0.999);
		// Plano del codo: el doblez de ahora; con el brazo recto, abajo (-Z de la malla) y atrás (-Y).
		FVector Bend = (Elbow - A) - Dir * FVector::DotProduct(Elbow - A, Dir);
		if (Bend.SizeSquared() < 1e-4)
		{
			const FVector Down(0.0, -0.5, -1.0);
			Bend = Down - Dir * FVector::DotProduct(Down, Dir);
		}
		Bend = Bend.GetSafeNormal();
		const double CosA = FMath::Clamp((L1 * L1 + Reach * Reach - L2 * L2) / (2.0 * L1 * Reach), -1.0, 1.0);
		const double SinA = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosA * CosA));
		const FVector NewElbow = A + (Dir * CosA + Bend * SinA) * L1;
		const FVector NewWrist = A + Dir * Reach;
		// Giros en el espacio de la malla que llevan cada hueso a su nueva dirección, y de vuelta a locales.
		const FQuat UpperDelta = FQuat::FindBetweenNormals((Elbow - A).GetSafeNormal(), (NewElbow - A).GetSafeNormal());
		const FQuat UpperRot = UpperDelta * UpperCS.GetRotation();
		const FVector LowerDirAfter = UpperDelta.RotateVector(Wrist - Elbow).GetSafeNormal();
		const FQuat LowerDelta = FQuat::FindBetweenNormals(LowerDirAfter, (NewWrist - NewElbow).GetSafeNormal());
		const FQuat LowerRot = LowerDelta * UpperDelta * LowerCS.GetRotation();
		const FQuat UpperLocal = ParentCS.GetRotation().Inverse() * UpperRot;
		const FQuat LowerLocal = UpperRot.Inverse() * LowerRot;
		const float W = FMath::Clamp(Weight, 0.f, 1.f);
		Pose[Upper].SetRotation(FQuat::Slerp(Pose[Upper].GetRotation(), UpperLocal, W).GetNormalized());
		Pose[Lower].SetRotation(FQuat::Slerp(Pose[Lower].GetRotation(), LowerLocal, W).GetNormalized());
	}

	/** Lleva la articulación de un hueso (con sus hijos) hacia un punto del espacio de la malla. */
	void MoveJointToward(FCompactPose& Pose, FCompactPoseBoneIndex Bone, const FVector& Target, float Alpha)
	{
		if (!Bone.IsValid() || Alpha <= 0.001f) { return; }
		const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Bone);
		const FTransform ParentCS = Parent.IsValid() ? ComponentSpace(Pose, Parent) : FTransform::Identity;
		const FVector Current = (Pose[Bone] * ParentCS).GetLocation();
		Pose[Bone].SetTranslation(ParentCS.InverseTransformPosition(FMath::Lerp(Current, Target, static_cast<double>(Alpha))));
	}

	/** Giro del padre en el espacio de la malla (producto de los giros locales desde la raíz). */
	FQuat ParentRotation(const FCompactPose& Pose, FCompactPoseBoneIndex Bone)
	{
		FQuat Q = FQuat::Identity;
		for (FCompactPoseBoneIndex P = Pose.GetParentBoneIndex(Bone); P.IsValid(); P = Pose.GetParentBoneIndex(P))
		{
			Q = Pose[P].GetRotation() * Q;
		}
		return Q;
	}

	/** Gira un hueso Degrees alrededor de un eje del espacio de la malla que pasa por su articulación (los hijos le siguen). */
	void Turn(FCompactPose& Pose, FCompactPoseBoneIndex Bone, const FVector& Axis, float Degrees)
	{
		if (!Bone.IsValid() || FMath::Abs(Degrees) < 0.01f) { return; }
		const FQuat Parent = ParentRotation(Pose, Bone);
		const FQuat Delta(Axis, FMath::DegreesToRadians(Degrees));
		FTransform& Local = Pose[Bone];
		Local.SetRotation((Parent.Inverse() * Delta * Parent * Local.GetRotation()).GetNormalized());
	}

	/** A = mezcla de A con B (peso de B), hueso a hueso en espacio local. */
	void BlendInto(FCompactPose& A, const FCompactPose& B, float Weight)
	{
		const float W = FMath::Clamp(Weight, 0.f, 1.f);
		if (W <= 0.001f) { return; }
		for (const FCompactPoseBoneIndex I : A.ForEachBoneIndex())
		{
			const FTransform Current = A[I];
			A[I].Blend(Current, B[I], W);
		}
	}

	/**
	 * Como BlendInto, pero solo en Root y los huesos que cuelgan de él (un brazo entero desde el hombro): lo demás sigue
	 * con su animación (andar, correr, saltar).
	 */
	void BlendChainInto(FCompactPose& A, const FCompactPose& B, FCompactPoseBoneIndex Root, float Weight)
	{
		const float W = FMath::Clamp(Weight, 0.f, 1.f);
		if (!Root.IsValid() || W <= 0.001f) { return; }
		for (const FCompactPoseBoneIndex I : A.ForEachBoneIndex())
		{
			bool bInChain = false;
			for (FCompactPoseBoneIndex P = I; P.IsValid(); P = A.GetParentBoneIndex(P))
			{
				if (P == Root)
				{
					bInChain = true;
					break;
				}
			}
			if (!bInChain) { continue; }
			const FTransform Current = A[I];
			A[I].Blend(Current, B[I], W);
		}
	}

	/** Segundos con los que se funde el final de un clip en bucle con su principio (sin salto al volver a empezar). */
	constexpr float LoopFadeSeconds = 0.3f;

	/**
	 * Pose del clip en bucle en el tiempo Time. Con bLoopFade, el bucle dura lo que el clip menos LoopFadeSeconds: al
	 * empezar cada vuelta, el final del clip se funde con su principio, así que la pose no salta aunque el primer y el
	 * último fotograma no coincidan. Sin él (ciclos que ya cierran, como el de andar), bucle simple de todo el clip: el
	 * fundido mezclaría dos momentos distintos de la zancada y acortaría un paso.
	 */
	void SampleClip(const UAnimSequence* Clip, float Time, FPoseContext& Out, bool bLoopFade = true)
	{
		if (!Clip)
		{
			Out.ResetToRefPose();
			return;
		}
		const float Length = FMath::Max(0.01f, Clip->GetPlayLength());
		const float Fade = bLoopFade ? FMath::Min(LoopFadeSeconds, Length * 0.25f) : 0.f;
		const float Loop = FMath::Max(0.01f, Length - Fade);
		const float T = FMath::Fmod(FMath::Max(0.f, Time), Loop);
		FAnimationPoseData Data(Out);
		Clip->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(T), false));
		if (T < Fade && Fade > 0.001f)
		{
			FPoseContext Tail(Out);
			FAnimationPoseData TailData(Tail);
			Clip->GetAnimationPose(TailData, FAnimExtractContext(static_cast<double>(T + Loop), false));
			// Peso suave del principio: 0 justo al volver a empezar (se ve el final) y 1 al acabar la fusión.
			const float X = T / Fade;
			const float StartW = X * X * (3.f - 2.f * X);
			for (const FCompactPoseBoneIndex I : Out.Pose.ForEachBoneIndex())
			{
				const FTransform Start = Out.Pose[I];
				Out.Pose[I].Blend(Tail.Pose[I], Start, StartW);
			}
		}
	}

	/** Deja la cadera de los clips en su sitio (sin avance propio: lo mueve el personaje). */
	void KeepHipsInPlace(FCompactPose& Pose, const FBones& B)
	{
		if (!B.Hips.IsValid()) { return; }
		const FTransform& Ref = Pose.GetBoneContainer().GetRefPoseTransform(B.Hips);
		FVector T = Pose[B.Hips].GetTranslation();
		T.X = Ref.GetTranslation().X;
		T.Y = Ref.GetTranslation().Y;
		Pose[B.Hips].SetTranslation(T);
	}

	void Lift(FCompactPose& Pose, const FBones& B, float Units)
	{
		if (B.Hips.IsValid()) { Pose[B.Hips].AddToTranslation(FVector(0.0, 0.0, Units)); }
	}

	/** Exagera el giro local de un hueso respecto de la postura de referencia (K > 1 alarga la zancada de un clip). */
	void Amplify(FCompactPose& Pose, FCompactPoseBoneIndex Bone, double K)
	{
		if (!Bone.IsValid()) { return; }
		const FQuat Ref = Pose.GetBoneContainer().GetRefPoseTransform(Bone).GetRotation();
		FQuat Delta = Pose[Bone].GetRotation() * Ref.Inverse();
		if (Delta.W < 0.0) { Delta = FQuat(-Delta.X, -Delta.Y, -Delta.Z, -Delta.W); }
		FVector Axis;
		double Angle = 0.0;
		Delta.ToAxisAndAngle(Axis, Angle);
		Pose[Bone].SetRotation((FQuat(Axis, Angle * K) * Ref).GetNormalized());
	}

	// ── Poses (sobre la postura en T; brazo izquierdo a lo largo de +X, derecho de -X, piernas hacia -Z) ──
	// Brazo izquierdo: abajo = +Y, arriba = -Y, adelante = +Z. Derecho: al revés en Y y en Z.
	// Piernas: adelante = +X. Rodilla (pierna baja hacia atrás) = -X. Espalda hacia delante = -X. Cabeza arriba = +X.

	void ArmsRelaxed(FCompactPose& P, const FBones& B, float Down = 68.f)
	{
		Turn(P, B.LArm, AxisY, Down);
		Turn(P, B.RArm, AxisY, -Down);
	}

	void PoseAir(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float Flail = 8.f * FMath::Sin(F.Clock * 10.f);
		const float Up = 35.f + 25.f * F.Falling;
		Turn(P, B.LArm, AxisY, -Up + Flail);
		Turn(P, B.LArm, AxisZ, 20.f);
		Turn(P, B.RArm, AxisY, Up + Flail);
		Turn(P, B.RArm, AxisZ, -20.f);
		Turn(P, B.LFore, AxisZ, 25.f);
		Turn(P, B.RFore, AxisZ, -25.f);
		Turn(P, B.LUp, AxisX, 50.f - 15.f * F.Falling);
		Turn(P, B.RUp, AxisX, 38.f - 10.f * F.Falling);
		Turn(P, B.LLeg, AxisX, -80.f);
		Turn(P, B.RLeg, AxisX, -65.f);
		Turn(P, B.Spine, AxisX, 6.f);
	}

	/**
	 * Carrera del sprint (RunTime cuenta ciclos): zancada larga con fase de vuelo, la rodilla recogida mientras la
	 * pierna vuelve hacia delante, braceo contrario a las piernas con los codos doblados y el tronco hacia delante.
	 */
	void PoseRun(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float Ph = F.RunTime * TwoPiF;
		const float S = FMath::Sin(Ph);
		const float C = FMath::Cos(Ph);
		const float K = F.SprintW;
		Turn(P, B.Spine, AxisX, -(5.f + 5.f * K));
		Turn(P, B.Spine1, AxisZ, 8.f * S);
		Turn(P, B.Head, AxisX, 8.f + 4.f * K);
		// Piernas: la izquierda va hacia delante con S > 0 y recoge la rodilla mientras avanza (C > 0).
		const float Stride = F.RunStride;
		Turn(P, B.LUp, AxisX, 6.f + Stride * S);
		Turn(P, B.RUp, AxisX, 6.f - Stride * S);
		Turn(P, B.LLeg, AxisX, -(14.f + 80.f * FMath::Pow(FMath::Max(0.f, C), 1.4f)));
		Turn(P, B.RLeg, AxisX, -(14.f + 80.f * FMath::Pow(FMath::Max(0.f, -C), 1.4f)));
		Turn(P, B.LFoot, AxisX, -25.f * FMath::Max(0.f, C));
		Turn(P, B.RFoot, AxisX, -25.f * FMath::Max(0.f, -C));
		// Brazos abajo, codos a unos 80° y braceo al revés que las piernas.
		const float Arm = 38.f + 10.f * K;
		Turn(P, B.LArm, AxisY, 74.f);
		Turn(P, B.RArm, AxisY, -74.f);
		Turn(P, B.LArm, AxisX, -Arm * S);
		Turn(P, B.RArm, AxisX, Arm * S);
		Turn(P, B.LFore, AxisX, 80.f);
		Turn(P, B.RFore, AxisX, 80.f);
		// Más alta en el vuelo (piernas abiertas) y más baja al pisar.
		Lift(P, B, 1.6f * FMath::Abs(S) - 0.4f);
	}

	/**
	 * Panzazo (el personaje ya tumba la malla -80° y la aplasta un poco): brazos por delante de la cabeza apoyados en
	 * el suelo, que reman hacia los lados, y piernas estiradas hacia atrás que patalean con los pies en el suelo. En la
	 * malla, +Y (la tripa) mira al suelo: los giros hacia +Y bajan brazos y piernas hasta apoyarlos.
	 */
	void PoseDive(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float Paddle = 0.5f + 0.5f * FMath::Sin(F.Clock * 11.f);
		Turn(P, B.LArm, AxisY, -86.f + 32.f * Paddle);
		Turn(P, B.RArm, AxisY, 86.f - 32.f * Paddle);
		Turn(P, B.LArm, AxisX, -20.f);
		Turn(P, B.RArm, AxisX, -20.f);
		Turn(P, B.LFore, AxisY, -12.f * Paddle);
		Turn(P, B.RFore, AxisY, 12.f * Paddle);
		const float Kick = 14.f * FMath::Sin(F.Clock * 14.f);
		Turn(P, B.LUp, AxisX, 14.f + Kick);
		Turn(P, B.RUp, AxisX, 14.f - Kick);
		Turn(P, B.LFoot, AxisX, -60.f);
		Turn(P, B.RFoot, AxisX, -60.f);
		Turn(P, B.Neck, AxisX, 24.f);
		Turn(P, B.Head, AxisX, 18.f);
	}

	/**
	 * Arrastre sobre la tripa (el personaje ya tumba la malla -80° y la aplasta): cabeza levantada mirando adelante,
	 * brazos abiertos por delante que rozan el suelo y tiemblan con los baches (casi parada, reman), piernas con las
	 * rodillas dobladas y los pies arriba pataleando, la espalda arqueada y el cuerpo que se balancea sobre la tripa. Más
	 * deprisa, más vibra; los golpes (caer de tripa, chocar) sacuden brazos, pies y cabeza.
	 *
	 * Los giros van en el espacio de la malla de pie: el eje largo del cuerpo es Z (tumbada, apunta hacia delante) y la
	 * tripa mira a +Y (tumbada, al suelo). Girar la cadera sobre Z la hace rodar sobre la tripa; sobre Y, culear.
	 */
	void PoseBellySlide(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float T = F.Clock;
		const float V = F.SlideSpeed;
		const float Imp = F.SlideImpact;
		// Casi parada: rema con los brazos (para levantarse o reptar).
		const float Row = 1.f - FMath::Clamp((V - 0.08f) / 0.25f, 0.f, 1.f);
		const float Stroke = Row * (0.5f + 0.5f * FMath::Sin(T * 7.f));

		// Balanceo: rueda sobre la tripa y culea un poco; más deprisa, más; con los golpes, un temblor rápido.
		const float Rock = (2.5f + 5.5f * V) * FMath::Sin(T * (4.5f + 4.f * V)) + 6.f * Imp * FMath::Sin(T * 23.f);
		const float Sway = (2.f + 4.f * V) * FMath::Sin(T * 2.3f + 0.7f);
		Turn(P, B.Hips, AxisZ, Rock);
		Turn(P, B.Hips, AxisY, Sway);
		// Espalda arqueada hacia atrás (+X): pecho y piernas arriba, la tripa en el suelo.
		Turn(P, B.Spine, AxisX, 7.f + 3.f * V);
		Turn(P, B.Spine1, AxisX, 4.f);

		// Cabeza arriba (mira adelante), mirando a los lados sin prisa; cabecea con los golpes.
		Turn(P, B.Neck, AxisX, 28.f + 6.f * V);
		Turn(P, B.Head, AxisX, 14.f + 9.f * Imp * FMath::Sin(T * 19.f));
		Turn(P, B.Head, AxisZ, (6.f + 6.f * (1.f - V)) * FMath::Sin(T * 1.7f));

		// Brazos abiertos por delante de la cabeza (arriba = -Y en el izquierdo) y apoyados (+Z, hacia la tripa); tiemblan
		// con los baches y, casi parada, reman hacia atrás.
		const float Jitter = (3.f + 9.f * V) * FMath::Sin(T * (13.f + 9.f * V)) + 22.f * Imp * FMath::Sin(T * 17.f);
		const float Spread = -58.f + 30.f * Stroke + Jitter;
		const float Press = 16.f - 10.f * Stroke;
		Turn(P, B.LArm, AxisY, Spread);
		Turn(P, B.RArm, AxisY, -Spread);
		Turn(P, B.LArm, AxisZ, Press);
		Turn(P, B.RArm, AxisZ, -Press);
		Turn(P, B.LFore, AxisZ, 12.f);
		Turn(P, B.RFore, AxisZ, -12.f);

		// Piernas algo abiertas, las rodillas dobladas con los pies arriba y pataleando (más rápido cuanto más deprisa).
		const float Kick = (5.f + 9.f * V) * FMath::Sin(T * (7.f + 6.f * V));
		const float KneeRate = 6.f + 5.f * V;
		const float Knee = 55.f + 18.f * Imp;
		Turn(P, B.LUp, AxisY, -10.f);
		Turn(P, B.RUp, AxisY, 10.f);
		Turn(P, B.LUp, AxisX, 6.f + Kick);
		Turn(P, B.RUp, AxisX, 6.f - Kick);
		Turn(P, B.LLeg, AxisX, -(Knee + 16.f * FMath::Sin(T * KneeRate)));
		Turn(P, B.RLeg, AxisX, -(Knee + 16.f * FMath::Sin(T * KneeRate + 2.1f)));
		Turn(P, B.LFoot, AxisX, -30.f);
		Turn(P, B.RFoot, AxisX, -30.f);
	}

	void PoseSwim(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float T = F.Clock * TwoPiF * UTN_TurtleAnimInstance::SwimStrokeHz;
		Turn(P, B.Spine, AxisX, -22.f);
		Turn(P, B.Head, AxisX, 24.f);
		Turn(P, B.LArm, AxisY, 12.f + 30.f * FMath::Cos(T));
		Turn(P, B.LArm, AxisZ, 50.f + 35.f * FMath::Sin(T));
		Turn(P, B.RArm, AxisY, -12.f - 30.f * FMath::Cos(T));
		Turn(P, B.RArm, AxisZ, -50.f - 35.f * FMath::Sin(T));
		Turn(P, B.LFore, AxisZ, 20.f * (1.f - FMath::Sin(T)));
		Turn(P, B.RFore, AxisZ, -20.f * (1.f - FMath::Sin(T)));
		Turn(P, B.LUp, AxisX, 25.f * FMath::Sin(3.f * T));
		Turn(P, B.RUp, AxisX, -25.f * FMath::Sin(3.f * T));
		Turn(P, B.LLeg, AxisX, -20.f);
		Turn(P, B.RLeg, AxisX, -20.f);
	}

	/** Lleva a otra tortuga en alto. */
	void PoseCarry(FCompactPose& P, const FBones& B)
	{
		Turn(P, B.LArm, AxisY, -80.f);
		Turn(P, B.LArm, AxisZ, 10.f);
		Turn(P, B.RArm, AxisY, 80.f);
		Turn(P, B.RArm, AxisZ, -10.f);
		Turn(P, B.LFore, AxisY, -25.f);
		Turn(P, B.RFore, AxisY, 25.f);
		Turn(P, B.Spine, AxisX, 4.f);
		Turn(P, B.LUp, AxisX, 10.f);
		Turn(P, B.RUp, AxisX, 10.f);
		Turn(P, B.LLeg, AxisX, -15.f);
		Turn(P, B.RLeg, AxisX, -15.f);
	}

	/** La llevan en alto y patalea. */
	void PoseCarried(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float T = F.Clock;
		Turn(P, B.LArm, AxisY, 20.f + 35.f * FMath::Sin(T * 12.f));
		Turn(P, B.LArm, AxisZ, 20.f * FMath::Cos(T * 9.f));
		Turn(P, B.RArm, AxisY, -20.f - 35.f * FMath::Sin(T * 12.f + 1.f));
		Turn(P, B.RArm, AxisZ, -20.f * FMath::Cos(T * 9.f + 0.5f));
		Turn(P, B.LUp, AxisX, 40.f * FMath::Sin(T * 10.f));
		Turn(P, B.RUp, AxisX, -40.f * FMath::Sin(T * 10.f));
		Turn(P, B.LLeg, AxisX, -30.f);
		Turn(P, B.RLeg, AxisX, -30.f);
		Turn(P, B.Head, AxisY, 15.f * FMath::Sin(T * 7.f));
	}

	// ── Aletas: lo que lleva, guardar o sacar del caparazón y lanzar ─────────
	// Los brazos se colocan desde la postura en T girando primero sobre Z (al frente: +Z el izquierdo, -Z el derecho) y
	// luego sobre X, que sube (+) o baja (-) el brazo ya puesto al frente; el antebrazo, sobre X, se dobla por el codo
	// hacia arriba (+). Solo se mezclan los huesos de cada brazo (BlendChainInto): las piernas siguen andando.

	/**
	 * En la aleta derecha: el brazo abajo con el codo junto a la cadera y el antebrazo al frente, como una bandeja (por un
	 * extremo, el antebrazo algo más alto), y la aleta un poco hacia el centro.
	 */
	void PoseHoldOne(FCompactPose& P, const FBones& B, bool bByEnd)
	{
		Turn(P, B.RArm, AxisZ, -98.f);
		Turn(P, B.RArm, AxisX, -72.f);
		Turn(P, B.RFore, AxisX, bByEnd ? 96.f : 80.f);
		Turn(P, B.RFore, AxisZ, -10.f);
	}

	/**
	 * Abrazado con las dos aletas: brazos al frente y abajo con los codos abiertos y los antebrazos que se cierran por
	 * delante rodeándolo. Open (0..1) lo abre para lo más ancho (lo ajusta el inventario midiendo las manos).
	 */
	void PoseHug(FCompactPose& P, const FBones& B, float Open)
	{
		const float Spread = FMath::Lerp(10.f, 38.f, Open);
		const float Wrap = FMath::Lerp(62.f, 22.f, Open);
		Turn(P, B.LArm, AxisZ, 90.f - Spread);
		Turn(P, B.RArm, AxisZ, -(90.f - Spread));
		Turn(P, B.LArm, AxisX, -40.f);
		Turn(P, B.RArm, AxisX, -40.f);
		Turn(P, B.LFore, AxisX, 38.f);
		Turn(P, B.RFore, AxisX, 38.f);
		Turn(P, B.LFore, AxisZ, Wrap);
		Turn(P, B.RFore, AxisZ, -Wrap);
	}

	/** La aleta derecha a la espalda por encima del hombro, a la altura del caparazón (guardar o sacar algo). */
	void PoseStashReach(FCompactPose& P, const FBones& B)
	{
		Turn(P, B.RArm, AxisZ, -65.f);
		Turn(P, B.RArm, AxisX, 140.f);
		Turn(P, B.RFore, AxisX, 60.f);
	}

	/** Un momento del saque de banda: altura del brazo, codo, cuánto se juntan las aletas, espalda y cabeza. */
	struct FThrowKey
	{
		float U;
		float Elev;
		float Bend;
		float In;
		float Spine;
		float Head;
	};

	/**
	 * Saque de banda por fases (U): de -1 a 0 toma impulso (de las aletas en alto, como al llevarla, a detrás de la cabeza
	 * con la espalda arqueada, y otra vez arriba, donde suelta); de 0 a 1 acompaña hacia delante y abajo con el cuerpo.
	 */
	FThrowKey ThrowKeyAt(float U)
	{
		static constexpr FThrowKey Keys[] = {
			{ -1.f,  95.f, 15.f,  8.f,   4.f,  2.f },
			{ -0.4f, 128.f, 58.f, 10.f,  12.f,  8.f },
			{  0.f,  98.f, 12.f, 12.f,   0.f,  3.f },
			{  0.5f, 18.f,  0.f, 10.f, -12.f, -4.f },
			{  1.f, -30.f,  0.f,  6.f,  -6.f, -2.f },
		};
		constexpr int32 Num = static_cast<int32>(UE_ARRAY_COUNT(Keys));
		if (U <= Keys[0].U) { return Keys[0]; }
		for (int32 k = 1; k < Num; ++k)
		{
			if (U <= Keys[k].U)
			{
				const FThrowKey& A = Keys[k - 1];
				const FThrowKey& C = Keys[k];
				const float X = (U - A.U) / FMath::Max(0.001f, C.U - A.U);
				const float S = X * X * (3.f - 2.f * X);
				return FThrowKey{ U, FMath::Lerp(A.Elev, C.Elev, S), FMath::Lerp(A.Bend, C.Bend, S), FMath::Lerp(A.In, C.In, S),
					FMath::Lerp(A.Spine, C.Spine, S), FMath::Lerp(A.Head, C.Head, S) };
			}
		}
		return Keys[Num - 1];
	}

	/** Los brazos del saque de banda en el momento K (las dos aletas o solo una). */
	void PoseThrowArms(FCompactPose& P, const FBones& B, const FThrowKey& K, bool bLeft, bool bRight)
	{
		if (bLeft)
		{
			Turn(P, B.LArm, AxisZ, 90.f + K.In);
			Turn(P, B.LArm, AxisX, K.Elev);
			Turn(P, B.LFore, AxisX, K.Bend);
		}
		if (bRight)
		{
			Turn(P, B.RArm, AxisZ, -(90.f + K.In));
			Turn(P, B.RArm, AxisX, K.Elev);
			Turn(P, B.RFore, AxisX, K.Bend);
		}
	}

	/**
	 * A mitad del levantarse: el tronco hacia delante, los brazos abajo y un poco delante empujando el suelo con los
	 * codos doblados, y las rodillas dobladas (cadera abajo).
	 */
	void PoseGetUpFlex(FCompactPose& P, const FBones& B)
	{
		Turn(P, B.Spine, AxisX, -28.f);
		Turn(P, B.Head, AxisX, 14.f);
		Turn(P, B.LArm, AxisY, 72.f);
		Turn(P, B.RArm, AxisY, -72.f);
		Turn(P, B.LArm, AxisZ, 28.f);
		Turn(P, B.RArm, AxisZ, -28.f);
		Turn(P, B.LFore, AxisZ, 35.f);
		Turn(P, B.RFore, AxisZ, -35.f);
		Turn(P, B.LUp, AxisX, 55.f);
		Turn(P, B.RUp, AxisX, 45.f);
		Turn(P, B.LLeg, AxisX, -85.f);
		Turn(P, B.RLeg, AxisX, -75.f);
		Lift(P, B, -6.f);
	}

	/** Tumbada: brazos y patas flojos y abiertos, la cabeza caída a un lado. */
	void PoseDown(FCompactPose& P, const FBones& B)
	{
		Turn(P, B.LArm, AxisY, 22.f);
		Turn(P, B.LArm, AxisZ, -10.f);
		Turn(P, B.RArm, AxisY, -22.f);
		Turn(P, B.RArm, AxisZ, 10.f);
		Turn(P, B.LUp, AxisY, -20.f);
		Turn(P, B.RUp, AxisY, 20.f);
		Turn(P, B.Head, AxisY, 25.f);
		Turn(P, B.Head, AxisX, -10.f);
	}

	/** Emotes del catálogo (0-9) salvo la fiesta (9), que es el clip de gritar (con rebote aparte). */
	void PoseEmote(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float T = F.EmoteTime;
		switch (F.Emote)
		{
		case 0: // WAZAAA: brazo derecho en alto (por fuera de la cabeza) y la mano que saluda.
			ArmsRelaxed(P, B);
			Turn(P, B.RArm, AxisY, 68.f + 58.f);
			Turn(P, B.RArm, AxisZ, -10.f);
			Turn(P, B.RFore, AxisY, -6.f + 20.f * FMath::Sin(T * 14.f));
			Turn(P, B.Head, AxisY, -8.f);
			break;
		case 1: // HAPPIE: brazos arriba y palmadas encima de la cabeza (las manos se juntan en cada una).
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 16.f);
			Turn(P, B.LArm, AxisY, -78.f);
			Turn(P, B.LArm, AxisZ, -10.f);
			Turn(P, B.RArm, AxisY, 78.f);
			Turn(P, B.RArm, AxisZ, 10.f);
			Turn(P, B.LFore, AxisY, -(26.f + 14.f * Pulse));
			Turn(P, B.RFore, AxisY, 26.f + 14.f * Pulse);
			Lift(P, B, 1.5f * FMath::Abs(FMath::Sin(T * 8.f)));
			break;
		}
		case 2: // PARACOPTER: brazos en cruz y el cuerpo de arriba girando sin parar (tres vueltas por segundo).
			Turn(P, B.Spine, AxisZ, FMath::Fmod(T * 1080.f, 360.f));
			Lift(P, B, 2.f * FMath::Abs(FMath::Sin(T * 6.f)));
			break;
		case 3: // SAX-O: brazos delante y una palmada fuerte cada 0,7 s con las manos juntas en el centro.
		{
			const float Phase = FMath::Fmod(T, 0.7f) / 0.7f;
			const float Clap = Phase < 0.25f ? Phase / 0.25f : FMath::Max(0.f, 1.f - (Phase - 0.25f) / 0.5f);
			Turn(P, B.LArm, AxisY, 12.f);
			Turn(P, B.LArm, AxisZ, 60.f + 42.f * Clap);
			Turn(P, B.RArm, AxisY, -12.f);
			Turn(P, B.RArm, AxisZ, -60.f - 42.f * Clap);
			Turn(P, B.Spine, AxisX, -8.f * Clap);
			break;
		}
		case 4: // Aplaudir: palmaditas rápidas delante del pecho.
			Turn(P, B.LArm, AxisY, 35.f);
			Turn(P, B.LArm, AxisZ, 70.f + 15.f * FMath::Sin(T * 18.f));
			Turn(P, B.RArm, AxisY, -35.f);
			Turn(P, B.RArm, AxisZ, -70.f - 15.f * FMath::Sin(T * 18.f));
			Turn(P, B.LFore, AxisZ, 40.f);
			Turn(P, B.RFore, AxisZ, -40.f);
			break;
		case 5: // RUN: carrera ninja, el tronco hacia delante, los brazos estirados hacia atrás y rodillas arriba.
		{
			const float S = FMath::Sin(T * TwoPiF * 2.5f);
			Turn(P, B.Spine, AxisX, -20.f);
			Turn(P, B.Head, AxisX, 16.f);
			Turn(P, B.LArm, AxisZ, -78.f);
			Turn(P, B.LArm, AxisX, 18.f);
			Turn(P, B.RArm, AxisZ, 78.f);
			Turn(P, B.RArm, AxisX, 18.f);
			Turn(P, B.LUp, AxisX, 50.f * FMath::Max(0.f, S));
			Turn(P, B.RUp, AxisX, 50.f * FMath::Max(0.f, -S));
			Turn(P, B.LLeg, AxisX, -20.f * FMath::Max(0.f, S));
			Turn(P, B.RLeg, AxisX, -20.f * FMath::Max(0.f, -S));
			Lift(P, B, 2.f * FMath::Abs(S));
			break;
		}
		case 6: // SUPERKIRK: vuela como Superman, todo el cuerpo inclinado 45°, brazos al frente en V y pies en punta.
		{
			Turn(P, B.LArm, AxisY, -65.f);
			Turn(P, B.LArm, AxisZ, 10.f);
			Turn(P, B.RArm, AxisY, 65.f);
			Turn(P, B.RArm, AxisZ, -10.f);
			const float Kick = 6.f * FMath::Sin(T * 5.f);
			Turn(P, B.LUp, AxisX, Kick);
			Turn(P, B.RUp, AxisX, -Kick);
			Turn(P, B.LFoot, AxisX, -60.f);
			Turn(P, B.RFoot, AxisX, -60.f);
			Turn(P, B.Neck, AxisX, 14.f);
			Turn(P, B.Head, AxisX, 10.f);
			Turn(P, B.Hips, AxisX, -45.f);
			Lift(P, B, 8.f + 2.f * FMath::Sin(T * 3.f));
			break;
		}
		case 7: // Señalar: brazo derecho al frente.
			ArmsRelaxed(P, B);
			Turn(P, B.RArm, AxisY, -68.f + 10.f);
			Turn(P, B.RArm, AxisZ, -85.f);
			Turn(P, B.Head, AxisZ, -10.f);
			break;
		case 8: // Modo loco: todo se mueve a su aire.
			Turn(P, B.LArm, AxisY, 60.f * FMath::Sin(T * 7.f));
			Turn(P, B.LArm, AxisZ, 50.f * FMath::Sin(T * 5.3f));
			Turn(P, B.RArm, AxisY, -60.f * FMath::Sin(T * 6.1f + 1.f));
			Turn(P, B.RArm, AxisZ, -50.f * FMath::Sin(T * 4.7f));
			Turn(P, B.LUp, AxisX, 45.f * FMath::Sin(T * 8.2f));
			Turn(P, B.RUp, AxisX, -45.f * FMath::Sin(T * 7.7f));
			Turn(P, B.Spine, AxisZ, 30.f * FMath::Sin(T * 3.f));
			Turn(P, B.Head, AxisX, 25.f * FMath::Sin(T * 9.f));
			break;
		default:
			break;
		}
	}

	// ── Modo carrera: celebraciones del podio y zambullida del acantilado de la meta ──

	/** Segundos de cada bucle de celebración: la pose vuelve exactamente al principio al acabar, como un GIF. */
	constexpr float TrophyLoop = 1.6f;
	constexpr float DisappointedLoop = 3.2f;
	constexpr float TantrumLoop = 1.2f;

	/** Unidades de la malla que baja la cadera para sentarse si la postura de referencia no da una altura razonable. */
	constexpr float SitDropFallback = 18.f;

	/** Zambullida: segundos tras empezar a caer en los que aún puede empezar y giro del cuerpo (grados) mínimo y máximo. */
	constexpr float CliffDiveStartWindow = 0.35f;
	/**
	 * Pasada esa ventana, también empieza si cae deprisa (cm/s) dentro de la zona: la caída que pasa por el vacío sobre el
	 * agua (p. ej. lanzada desde más atrás) entra igualmente de cabeza. Un salto que vuelve a la repisa no llega a tanto.
	 */
	constexpr float CliffDiveLateFallSpeed = 1000.f;
	constexpr float CliffDiveMinPitch = 40.f;
	constexpr float CliffDiveMaxPitch = 165.f;

	/**
	 * Trofeo: los dos brazos arriba con las manos juntas sobre la cabeza sujetando la concha (el podio la pone entre las
	 * manos), dos saltitos por vuelta en los que estira los brazos para subirla, el pecho fuera, la cabeza mirándola y
	 * un meneo de lado a lado.
	 */
	void PoseTrophy(FCompactPose& P, const FBones& B, float T)
	{
		const float Phase = FMath::Fmod(T, TrophyLoop) / TrophyLoop;
		const float Hop = FMath::Abs(FMath::Sin(Phase * TwoPiF));
		const float Land = 1.f - Hop;
		// Brazos arriba (como las palmadas de HAPPIE) con los codos hacia dentro: las manos se juntan sobre la cabeza.
		Turn(P, B.LArm, AxisY, -(72.f + 12.f * Hop));
		Turn(P, B.RArm, AxisY, 72.f + 12.f * Hop);
		Turn(P, B.LArm, AxisZ, -8.f);
		Turn(P, B.RArm, AxisZ, 8.f);
		Turn(P, B.LFore, AxisY, -(38.f - 8.f * Hop));
		Turn(P, B.RFore, AxisY, 38.f - 8.f * Hop);
		// Pecho fuera, la cabeza mirando la concha y un meneo de lado a lado (ida y vuelta en cada bucle).
		Turn(P, B.Spine, AxisX, 5.f);
		Turn(P, B.Spine1, AxisX, 3.f);
		Turn(P, B.Spine, AxisZ, 8.f * FMath::Sin(Phase * TwoPiF));
		Turn(P, B.Neck, AxisX, 8.f);
		Turn(P, B.Head, AxisX, 14.f + 4.f * Hop);
		// Rodillas que se doblan al caer de cada saltito.
		Turn(P, B.LUp, AxisX, 16.f * Land);
		Turn(P, B.RUp, AxisX, 16.f * Land);
		Turn(P, B.LLeg, AxisX, -30.f * Land);
		Turn(P, B.RLeg, AxisX, -30.f * Land);
		Turn(P, B.LFoot, AxisX, 14.f * Land);
		Turn(P, B.RFoot, AxisX, 14.f * Land);
		Lift(P, B, 4.f * Hop - 0.7f * Land);
	}

	/**
	 * Decepcionada: hombros caídos, brazos colgando flojos, espalda encorvada y cabeza gacha. En cada vuelta coge aire
	 * (el pecho y la cabeza suben), lo suelta de golpe en un suspiro y se hunde más; luego niega despacio con la cabeza
	 * y arrastra un pie por la arena.
	 */
	void PoseDisappointed(FCompactPose& P, const FBones& B, float T)
	{
		const float U = FMath::Fmod(T, DisappointedLoop);
		const float Breath = U < 0.9f ? FMath::Sin(U / 0.9f * HALF_PI) : FMath::Max(0.f, 1.f - (U - 0.9f) / 0.6f);
		const float Slump = 1.f - 0.7f * Breath;
		ArmsRelaxed(P, B, 72.f);
		Turn(P, B.LShoulder, AxisY, 14.f * Slump);
		Turn(P, B.RShoulder, AxisY, -14.f * Slump);
		Turn(P, B.LArm, AxisZ, 12.f * Slump);
		Turn(P, B.RArm, AxisZ, -12.f * Slump);
		Turn(P, B.LFore, AxisZ, 10.f);
		Turn(P, B.RFore, AxisZ, -10.f);
		Turn(P, B.Spine, AxisX, -12.f * Slump + 4.f * Breath);
		Turn(P, B.Spine1, AxisX, -6.f * Slump);
		Turn(P, B.Neck, AxisX, -14.f * Slump);
		Turn(P, B.Head, AxisX, -16.f * Slump + 10.f * Breath);
		// «No puede ser»: tras el suspiro, una vez a cada lado hasta el final del bucle.
		const float Shake = U > 1.5f ? FMath::Sin((U - 1.5f) / (DisappointedLoop - 1.5f) * TwoPiF) : 0.f;
		Turn(P, B.Head, AxisZ, 12.f * Shake);
		const float Scuff = (U > 2.3f && U < 3.f) ? FMath::Sin((U - 2.3f) / 0.7f * PI) : 0.f;
		Turn(P, B.LUp, AxisX, 3.f * Slump);
		Turn(P, B.RUp, AxisX, 3.f * Slump + 12.f * Scuff);
		Turn(P, B.LLeg, AxisX, -6.f * Slump);
		Turn(P, B.RLeg, AxisX, -6.f * Slump - 10.f * Scuff);
		Turn(P, B.RFoot, AxisX, -15.f * Scuff);
		Lift(P, B, 0.4f * Breath - 0.3f * Slump);
	}

	/**
	 * Pataleta: sentada en el suelo (la cadera baja hasta apoyar el culete), echada un poco hacia atrás y meciéndose,
	 * con las piernas estiradas al frente pataleando alternas (los talones golpean el suelo), los puños aporreando el
	 * suelo a los lados, la barbilla arriba gritando y sacudiendo la cabeza.
	 */
	void PoseTantrum(FCompactPose& P, const FBones& B, float T)
	{
		const float Rate = TwoPiF / TantrumLoop;
		float Drop = SitDropFallback;
		if (B.Hips.IsValid())
		{
			const float HipZ = static_cast<float>(P.GetBoneContainer().GetRefPoseTransform(B.Hips).GetTranslation().Z);
			if (HipZ > 10.f && HipZ < 40.f) { Drop = HipZ - 5.f; }
		}
		Lift(P, B, -Drop + 0.8f * FMath::Abs(FMath::Sin(T * Rate * 3.f)));
		Turn(P, B.Spine, AxisX, 10.f + 6.f * FMath::Sin(T * Rate));
		const float KickL = FMath::Max(0.f, FMath::Sin(T * Rate * 3.f));
		const float KickR = FMath::Max(0.f, -FMath::Sin(T * Rate * 3.f));
		Turn(P, B.LUp, AxisY, -12.f);
		Turn(P, B.RUp, AxisY, 12.f);
		Turn(P, B.LUp, AxisX, 80.f + 22.f * KickL);
		Turn(P, B.RUp, AxisX, 80.f + 22.f * KickR);
		Turn(P, B.LLeg, AxisX, -(6.f + 26.f * KickL));
		Turn(P, B.RLeg, AxisX, -(6.f + 26.f * KickR));
		Turn(P, B.LFoot, AxisX, 12.f);
		Turn(P, B.RFoot, AxisX, 12.f);
		// Puños: el brazo baja estirándose contra el suelo y vuelve a subir con el codo doblado, uno y otro.
		const float PoundL = FMath::Max(0.f, FMath::Sin(T * Rate * 2.f));
		const float PoundR = FMath::Max(0.f, -FMath::Sin(T * Rate * 2.f));
		Turn(P, B.LArm, AxisY, 30.f + 40.f * PoundL);
		Turn(P, B.RArm, AxisY, -(30.f + 40.f * PoundR));
		Turn(P, B.LArm, AxisZ, 18.f);
		Turn(P, B.RArm, AxisZ, -18.f);
		Turn(P, B.LFore, AxisZ, 55.f - 40.f * PoundL);
		Turn(P, B.RFore, AxisZ, -(55.f - 40.f * PoundR));
		Turn(P, B.Neck, AxisX, 6.f);
		Turn(P, B.Head, AxisX, 10.f);
		Turn(P, B.Head, AxisZ, 16.f * FMath::Sin(T * Rate * 2.f));
		Turn(P, B.Head, AxisY, 6.f * FMath::Sin(T * Rate));
	}

	void PoseCelebration(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		switch (F.Celebration)
		{
		case ETNTurtleCelebration::Trophy:       PoseTrophy(P, B, F.CelebrationTime); break;
		case ETNTurtleCelebration::Disappointed: PoseDisappointed(P, B, F.CelebrationTime); break;
		case ETNTurtleCelebration::Tantrum:      PoseTantrum(P, B, F.CelebrationTime); break;
		default: break;
		}
	}

	/**
	 * Zambullida de cabeza desde el acantilado de la meta: cuerpo estirado con los brazos por encima de la cabeza y las
	 * manos juntas (por delante al girar), la cabeza entre los brazos y las piernas juntas y estiradas hacia atrás con
	 * las puntas de los pies. Al final, todo el cuerpo gira hacia delante sobre la cadera (CliffDivePitch): -X lleva la
	 * cabeza hacia +Y (delante) y hacia abajo.
	 */
	void PoseCliffDive(FCompactPose& P, const FBones& B, const FTNTurtleAnimFrame& F)
	{
		const float T = F.CliffDiveTime;
		const float Flutter = 2.f * FMath::Sin(T * 23.f);
		Turn(P, B.LArm, AxisY, -104.f + Flutter);
		Turn(P, B.RArm, AxisY, 104.f - Flutter);
		Turn(P, B.LArm, AxisZ, 6.f);
		Turn(P, B.RArm, AxisZ, -6.f);
		Turn(P, B.Neck, AxisX, -6.f);
		Turn(P, B.Head, AxisX, -8.f);
		Turn(P, B.LUp, AxisY, 4.f);
		Turn(P, B.RUp, AxisY, -4.f);
		Turn(P, B.LUp, AxisX, -6.f + 3.f * FMath::Sin(T * 17.f));
		Turn(P, B.RUp, AxisX, -6.f - 3.f * FMath::Sin(T * 17.f));
		Turn(P, B.LFoot, AxisX, -55.f);
		Turn(P, B.RFoot, AxisX, -55.f);
		Turn(P, B.Hips, AxisX, -F.CliffDivePitch);
	}

	/**
	 * Si WorldLocation está en la zona del borde del acantilado de la meta: lo dice el generador de la playa
	 * (ATN_BeachRaceGenerator::IsCliffJumpZone, modo carrera), que se busca una vez y se guarda con un puntero débil.
	 * Fuera de la playa no hay generador (se vuelve a buscar cada 5 s) y nunca hay zambullida.
	 */
	bool IsCliffJumpZone(UWorld* World, TWeakObjectPtr<AActor>& Cache, double& NextLookup, const FVector& WorldLocation)
	{
		if (!World) { return false; }
		ATN_BeachRaceGenerator* Generator = Cast<ATN_BeachRaceGenerator>(Cache.Get());
		if (!Generator)
		{
			const double Now = World->GetTimeSeconds();
			if (Now < NextLookup) { return false; }
			NextLookup = Now + 5.0;
			for (TActorIterator<ATN_BeachRaceGenerator> It(World); It; ++It)
			{
				Generator = *It;
				break;
			}
			Cache = Generator;
			if (!Generator) { return false; }
		}
		return Generator->IsCliffJumpZone(WorldLocation);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Evaluación
// ─────────────────────────────────────────────────────────────────────────────

bool FTNTurtleAnimProxy::Evaluate(FPoseContext& Output)
{
	using namespace TNTurtleAnim;
	const FTNTurtleAnimFrame& F = Frame;
	const FBones B = ResolveBones(Output.Pose.GetBoneContainer());

	// 1. Locomoción: espera y andar con los clips (como ABS_Walk, por velocidad) y la carrera del sprint encima.
	SampleClip(IdleClip, F.IdleTime, Output);
	if (F.WalkW > 0.01f)
	{
		FPoseContext Walk(Output);
		// El ciclo de andar ya cierra (primer y último fotograma iguales): bucle simple, sin fundido.
		SampleClip(WalkClip, F.WalkTime, Walk, false);
		// Zancada más larga que la del clip (las patas de la tortuga son cortas): con su ritmo, los pies no patinan.
		Amplify(Walk.Pose, B.LUp, 1.3);
		Amplify(Walk.Pose, B.RUp, 1.3);
		Amplify(Walk.Pose, B.LLeg, 1.15);
		Amplify(Walk.Pose, B.RLeg, 1.15);
		BlendInto(Output.Pose, Walk.Pose, F.WalkW);
	}
	KeepHipsInPlace(Output.Pose, B);
	if (F.RunW > 0.01f)
	{
		FPoseContext Run(Output);
		Run.ResetToRefPose();
		PoseRun(Run.Pose, B, F);
		BlendInto(Output.Pose, Run.Pose, F.RunW);
	}

	// 2. La fiesta es el clip de gritar (con rebote). Si se cambia a otro emote, el anterior se funde encima del nuevo.
	auto CheerLayer = [&](float Weight, float Time)
	{
		if (Weight < 0.01f || !CheerClip) { return; }
		FPoseContext Cheer(Output);
		SampleClip(CheerClip, Time, Cheer);
		KeepHipsInPlace(Cheer.Pose, B);
		Lift(Cheer.Pose, B, 2.f * FMath::Abs(FMath::Sin(Time * TwoPiF * 2.f)));
		BlendInto(Output.Pose, Cheer.Pose, Weight);
	};
	if (F.PrevEmote == 9) { CheerLayer(F.PrevEmoteW, F.PrevEmoteTime); }
	if (F.Emote == 9) { CheerLayer(F.EmoteW, F.EmoteTime); }

	// 3. Poses de estado sobre la postura en T, mezcladas por su peso.
	auto Layer = [&](float Weight, TFunctionRef<void(FCompactPose&)> Build)
	{
		if (Weight < 0.01f) { return; }
		FPoseContext Target(Output);
		Target.ResetToRefPose();
		Build(Target.Pose);
		BlendInto(Output.Pose, Target.Pose, Weight);
	};
	Layer(F.AirW, [&](FCompactPose& P) { PoseAir(P, B, F); });
	Layer(F.SwimW, [&](FCompactPose& P) { PoseSwim(P, B, F); });
	// Panzazo: en el aire, la pose de vuelo; sobre la tripa, la del arrastre (se reparten el peso por SlideW). Son dos
	// mezclas seguidas: la segunda se queda con SlideMix y a la primera le toca lo suyo de lo que quede.
	if (F.DiveW >= 0.01f)
	{
		const float SlideMix = F.DiveW * F.SlideW;
		const float AirMix = F.DiveW - SlideMix;
		const float FirstW = SlideMix < 0.999f ? AirMix / (1.f - SlideMix) : 0.f;
		Layer(FirstW, [&](FCompactPose& P) { PoseDive(P, B, F); });
		Layer(SlideMix, [&](FCompactPose& P) { PoseBellySlide(P, B, F); });
	}
	Layer(F.CarryW, [&](FCompactPose& P) { PoseCarry(P, B); });
	Layer(F.CarriedW, [&](FCompactPose& P) { PoseCarried(P, B, F); });
	Layer(F.DownW, [&](FCompactPose& P) { PoseDown(P, B); });
	if (F.PrevEmote >= 0 && F.PrevEmote != 9)
	{
		FTNTurtleAnimFrame PrevFrame = F;
		PrevFrame.Emote = F.PrevEmote;
		PrevFrame.EmoteTime = F.PrevEmoteTime;
		Layer(F.PrevEmoteW, [&](FCompactPose& P) { PoseEmote(P, B, PrevFrame); });
	}
	if (F.Emote >= 0 && F.Emote != 9)
	{
		Layer(F.EmoteW, [&](FCompactPose& P) { PoseEmote(P, B, F); });
	}
	// Modo carrera: zambullida de cabeza desde el acantilado de la meta y celebraciones del podio.
	Layer(F.CliffDiveW, [&](FCompactPose& P) { PoseCliffDive(P, B, F); });
	Layer(F.CelebrationW, [&](FCompactPose& P) { PoseCelebration(P, B, F); });

	// 3b. Levantarse del derribo: parte de la pose en la que quedó el ragdoll y llega a la de pie, pasando por un
	// empujón de brazos contra el suelo y las rodillas dobladas. Al levantarse de la tripa tras el panzazo, el mismo
	// empujón mientras el personaje endereza la malla.
	Layer(F.GetUpFlex, [&](FCompactPose& P) { PoseGetUpFlex(P, B); });
	Layer(F.BellyGetUpW, [&](FCompactPose& P) { PoseGetUpFlex(P, B); });
	if (F.GetUpW > 0.01f && GetUpPose.Num() > 0)
	{
		FPoseContext Ground(Output);
		Ground.ResetToRefPose();
		const FBoneContainer& Container = Ground.Pose.GetBoneContainer();
		for (const FCompactPoseBoneIndex I : Ground.Pose.ForEachBoneIndex())
		{
			const int32 MeshIndex = Container.MakeMeshPoseIndex(I).GetInt();
			if (GetUpPose.IsValidIndex(MeshIndex)) { Ground.Pose[I] = GetUpPose[MeshIndex]; }
		}
		BlendInto(Output.Pose, Ground.Pose, F.GetUpW);
	}

	// 3c. Aletas: los brazos que sujetan lo que lleva, la aleta a la espalda para guardar o sacar algo del caparazón y el
	// saque de banda. Solo los brazos (desde el hombro): el resto sigue con lo de arriba.
	const FCompactPoseBoneIndex LChain = B.LShoulder.IsValid() ? B.LShoulder : B.LArm;
	const FCompactPoseBoneIndex RChain = B.RShoulder.IsValid() ? B.RShoulder : B.RArm;
	auto ArmLayer = [&](float Weight, bool bLeftArm, bool bRightArm, TFunctionRef<void(FCompactPose&)> Build)
	{
		if (Weight < 0.01f) { return; }
		FPoseContext Target(Output);
		Target.ResetToRefPose();
		Build(Target.Pose);
		if (bLeftArm) { BlendChainInto(Output.Pose, Target.Pose, LChain, Weight); }
		if (bRightArm) { BlendChainInto(Output.Pose, Target.Pose, RChain, Weight); }
	};
	if (F.HoldStyle != 0 && F.HoldW >= 0.01f)
	{
		const bool bHug = F.HoldStyle == 2;
		ArmLayer(F.HoldW, bHug, true, [&](FCompactPose& P)
		{
			if (bHug) { PoseHug(P, B, F.HoldOpen); }
			else { PoseHoldOne(P, B, F.HoldStyle == 3); }
		});
		// Abrazando algo grande, el cuerpo se echa un poco atrás.
		if (bHug) { Turn(Output.Pose, B.Spine, AxisX, 3.f * F.HoldW); }
	}
	if (F.StashW >= 0.01f)
	{
		ArmLayer(F.StashW, false, true, [&](FCompactPose& P) { PoseStashReach(P, B); });
		// El pecho se gira un poco hacia ese lado y mira por encima del hombro.
		Turn(Output.Pose, B.Spine1, AxisZ, 10.f * F.StashW);
		Turn(Output.Pose, B.Head, AxisZ, 18.f * F.StashW);
	}
	if (F.ThrowW >= 0.01f)
	{
		const FThrowKey Key = ThrowKeyAt(F.ThrowU);
		ArmLayer(F.ThrowW, F.bThrowBoth, true, [&](FCompactPose& P) { PoseThrowArms(P, B, Key, F.bThrowBoth, true); });
		const float BodyShare = (F.bThrowBoth ? 1.f : 0.6f) * F.ThrowW;
		Turn(Output.Pose, B.Spine, AxisX, Key.Spine * BodyShare);
		Turn(Output.Pose, B.Head, AxisX, Key.Head * BodyShare);
	}

	// 4. Capas encima de lo que haya: inclinación al correr y en las curvas, cansancio y caparazón.
	Turn(Output.Pose, B.Spine, AxisX, F.LeanPitch);
	Turn(Output.Pose, B.Spine, AxisY, F.LeanRoll);
	if (F.TiredW > 0.01f)
	{
		Turn(Output.Pose, B.Spine1, AxisX, -12.f * F.TiredW);
		Turn(Output.Pose, B.Spine2, AxisX, -3.f * FMath::Sin(F.Clock * 5.f) * F.TiredW);
		Turn(Output.Pose, B.Head, AxisX, -10.f * F.TiredW);
	}
	if (F.ShellW > 0.01f)
	{
		// Se mete en el caparazón: cabeza, brazos y patas encogen y se meten dentro del cuerpo (hacia el centro del
		// tronco) y el caparazón baja hasta apoyarse en el suelo.
		const FVector Tiny(FMath::Lerp(1.f, 0.08f, F.ShellW));
		const FVector Inside = B.Spine1.IsValid() ? ComponentSpace(Output.Pose, B.Spine1).GetLocation() : FVector(0.0, 1.0, 29.0);
		for (const FCompactPoseBoneIndex Limb : { B.LArm, B.RArm, B.LUp, B.RUp, B.Neck })
		{
			if (!Limb.IsValid()) { continue; }
			MoveJointToward(Output.Pose, Limb, Inside, 0.85f * F.ShellW);
			Output.Pose[Limb].SetScale3D(Tiny);
		}
		Turn(Output.Pose, B.Spine, AxisX, -6.f * F.ShellW);
		// De pie, el caparazón baja hasta el suelo. Con cuerpo físico la malla ya va tumbada sobre la caja (ese eje apunta
		// hacia delante), así que no se baja.
		if (!F.bShellBody)
		{
			Lift(Output.Pose, B, -19.f * F.ShellW);
		}
	}

	// 5. VR: las manos del cuerpo van a los mandos (el que coge es la mano, no el cuerpo).
	ReachArm(Output.Pose, B.LArm, B.LFore, B.LHand, F.VRHandL, F.VRArmLW);
	ReachArm(Output.Pose, B.RArm, B.RFore, B.RHand, F.VRHandR, F.VRArmRW);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hilo de juego
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	// Clips de la tortuga de los ajustes de arte (UTN_ArtSettings, «Tortuga|Animaciones»): con otro esqueleto, se cambian allí.
	IdleAnim = TNTurtleArt::GetClip(ETNTurtleClip::Idle);
	WalkAnim = TNTurtleArt::GetClip(ETNTurtleClip::Walk);
	CheerAnim = TNTurtleArt::GetClip(ETNTurtleClip::Cheer);
	if (const APawn* Owner = TryGetPawnOwner()) { PrevYaw = Owner->GetActorRotation().Yaw; }
}

FAnimInstanceProxy* UTN_TurtleAnimInstance::CreateAnimInstanceProxy()
{
	return new FTNTurtleAnimProxy(this);
}

void UTN_TurtleAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FTNTurtleAnimProxy*>(InProxy);
}

void UTN_TurtleAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	using namespace TNTurtleAnim;
	Super::NativeUpdateAnimation(DeltaSeconds);
	const float Dt = FMath::Max(DeltaSeconds, 1e-4f);
	FTNTurtleAnimFrame& F = Frame;
	F.Clock += Dt;
	F.IdleTime += Dt;

	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Move = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	const FVector Velocity = Turtle ? Turtle->GetVelocity() : FVector::ZeroVector;
	const float Speed = static_cast<float>(Velocity.Size2D());
	auto Ease = [Dt](float& Value, bool bOn, float Rate) { Value = FMath::FInterpTo(Value, bOn ? 1.f : 0.f, Dt, Rate); };

	// Locomoción: pesos por velocidad y fases que avanzan al ritmo de los pasos (andar hasta la velocidad normal,
	// 4,5 m/s; la carrera entra con el sprint).
	const USkeletalMeshComponent* SkelMesh = GetSkelMeshComponent();
	const float Scale = SkelMesh ? FMath::Max(0.1f, static_cast<float>(SkelMesh->GetComponentScale().Z)) : 2.5f;
	F.WalkW = FMath::FInterpTo(F.WalkW, FMath::Clamp(Speed / 150.f, 0.f, 1.f), Dt, 8.f);
	// La carrera entra con el sprint (o al pasar un 8 % de la velocidad de andar del nivel: en el lobby es 2 m/s).
	const UTN_StaminaComponent* StaminaComp = Turtle ? Turtle->GetStaminaComponent() : nullptr;
	const float WalkRef = StaminaComp ? FMath::Max(100.f, StaminaComp->GetWalkSpeed()) : 450.f;
	const bool bSprinting = StaminaComp && StaminaComp->IsSprinting() && Speed > WalkRef * 0.6f;
	const float RunTarget = FMath::Max(bSprinting ? 1.f : 0.f, FMath::Clamp((Speed - WalkRef * 1.08f) / (WalkRef * 0.3f), 0.f, 1.f));
	F.RunW = FMath::FInterpTo(F.RunW, RunTarget, Dt, 7.f);
	F.SprintW = FMath::FInterpTo(F.SprintW, FMath::Clamp((Speed - WalkRef * 1.3f) / (WalkRef * 0.6f), 0.f, 1.f), Dt, 4.f);
	F.WalkTime += Dt * FMath::Clamp(Speed / (WalkNaturalUnits * Scale), 0.5f, 4.5f);
	// Carrera: de 2 a 3,4 ciclos por segundo según la velocidad y la amplitud del paso que hace que el pie apoyado
	// barra el suelo a la velocidad del cuerpo (pierna de LegUnits * Scale).
	const float Cadence = FMath::Clamp(0.8f + Speed / 300.f, 2.f, 3.4f);
	F.RunTime += Dt * Cadence;
	F.RunStride = FMath::Clamp(FMath::RadiansToDegrees(Speed / (LegUnits * Scale * TwoPiF * Cadence)), 20.f, 48.f);

	const bool bSwim = Move && Move->IsSwimming();
	// Panzazo hasta que se levanta (el dueño y el servidor lo saben al momento; el resto, al acabar el panzazo).
	const bool bDive = Turtle && Turtle->IsBellyPoseActive();
	const bool bBellyGround = Turtle && Turtle->IsBellyOnGround();
	const bool bAir = Move && Move->IsFalling() && !bDive && !bSwim;
	const UTN_CarryComponent* Carry = Turtle ? Turtle->GetCarryComponent() : nullptr;
	const UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr;
	const bool bCarrying = Carry && Carry->IsCarrying();
	Ease(F.AirW, bAir, 12.f);
	F.Falling = FMath::FInterpTo(F.Falling, (bAir && Velocity.Z < -200.0) ? 1.f : 0.f, Dt, 6.f);
	Ease(F.DiveW, bDive, 14.f);
	Ease(F.SlideW, bBellyGround, 10.f);
	F.SlideSpeed = FMath::FInterpTo(F.SlideSpeed, bBellyGround ? FMath::Clamp(Speed / 700.f, 0.f, 1.f) : 0.f, Dt, 10.f);
	// Golpes sobre la tripa: al caer de tripa y al chocar arrastrándose (la velocidad cambia de golpe).
	const FVector2D BellyVelocity(Velocity.X, Velocity.Y);
	if (bBellyGround && bWasBellyAir)
	{
		F.SlideImpact = 1.f;
	}
	else if (bBellyGround && bWasBellyGround && (BellyVelocity - PrevBellyVelocity).Size() > 250.0)
	{
		F.SlideImpact = FMath::Max(F.SlideImpact, 0.8f);
	}
	F.SlideImpact = FMath::Max(0.f, F.SlideImpact - Dt * 3.5f);
	// Levantarse de la tripa: al acabarse la pose en el suelo (no de un brinco), empujón de brazos y rodillas mientras
	// el personaje endereza la malla.
	if (bWasBellyGround && !bDive && Move && !Move->IsFalling() && !bSwim && !(Turtle && Turtle->IsKnockedDown()))
	{
		BellyGetUpElapsed = 0.f;
	}
	if (BellyGetUpElapsed >= 0.f)
	{
		BellyGetUpElapsed += Dt;
		const float GetUpX = FMath::Clamp(BellyGetUpElapsed / 0.45f, 0.f, 1.f);
		F.BellyGetUpW = 0.85f * FMath::Sin(GetUpX * PI);
		if (GetUpX >= 1.f)
		{
			BellyGetUpElapsed = -1.f;
			F.BellyGetUpW = 0.f;
		}
	}
	bWasBellyGround = bBellyGround;
	bWasBellyAir = bDive && Move && Move->IsFalling();
	PrevBellyVelocity = BellyVelocity;
	Ease(F.SwimW, bSwim, 6.f);
	Ease(F.ShellW, Turtle && Turtle->IsInShell(), 10.f);
	const UTN_ShellComponent* ShellComp = Turtle ? Turtle->GetShellComponent() : nullptr;
	F.bShellBody = ShellComp && ShellComp->HasLocalBody();
Ease(F.CarryW, bCarrying, 8.f);
	Ease(F.CarriedW, Carry && Carry->IsBeingCarried(), 8.f);
	Ease(F.DownW, Turtle && Turtle->IsKnockedDown(), 6.f);
	Ease(F.TiredW, Stamina && Stamina->IsExhausted(), 4.f);

	// VR: las manos del cuerpo siguen a los mandos, salvo bailando, en el caparazón, tumbada o llevando a otra tortuga.
	{
		FVector HandL = FVector::ZeroVector;
		FVector HandR = FVector::ZeroVector;
		bool bHandL = false;
		bool bHandR = false;
		const USkeletalMeshComponent* SkelComp = GetSkelMeshComponent();
		const bool bArms = Turtle && SkelComp && Turtle->AreVRArmsFollowing() && Turtle->GetVRHandTargets(HandL, HandR, bHandL, bHandR);
		Ease(F.VRArmLW, bArms && bHandL, 10.f);
		Ease(F.VRArmRW, bArms && bHandR, 10.f);
		if (bArms)
		{
			// Las del dueño, tal cual (sin retraso en las gafas); las de los demás llegan a saltos (15 por segundo): suaves.
			const FTransform& ToWorld = SkelComp->GetComponentTransform();
			const bool bSmooth = !Turtle->IsLocallyControlled();
			auto Follow = [&](FVector& Current, const FVector& World, float Weight)
			{
				const FVector Target = ToWorld.InverseTransformPosition(World);
				Current = bSmooth && Weight > 0.05f ? FMath::VInterpTo(Current, Target, Dt, 18.f) : Target;
			};
			if (bHandL) { Follow(F.VRHandL, HandL, F.VRArmLW); }
			if (bHandR) { Follow(F.VRHandR, HandR, F.VRArmRW); }
		}
	}

	// Lanzamiento como un saque de banda: con la E, mientras la lleva en alto, las dos aletas toman impulso detrás de la
	// cabeza (lo marca UTN_CarryComponent) y, al soltarla (con la E o con el panzazo), acompañan hacia delante y abajo. Un
	// objeto (PlayThrow), con la aleta derecha: desde detrás de la cabeza hacia delante.
	const float Windup = (bCarrying && Carry) ? Carry->GetThrowWindupAlpha() : -1.f;
	if (bWasCarrying && !bCarrying)
	{
		ThrowFollowElapsed = 0.f;
		ThrowFollowFrom = 0.f;
		ThrowFollowSeconds = 0.35f;
		F.bThrowBoth = true;
	}
	if (bPendingThrow)
	{
		bPendingThrow = false;
		ThrowFollowElapsed = 0.f;
		ThrowFollowFrom = bPendingThrowBoth ? 0.f : -0.4f;
		ThrowFollowSeconds = bPendingThrowBoth ? 0.35f : 0.42f;
		F.bThrowBoth = bPendingThrowBoth;
	}
	bWasCarrying = bCarrying;
	if (Windup >= 0.f)
	{
		// Entra casi de golpe desde la pose de llevarla en alto (se parecen: las aletas ya están arriba).
		F.ThrowU = -1.f + Windup;
		F.ThrowW = FMath::FInterpTo(F.ThrowW, 1.f, Dt, 25.f);
		F.bThrowBoth = true;
		ThrowFollowElapsed = -1.f;
	}
	else if (ThrowFollowElapsed >= 0.f)
	{
		ThrowFollowElapsed += Dt;
		const float ThrowX = FMath::Clamp(ThrowFollowElapsed / FMath::Max(0.05f, ThrowFollowSeconds), 0.f, 1.f);
		F.ThrowU = FMath::Lerp(ThrowFollowFrom, 1.f, ThrowX);
		F.ThrowW = ThrowX < 0.7f ? 1.f : 1.f - (ThrowX - 0.7f) / 0.3f;
		if (ThrowX >= 1.f)
		{
			ThrowFollowElapsed = -1.f;
			F.ThrowW = 0.f;
		}
	}
	else
	{
		// Toma de impulso que no acabó en lanzamiento (se cortó): las aletas vuelven.
		Ease(F.ThrowW, false, 10.f);
	}

	// Emote: entra suave y, al acabar, sale suave con el último.
	const int32 Emote = Turtle ? Turtle->GetActiveEmoteIndex() : -1;
	// Al cambiar de emote, el anterior no desaparece de golpe: pasa a la capa de fundido y se apaga mientras entra el nuevo.
	if (F.PrevEmote >= 0)
	{
		F.PrevEmoteTime += Dt;
		Ease(F.PrevEmoteW, false, 7.f);
		if (F.PrevEmoteW < 0.01f) { F.PrevEmote = -1; }
	}
	if (Emote >= 0 && Emote <= 9)
	{
		if (Emote != LastEmote)
		{
			if (LastEmote >= 0 && F.EmoteW > 0.05f)
			{
				F.PrevEmote = LastEmote;
				F.PrevEmoteTime = F.EmoteTime;
				F.PrevEmoteW = F.EmoteW;
			}
			F.EmoteW = 0.f;
		}
		F.Emote = Emote;
		F.EmoteTime = Turtle->GetEmoteTime();
		LastEmote = Emote;
		Ease(F.EmoteW, true, 10.f);
	}
	else
	{
		Ease(F.EmoteW, false, 8.f);
		F.EmoteTime += Dt;
		if (F.EmoteW < 0.01f) { F.Emote = -1; LastEmote = -1; }
	}

	// Celebración del podio (modo carrera): si se pide otra, la que había sale antes de que entre la nueva.
	if (F.Celebration != WantedCelebration && F.CelebrationW < 0.02f)
	{
		F.Celebration = WantedCelebration;
		F.CelebrationTime = 0.f;
	}
	Ease(F.CelebrationW, F.Celebration != ETNTurtleCelebration::None && F.Celebration == WantedCelebration, 6.f);
	F.CelebrationTime += Dt;

	// Zambullida de cabeza desde el acantilado de la meta (modo carrera): al despegar o empezar a caer en la zona del
	// borde (la dice el generador de la playa), hasta aterrizar o tocar el agua. Cosmética y local en cada máquina, a
	// partir del movimiento replicado (sin RPC).
	const bool bFallingNow = Move && Move->IsFalling() && !bSwim;
	FallElapsed = (bFallingNow && bWasFallingForDive) ? FallElapsed + Dt : 0.f;
	const bool bCanCliffDive = bFallingNow && !bDive && Turtle && !Turtle->IsInShell() && !Turtle->IsKnockedDown()
		&& !(Carry && Carry->IsBeingCarried());
	if (!bCanCliffDive)
	{
		bCliffDive = false;
	}
	else if (!bCliffDive && (FallElapsed <= CliffDiveStartWindow || Velocity.Z < -CliffDiveLateFallSpeed)
		&& IsCliffJumpZone(GetWorld(), CliffZoneSource, NextCliffZoneLookup, Turtle->GetActorLocation()))
	{
		bCliffDive = true;
		F.CliffDiveTime = 0.f;
		F.CliffDivePitch = CliffDiveMinPitch;
	}
	bWasFallingForDive = bFallingNow;
	Ease(F.CliffDiveW, bCliffDive, bCliffDive ? 9.f : 14.f);
	if (bCliffDive)
	{
		// El cuerpo sigue la trayectoria: tumbado en lo alto del salto y casi vertical, cabeza abajo, al caer deprisa.
		F.CliffDiveTime += Dt;
		const float Along = 90.f + FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-Velocity.Z), FMath::Max(150.f, Speed)));
		F.CliffDivePitch = FMath::FInterpTo(F.CliffDivePitch, FMath::Clamp(Along, CliffDiveMinPitch, CliffDiveMaxPitch), Dt, 4.f);
	}

	// Lo que lleva en las aletas (UTN_InventoryComponent): los brazos lo sujetan andando, corriendo o saltando; abrazado
	// (grande), también nadando, en el panzazo o con un emote. Con las aletas en otra cosa, el objeto solo las sigue. Si
	// cambia la forma de sujetarlo, la de antes sale antes de que entre la nueva.
	{
		const UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
		const uint8 WantHold = Inventory ? static_cast<uint8>(Inventory->GetShownHold()) : 0;
		const bool bHandsFree = Turtle && !Turtle->IsKnockedDown() && !Turtle->IsInShell() && !bCarrying
			&& !(Carry && Carry->IsBeingCarried()) && GetUpDuration <= 0.f && !bCliffDive;
		const bool bArmsFree = bHandsFree && !bDive && !bSwim && Emote < 0;
		const bool bHugStyle = WantHold == static_cast<uint8>(ETNItemHold::Hug);
		const bool bWantHold = WantHold != 0 && (bHugStyle ? bHandsFree : bArmsFree);
		if (WantHold != F.HoldStyle)
		{
			Ease(F.HoldW, false, 14.f);
			if (F.HoldW < 0.03f) { F.HoldStyle = WantHold; }
		}
		else
		{
			Ease(F.HoldW, bWantHold, 10.f);
		}
		F.HoldOpen = Inventory ? Inventory->GetHugOpen() : 0.5f;
		F.StashW = Inventory ? Inventory->GetStashReach() : 0.f;
	}

	// Inclinación: hacia dentro de las curvas y un poco hacia delante al correr.
	const float Yaw = Turtle ? static_cast<float>(Turtle->GetActorRotation().Yaw) : PrevYaw;
	const float YawRate = FMath::FindDeltaAngleDegrees(PrevYaw, Yaw) / Dt;
	PrevYaw = Yaw;
	const float RollTarget = FMath::Clamp(-YawRate * 0.03f * FMath::Clamp(Speed / 400.f, 0.f, 1.f), -12.f, 12.f);
	F.LeanRoll = FMath::FInterpTo(F.LeanRoll, bSwim || bDive ? 0.f : RollTarget, Dt, 6.f);
	F.LeanPitch = FMath::FInterpTo(F.LeanPitch, -7.f * F.RunW, Dt, 4.f);

	// Levantarse: la pose del suelo pierde peso con una curva suave y el empujón de brazos sube y baja en medio.
	if (GetUpDuration > 0.f)
	{
		GetUpElapsed += Dt;
		const float X = FMath::Clamp(GetUpElapsed / GetUpDuration, 0.f, 1.f);
		F.GetUpW = 1.f - X * X * (3.f - 2.f * X);
		F.GetUpFlex = 0.8f * FMath::Sin(X * PI);
		if (X >= 1.f)
		{
			GetUpDuration = 0.f;
			F.GetUpW = 0.f;
			F.GetUpFlex = 0.f;
			GetUpPose.Reset();
		}
	}

	FTNTurtleAnimProxy& Proxy = GetProxyOnGameThread<FTNTurtleAnimProxy>();
	Proxy.Frame = F;
	Proxy.IdleClip = IdleAnim;
	Proxy.WalkClip = WalkAnim;
	Proxy.CheerClip = CheerAnim;
	// La pose del suelo solo viaja al proxy cuando cambia (al empezar y al acabar).
	if (GetUpPose.Num() > 0 && !bGetUpPoseSent)
	{
		Proxy.GetUpPose = GetUpPose;
		bGetUpPoseSent = true;
	}
	else if (GetUpPose.Num() == 0 && Proxy.GetUpPose.Num() > 0)
	{
		Proxy.GetUpPose.Reset();
	}
}

void UTN_TurtleAnimInstance::SetCelebration(ETNTurtleCelebration InCelebration)
{
	WantedCelebration = InCelebration;
}

void UTN_TurtleAnimInstance::PlayThrow(bool bBothFlippers)
{
	bPendingThrow = true;
	bPendingThrowBoth = bBothFlippers;
}

bool UTN_TurtleAnimInstance::SnapOutOfShellPose()
{
	if (Frame.ShellW <= 0.f)
	{
		return false;
	}
	Frame.ShellW = 0.f;
	// La evaluación lee la copia del proxy (la de NativeUpdateAnimation llega en el siguiente, y con la pausa no llega).
	GetProxyOnGameThread<FTNTurtleAnimProxy>().Frame.ShellW = 0.f;
	return true;
}

void UTN_TurtleAnimInstance::BeginGetUp(const TArray<FTransform>& LocalPose, float Seconds)
{
	GetUpPose = LocalPose;
	GetUpElapsed = 0.f;
	GetUpDuration = FMath::Max(0.1f, Seconds);
	bGetUpPoseSent = false;
	Frame.GetUpW = 1.f;
	Frame.GetUpFlex = 0.f;
}
