#include "Vehicles/TN_BuggyCosmetics.h"

namespace TNBuggyCosmeticsDetail
{
	/** Color sRGB 0xRRGGBB (como en cualquier herramienta de arte) a lineal: lo que esperan los parámetros de M_BuggyPaint. */
	FLinearColor Hex(uint32 RGB)
	{
		return FLinearColor::FromSRGBColor(FColor((RGB >> 16) & 0xFF, (RGB >> 8) & 0xFF, RGB & 0xFF));
	}

	FTNBuggyModelInfo Model(const TCHAR* Suffix, ETNBuggyBodyStyle Style, int32 Price, const FText& Name, const FText& Description)
	{
		FTNBuggyModelInfo Out;
		Out.Id = Suffix ? FName(*(FString(TNBuggyCosmetics::ModelPrefix()) + Suffix)) : NAME_None;
		Out.Style = Style;
		Out.Price = Price;
		Out.Name = Name;
		Out.Description = Description;
		return Out;
	}

	FTNBuggyPaintInfo Paint(const TCHAR* Suffix, int32 Price, const FText& Name, const FText& Description, uint32 Base, uint32 Plates,
		uint32 Accent, uint32 PatternColor, ETNBuggyPattern Pattern, float PatternScale = 1.f, float Shine = 0.f, float Glow = 0.f)
	{
		FTNBuggyPaintInfo Out;
		Out.Id = Suffix ? FName(*(FString(TNBuggyCosmetics::PaintPrefix()) + Suffix)) : NAME_None;
		Out.Price = Price;
		Out.Name = Name;
		Out.Description = Description;
		Out.Base = Hex(Base);
		Out.Plates = Hex(Plates);
		Out.Accent = Hex(Accent);
		Out.PatternColor = Hex(PatternColor);
		Out.Pattern = Pattern;
		Out.PatternScale = PatternScale;
		Out.Shine = Shine;
		Out.Glow = Glow;
		return Out;
	}
}

namespace TNBuggyCosmetics
{
	const TArray<FTNBuggyModelInfo>& Models()
	{
		using namespace TNBuggyCosmeticsDetail;
		static const TArray<FTNBuggyModelInfo> List = {
			Model(nullptr, ETNBuggyBodyStyle::Stock, 0,
				NSLOCTEXT("Tortunabo", "BuggyModelStock", "Buggy de Serie"),
				NSLOCTEXT("Tortunabo", "BuggyModelStockDesc", "El buggy oficial del Rally: chasis de tubos, pontones con dorsal y el color de tu equipo. Gratis y a prueba de cocos.")),
			Model(TEXT("Clasico"), ETNBuggyBodyStyle::Classic, 900,
				NSLOCTEXT("Tortunabo", "BuggyModelClassic", "Buggy Clásico"),
				NSLOCTEXT("Tortunabo", "BuggyModelClassicDesc", "Un buggy que es una tortuga: caparazón de placas, aletas por guardabarros y unos ojos que alumbran la pista.")),
			Model(TEXT("Caiman"), ETNBuggyBodyStyle::Offroad, 1200,
				NSLOCTEXT("Tortunabo", "BuggyModelCaiman", "Caimán Todoterreno"),
				NSLOCTEXT("Tortunabo", "BuggyModelCaimanDesc", "Como la tortuga caimán: placas con pinchos, pico de hierro y un tubo de buceo por si toca cruzar un río.")),
			Model(TEXT("Laud"), ETNBuggyBodyStyle::Racer, 1500,
				NSLOCTEXT("Tortunabo", "BuggyModelLaud", "Laúd Bólido"),
				NSLOCTEXT("Tortunabo", "BuggyModelLaudDesc", "La tortuga laúd es la más rápida del mar, y este bólido también: caparazón de crestas, alerón gigante y escapes laterales.")),
		};
		return List;
	}

