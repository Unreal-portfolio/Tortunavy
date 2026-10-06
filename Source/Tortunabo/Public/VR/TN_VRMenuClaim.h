#pragma once

#include "CoreMinimal.h"

/**
 * X e Y de los mandos Touch llegan a los menús como la X y la Y de un mando normal antes de caer en aceptar o atrás (#648).
 * Un widget que de verdad usa X o Y para algo propio (borrar un carácter del código de sala, refrescar la lista de salas,
 * devolver una tecla a la de serie) lo anota aquí al actuar; así FTNVRInputProcessor distingue a ese widget de los que se
 * tragan toda tecla para que no llegue al juego (la pausa): que Slate dé la tecla por atendida no basta, y sin esta
 * distinción X no aceptaba ni Y iba atrás.
 *
 * Es solo un contador: el procesador lo lee antes y después de mandar la tecla (Slate la entrega en el momento). Con un
 * mando de verdad nadie lo lee y anotar no cuesta nada.
 */
namespace TNVRMenuClaim
{
	inline uint32& Counter()
	{
		static uint32 Value = 0;
		return Value;
	}

	/** El widget que atiende X o Y de verdad lo llama justo antes de devolver «atendida». */
	inline void Claim()
	{
		++Counter();
	}

	/** Cuántas veces se ha anotado (solo importa si cambia entre dos lecturas). */
	inline uint32 Count()
	{
		return Counter();
	}
}
