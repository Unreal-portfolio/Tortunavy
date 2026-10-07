"""Catálogo de skins de la tienda (filas de DT_Skins): caparazones, colores y ojos con su rareza y su precio (#873).

Datos puros (sin el módulo `unreal`) para poder probarlos con pytest (Scripts/tests/test_cosmetics_skins.py). Los usa
build_cosmetics.fill_tables. Los colores se escriben en sRGB hexadecimal y se pasan a lineal.

Precio suelto en puntos de final de partida según la rareza (PRICE_BY_RARITY, de 100 a 400). La caja sorpresa pesa cada
rareza con los valores de DA_PointsEconomy (/Game/Blueprints/Gameplay/Economy).
"""

PRICE_BY_RARITY = {"Common": 100, "Rare": 200, "Epic": 400}

# Filas añadidas para llenar la tienda y la caja sorpresa (#873): variaciones de color de las que ya había.
NEW_IN_873 = ("Shell_Pearl", "Shell_Ruby", "Shell_MintSpots", "Shell_NightWaves", "Body_Peach", "Body_Teal",
              "Body_Midnight", "Eyes_Ruby", "Eyes_Aurora")

# (id, nombre, color principal, segundo color, dibujo, escala del dibujo, brillo, luz propia, lo que dice el tendero, rareza)
SHELLS = [
    ("Scutes", "Escamas clásicas", 0xC8A165, 0x5E3F22, "Scutes", 1.0, 0.0, 0.0, "Hexágonos color miel con juntas de chocolate. El caparazón de tortuga por excelencia.", "Common"),
    ("Coral", "Coral con lunares", 0xFF7A5C, 0xFFF1DC, "Spots", 1.0, 0.0, 0.0, "Coral del arrecife con lunares de espuma. Alegre y veraniego.", "Common"),
    ("Waves", "Oleaje", 0x1E5FA8, 0xE8FBFF, "Waves", 1.0, 0.0, 0.0, "Olas que rompen en tu espalda. Para las tortugas más marineras.", "Common"),
    ("Gold", "Oro pirata", 0xFFC93C, 0xB8860B, "Scutes", 1.0, 1.0, 0.0, "Brilla más que el tesoro de un galeón. Cuidado con las gaviotas.", "Epic"),
    ("Lava", "Volcán", 0x2A2626, 0xFF7A1A, "Lava", 1.0, 0.0, 3.0, "Roca volcánica con grietas que brillan. Calentito, calentito.", "Epic"),
    ("Galaxy", "Galaxia", 0x1A1F4D, 0xFFF3B0, "Stars", 1.0, 0.0, 4.0, "Un cielo estrellado para las noches de carrera.", "Epic"),
    ("Melon", "Sandía", 0x8FD16A, 0x2E7D32, "Melon", 1.0, 0.0, 0.0, "Rayas de sandía fresquita. Nadie se lo va a comer, tranquilidad.", "Rare"),
    ("Checker", "Tablero", 0xF4EFE2, 0x26232E, "Checker", 1.0, 0.0, 0.0, "Cuadros blancos y negros, como la bandera de meta.", "Rare"),
    ("Moss", "Musgo", 0x5E8C3A, 0x9CCB5E, "Spots", 0.7, 0.0, 0.0, "Musgo de la selva. Camuflaje perfecto entre los helechos.", "Common"),
    ("Candy", "Algodón de azúcar", 0xFF9EC8, 0xFFFFFF, "Waves", 0.8, 0.0, 0.0, "Rosa de feria con remolinos de nube. Dulce, dulce.", "Rare"),
    ("Pearl", "Nácar", 0xF4EEE6, 0xC9B8D9, "Scutes", 1.0, 0.6, 0.0, "Nácar de ostra con reflejos lila. Brilla como una perla al sol.", "Rare"),
    ("Ruby", "Rubí", 0xB3122E, 0x5A0614, "Scutes", 1.0, 1.0, 0.0, "Escamas de rubí pulido. La joya más rara del arrecife.", "Epic"),
    ("MintSpots", "Menta con lunares", 0x9FE6C8, 0xFFFFFF, "Spots", 1.2, 0.0, 0.0, "Menta fresquita con lunares de nata. Como un helado de verano.", "Common"),
    ("NightWaves", "Olas de noche", 0x0F1B3D, 0x6FA8FF, "Waves", 1.0, 0.0, 1.5, "Olas que brillan en la oscuridad. Para nadar a la luz de la luna.", "Rare"),
]

