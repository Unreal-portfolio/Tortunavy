"""Crea los materiales de decal de las trampas de Supervivencia que coloca el generador (#516 y #517).

Se ejecuta DENTRO del editor de Unreal o sin ventana (el C++ carga los materiales por ruta y, si no existen, la trampa
funciona igual pero no se ve su marca en el suelo):
    exec(open(r"<repo>/Scripts/create_survival_decals.py", encoding="utf-8").read())
    UnrealEditor-Cmd <uproject> -run=pythonscript -script="<repo>/Scripts/create_survival_decals.py" -nullrhi -unattended

Crea en /Game/ProcMap/Materials (decals de dominio "Deferred Decal" y mezcla "Translucent", dibujados con código, sin
texturas):
  - M_SlowZoneSyrupDecal: charco de sirope de las zonas lentas (ATN_SlowZoneVolume): ámbar brillante, vetas más
    oscuras, borde irregular que llena casi toda la caja (lo que se ve es lo que frena) y un reborde más claro.
    Parámetro Seed: la forma del borde (el C++ pone uno distinto a cada zona).
  - M_QuadCrossingDecal: franjas amarillas y negras de un cruce de quads (ATN_ProcQuadCrossing), con la pintura algo
    gastada. Parámetro Warn (0..1): el C++ lo hace parpadear antes de que pase el quad y las franjas se encienden
    en rojo.

Idempotente: si un asset existe, se le rehace el grafo. Los guarda al acabar.
"""

import unreal

FOLDER = "/Game/ProcMap/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

_warnings = []


def expr(material, cls, x, y):
    return mel.create_material_expression(material, cls, x, y)


def fresh_material(name):
    path = f"{FOLDER}/{name}"
    if asset_lib.does_asset_exist(path):
        material = asset_lib.load_asset(path)
        mel.delete_all_material_expressions(material)
    else:
        asset_lib.make_directory(FOLDER)
        material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    return material


def set_first(obj, names, value, what):
    """Pone la primera propiedad que exista de names (el nombre en Python cambia según la versión); avisa si ninguna."""
    last = None
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception as error:  # noqa: BLE001 - el editor lanza excepciones de varios tipos
            last = error
    _warnings.append(f"{what}: no se pudo poner ({last}). Ponlo a mano en el editor.")
    return False


SYRUP_HLSL = r"""
// Charco de sirope de una zona lenta. UV: 0..1 en la cara del decal (la caja de la zona vista desde arriba); Seed cambia
// el borde de una zona a otra. Salidas: color base, Alpha y Rough.
struct FTNSyrup
{
	float SHash(float2 P)
	{
		float3 P3 = frac(float3(P.x, P.y, P.x) * 0.1031);
		P3 += dot(P3, float3(P3.y, P3.z, P3.x) + 33.33);
		return frac((P3.x + P3.y) * P3.z);
	}
	float SNoise(float2 X)
	{
		float2 I = floor(X);
		float2 F = X - I;
		float2 U = F * F * (3.0 - 2.0 * F);
		return lerp(lerp(SHash(I), SHash(I + float2(1.0, 0.0)), U.x), lerp(SHash(I + float2(0.0, 1.0)), SHash(I + float2(1.0, 1.0)), U.x), U.y);
	}
	float SFbm(float2 X)
	{
		return 0.55 * SNoise(X) + 0.3 * SNoise(X * 2.1 + 7.3) + 0.15 * SNoise(X * 4.3 + 1.9);
	}
};
FTNSyrup TN;
float2 P = UV;
// Distancia al borde de la caja (0 en el borde, 0.5 en el centro) con un borde ondulado: el charco llena casi toda la
// zona, que es lo que frena.
float Edge = min(min(P.x, 1.0 - P.x), min(P.y, 1.0 - P.y));
float Wobble = (TN.SFbm(P * 6.0 + Seed * 3.1) - 0.5) * 0.08;
float Inside = saturate((Edge - 0.03 + Wobble) * 40.0);
// Gotas sueltas justo fuera del borde.
float Drops = step(0.80, TN.SNoise(P * 22.0 + Seed * 5.7)) * saturate((Edge + 0.02) * 30.0) * (1.0 - Inside);
float Cover = max(Inside, Drops);
Alpha = Cover * 0.88;

// Ámbar con vetas oscuras que se arremolinan y un reborde más claro (lineal).
float3 Amber = float3(0.62, 0.28, 0.03);
float3 Deep = float3(0.22, 0.07, 0.01);
float3 Rim = float3(0.95, 0.62, 0.12);
float Swirl = TN.SFbm(float2(P.x * 3.0 + TN.SFbm(P * 4.0 + Seed) * 1.5, P.y * 3.0) + Seed * 2.3);
float3 Col = lerp(Amber, Deep, smoothstep(0.45, 0.75, Swirl) * 0.7);
float RimBand = saturate(1.0 - abs(Edge - 0.035 - Wobble) * 30.0);
Col = lerp(Col, Rim, RimBand * 0.6);
// Muy brillante en el centro (pegajoso), algo menos en el reborde.
Rough = lerp(0.08, 0.3, RimBand);
return Col;
"""


