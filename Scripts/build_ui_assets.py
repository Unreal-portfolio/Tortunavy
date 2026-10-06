"""Crea los materiales de la interfaz de la partida (estilo Tortunavy, boceto para el equipo de arte).

Se ejecuta DENTRO del editor de Unreal:
    exec(open(r"<repo>/Scripts/build_ui_assets.py", encoding="utf-8").read())

Crea en /Game/UI/HUD:
  - M_UI_TurtleBadge: distintivo del jugador. En el centro, un disco de mar con una ola que se mueve (la cara
    cartoon de la tortuga va encima, en otro widget: TN_HUDFaces.h); alrededor, un salvavidas grueso que es el
    medidor de energía: el tramo que queda toma el color de la energía (verde, amarillo, naranja y rojo al final),
    lo gastado es un surco oscuro (se vacía en sentido horario), la zona bloqueada por el peso se ve en marrón
    rayado y todo late en rojo al quedarse sin aliento; cuatro vueltas de cuerda y un filo azul marino de pegatina.
    Parámetros: Energy, Weight, Exhausted.
  - M_UI_RadialWheel: rueda radial de emotes y frases (UTN_RunRadialWheelWidget): salvavidas azul marino con gajos
    alternos, divisorias de espuma y bordes crema; el gajo apuntado se ilumina en azul mar con un filo coral que
    late. El centro queda hueco para la cara. Parámetros: Slices, Selected (-1 = ninguno), OffsetDeg (gajo 0).

Si el material ya existe, rehace su grafo en el sitio (misma ruta, mismas referencias) con el HLSL de este script.
Los colores se escriben en sRGB hexadecimal y se pasan a lineal (la salida de un material de UI es lineal).
"""

import unreal

FOLDER = "/Game/UI/HUD"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def lin(hex_rgb):
    """Color sRGB 0xRRGGBB a float3 lineal de HLSL."""
    def ch(v):
        c = v / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (hex_rgb >> 16) & 255, (hex_rgb >> 8) & 255, hex_rgb & 255
    return "float3(%.4f, %.4f, %.4f)" % (ch(r), ch(g), ch(b))


PALETTE = {
    "SEA_LIGHT": lin(0x37B6DB),
    "SEA_DEEP": lin(0x0C2C57),
    "FOAM": lin(0xD6F5F2),
    "NAVY": lin(0x0F2340),
    "GROOVE": lin(0x0B1B33),
    "GREEN": lin(0x3DDC62),
    "YELLOW": lin(0xFFD23F),
    "ORANGE": lin(0xFF8A3D),
    "RED": lin(0xFF3B30),
    "BLOCKED": lin(0x6B4423),
    "ALARM": lin(0xFF2B1C),
    "ROPE_LIGHT": lin(0xE6C48C),
    "ROPE_DARK": lin(0x8C5A2E),
}