	const TArray<FTNBuggyPaintInfo>& Paints()
	{
		using namespace TNBuggyCosmeticsDetail;
		using P = ETNBuggyPattern;
		static const TArray<FTNBuggyPaintInfo> List = {
			Paint(nullptr, 0,
				NSLOCTEXT("Tortunabo", "BuggyPaintSerie", "Pintura de serie"),
				NSLOCTEXT("Tortunabo", "BuggyPaintSerieDesc", "La de fábrica: el buggy de serie lleva el color de tu equipo y las tortugas, su verde natural."),
				0x2F7A3F, 0x58B75A, 0x9ED36A, 0x1F5424, P::Plain),
			Paint(TEXT("Coral"), 300,
				NSLOCTEXT("Tortunabo", "BuggyPaintCoral", "Coral Bravo"),
				NSLOCTEXT("Tortunabo", "BuggyPaintCoralDesc", "Rojo coral para las que salen a ganar. Dicen que corre un diez por ciento más. Lo dicen."),
				0xC73A2A, 0xFF6A52, 0xFFC9A3, 0xFFE0C2, P::Plain, 1.f, 0.15f),
			Paint(TEXT("Marino"), 400,
				NSLOCTEXT("Tortunabo", "BuggyPaintMarino", "Azul Marino"),
				NSLOCTEXT("Tortunabo", "BuggyPaintMarinoDesc", "Azul de alta mar con olas de espuma. Para tortugas que echan de menos el océano."),
				0x12305A, 0x1E9CC6, 0xE0F8F5, 0x62D2EA, P::Waves, 1.2f, 0.1f),
			Paint(TEXT("Arena"), 300,
				NSLOCTEXT("Tortunabo", "BuggyPaintArena", "Arena Dorada"),
				NSLOCTEXT("Tortunabo", "BuggyPaintArenaDesc", "Del color de la playa donde naciste. Disimula de maravilla el barro."),
				0xC89B5E, 0xF5D9A3, 0xFFF2D4, 0xB07F45, P::Plain, 1.f, 0.1f),
			Paint(TEXT("Sandia"), 500,
				NSLOCTEXT("Tortunabo", "BuggyPaintSandia", "Sandía Veraniega"),
				NSLOCTEXT("Tortunabo", "BuggyPaintSandiaDesc", "Rayas de sandía y aletas rosas. Huele a verano, pero no se come."),
				0x2E7D32, 0x7CB342, 0xFF5A6E, 0x1B5E20, P::Melon),
			Paint(TEXT("Meta"), 800,
				NSLOCTEXT("Tortunabo", "BuggyPaintMeta", "Ajedrez de Meta"),
				NSLOCTEXT("Tortunabo", "BuggyPaintMetaDesc", "Cuadros de bandera de meta. Para que todos sepan dónde vas a acabar: delante."),
				0x1A1A1A, 0xF5F5F5, 0xFFCB3D, 0x151515, P::Checker, 1.f, 0.25f),
			Paint(TEXT("Lava"), 1200,
				NSLOCTEXT("Tortunabo", "BuggyPaintLava", "Lava Volcánica"),
				NSLOCTEXT("Tortunabo", "BuggyPaintLavaDesc", "Grietas de lava que brillan de verdad. No apto para tortugas frioleras... ni para las calurosas."),
				0x2B1B17, 0x3A2420, 0x6B2A1A, 0xFF6A1A, P::Lava, 1.f, 0.1f, 6.f),
			Paint(TEXT("Estrellas"), 1000,
				NSLOCTEXT("Tortunabo", "BuggyPaintEstrellas", "Noche Estrellada"),
				NSLOCTEXT("Tortunabo", "BuggyPaintEstrellasDesc", "Un cielo de estrellas pintado a mano. De noche brilla tanto que casi no hacen falta los faros."),
				0x0A1C38, 0x1B2F6B, 0x8F7BFF, 0xFFF2B0, P::Stars, 1.f, 0.2f, 4.f),
			Paint(TEXT("Mariquita"), 450,
				NSLOCTEXT("Tortunabo", "BuggyPaintMariquita", "Mariquita"),
				NSLOCTEXT("Tortunabo", "BuggyPaintMariquitaDesc", "Rojo con lunares negros. Trae suerte, o eso dice mi abuela, que tiene ciento veinte años."),
				0x1A1A1A, 0xE53935, 0x2A2A2A, 0x111111, P::Spots, 1.1f, 0.2f),
			Paint(TEXT("Medusa"), 500,
				NSLOCTEXT("Tortunabo", "BuggyPaintMedusa", "Medusa Rosa"),
				NSLOCTEXT("Tortunabo", "BuggyPaintMedusaDesc", "Rosa medusa con burbujitas. No pica, te lo prometo."),
				0xC2185B, 0xFF8FA3, 0xFFD1DC, 0xFFF0F5, P::Spots, 0.8f, 0.3f, 0.6f),
			Paint(TEXT("Oro"), 2000,
				NSLOCTEXT("Tortunabo", "BuggyPaintOro", "Oro Pirata"),
				NSLOCTEXT("Tortunabo", "BuggyPaintOroDesc", "Oro del galeón hundido, pulido a mano. Es lo más caro de la tienda, y se nota desde la luna."),
				0x7A5410, 0xFFC93D, 0xFFE7A0, 0xB8860B, P::Scutes, 1.f, 0.95f),
			Paint(TEXT("Plata"), 1500,
				NSLOCTEXT("Tortunabo", "BuggyPaintPlata", "Plata Pulida"),
				NSLOCTEXT("Tortunabo", "BuggyPaintPlataDesc", "Brilla como un espejo. Peinarse antes de cada carrera es obligatorio."),
				0x7D858E, 0xD9DEE3, 0x4E5A66, 0xFFFFFF, P::Plain, 1.f, 1.f),
			Paint(TEXT("Alga"), 600,
				NSLOCTEXT("Tortunabo", "BuggyPaintAlga", "Camuflaje de Alga"),
				NSLOCTEXT("Tortunabo", "BuggyPaintAlgaDesc", "Para esconderse entre las algas. Los cangrejos no te verán venir."),
				0x3B5323, 0x6B8E23, 0x8B7D5B, 0x23361A, P::Camo),
			Paint(TEXT("Llamas"), 1200,
				NSLOCTEXT("Tortunabo", "BuggyPaintLlamas", "Llamas Infernales"),
				NSLOCTEXT("Tortunabo", "BuggyPaintLlamasDesc", "Llamas de hot rod que salen del morro. No queman, pero dan muchísimo miedo."),
				0x141414, 0x262626, 0xD84315, 0xFF7A1A, P::Flames, 1.f, 0.4f, 1.5f),
			Paint(TEXT("Carreras"), 600,
				NSLOCTEXT("Tortunabo", "BuggyPaintCarreras", "Rayas de Carreras"),
				NSLOCTEXT("Tortunabo", "BuggyPaintCarrerasDesc", "Azul con dos rayas blancas, como los coches de carreras de antes. Más rápido no va, pero lo parece."),
				0x1565C0, 0x1E88E5, 0xF5F5F5, 0xFFFFFF, P::Stripes, 1.f, 0.35f),
			Paint(TEXT("Abisal"), 1400,
				NSLOCTEXT("Tortunabo", "BuggyPaintAbisal", "Abisal Luminosa"),
				NSLOCTEXT("Tortunabo", "BuggyPaintAbisalDesc", "Como los peces del fondo del mar: lunares que brillan en la oscuridad. Ideal para los túneles."),
				0x061428, 0x0B2545, 0x0E3A5A, 0x22F0FF, P::Spots, 0.7f, 0.2f, 5.f),
			Paint(TEXT("Carey"), 700,
				NSLOCTEXT("Tortunabo", "BuggyPaintCarey", "Carey"),
				NSLOCTEXT("Tortunabo", "BuggyPaintCareyDesc", "El dibujo de la tortuga carey, con su brillo de caramelo. Un clásico de la familia."),
				0x3E2412, 0xB5651D, 0xD9A066, 0x4A2410, P::Camo, 0.6f, 0.3f),
			Paint(TEXT("Hielo"), 500,
				NSLOCTEXT("Tortunabo", "BuggyPaintHielo", "Hielo Polar"),
				NSLOCTEXT("Tortunabo", "BuggyPaintHieloDesc", "Blanco y azul hielo. Para las tortugas que se escaparon del Ártico."),
				0xA9D8EE, 0xEAF7FF, 0x62D2EA, 0x8CC4E0, P::Scutes, 1.f, 0.45f),
			Paint(TEXT("Atardecer"), 700,
				NSLOCTEXT("Tortunabo", "BuggyPaintAtardecer", "Atardecer Tropical"),
				NSLOCTEXT("Tortunabo", "BuggyPaintAtardecerDesc", "Naranja, oro y morado de puesta de sol. Queda precioso en las fotos de la meta."),
				0xFF6A52, 0xFFCB3D, 0x7B3FA0, 0xFF9A80, P::Waves, 1.4f, 0.15f),
			Paint(TEXT("Tiburon"), 400,
				NSLOCTEXT("Tortunabo", "BuggyPaintTiburon", "Tiburón"),
				NSLOCTEXT("Tortunabo", "BuggyPaintTiburonDesc", "Gris tiburón con la barriga blanca. Los demás buggies se apartan solos."),
				0x4F5D6A, 0x7E8E9C, 0xF0F0F0, 0xF0F0F0, P::Plain, 1.f, 0.25f),
			Paint(TEXT("Pulpo"), 550,
				NSLOCTEXT("Tortunabo", "BuggyPaintPulpo", "Pulpo Morado"),
				NSLOCTEXT("Tortunabo", "BuggyPaintPulpoDesc", "Morado de pulpo con ventosas de mentira. Ocho brazos para el volante, por si acaso."),
				0x4A148C, 0x8E24AA, 0xCE93D8, 0xE1BEE7, P::Spots, 0.6f, 0.2f),
			Paint(TEXT("Caramelo"), 500,
				NSLOCTEXT("Tortunabo", "BuggyPaintCaramelo", "Caramelo de Feria"),
				NSLOCTEXT("Tortunabo", "BuggyPaintCarameloDesc", "Rosa y blanco como un algodón de azúcar. Pegajoso no es: lo he comprobado."),
				0xFF8FA3, 0xFFFFFF, 0x62D2EA, 0xFF4F7B, P::Stripes, 1.f, 0.3f),
		};
		return List;
	}