# (id, nombre, color, barriga, cuánto se nota la barriga, lo que dice el tendero, rareza)
BODIES = [
    ("Ocean", "Azul océano", 0x3A8FD9, 0xE8F4FF, 0.45, "Del color del mar abierto. Te camuflas al nadar.", "Common"),
    ("Bubblegum", "Rosa chicle", 0xF28DB2, 0xFFF0F5, 0.45, "Rosa chicle con la barriga de nata. Muy dulce.", "Common"),
    ("Lavender", "Lavanda", 0x9B6BD6, 0xF1E6FF, 0.45, "Morado lavanda: huele a playa tranquila.", "Common"),
    ("Sunny", "Amarillo sol", 0xF4C542, 0xFFF8DC, 0.45, "Amarillo como el sol de mediodía. Se te ve desde lejos.", "Common"),
    ("Coral", "Rojo coral", 0xE8574A, 0xFFE3D6, 0.45, "Rojo coral, el color de los valientes del arrecife.", "Common"),
    ("Mint", "Menta", 0x7FE0C0, 0xF0FFF8, 0.4, "Menta fresquita para los días de calor.", "Common"),
    ("Orange", "Naranja", 0xF28C38, 0xFFEBD2, 0.45, "Naranja atardecer. Combina con todo.", "Common"),
    ("Snow", "Blanco nieve", 0xEDEFF2, 0xFFFFFF, 0.3, "Blanco nieve. Muy elegante, pero no te revuelques en la arena.", "Rare"),
    ("Charcoal", "Carbón", 0x3B3F4A, 0xB8BEC8, 0.4, "Gris carbón con la barriga plateada. Misteriosa.", "Rare"),
    ("Sand", "Arena", 0xE3C79A, 0xFFF5E1, 0.45, "Color arena de playa. Es el que llevo yo, ¿se nota?", "Common"),
    ("Lime", "Lima", 0x9ED94B, 0xF6FFE0, 0.4, "Verde lima, ácido y veloz.", "Common"),
    ("Forest", "Verde bosque", 0x2E6B3A, 0xD8E8B0, 0.45, "Verde oscuro de la selva profunda.", "Common"),
    ("Peach", "Melocotón", 0xFFB38A, 0xFFF1E6, 0.45, "Melocotón maduro con la barriga de crema. Suavecita.", "Common"),
    ("Teal", "Turquesa", 0x1FA7A0, 0xE6FFFB, 0.4, "Turquesa de laguna tropical. Te confundirán con el agua.", "Common"),
    ("Midnight", "Medianoche", 0x23285A, 0x9AA6E8, 0.45, "Azul medianoche con la barriga de luna. Para las tortugas nocturnas.", "Rare"),
]

