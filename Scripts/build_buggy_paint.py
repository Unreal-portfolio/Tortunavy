"""Crea /Game/Vehicles/Buggy/M_BuggyPaint: la pintura del buggy del Rally (#297, #115).

Se ejecuta dentro del editor o sin interfaz:
    UnrealEditor-Win64-DebugGame-Cmd.exe <uproject> -run=pythonscript -script=<repo>/Scripts/build_buggy_paint.py -unattended -nullrhi

Pinta dos clases de malla, según ZoneScheme:
  - 0: las carrocerías de tortuga, construidas en C++ (Source/Tortunabo/Private/Vehicles/TN_BuggyArt.cpp), que llevan en
    el color de vértice qué es cada cara: el alfa es la zona (en octavos) y el RGB, las máscaras o el color.
  - 1: el buggy de serie de Art/Source (SM_TN_BuggyBody y SM_TN_BuggyTire), con las zonas de M_TN_BuggyZones: el RGB marca
    pintura (1,0,0), detalle (0,1,0), chasis (0,0,0), neumático (0,0,1) o luz (1,1,1) y el alfa es el sombreado. La
    pintura lleva PlateColor y el dibujo; el detalle, AccentColor; el chasis y las barras, BaseColor con un punto de metal;
    el neumático es goma y las luces brillan con LightGlow, como en su material.
Zonas de las tortugas (ZoneScheme 0):
  - Zona pintura (alfa 1): R = carrocería (BaseColor), G = placas del caparazón (PlateColor), B = piel de la tortuga:
    cabeza, aletas y cola (AccentColor). El valor de la máscara es el sombreado de la cara. El dibujo (Pattern,
    PatternColor, PatternScale) va sobre la carrocería y las placas (las franjas y las llamas, también sobre la piel);
    Shine es el brillo metálico y Glow, la luz propia del dibujo.
  - Zona pintura sin dibujo (alfa 0,875): igual, sin dibujo (llantas).
  - Zona equipo (alfa 0,75): TeamColor por el R (iris de los ojos y banderín).
  - Zona luz (alfa 0,5): el RGB es el color y brilla con LightGlow (ojos-faro, pilotos, relojes).
  - Zona metal (alfa 0,25) y mate (alfa 0): el RGB es el color lineal.
Pattern: 0 liso, 1 escamas, 2 lunares, 3 olas, 4 estrellas, 5 grietas de lava, 6 ajedrez, 7 sandía (los mismos de
M_TurtleBody), 8 franjas de carreras, 9 llamas y 10 camuflaje. La posición y la normal son las locales de la malla
(cm); el dibujo se proyecta por la cara dominante.

Los colores se escriben en sRGB hexadecimal y se pasan a lineal.
"""

import unreal

FOLDER = "/Game/Vehicles/Buggy"
NAME = "M_BuggyPaint"

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