	const FTNBuggyModelInfo* FindModel(FName Id)
	{
		return Models().FindByPredicate([Id](const FTNBuggyModelInfo& Info) { return Info.Id == Id; });
	}

	const FTNBuggyPaintInfo* FindPaint(FName Id)
	{
		return Paints().FindByPredicate([Id](const FTNBuggyPaintInfo& Info) { return Info.Id == Id; });
	}

	const FTNBuggyModelInfo& ResolveModel(FName Id)
	{
		const FTNBuggyModelInfo* Found = FindModel(Id);
		return Found ? *Found : Models()[0];
	}

	const FTNBuggyPaintInfo& ResolvePaint(FName Id)
	{
		const FTNBuggyPaintInfo* Found = FindPaint(Id);
		return Found ? *Found : Paints()[0];
	}

	TArray<FName> CatalogIds(ETNCosmeticCategory Category)
	{
		TArray<FName> Out;
		if (Category == ETNCosmeticCategory::BuggyModel)
		{
			for (const FTNBuggyModelInfo& Info : Models()) { if (Info.Id != NAME_None) { Out.Add(Info.Id); } }
		}
		else if (Category == ETNCosmeticCategory::BuggyPaint)
		{
			for (const FTNBuggyPaintInfo& Info : Paints()) { if (Info.Id != NAME_None) { Out.Add(Info.Id); } }
		}
		return Out;
	}