# Ojos: (id, nombre, color del iris o la pupila, segundo color, tipo (ETNEyeStyle), luz propia, lo que dice el tendero, rareza)
EYES = [
    ("Ocean", "Iris azul mar", 0x2E86DE, 0xFFFFFF, "Iris", 0.0, "Azules como el mar abierto. Miran lejos, muy lejos.", "Common"),
    ("Emerald", "Iris esmeralda", 0x27B36A, 0xFFFFFF, "Iris", 0.0, "Verdes como la selva después de la lluvia.", "Common"),
    ("Honey", "Iris miel", 0xD9902B, 0xFFFFFF, "Iris", 0.0, "Color miel: cálidos y dulces.", "Common"),
    ("Cat", "Ojos de gato", 0xF4C430, 0xFFFFFF, "Cat", 0.0, "Pupila de rendija. Ven en la oscuridad... o eso dicen.", "Rare"),
    ("Star", "Pupilas de estrella", 0xFFCB3D, 0xFFFFFF, "Star", 0.0, "Para las tortugas que brillan en la carrera.", "Rare"),
    ("Heart", "Pupilas de corazón", 0xFF4F7B, 0xFFFFFF, "Heart", 0.0, "Enamorada del mar, de la playa y de ganar.", "Rare"),
    ("Toon", "Ojos de dibujo", 0x13233B, 0xFFFFFF, "Toon", 0.0, "Pupilas enormes y brillos de dibujos animados.", "Rare"),
    ("Spiral", "Hipnóticos", 0x9B5DE5, 0xFFFFFF, "Spiral", 0.0, "Giran y giran... ¿dónde estaba la meta?", "Epic"),
    ("Galaxy", "Galaxia", 0x3B2F8F, 0xFFF3B0, "Galaxy", 3.0, "Un universo entero en cada ojo. Brillan en la oscuridad.", "Epic"),
    ("Ruby", "Iris rubí", 0xD7263D, 0xFFFFFF, "Iris", 0.0, "Rojos como un rubí. Miradas que echan chispas.", "Common"),
    ("Aurora", "Aurora", 0x0E6B5C, 0x9BFFCE, "Galaxy", 3.0, "Una aurora boreal en cada ojo. Nadie ha visto nada igual.", "Epic"),
]

# Columnas de materiales por ranura (malla unificada de 5 ranuras): vacías a propósito, la tortuga de demo se pinta con los
# colores de la fila (M_TurtleBody). Van todas en el JSON: si falta alguna, el importador abre un diálogo modal que
# bloquea el editor.
EMPTY_SLOTS = {"BellyMaterial": "None", "EyeShineMaterial": "None", "EyesMouthMaterial": "None",
               "SkinMaterial": "None", "ShellMaterial": "None", "Icon": "None"}


def _srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lin_json(hex_rgb):
    """Color sRGB hexadecimal como color lineal para el JSON del DataTable."""
    r, g, b = (hex_rgb >> 16) & 255, (hex_rgb >> 8) & 255, hex_rgb & 255
    return {"R": round(_srgb_to_linear(r / 255.0), 5), "G": round(_srgb_to_linear(g / 255.0), 5),
            "B": round(_srgb_to_linear(b / 255.0), 5), "A": 1.0}


def _row(row, name, category, rarity, desc, look):
    return {"Name": row, "SkinId": row, "DisplayName": name, "Category": category, "Price": PRICE_BY_RARITY[rarity],
            "Rarity": rarity, "Description": desc, **look, **EMPTY_SLOTS}


def skin_rows():
    """Filas de DT_Skins en su orden: caparazones, colores y ojos."""
    rows = []
    for sid, name, c1, c2, pattern, scale, shine, glow, desc, rarity in SHELLS:
        rows.append(_row("Shell_" + sid, name, "Shell", rarity, desc, {
            "Color": lin_json(c1), "Color2": lin_json(c2), "BellyAmount": 0.0, "Pattern": pattern,
            "PatternScale": scale, "Shine": shine, "Glow": glow, "EyeStyle": "Classic"}))
    for sid, name, c1, c2, belly, desc, rarity in BODIES:
        rows.append(_row("Body_" + sid, name, "Body", rarity, desc, {
            "Color": lin_json(c1), "Color2": lin_json(c2), "BellyAmount": belly, "Pattern": "Plain",
            "PatternScale": 1.0, "Shine": 0.0, "Glow": 0.0, "EyeStyle": "Classic"}))
    for sid, name, c1, c2, style, glow, desc, rarity in EYES:
        rows.append(_row("Eyes_" + sid, name, "Eyes", rarity, desc, {
            "Color": lin_json(c1), "Color2": lin_json(c2), "BellyAmount": 0.0, "Pattern": "Plain",
            "PatternScale": 1.0, "Shine": 0.0, "Glow": glow, "EyeStyle": style}))
    return rows