QUAD_HLSL = r"""
// Franjas de aviso de un cruce de quads. UV: 0..1 en la cara del decal; Warn: 0 en reposo, 1 en el pico del parpadeo.
// Salidas: color base, Alpha, Rough y Glow (emisivo).
struct FTNStripes
{
	float QHash(float2 P)
	{
		float3 P3 = frac(float3(P.x, P.y, P.x) * 0.1031);
		P3 += dot(P3, float3(P3.y, P3.z, P3.x) + 33.33);
		return frac((P3.x + P3.y) * P3.z);
	}
	float QNoise(float2 X)
	{
		float2 I = floor(X);
		float2 F = X - I;
		float2 U = F * F * (3.0 - 2.0 * F);
		return lerp(lerp(QHash(I), QHash(I + float2(1.0, 0.0)), U.x), lerp(QHash(I + float2(0.0, 1.0)), QHash(I + float2(1.0, 1.0)), U.x), U.y);
	}
};
FTNStripes TN;
float2 P = UV;
float Edge = min(min(P.x, 1.0 - P.x), min(P.y, 1.0 - P.y));
float Inside = saturate(Edge * 80.0);
// Franjas en diagonal, con el borde algo blando.
float Band = frac(P.x * 4.0 + P.y * 7.0);
float Stripe = smoothstep(0.46, 0.5, Band) * (1.0 - smoothstep(0.96, 1.0, Band));
// Pintura gastada: calvas donde se ve el suelo.
float Wear = saturate((TN.QNoise(P * 18.0) * 0.7 + TN.QNoise(P * 55.0) * 0.3 - 0.18) * 4.0);
Alpha = Inside * Wear * 0.92;
float3 Yellow = float3(1.0, 0.72, 0.02);
float3 Black = float3(0.02, 0.02, 0.02);
float3 Red = float3(1.0, 0.06, 0.03);
float3 Col = lerp(Black, lerp(Yellow, Red, saturate(Warn)), Stripe);
Glow = Red * Stripe * saturate(Warn) * 6.0;
Rough = 0.65;
return Col;
"""


def build_decal(name, hlsl, description, params, outputs, specular):
    """Decal translúcido con un nodo Custom: entradas UV y params (nombre, valor por defecto); outputs (nombre, tipo,
    propiedad del material) además del color base."""
    m = fresh_material(name)
    set_first(m, ["material_domain"], unreal.MaterialDomain.MD_DEFERRED_DECAL, "Material Domain = Deferred Decal")
    set_first(m, ["blend_mode"], unreal.BlendMode.BLEND_TRANSLUCENT, "Blend Mode = Translucent")

    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    uv.set_editor_property("coordinate_index", 0)
    nodes = [("UV", uv)]
    for k, (pname, default) in enumerate(params):
        node = expr(m, unreal.MaterialExpressionScalarParameter, -900, 200 + 120 * k)
        node.set_editor_property("parameter_name", pname)
        node.set_editor_property("default_value", default)
        nodes.append((pname, node))

    custom = expr(m, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("code", hlsl)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", description)
    inputs = []
    for iname, _ in nodes:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", iname)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    extra = []
    for oname, otype, _ in outputs:
        co = unreal.CustomOutput()
        co.set_editor_property("output_name", oname)
        co.set_editor_property("output_type", otype)
        extra.append(co)
    custom.set_editor_property("additional_outputs", extra)
    for iname, node in nodes:
        mel.connect_material_expressions(node, "", custom, iname)

    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for oname, _, prop in outputs:
        mel.connect_material_property(custom, oname, prop)
    spec = expr(m, unreal.MaterialExpressionConstant, -500, 500)
    spec.set_editor_property("r", specular)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
MP = unreal.MaterialProperty


def main():
    built = [
        build_decal("M_SlowZoneSyrupDecal", SYRUP_HLSL, "SlowZoneSyrup", [("Seed", 1.0)],
                    [("Alpha", F1, MP.MP_OPACITY), ("Rough", F1, MP.MP_ROUGHNESS)], 0.7),
        build_decal("M_QuadCrossingDecal", QUAD_HLSL, "QuadCrossingStripes", [("Warn", 0.0)],
                    [("Alpha", F1, MP.MP_OPACITY), ("Rough", F1, MP.MP_ROUGHNESS), ("Glow", F3, MP.MP_EMISSIVE_COLOR)], 0.3),
    ]
    for decal in built:
        unreal.log(f"[create_survival_decals] {decal.get_path_name()}")
    for warning in _warnings:
        unreal.log_warning(f"[create_survival_decals] {warning}")
    unreal.log("[create_survival_decals] Hecho. Los materiales tardan 1-2 min en compilar sus sombreadores.")


main()