BADGE_HLSL = r"""
float2 P = UV - 0.5;
float R = length(P);
float A01 = frac(atan2(P.x, -P.y) / 6.2831853 + 1.0);
const float AA = 0.006;
const float DiscR = 0.285;
const float RingIn = 0.297;
const float Edge = 0.468;

// Disco central: mar con degradado, una ola que corre y sombra interior (la cara va encima, en otro widget).
float3 Col = lerp(@SEA_LIGHT@, @SEA_DEEP@, saturate((P.y + DiscR) / (2.0 * DiscR)));
float WaveY = 0.13 + 0.015 * sin(P.x * 40.0 + TimeS * 2.0);
Col *= lerp(1.0, 0.62, smoothstep(WaveY - 0.004, WaveY + 0.004, P.y));
Col = lerp(Col, @FOAM@, smoothstep(0.011, 0.0, abs(P.y - WaveY)) * 0.8);
Col *= lerp(1.0, 0.7, smoothstep(DiscR - 0.06, DiscR, R));
Col = lerp(Col, @NAVY@, smoothstep(DiscR - AA, DiscR, R));

// Salvavidas de energía: el tramo que queda, del color de la energía (verde, amarillo, naranja y rojo al final),
// con gajos alternos y volumen de tubo; lo gastado es un surco oscuro y lo bloqueado por el peso, marrón rayado.
float T = saturate((R - RingIn) / (Edge - RingIn));
float3 EnergyCol = lerp(@RED@, @ORANGE@, saturate((Energy - 0.1) / 0.15));
EnergyCol = lerp(EnergyCol, @YELLOW@, saturate((Energy - 0.25) / 0.15));
EnergyCol = lerp(EnergyCol, @GREEN@, saturate((Energy - 0.4) / 0.2));
float Seg = floor(A01 * 8.0);
float Stripe = fmod(Seg, 2.0) < 1.0 ? 1.0 : 0.78;
float Lit = smoothstep(A01 - 0.004, A01 + 0.004, Energy);
float3 Groove = @GROOVE@ * lerp(0.85, 1.0, Stripe);
float Blocked = step(1.0 - Weight, A01) * step(0.001, Weight);
float Hatch = step(0.5, frac((P.x + P.y) * 36.0));
Groove = lerp(Groove, @BLOCKED@ * lerp(0.75, 1.1, Hatch), Blocked);
float3 RingCol = lerp(Groove, EnergyCol * Stripe, Lit);
float Tube = sin(T * 3.14159);
RingCol *= 0.58 + 0.42 * Tube;
float Light = saturate(dot(P / max(R, 1e-4), float2(-0.55, -0.83)));
RingCol += 0.3 * Light * smoothstep(0.2, 0.0, abs(T - 0.3)) * Lit;
float Pulse = Exhausted * (0.5 + 0.5 * sin(TimeS * 11.0));
RingCol = lerp(RingCol, @ALARM@, Pulse * 0.5);
float WrapArc = abs(frac((A01 - 0.125) * 4.0 + 0.5) - 0.5) * 0.25 * 6.2831853 * R;
float3 RopeCol = lerp(@ROPE_DARK@, @ROPE_LIGHT@, step(0.5, frac(T * 3.0 + WrapArc * 30.0)));
RingCol = lerp(RingCol, RopeCol, smoothstep(0.015, 0.01, WrapArc));
Col = lerp(Col, RingCol, smoothstep(RingIn - AA, RingIn, R));

// Filo azul marino de pegatina y silueta.
Col = lerp(Col, @NAVY@, smoothstep(Edge - AA, Edge, R));
float Alpha = 1.0 - smoothstep(0.49 - AA, 0.49, R);
return float4(Col, Alpha);
"""


WHEEL_HLSL = r"""
float2 P = UV - 0.5;
float R = length(P);
const float AA = 0.004;
const float Inner = 0.165;
const float Outer = 0.462;
float N = max(Slices, 1.0);
float Step = 6.2831853 / N;
// Mismo convenio que UTN_RadialWheelWidgetBase: ángulo matemático (Y hacia arriba), gajo 0 centrado en OffsetDeg.
float A = atan2(-P.y, P.x) - radians(OffsetDeg) + Step * 0.5;
A = A - 6.2831853 * floor(A / 6.2831853);
float Idx = floor(A / Step);
float Local = A - Idx * Step;
float Divider = min(Local, Step - Local) * R;

float3 Col = lerp(@NAVY_TOP@, @NAVY_DEEP@, saturate(P.y + 0.5));
Col *= fmod(Idx, 2.0) < 1.0 ? 1.0 : 0.86;
float Sel = step(abs(Idx - Selected), 0.5) * step(0.0, Selected);
float Pulse = 0.5 + 0.5 * sin(TimeS * 6.0);
Col = lerp(Col, lerp(@SEA@, @SEA_LIGHT@, saturate((R - Inner) / (Outer - Inner))), Sel * (0.8 + 0.12 * Pulse));
Col = lerp(Col, @FOAM@, smoothstep(0.005, 0.0015, Divider) * 0.75);
Col = lerp(Col, @CORAL@, Sel * smoothstep(0.016, 0.007, abs(R - (Outer - 0.02))) * 0.95);
Col = lerp(Col, @CREAM@, smoothstep(0.011, 0.004, abs(R - Outer)));
Col = lerp(Col, @CREAM@, smoothstep(0.009, 0.003, abs(R - Inner)));
float OutEdge = smoothstep(Outer + 0.006, Outer + 0.012, R);
Col = lerp(Col, @NAVY_OUT@, OutEdge);
float Alpha = (1.0 - smoothstep(0.49 - AA, 0.49, R)) * smoothstep(Inner - 0.012 - AA, Inner - 0.012, R);
Alpha *= lerp(0.93, 1.0, max(OutEdge, Sel));
return float4(Col, Alpha);
"""