BUGGY_PAINT_HLSL = r"""
// VC: color de vértice. P y N: posición y normal locales de la malla del buggy (cm). Scheme 0 (tortugas): A = zona en
// octavos (8 pintura, 7 pintura sin dibujo, 6 equipo, 4 luz, 2 metal, 0 mate). Scheme 1 (buggy de serie): RGB = zona de
// M_TN_BuggyZones y A = sombreado.
float Code = round(VC.a * 8.0);
float3 AN = abs(N);
// Proyección del dibujo: desde arriba casi todo el lomo (menos costuras entre caras), de lado o de frente el resto.
bool bTop = N.z > 0.4;
bool bSide = !bTop && AN.y >= AN.x;
float S = max(PatternScale, 0.05);
float2 Q = (bTop ? P.xy : (bSide ? P.xz : P.yz)) / (10.0 * S);
int Mode = (int)round(Pattern);
float Pat = 0.0;
float Relief = 1.0;
float Heat = 1.0;
if (Mode == 1 || Mode == 2)
{
    // Panal de hexágonos: escamas (juntas) o lunares (centros).
    float Cell = Mode == 1 ? 4.2 : 3.1;
    float2 R = float2(1.0, 1.7320508) * Cell;
    float2 H = R * 0.5;
    float2 A = (frac(Q / R) - 0.5) * R;
    float2 B = (frac((Q - H) / R) - 0.5) * R;
    float2 G = dot(A, A) < dot(B, B) ? A : B;
    float2 AG = abs(G);
    float HexD = max(dot(AG, float2(0.5, 0.8660254)), AG.x) / Cell;
    if (Mode == 1)
    {
        Pat = smoothstep(0.40, 0.46, HexD);
        Relief = lerp(1.12, 0.88, saturate(HexD * 2.0));
    }
    else
    {
        Pat = 1.0 - smoothstep(0.24, 0.29, length(G) / Cell);
    }
}
else if (Mode == 3)
{
    // Olas de espuma.
    float W = Q.y + 0.7 * sin(Q.x * 0.9);
    float F = frac(W / 3.4);
    Pat = smoothstep(0.06, 0.14, F) * smoothstep(0.46, 0.38, F);
}
else if (Mode == 4)
{
    // Cielo de estrellas.
    float2 Cell = floor(Q / 1.7);
    float2 Rnd = frac(sin(float2(dot(Cell, float2(127.1, 311.7)), dot(Cell, float2(269.5, 183.3)))) * 43758.5453);
    float2 Loc = frac(Q / 1.7) - 0.5 - (Rnd - 0.5) * 0.55;
    float Size = lerp(0.07, 0.2, Rnd.x) * step(0.42, Rnd.y);
    Pat = 1.0 - smoothstep(Size * 0.5, Size, length(Loc));
    Relief = 0.85 + 0.35 * (0.5 + 0.5 * sin(Q.x * 0.7 + 1.3) * sin(Q.y * 0.6));
}
else if (Mode == 5)
{
    // Grietas de lava (Voronoi).
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
    // Ajedrez de bandera de meta.
    float2 K = floor(Q / 2.4);
    Pat = abs(fmod(K.x + K.y, 2.0));
}
else if (Mode == 7)
{
    // Sandía: franjas temblonas.
    float F = frac((Q.x + 0.45 * sin(Q.y * 1.1)) / 2.8);
    Pat = smoothstep(0.08, 0.16, F) * smoothstep(0.56, 0.48, F);
}
else if (Mode == 8)
{
    // Franjas de carreras: dos anchas y un filete encima; en los costados, una banda a lo largo.
    if (bSide)
    {
        float Z = P.z / S;
        Pat = step(66.0, Z) * step(Z, 76.0) + step(79.0, Z) * step(Z, 81.5);
    }
    else
    {
        float Yb = abs(P.y) / S;
        Pat = step(12.0, Yb) * step(Yb, 28.0) + step(32.0, Yb) * step(Yb, 35.0);
    }
}
else if (Mode == 9)
{
    // Llamas de hot rod que salen del morro: el largo de cada lengua cambia a lo ancho (arriba) o a lo alto (lados).
    float W = (bSide ? P.z : P.y) / S;
    float L = 110.0 + 80.0 * pow(abs(sin(W * 0.07 + 0.6)), 0.7) + 25.0 * sin(W * 0.21);
    float Back = (160.0 - P.x) / S;
    float Edge = L - Back;
    Pat = smoothstep(-1.5, 1.5, Edge);
    Heat = 1.0 + 0.55 * saturate(Edge / 45.0);
}
else if (Mode == 10)
{
    // Camuflaje de manchas.
    float2 C = Q * 0.55;
    float Nz = sin(C.x * 1.7 + sin(C.y * 1.3)) * cos(C.y * 1.9 + sin(C.x * 0.7)) + 0.5 * sin(C.x * 3.1 + C.y * 2.3);
    Pat = smoothstep(0.15, 0.3, Nz);
}

Spec = 0.5;
if (Scheme > 0.5)
{
    float StockShade = VC.a;
    bool bLight = VC.r > 0.5 && VC.g > 0.5 && VC.b > 0.5;
    bool bRubber = !bLight && VC.b > 0.5;
    bool bDetail = !bLight && !bRubber && VC.g > 0.5;
    bool bPaintZone = !bLight && !bRubber && !bDetail && VC.r > 0.5;
    float3 LightTint = float3(1.0, 0.855, 0.319);
    Metal = 0.0;
    Rough = 0.7;
    Spec = 0.25;
    Emis = float3(0.0, 0.0, 0.0);
    if (bLight)
    {
        Rough = 0.25;
        Emis = LightTint * LightGlow;
        return LightTint * StockShade;
    }
    if (bRubber)
    {
        // Goma: especular bajo, como M_TN_BuggyZones (con 0,5 sale azulada al reflejar el cielo).
        Rough = 0.85;
        Spec = 0.15;
        return float3(0.024, 0.021, 0.033) * StockShade;
    }
    float OnStock = bPaintZone ? 1.0 : 0.0;
    float3 ZoneCol = bPaintZone ? PlateColor : (bDetail ? AccentColor : BaseColor);
    bool bPainted = bPaintZone || bDetail;
    Metal = bPainted ? Shine * 0.9 : lerp(0.35, 0.9, Shine);
    Rough = bPainted ? lerp(0.62, 0.16, Shine) : lerp(0.45, 0.2, Shine);
    float StockTwinkle = 0.8 + 0.2 * sin(TimeS * 2.3 + Q.x * 1.7 + Q.y);
    Emis = Pat * OnStock * Glow * PatternColor * Heat * StockTwinkle * StockShade;
    return lerp(ZoneCol * lerp(1.0, Relief, OnStock), PatternColor * Heat, Pat * OnStock) * StockShade;
}

float3 Col = VC.rgb;
Metal = 0.0;
Rough = 0.72;
Emis = float3(0.0, 0.0, 0.0);
if (Code >= 6.5)
{
    // Pintura: máscaras de carrocería, placas y piel (con su sombreado); el dibujo, sobre carrocería y placas (las
    // franjas y las llamas, también sobre la piel), nunca en la pintura sin dibujo.
    float Shade = max(max(VC.r, VC.g), VC.b);
    float OnSkin = (Mode == 8 || Mode == 9) ? 1.0 : 0.0;
    float OnBody = step(7.5, Code) * saturate(step(0.001, VC.r + VC.g) * (1.0 - step(0.001, VC.b)) + step(0.001, VC.b) * OnSkin);
    float3 Paint = VC.r * BaseColor + VC.g * PlateColor + VC.b * AccentColor;
    float3 PatC = PatternColor * Shade * Heat;
    Col = lerp(Paint * lerp(1.0, Relief, OnBody), PatC, Pat * OnBody);
    Metal = Shine * 0.9;
    Rough = lerp(0.62, 0.16, Shine);
    float Twinkle = 0.8 + 0.2 * sin(TimeS * 2.3 + Q.x * 1.7 + Q.y);
    Emis = Pat * OnBody * Glow * PatternColor * Heat * Twinkle * Shade;
}
else if (Code >= 5.5)
{
    // Color del equipo (iris y banderín), con un poco de luz para que se lea de lejos.
    Col = TeamColor * VC.r;
    Rough = 0.4;
    Emis = TeamColor * VC.r * 0.25;
}
else if (Code >= 3.5)
{
    // Luces: ojos-faro, pilotos y relojes.
    Rough = 0.25;
    Emis = VC.rgb * LightGlow;
}
else if (Code >= 1.5)
{
    // Metal: cromados, bronce del cañón, barras.
    Metal = 1.0;
    Rough = 0.28;
}
return Col;
"""