	int32 PriceOf(ETNCosmeticCategory Category, FName Id)
	{
		if (Id == NAME_None) { return 0; }
		if (Category == ETNCosmeticCategory::BuggyModel)
		{
			const FTNBuggyModelInfo* Info = FindModel(Id);
			return Info ? FMath::Max(0, Info->Price) : 0;
		}
		if (Category == ETNCosmeticCategory::BuggyPaint)
		{
			const FTNBuggyPaintInfo* Info = FindPaint(Id);
			return Info ? FMath::Max(0, Info->Price) : 0;
		}
		return 0;
	}

	bool IsKnown(ETNCosmeticCategory Category, FName Id)
	{
		if (Category == ETNCosmeticCategory::BuggyModel) { return FindModel(Id) != nullptr; }
		if (Category == ETNCosmeticCategory::BuggyPaint) { return FindPaint(Id) != nullptr; }
		return false;
	}

	bool CategoryOf(FName Id, ETNCosmeticCategory& OutCategory)
	{
		if (Id == NAME_None) { return false; }
		if (FindModel(Id)) { OutCategory = ETNCosmeticCategory::BuggyModel; return true; }
		if (FindPaint(Id)) { OutCategory = ETNCosmeticCategory::BuggyPaint; return true; }
		return false;
	}

	bool IsKnownBuggyId(FName Id)
	{
		ETNCosmeticCategory Ignored;
		return CategoryOf(Id, Ignored);
	}

	bool CanEquip(const FTN_BuggyLook& Look, const TSet<FName>& Unlocked)
	{
		const auto Allowed = [&Unlocked](ETNCosmeticCategory Category, FName Id)
		{
			return Id == NAME_None || (IsKnown(Category, Id) && (PriceOf(Category, Id) == 0 || Unlocked.Contains(Id)));
		};
		return Allowed(ETNCosmeticCategory::BuggyModel, Look.ModelId) && Allowed(ETNCosmeticCategory::BuggyPaint, Look.PaintId);
	}

	FTN_BuggyLook Sanitize(const FTN_BuggyLook& Look)
	{
		FTN_BuggyLook Out;
		Out.ModelId = FindModel(Look.ModelId) ? Look.ModelId : NAME_None;
		Out.PaintId = FindPaint(Look.PaintId) ? Look.PaintId : NAME_None;
		return Out;
	}

	bool FilterKnownIds(const TArray<FName>& Ids, int32 MaxIds, TSet<FName>& OutKnown)
	{
		if (Ids.Num() > MaxIds) { return false; }
		OutKnown.Reset();
		for (const FName Id : Ids)
		{
			if (IsKnownBuggyId(Id)) { OutKnown.Add(Id); }
		}
		return true;
	}

	FString LookKey(const FTN_BuggyLook& Look)
	{
		return FString::Printf(TEXT("%s|%s"), *Look.ModelId.ToString(), *Look.PaintId.ToString());
	}
}
