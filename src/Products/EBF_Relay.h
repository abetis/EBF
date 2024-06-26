#ifndef __EBF_RELAY_H__
#define __EBF_RELAY_H__

#include <Arduino.h>
#if __has_include("EBF_Config.h")
	#include "EBF_Config.h"
#endif

#include "../Core/EBF_Global.h"
#include "../Core/EBF_DigitalOutput.h"

class EBF_Relay : protected EBF_DigitalOutput {
	public:
		uint8_t Init(uint8_t pinNumber);
		uint8_t SetValue(uint8_t value);

		uint8_t On();
		uint8_t Off();

	private:
		enum RelayState {
			RELAY_OFF = 0,
			RELAY_ON,
		};

		RelayState state;

		uint8_t Process();
};

#endif
