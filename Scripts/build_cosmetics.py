"""Crea el contenido de la tienda y el probador del lobby (cosméticos de verdad, no los prototipos de las estatuas).

Se ejecuta DENTRO del editor de Unreal:
    exec(open(r"<repo>/Scripts/build_cosmetics.py", encoding="utf-8").read())

Crea o rehace:
  /Game/Cosmetics/Materials
    - M_CosmeticVertexColor: cascos low-poly (color de vértice; el alfa del vértice es el brillo metálico).
    - M_TurtleBody: cuerpo y caparazón de la tortuga de demo (TotugaDemo_Rig, ranura "lambert4"). Como en esa malla
      el caparazón y el cuerpo comparten material, las zonas salen de la posición local antes del skinning:
      caparazón = detrás del torso entre la cintura y el cuello; barriga = delante del torso. Parámetros: BodyColor,
      BellyColor, BellyAmount, ShellColor, ShellColor2, ShellPattern (0 liso, 1 escamas, 2 lunares, 3 olas,
      4 estrellas, 5 grietas de lava, 6 ajedrez, 7 sandía), PatternScale, ShellShine, ShellGlow, ShellMatchBody.
      Ojos: máscara por posición (las dos esferas de los ojos, en (±4,47; 8,46; 46,06) con radio 3,98, solo el
      casquete que asoma) con EyeStyle (0 clásicos, 1 iris, 2 estrella, 3 corazón, 4 de dibujo, 5 espiral, 6 gato,
      7 galaxia), EyeColor, EyeColor2, EyeGlow y la animación EyeBlink (párpado) y EyeDizzy (espiral al noquear).
      Cara (la anima UTN_TurtleFaceComponent): EyeTired (párpados a media asta y mirada baja), EyeSqueeze (ojos
      apretados «>_<»), MouthOpen y MouthSmile (boca pintada alrededor del hueco de la malla bajo la nariz) y FaceBlush
      (colorete en los mofletes).
    - M_TurtleHelmetSlot: ranura "lambert2" (casco rojo de serie + lengua rígida). HideHelmet recorta el casco (con uno
      de la tienda) y HideTongue esconde la lengua de la malla (la del jugador es procedural); HelmetColor es el rojo
      de serie (el difuso de M_TortugaDemo).
    - M_TurtleFaceParts: lengua y gotas de sudor procedurales (color de vértice; el alfa es lo mojado: brillo).
  /Game/UI/Shop/M_UI_Preview: pinta en la UI la captura de la vista previa (SceneColorHDR: alfa invertido y sin
    curva de tono; Exposure la ajusta).
  /Game/Cosmetics/Helmets/SM_Helmet_<Id>: los cascos de Scripts/cosmetics_meshes.py.
  DT_Helmets y DT_Skins (/Game/Blueprints/Gameplay/Cosmetics): catálogo de la tienda (cascos gratis; skins de
  cosmetics_skins.py con rareza y precio en puntos, #873); solo si el C++ ya tiene las columnas de FTN_SkinData
  (Color, Pattern, Rarity...).

Los colores se escriben en sRGB hexadecimal y se pasan a lineal.
"""

import importlib
import math
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else r"C:\Users\mokiu\Documents\Unreal Projects\Tortunabo\Scripts"
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import cosmetics_meshes as CM  # noqa: E402
import cosmetics_skins as CS  # noqa: E402

importlib.reload(CM)
importlib.reload(CS)

MAT_FOLDER = "/Game/Cosmetics/Materials"
HELMET_FOLDER = "/Game/Cosmetics/Helmets"
UI_FOLDER = "/Game/UI/Shop"
DT_HELMETS = "/Game/Blueprints/Gameplay/Cosmetics/DT_Helmets"
DT_SKINS = "/Game/Blueprints/Gameplay/Cosmetics/DT_Skins"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lin_color(hex_rgb, alpha=1.0):
    r, g, b = (hex_rgb >> 16) & 255, (hex_rgb >> 8) & 255, hex_rgb & 255
    return unreal.LinearColor(srgb_to_linear(r / 255.0), srgb_to_linear(g / 255.0), srgb_to_linear(b / 255.0), alpha)


def expr(material, cls, x, y):
    return mel.create_material_expression(material, cls, x, y)


def fresh_material(folder, name):
    path = f"{folder}/{name}"
    if asset_lib.does_asset_exist(path):
        material = asset_lib.load_asset(path)
        mel.delete_all_material_expressions(material)
    else:
        asset_lib.make_directory(folder)
        material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    return material


def scalar(material, name, value, x, y):
    s = expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("default_value", value)
    return s


def vector(material, name, value, x, y):
    v = expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    v.set_editor_property("parameter_name", name)
    v.set_editor_property("default_value", value)
    return v