def build_buggy_paint_material():
    m = fresh_material(FOLDER, NAME)
    pre_pos = expr(m, unreal.MaterialExpressionPreSkinnedPosition, -1500, -300)
    pos = expr(m, unreal.MaterialExpressionVertexInterpolator, -1250, -300)
    mel.connect_material_expressions(pre_pos, "", pos, "")
    pre_nrm = expr(m, unreal.MaterialExpressionPreSkinnedNormal, -1500, -180)
    nrm = expr(m, unreal.MaterialExpressionVertexInterpolator, -1250, -180)
    mel.connect_material_expressions(pre_nrm, "", nrm, "")
    vc = expr(m, unreal.MaterialExpressionVertexColor, -1250, -60)
    # El Custom recibe el color de vértice entero (RGBA).
    vc_rgba = expr(m, unreal.MaterialExpressionAppendVector, -1050, -60)
    mel.connect_material_expressions(vc, "", vc_rgba, "A")
    mel.connect_material_expressions(vc, "A", vc_rgba, "B")
    inputs = [
        ("VC", vc_rgba),
        ("P", pos),
        ("N", nrm),
        ("BaseColor", vector(m, "BaseColor", lin_color(0x2F7A3F), -1250, 80)),
        ("PlateColor", vector(m, "PlateColor", lin_color(0x58B75A), -1250, 200)),
        ("AccentColor", vector(m, "AccentColor", lin_color(0x9ED36A), -1250, 320)),
        ("PatternColor", vector(m, "PatternColor", lin_color(0x1F5424), -1250, 440)),
        ("TeamColor", vector(m, "TeamColor", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -1250, 560)),
        ("Pattern", scalar(m, "Pattern", 0.0, -1250, 680)),
        ("PatternScale", scalar(m, "PatternScale", 1.0, -1250, 760)),
        ("Shine", scalar(m, "Shine", 0.0, -1250, 840)),
        ("Glow", scalar(m, "Glow", 0.0, -1250, 920)),
        ("LightGlow", scalar(m, "LightGlow", 0.9, -1250, 1000)),
        ("TimeS", expr(m, unreal.MaterialExpressionTime, -1250, 1080)),
        ("Scheme", scalar(m, "ZoneScheme", 0.0, -1250, 1160)),
    ]
    custom = custom_node(m, BUGGY_PAINT_HLSL, inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -700, 200, "BuggyPaint",
                         extra_outputs=[("Metal", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                                        ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                                        ("Emis", unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                                        ("Spec", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(custom, "Metal", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(custom, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(custom, "Emis", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(custom, "Spec", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    unreal.log(f"[Buggy] {FOLDER}/{NAME} listo.")
    return m


def main():
    build_buggy_paint_material()


if __name__ == "__main__":
    main()