WHEEL_PALETTE = {
    "NAVY_TOP": lin(0x1A4273),
    "NAVY_DEEP": lin(0x0A1C38),
    "NAVY_OUT": lin(0x0F2340),
    "SEA": lin(0x1E9CC6),
    "SEA_LIGHT": lin(0x62D2EA),
    "FOAM": lin(0xE0F8F5),
    "CREAM": lin(0xFFFBF0),
    "CORAL": lin(0xFF6A52),
}


def fill(code, palette):
    for key, value in palette.items():
        code = code.replace("@%s@" % key, value)
    return code


def expr(material, cls, x, y):
    return mel.create_material_expression(material, cls, x, y)


def build_custom_ui_material(name, hlsl, scalars, description):
    """Material de UI translúcido con un nodo Custom (HLSL) que devuelve float4: color en emisivo y alfa en opacidad.

    scalars: lista de (nombre, valor por defecto) que pasan como parámetros escalares; además entran UV y TimeS.
    Si el asset existe, se rehace su grafo en el sitio.
    """
    path = f"{FOLDER}/{name}"
    if asset_lib.does_asset_exist(path):
        material = asset_lib.load_asset(path)
        mel.delete_all_material_expressions(material)
    else:
        asset_lib.make_directory(FOLDER)
        material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    sources = []
    for i, (pname, default) in enumerate(scalars):
        s = expr(material, unreal.MaterialExpressionScalarParameter, -900, i * 80)
        s.set_editor_property("parameter_name", pname)
        s.set_editor_property("default_value", default)
        sources.append((pname, s))
    sources.append(("UV", expr(material, unreal.MaterialExpressionTextureCoordinate, -900, 400)))
    sources.append(("TimeS", expr(material, unreal.MaterialExpressionTime, -900, 480)))

    custom = expr(material, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("code", hlsl)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", description)
    inputs = []
    for pname, _ in sources:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", pname)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    for pname, src in sources:
        mel.connect_material_expressions(src, "", custom, pname)

    rgb = expr(material, unreal.MaterialExpressionComponentMask, -250, 60)
    rgb.set_editor_property("r", True)
    rgb.set_editor_property("g", True)
    rgb.set_editor_property("b", True)
    rgb.set_editor_property("a", False)
    mel.connect_material_expressions(custom, "", rgb, "")
    alpha = expr(material, unreal.MaterialExpressionComponentMask, -250, 200)
    for c, v in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(c, v)
    mel.connect_material_expressions(custom, "", alpha, "")
    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(material)
    asset_lib.save_loaded_asset(material)
    return material


def main():
    badge = build_custom_ui_material("M_UI_TurtleBadge", fill(BADGE_HLSL, PALETTE),
                                     [("Energy", 1.0), ("Weight", 0.0), ("Exhausted", 0.0)], "TurtleBadge")
    unreal.log(f"[UI] Material del distintivo: {badge.get_path_name()}")
    wheel = build_custom_ui_material("M_UI_RadialWheel", fill(WHEEL_HLSL, WHEEL_PALETTE),
                                     [("Slices", 8.0), ("Selected", -1.0), ("OffsetDeg", 90.0)], "RadialWheel")
    unreal.log(f"[UI] Material de la rueda radial: {wheel.get_path_name()}")


main()