def custom_node(material, code, inputs, output_type, x, y, description, extra_outputs=()):
    """Nodo Custom con entradas (nombre, expresión) y salidas adicionales (nombre, tipo)."""
    custom = expr(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", output_type)
    custom.set_editor_property("description", description)
    ins = []
    for name, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    custom.set_editor_property("inputs", ins)
    outs = []
    for name, otype in extra_outputs:
        co = unreal.CustomOutput()
        co.set_editor_property("output_name", name)
        co.set_editor_property("output_type", otype)
        outs.append(co)
    if outs:
        custom.set_editor_property("additional_outputs", outs)
    for name, src in inputs:
        mel.connect_material_expressions(src, "", custom, name)
    return custom


# ── Materiales ───────────────────────────────────────────────────────────────

def build_vertex_color_material():
    m = fresh_material(MAT_FOLDER, "M_CosmeticVertexColor")
    vc = expr(m, unreal.MaterialExpressionVertexColor, -600, 0)
    rough = expr(m, unreal.MaterialExpressionLinearInterpolate, -300, 200)
    rough.set_editor_property("const_a", 0.62)
    rough.set_editor_property("const_b", 0.24)
    mel.connect_material_expressions(vc, "A", rough, "Alpha")
    metal = expr(m, unreal.MaterialExpressionMultiply, -300, 80)
    metal.set_editor_property("const_b", 0.95)
    mel.connect_material_expressions(vc, "A", metal, "A")
    mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


TURTLE_BODY_HLSL = r"""
// P: posición local de TotugaDemo_Rig antes del skinning (mira a +Y; ~53 de alto). Zonas (ver build_cosmetics.py).
float YFront = lerp(0.4, 3.2, saturate((P.z - 29.0) / 9.0));
float ShellM = step(P.y, YFront) * step(22.3, P.z) * step(P.z, 38.4) * step(abs(P.x), 7.2);
float BellyM = smoothstep(2.0, 3.4, P.y) * smoothstep(21.0, 23.0, P.z) * smoothstep(36.6, 34.8, P.z) * smoothstep(6.2, 4.6, abs(P.x));
float3 BodyCol = lerp(BodyColor, BellyColor, BellyM * BellyAmount);

// Dibujo del caparazón visto desde atrás (X, Z), centrado en el lomo.
float2 Q = float2(P.x, P.z - 30.0) / max(PatternScale, 0.05);
int Mode = (int)round(ShellPattern);
float Pat = 0.0;
float Relief = 1.0;
if (Mode == 1 || Mode == 2)
{
    // Panal de hexágonos: escamas (juntas) o lunares (centros).
    float S = Mode == 1 ? 4.2 : 3.1;
    float2 R = float2(1.0, 1.7320508) * S;
    float2 H = R * 0.5;
    float2 A = (frac(Q / R) - 0.5) * R;
    float2 B = (frac((Q - H) / R) - 0.5) * R;
    float2 G = dot(A, A) < dot(B, B) ? A : B;
    float2 AG = abs(G);
    float HexD = max(dot(AG, float2(0.5, 0.8660254)), AG.x) / S;
    if (Mode == 1)
    {
        Pat = smoothstep(0.40, 0.46, HexD);
        Relief = lerp(1.12, 0.88, saturate(HexD * 2.0));
    }
    else
    {
        Pat = 1.0 - smoothstep(0.24, 0.29, length(G) / S);
    }
}
else if (Mode == 3)
{
    // Olas: franjas onduladas de espuma.
    float W = Q.y + 0.7 * sin(Q.x * 0.9);
    float F = frac(W / 3.4);
    Pat = smoothstep(0.06, 0.14, F) * smoothstep(0.46, 0.38, F);
}
else if (Mode == 4)
{
    // Galaxia: estrellitas sueltas y nebulosa suave.
    float2 Cell = floor(Q / 1.7);
    float2 Rnd = frac(sin(float2(dot(Cell, float2(127.1, 311.7)), dot(Cell, float2(269.5, 183.3)))) * 43758.5453);
    float2 Loc = frac(Q / 1.7) - 0.5 - (Rnd - 0.5) * 0.55;
    float Size = lerp(0.07, 0.2, Rnd.x) * step(0.42, Rnd.y);
    Pat = 1.0 - smoothstep(Size * 0.5, Size, length(Loc));
    Relief = 0.85 + 0.35 * (0.5 + 0.5 * sin(Q.x * 0.7 + 1.3) * sin(Q.y * 0.6));
}
else if (Mode == 5)
{
    // Lava: grietas de Voronoi que brillan.
    float2 G2 = Q / 2.6;
    float2 Base = floor(G2);
    float F1 = 8.0, F2 = 8.0;
    [unroll] for (int j = -1; j <= 1; ++j)
    {
        [unroll] for (int i = -1; i <= 1; ++i)
        {
            float2 C = Base + float2(i, j);
            float2 Rn = frac(sin(float2(dot(C, float2(127.1, 311.7)), dot(C, float2(269.5, 183.3)))) * 43758.5453);
            float D = length(G2 - C - Rn);
            if (D < F1) { F2 = F1; F1 = D; } else if (D < F2) { F2 = D; }
        }
    }
    Pat = 1.0 - smoothstep(0.04, 0.12, F2 - F1);
    Relief = lerp(0.8, 1.1, saturate(F1));
}
else if (Mode == 6)
{
    // Ajedrez.
    float2 K = floor(Q / 2.4);
    Pat = abs(fmod(K.x + K.y, 2.0));
}
else if (Mode == 7)
{
    // Sandía: franjas verticales temblonas.
    float F = frac((Q.x + 0.45 * sin(Q.y * 1.1)) / 2.8);
    Pat = smoothstep(0.08, 0.16, F) * smoothstep(0.56, 0.48, F);
}

float3 C1 = lerp(ShellColor, BodyColor * 0.72, ShellMatchBody) * Relief;
float3 ShellCol = lerp(C1, ShellColor2, Pat);
Metal = ShellM * ShellShine;
Rough = lerp(0.62, lerp(0.5, 0.2, ShellShine), ShellM);
float Twinkle = 0.8 + 0.2 * sin(TimeS * 2.3 + Q.x * 1.7 + Q.y);
Emis = ShellM * ShellGlow * Pat * ShellColor2 * Twinkle;

// ── Ojos: esferas de TotugaDemo_Rig en (±4,47; 8,46; 46,06) con radio 3,98 (simétricas en X) ──
float3 EyeQ = float3(abs(P.x) - 4.469, P.y - 8.462, P.z - 46.062);
float EyeDist = length(EyeQ);
float3 EyeN = EyeQ / max(EyeDist, 0.001);
// Solo el casquete que asoma (arriba, delante y afuera); la base queda bajo la piel.
float EyeM = step(EyeDist, 4.12) * step(0.17, dot(EyeN, normalize(float3(0.66, 0.62, 0.42))));
float3 EyeGaze = normalize(float3(0.25, 0.93, 0.27));
float3 EyeUp = normalize(float3(0.0, 0.0, 1.0) - EyeGaze * EyeGaze.z);
float3 EyeRight = cross(EyeUp, EyeGaze);
// Plano del ojo: radio del iris = 1 (unos 34 grados). EyeUV lleva la x igual en los dos ojos (brillos del mismo lado);
// EyeSym es simétrica (para las formas simétricas); su x positiva va hacia la nariz. EyeFlat es el plano sin mover
// (párpados y «>_<»); con cansancio (EyeTired: 0,5 cansada, 1 jadeando) la mirada baja: pupilas y formas se desplazan.
float TiredK = saturate(EyeTired);
float2 EyeFlat = float2(dot(EyeN, EyeRight), dot(EyeN, EyeUp)) / 0.56;
float2 EyeSym = EyeFlat + float2(0.0, 0.34 * saturate(TiredK * 1.6));
float2 EyeUV = float2(EyeSym.x * sign(P.x + 0.0001), EyeSym.y);
float EyeR = length(EyeUV);
float EyeFront = step(0.0, dot(EyeN, EyeGaze));
int EyeMode = EyeDizzy > 0.5 ? 5 : (int)round(EyeStyle);
float3 Sclera = float3(0.92, 0.92, 0.88);
float3 EyeInk = float3(0.006, 0.01, 0.025);
float3 EyeCol = Sclera;
float3 EyeEmis = float3(0.0, 0.0, 0.0);
float EyeShine = 1.0;
if (EyeMode == 1 || EyeMode == 6 || EyeMode == 7)
{
    // Iris de color con aro oscuro y pupila: redonda, de rendija (gato) o pequeña con estrellitas (galaxia).
    float IrisM = (1.0 - smoothstep(0.94, 1.0, EyeR)) * EyeFront;
    float3 IrisCol = lerp(EyeColor * 1.15, EyeColor * 0.45, smoothstep(0.55, 0.98, EyeR));
    if (EyeMode == 7)
    {
        float2 SCell = floor(EyeUV * 6.0);
        float2 SRnd = frac(sin(float2(dot(SCell, float2(127.1, 311.7)), dot(SCell, float2(269.5, 183.3)))) * 43758.5453);
        float SLoc = length(frac(EyeUV * 6.0) - 0.5 - (SRnd - 0.5) * 0.5);
        float Spark = (1.0 - smoothstep(0.06, 0.16, SLoc)) * step(0.5, SRnd.y) * (0.6 + 0.4 * sin(TimeS * 3.0 + SRnd.x * 20.0));
        IrisCol = lerp(EyeColor * lerp(1.0, 0.35, EyeR), EyeColor2, Spark);
        EyeEmis = IrisM * (EyeColor * 0.3 + EyeColor2 * Spark) * EyeGlow;
    }
    EyeCol = lerp(Sclera, IrisCol, IrisM);
    float PupilM = EyeMode == 6
        ? 1.0 - smoothstep(0.9, 1.0, length(float2(EyeUV.x / 0.2, EyeUV.y / 0.9)))
        : 1.0 - smoothstep(EyeMode == 7 ? 0.22 : 0.4, EyeMode == 7 ? 0.28 : 0.46, EyeR);
    EyeCol = lerp(EyeCol, EyeInk, PupilM * EyeFront);
}
else if (EyeMode == 2)
{
    // Estrella de cinco puntas (una hacia arriba) del color, con contorno oscuro.
    float SAng = atan2(EyeSym.x, EyeSym.y);
    float SSeg = 6.2831853 / 5.0;
    float SPh = abs(frac(SAng / SSeg + 0.5) - 0.5) * 2.0;
    float SRad = lerp(0.98, 0.46, SPh);
    float StarM = (1.0 - smoothstep(SRad - 0.05, SRad, EyeR)) * EyeFront;
    float StarEdge = (1.0 - smoothstep(SRad + 0.02, SRad + 0.1, EyeR)) * EyeFront;
    EyeCol = lerp(lerp(Sclera, EyeInk, StarEdge), EyeColor, StarM);
}
else if (EyeMode == 3)
{
    // Corazón (la curva clásica), del color y con contorno oscuro.
    float2 Hp = EyeSym / 0.82 + float2(0.0, 0.18);
    float Hq = Hp.x * Hp.x + Hp.y * Hp.y - 1.0;
    float Hv = Hq * Hq * Hq - Hp.x * Hp.x * Hp.y * Hp.y * Hp.y;
    float HeartM = (1.0 - smoothstep(-0.02, 0.02, Hv)) * EyeFront;
    float HeartEdge = (1.0 - smoothstep(0.02, 0.14, Hv)) * EyeFront;
    EyeCol = lerp(lerp(Sclera, EyeInk, HeartEdge), EyeColor, HeartM);
}
else if (EyeMode == 4)
{
    // De dibujo: pupila enorme (del color) con dos brillos grandes.
    EyeCol = lerp(Sclera, EyeColor, (1.0 - smoothstep(0.82, 0.88, EyeR)) * EyeFront);
}
else if (EyeMode == 5)
{
    // Espiral que gira (noqueada: siempre oscura).
    float3 SpCol = EyeDizzy > 0.5 ? EyeInk : EyeColor;
    float SpAng = atan2(EyeSym.y, EyeSym.x) + TimeS * 5.0;
    float Sp = frac(EyeR * 3.0 - SpAng / 6.2831853);
    float SpLine = smoothstep(0.3, 0.42, Sp) * smoothstep(0.78, 0.66, Sp) * step(EyeR, 1.1) * EyeFront;
    EyeCol = lerp(Sclera, SpCol, SpLine);
    EyeShine = 0.0;
}
else
{
    // Clásicos: pupila negra redonda.
    EyeCol = lerp(Sclera, EyeInk, (1.0 - smoothstep(0.5, 0.56, EyeR)) * EyeFront);
}
// Brillos: uno grande arriba y otro pequeño abajo, al mismo lado en los dos ojos.
float BigShine = 1.0 - smoothstep(EyeMode == 4 ? 0.26 : 0.15, EyeMode == 4 ? 0.3 : 0.19, length(EyeUV - float2(-0.3, 0.34)));
float SmallShine = 1.0 - smoothstep(EyeMode == 4 ? 0.12 : 0.07, EyeMode == 4 ? 0.15 : 0.1, length(EyeUV - float2(0.3, -0.28)));
EyeCol = lerp(EyeCol, float3(1.0, 1.0, 1.0), max(BigShine, SmallShine * 0.9) * EyeShine * EyeFront);
// Párpado: baja desde arriba (EyeBlink) con una raya oscura de pestaña. Con cansancio se queda a media asta (como las
// caras del HUD: cansada tapa el tercio de arriba; jadeando, algo más de la mitad), un poco más caído por fuera.
float EyeHeight = dot(EyeN, EyeUp);
float BlinkH = lerp(1.05, -1.05, saturate(EyeBlink));
float TiredH = (TiredK < 0.5 ? lerp(1.05, 0.2, TiredK * 2.0) : lerp(0.2, -0.12, TiredK * 2.0 - 1.0)) + 0.08 * TiredK * EyeFlat.x;
float LidH = min(BlinkH, TiredH);
float LidOn = max(saturate(EyeBlink), TiredK);
float Lid = step(LidH, EyeHeight) * step(0.01, LidOn);
float Lash = (1.0 - smoothstep(0.0, 0.08, abs(EyeHeight - LidH))) * step(0.02, LidOn);
EyeCol = lerp(lerp(EyeCol, EyeInk, Lash), BodyCol * 0.92, Lid);
// Ojos apretados «>_<» (EyeSqueeze: al agotarse y al gritar): párpado cerrado con un galón de tinta que apunta a la nariz.
if (EyeSqueeze > 0.5 && EyeDizzy < 0.5)
{
    float2 ChA = float2(-0.55, 0.5);
    float2 ChB = float2(0.45, 0.0);
    float2 ChC = float2(-0.55, -0.5);
    float2 Pa = EyeFlat - ChA;
    float2 Ba = ChB - ChA;
    float Da = length(Pa - Ba * saturate(dot(Pa, Ba) / dot(Ba, Ba)));
    float2 Pb = EyeFlat - ChB;
    float2 Bb = ChC - ChB;
    float Db = length(Pb - Bb * saturate(dot(Pb, Bb) / dot(Bb, Bb)));
    float Chevron = (1.0 - smoothstep(0.1, 0.17, min(Da, Db))) * EyeFront;
    EyeCol = lerp(BodyCol * 0.92, EyeInk, Chevron);
    Lid = 1.0;
}

// ── Boca y mofletes, pintados en el plano de la cara (x, z). La malla tiene un hueco bajo la nariz (|x| < 1,7;
// z 41,3-43,9; fondo en y ~ 10, borde en y ~ 13,7) por donde sale la lengua. MouthOpen (0 casi cerrada, 1 abierta del
// todo) y MouthSmile (1 sonrisa con el borde de arriba recto, 0 óvalo) dan la forma: dentro, granate con la lengua al
// fondo; alrededor, un filo oscuro; lo del hueco que queda fuera de la forma, piel en sombra.
float FaceFront = step(9.0, P.y) * step(P.y, 15.3) * step(abs(P.x), 3.8) * step(38.4, P.z) * step(P.z, 44.1);
float Cavity = FaceFront * step(P.y, 13.1) * step(abs(P.x), 1.95) * step(41.0, P.z);
float MO = saturate(MouthOpen);
float MS = saturate(MouthSmile);
float MHalfW = lerp(1.1, 2.45, MO);
float MHeight = lerp(0.9, 4.4, MO);
float MTop = 43.75;
float MCz = lerp(lerp(42.55, 41.55, MO), MTop, MS);
float MRv = lerp(MHeight * 0.5, MHeight, MS);
float MEll = (length(float2(P.x / MHalfW, (P.z - MCz) / MRv)) - 1.0) * min(MHalfW, MRv);
float MDist = max(MEll, lerp(-10.0, P.z - MTop, MS));
float MIn = (1.0 - smoothstep(-0.05, 0.05, MDist)) * FaceFront;
float MRim = (1.0 - smoothstep(0.14, 0.22, MDist)) * (1.0 - MIn) * FaceFront * (1.0 - Cavity);
float MBottom = MCz - MRv;
float2 MTq = float2(P.x / (0.62 * MHalfW), (P.z - MBottom - 0.3 * MHeight) / (0.36 * MHeight));
float MTongue = (1.0 - smoothstep(0.85, 1.0, length(MTq))) * MIn * smoothstep(0.12, 0.3, MO);
float3 FaceCol = lerp(BodyCol, BodyCol * 0.5, Cavity);
FaceCol = lerp(FaceCol, BodyCol * 0.25, MRim);
FaceCol = lerp(FaceCol, float3(0.147, 0.0116, 0.0185) * lerp(1.0, 0.55, Cavity), MIn);
FaceCol = lerp(FaceCol, float3(1.0, 0.159, 0.27) * lerp(0.9, 0.6, Cavity), MTongue);
// Colorete rosa bajo los ojos (cansada y, sobre todo, jadeando).
float2 BlushQ = float2((abs(P.x) - 5.0) / 1.8, (P.z - 41.1) / 1.15);
float BlushM = (1.0 - smoothstep(0.45, 1.0, length(BlushQ))) * step(8.5, P.y) * step(P.y, 15.0) * saturate(FaceBlush) * 0.7;
FaceCol = lerp(FaceCol, float3(1.0, 0.275, 0.366), BlushM);
Rough = lerp(Rough, 0.3, MIn);

float3 BaseCol = lerp(FaceCol, ShellCol, ShellM);
Metal = Metal * (1.0 - EyeM);
Rough = lerp(Rough, lerp(0.12, 0.6, Lid), EyeM);
Emis = lerp(Emis, EyeEmis * (1.0 - Lid), EyeM);
return lerp(BaseCol, EyeCol, EyeM);
"""


def build_turtle_body_material():
    m = fresh_material(MAT_FOLDER, "M_TurtleBody")
    pre = expr(m, unreal.MaterialExpressionPreSkinnedPosition, -1300, -200)
    interp = expr(m, unreal.MaterialExpressionVertexInterpolator, -1050, -200)
    mel.connect_material_expressions(pre, "", interp, "")
    green = 0x3A9A3F
    inputs = [
        ("P", interp),
        ("BodyColor", vector(m, "BodyColor", lin_color(green), -1100, 0)),
        ("BellyColor", vector(m, "BellyColor", lin_color(0xF3E3A6), -1100, 120)),
        ("BellyAmount", scalar(m, "BellyAmount", 0.0, -1100, 240)),
        ("ShellColor", vector(m, "ShellColor", lin_color(0x2F7A34), -1100, 320)),
        ("ShellColor2", vector(m, "ShellColor2", lin_color(0x1F5424), -1100, 440)),
        ("ShellPattern", scalar(m, "ShellPattern", 0.0, -1100, 560)),
        ("PatternScale", scalar(m, "PatternScale", 1.0, -1100, 640)),
        ("ShellShine", scalar(m, "ShellShine", 0.0, -1100, 720)),
        ("ShellGlow", scalar(m, "ShellGlow", 0.0, -1100, 800)),
        ("ShellMatchBody", scalar(m, "ShellMatchBody", 1.0, -1100, 880)),
        ("TimeS", expr(m, unreal.MaterialExpressionTime, -1100, 960)),
        # Ojos (ETNEyeStyle: 0 clásicos, 1 iris, 2 estrella, 3 corazón, 4 de dibujo, 5 espiral, 6 gato, 7 galaxia).
        ("EyeStyle", scalar(m, "EyeStyle", 0.0, -1100, 1040)),
        ("EyeColor", vector(m, "EyeColor", lin_color(0x13233B), -1100, 1120)),
        ("EyeColor2", vector(m, "EyeColor2", lin_color(0xFFFFFF), -1100, 1240)),
        ("EyeGlow", scalar(m, "EyeGlow", 0.0, -1100, 1360)),
        ("EyeBlink", scalar(m, "EyeBlink", 0.0, -1100, 1440)),
        ("EyeDizzy", scalar(m, "EyeDizzy", 0.0, -1100, 1520)),
        # Cara (UTN_TurtleFaceComponent): cansancio, ojos apretados, boca (de serie, sonrisa pequeña) y colorete.
        ("EyeTired", scalar(m, "EyeTired", 0.0, -1100, 1600)),
        ("EyeSqueeze", scalar(m, "EyeSqueeze", 0.0, -1100, 1680)),
        ("MouthOpen", scalar(m, "MouthOpen", 0.3, -1100, 1760)),
        ("MouthSmile", scalar(m, "MouthSmile", 1.0, -1100, 1840)),
        ("FaceBlush", scalar(m, "FaceBlush", 0.0, -1100, 1920)),
    ]
    custom = custom_node(m, TURTLE_BODY_HLSL, inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -600, 200, "TurtleBody",
                         extra_outputs=[("Metal", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                                        ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                                        ("Emis", unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(custom, "Metal", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(custom, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(custom, "Emis", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    m.set_editor_property("used_with_skeletal_mesh", True)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


HELMET_SLOT_HLSL = r"""
// Ranura del casco de serie de TotugaDemo_Rig: el casco (con sus correas) y la lengua rígida que sale de la boca.
// HideHelmet recorta el casco (se lleva uno de la tienda) y HideTongue la lengua (la del jugador es procedural).
float Tongue = step(abs(P.x), 2.3) * step(9.6, P.y) * step(P.z, 43.4) * step(39.8, P.z);
Col = lerp(HelmetColor, TongueColor, Tongue);
return lerp(1.0 - Tongue * HideTongue, Tongue * (1.0 - HideTongue), HideHelmet);
"""


def build_helmet_slot_material():
    m = fresh_material(MAT_FOLDER, "M_TurtleHelmetSlot")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    pre = expr(m, unreal.MaterialExpressionPreSkinnedPosition, -1000, 200)
    interp = expr(m, unreal.MaterialExpressionVertexInterpolator, -760, 200)
    mel.connect_material_expressions(pre, "", interp, "")
    hide = scalar(m, "HideHelmet", 1.0, -760, 320)
    hide_tongue = scalar(m, "HideTongue", 0.0, -760, 400)
    # El rojo de serie es el difuso de M_TortugaDemo (lineal), el mismo que tenía la lengua.
    helmet_color = vector(m, "HelmetColor", unreal.LinearColor(0.083478, 0.017751, 0.000447, 1.0), -760, -120)
    tongue_color = vector(m, "TongueColor", unreal.LinearColor(0.0835, 0.0178, 0.0004, 1.0), -760, 0)
    custom = custom_node(m, HELMET_SLOT_HLSL, [("P", interp), ("HideHelmet", hide), ("HideTongue", hide_tongue),
                                              ("HelmetColor", helmet_color), ("TongueColor", tongue_color)],
                         unreal.CustomMaterialOutputType.CMOT_FLOAT1, -450, 220, "HelmetSlotMask",
                         extra_outputs=[("Col", unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
    mel.connect_material_property(custom, "Col", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    rough = expr(m, unreal.MaterialExpressionConstant, -450, 120)
    rough.set_editor_property("r", 0.55)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("used_with_skeletal_mesh", True)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


def build_face_parts_material():
    """Lengua y gotas de sudor de UTN_TurtleFaceComponent (mallas procedurales): el color de vértice llega en lineal y su
    alfa es lo mojado (0 mate, 1 brillante); un poco de luz propia para que el rosa y el celeste no se apaguen a la sombra."""
    m = fresh_material(MAT_FOLDER, "M_TurtleFaceParts")
    vc = expr(m, unreal.MaterialExpressionVertexColor, -700, 0)
    rough = expr(m, unreal.MaterialExpressionLinearInterpolate, -400, 160)
    rough.set_editor_property("const_a", 0.6)
    rough.set_editor_property("const_b", 0.16)
    mel.connect_material_expressions(vc, "A", rough, "Alpha")
    glow = expr(m, unreal.MaterialExpressionMultiply, -400, 300)
    glow.set_editor_property("const_b", 0.1)
    mel.connect_material_expressions(vc, "", glow, "A")
    mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


PREVIEW_HLSL = r"""
// Captura SceneColorHDR: color lineal sin curva de tono y alfa = 1 - cobertura.
float4 S = Capture;
float3 X = max(S.rgb * Exposure, 0.0);
float3 Mapped = saturate((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14));
float Cover = saturate(1.0 - S.a);
return float4(Mapped, Cover);
"""


def build_preview_ui_material():
    m = fresh_material(UI_FOLDER, "M_UI_Preview")
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    tex = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    tex.set_editor_property("parameter_name", "Capture")
    tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    expo = scalar(m, "Exposure", 1.0, -900, 260)
    # El Custom recibe el float4 del sampler (RGBA).
    append = expr(m, unreal.MaterialExpressionAppendVector, -650, 0)
    mel.connect_material_expressions(tex, "RGB", append, "A")
    mel.connect_material_expressions(tex, "A", append, "B")
    custom = custom_node(m, PREVIEW_HLSL, [("Capture", append), ("Exposure", expo)], unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                         -420, 60, "PreviewCapture")
    rgb = expr(m, unreal.MaterialExpressionComponentMask, -200, 20)
    for c, v in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(c, v)
    mel.connect_material_expressions(custom, "", rgb, "")
    alpha = expr(m, unreal.MaterialExpressionComponentMask, -200, 160)
    for c, v in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(c, v)
    mel.connect_material_expressions(custom, "", alpha, "")
    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


# ── Cascos ───────────────────────────────────────────────────────────────────

def helmet_buffers(mesh):
    """Triángulos de cosmetics_meshes a buffers de GeometryScript (caras planas, color lineal dos veces)."""
    buf = unreal.GeometryScriptSimpleMeshBuffers()
    verts, normals, colors, tris, uvs = [], [], [], [], []
    for (a, b, c, col, shine) in mesh.tris:
        n = CM.norm(CM.cross(CM.sub(b, a), CM.sub(c, a)))
        r, g, bb = (col >> 16) & 255, (col >> 8) & 255, col & 255
        # El color de vértice del StaticMesh se guarda en sRGB y el nodo VertexColor lo lee tal cual: se decodifica
        # dos veces para que el material reciba el color lineal de la paleta (ver reference_ue_runtime_mesh).
        lc = unreal.LinearColor(srgb_to_linear(srgb_to_linear(r / 255.0)), srgb_to_linear(srgb_to_linear(g / 255.0)),
                                srgb_to_linear(srgb_to_linear(bb / 255.0)), float(shine))
        base = len(verts)
        for p in (a, b, c):
            verts.append(unreal.Vector(p[0], p[1], p[2]))
            normals.append(unreal.Vector(n[0], n[1], n[2]))
            colors.append(lc)
            uvs.append(unreal.Vector2D(p[0] / 20.0, p[2] / 20.0))
        # UE pinta la cara cuya normal es (C-A)x(B-A) (ver TN_ProcMapMeshKit.h): cosmetics_meshes orienta
        # (B-A)x(C-A) hacia fuera, así que se emite A, C, B. Si no, los cascos se ven del revés (huecos desde arriba).
        tris.append(unreal.IntVector(base, base + 2, base + 1))
    buf.set_editor_property("vertices", verts)
    buf.set_editor_property("normals", normals)
    buf.set_editor_property("vertex_colors", colors)
    buf.set_editor_property("uv0", uvs)
    buf.set_editor_property("triangles", tris)
    return buf


def build_helmet_meshes(material):
    asset_lib.make_directory(HELMET_FOLDER)
    out = {}
    for sid, fn, _, _ in CM.HELMETS:
        mesh = fn()
        dm = unreal.DynamicMesh()
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dm, helmet_buffers(mesh), 0)
        path = f"{HELMET_FOLDER}/SM_Helmet_{sid}"
        if asset_lib.does_asset_exist(path):
            sm = asset_lib.load_asset(path)
            opts = unreal.GeometryScriptCopyMeshToAssetOptions()
            opts.set_editor_property("enable_recompute_normals", False)
            opts.set_editor_property("enable_recompute_tangents", True)
            opts.set_editor_property("replace_materials", True)
            opts.set_editor_property("new_materials", [material])
            opts.set_editor_property("new_material_slot_names", ["Helmet"])
            unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm, sm, opts, unreal.GeometryScriptMeshWriteLOD())
        else:
            opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
            opts.set_editor_property("enable_recompute_normals", False)
            opts.set_editor_property("enable_recompute_tangents", True)
            opts.set_editor_property("enable_collision", False)
            opts.set_editor_property("enable_nanite", False)
            sm, _outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, path, opts)
            sm.set_material(0, material)
        asset_lib.save_loaded_asset(sm)
        out[sid] = sm
        unreal.log(f"[Cosméticos] {path}: {len(mesh.tris)} triángulos")
    return out


# ── Catálogo (DT_Helmets y DT_Skins) ─────────────────────────────────────────

def helmet_rows():
    """Filas de DT_Helmets (gratis: la tienda cobra las skins de DT_Skins, #873)."""
    helmets = []
    for sid, _fn, name, desc in CM.HELMETS:
        row = "Helmet_" + sid
        helmets.append({
            "Name": row, "HelmetId": row, "DisplayName": name,
            "DisplayMesh": f"{HELMET_FOLDER}/SM_Helmet_{sid}.SM_Helmet_{sid}",
            "Icon": "None",
            "MeshScale": {"X": 1.0, "Y": 1.0, "Z": 1.0},
            "MeshOffset": {"X": 0.0, "Y": 0.0, "Z": 0.0},
            "MeshRotation": {"Pitch": 0.0, "Yaw": 0.0, "Roll": 0.0},
            "Price": 0, "Description": desc,
        })
    return helmets


def fill_tables(which=("helmets", "skins")):
    """Rehace las filas de DT_Helmets y DT_Skins (o solo las de which). Las skins salen de cosmetics_skins.py."""
    import json
    tables = {"helmets": (DT_HELMETS, helmet_rows), "skins": (DT_SKINS, CS.skin_rows)}
    for key in which:
        path, make_rows = tables[key]
        rows = make_rows()
        dt = asset_lib.load_asset(path)
        ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json.dumps(rows, ensure_ascii=False))
        asset_lib.save_loaded_asset(dt)
        unreal.log(f"[Cosméticos] {path}: {len(rows)} filas ({'bien' if ok else 'ERROR'})")


def tables_ready():
    """Las columnas nuevas (Color, Pattern, EyeStyle...) existen si el módulo C++ ya está compilado con ellas."""
    try:
        row = unreal.TN_SkinData()
        return hasattr(row, "pattern") and hasattr(row, "eye_style") and hasattr(row, "rarity")
    except Exception:
        return False


def main():
    vc = build_vertex_color_material()
    build_turtle_body_material()
    build_helmet_slot_material()
    build_face_parts_material()
    build_preview_ui_material()
    build_helmet_meshes(vc)
    if tables_ready():
        fill_tables()
    else:
        unreal.log_warning("[Cosméticos] Falta compilar el C++ con las columnas nuevas de FTN_SkinData (EyeStyle): tablas sin tocar.")
    unreal.log("[Cosméticos] Listo.")


if __name__ == "__main__":
    main()
