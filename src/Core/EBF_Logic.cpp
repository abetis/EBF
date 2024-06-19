#include "EBF_Logic.h"
#include "EBF_HalInstance.h"

/// EBF_Logic implementation
EBF_Logic *EBF_Logic::pStaticInstance = new EBF_Logic();

#ifdef EBF_USE_INTERRUPTS

#define IMPLEMENT_EBF_ISR(interrupt) \
	void EBF_ISR_Handler_ ## interrupt() { \
		EBF_Logic::GetInstance()->HandleIsr(interrupt); \
	}

#if defined(ARDUINO_ARCH_AVR)
#if EXTERNAL_NUM_INTERRUPTS > 8
    #error Up to 8 external interrupts are currently supported for AVR platform
#endif
#if EXTERNAL_NUM_INTERRUPTS > 7
    IMPLEMENT_EBF_ISR(7)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 6
    IMPLEMENT_EBF_ISR(6)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 5
    IMPLEMENT_EBF_ISR(5)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 4
    IMPLEMENT_EBF_ISR(4)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 3
    IMPLEMENT_EBF_ISR(3)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 2
    IMPLEMENT_EBF_ISR(2)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 1
    IMPLEMENT_EBF_ISR(1)
#endif
#if EXTERNAL_NUM_INTERRUPTS > 0
    IMPLEMENT_EBF_ISR(0)
#endif
#else
	#error Current board type is not supported
#endif

#endif

EBF_Logic::EBF_Logic()
{
	pHalInstances = NULL;
	halIndex = 0;
#ifdef EBF_USE_INTERRUPTS
	isRunFromISR = 0;
#endif

#ifdef EBF_SLEEP_IMPLEMENTATION
	this->SleepConstructor();
#endif
}

EBF_Logic *EBF_Logic::GetInstance()
{
	return pStaticInstance;
}

uint8_t EBF_Logic::Init(uint8_t maxTimers, uint8_t queueSize)
{
	uint8_t rc;

#ifdef EBF_USE_INTERRUPTS
	for (uint8_t i=0; i<EXTERNAL_NUM_INTERRUPTS; i++) {
		pHalIsr[i] = NULL;
	}

	rc = this->msgQueue.Init(queueSize);
	if (rc != EBF_OK) {
		return rc;
	}

	rc = this->timers.Init(maxTimers, &msgQueue);
#else
	rc = this->timers.Init(maxTimers, NULL);
#endif
	if (rc != EBF_OK) {
		return rc;
	}

#ifdef EBF_SLEEP_IMPLEMENTATION
		InitSleep();
#endif

	if (EBF_HalInstance::GetNumberOfInstances() > 0) {
		pHalInstances = (EBF_HalInstance**)malloc(sizeof(EBF_HalInstance*) * EBF_HalInstance::GetNumberOfInstances());

		if (pHalInstances == NULL) {
			return EBF_NOT_ENOUGH_MEMORY;
		}
	}

	return EBF_OK;
}

uint8_t EBF_Logic::AddHalInstance(EBF_HalInstance &instance)
{
	if (halIndex >= EBF_HalInstance::GetNumberOfInstances()) {
		return EBF_INDEX_OUT_OF_BOUNDS;
	}

	pHalInstances[halIndex] = &instance;
	halIndex++;

	return EBF_OK;
}

uint8_t EBF_Logic::Process()
{
	uint8_t i;
	uint16_t delayWanted = EBF_NO_POLLING;
	EBF_HalInstance *pHal;
	unsigned long ms;

	// Start counting time before the execution of the callbacks, that might take some time
	uint32_t start = this->micros();

	// Process timers
	delayWanted = timers.Process(start);

	// Process HALs
	for (i=0; i<halIndex; i++) {
		pHal = pHalInstances[i];

		if (pHal->GetPollingInterval() == EBF_NO_POLLING) {
			continue;
		}

		ms = this->millis();

		if (ms - pHal->GetLastPollMillis() > pHal->GetPollingInterval()) {
			pHal->SetLastPollMillis(ms);

			pHal->Process();

			if (pHal->GetPollingInterval() < delayWanted) {
				delayWanted = pHal->GetPollingInterval();
			}
		} else {
			uint16_t pollWanted = pHal->GetPollingInterval() - (ms - pHal->GetLastPollMillis());

			if (pollWanted < delayWanted) {
				delayWanted = pollWanted;
			}
		}
	}

	// Should give other things some CPU time
	if (delayWanted == 0) {
		delayWanted = 1;
	}

	//Serial.print("Wanted delay: ");
	//Serial.println(delayWanted);

#ifdef EBF_SLEEP_IMPLEMENTATION
	// Try to power down the CPU for some time...
	if (delayWanted > 1 && sleepMode != EBF_SleepMode::EBF_NO_SLEEP) {
		// Enter sleep mode for delayWanted timer
		EnterSleep(delayWanted);

		// Since we don't know how long the power save lasted, just return the control.
		// The calculations will be done again when Process() will be called as part of the loop.
		return EBF_OK;
	}
#endif

	// Implementing our own delay loop, so we could check the messages from interrupts in the loop
	while (delayWanted > 0) {
		yield();

#ifdef EBF_USE_INTERRUPTS
		EBF_MessageQueue::MessageEntry msg;
		uint16_t rc;
		EBF_HalInstance *pHalInstance;

		// Messages are used to pass information from interrupts to normal run
		if (msgQueue.GetMessagesNumber() > 0) {
			rc = msgQueue.GetMessage(msg);

			if (rc == EBF_OK) {
				pHalInstance = (EBF_HalInstance*)msg.param1;

				pHalInstance->Process();
			}
		}
#endif
		while ( delayWanted > 0 && (this->micros() - start) >= 1000) {
			delayWanted--;
			start += 1000;
		}
	}

	return EBF_OK;
}

EBF_HalInstance *EBF_Logic::GetHalInstance(EBF_HalInstance::HAL_Type type, uint8_t id)
{
	uint8_t i;

	for (i=0; i<halIndex; i++) {
		if (pHalInstances[i]->GetType() == type && pHalInstances[i]->GetId() == id) {
			return pHalInstances[i];
		}
	}

	return NULL;
}

#ifdef EBF_USE_INTERRUPTS
uint8_t EBF_Logic::AttachInterrupt(uint8_t interruptNumber, EBF_HalInstance *pHalInstance, uint8_t mode)
{
	if (interruptNumber > EXTERNAL_NUM_INTERRUPTS) {
		return EBF_INDEX_OUT_OF_BOUNDS;
	}

	pHalIsr[interruptNumber] = pHalInstance;

#if defined(ARDUINO_ARCH_AVR)
	switch (interruptNumber)
	{
#if EXTERNAL_NUM_INTERRUPTS > 8
    #error Up to 8 external interrupts are currently supported for AVR platform
#endif
#if EXTERNAL_NUM_INTERRUPTS > 7
	case 7:
		attachInterrupt(7, EBF_ISR_Handler_7, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 6
	case 6:
		attachInterrupt(6, EBF_ISR_Handler_6, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 5
	case 5:
		attachInterrupt(5, EBF_ISR_Handler_5, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 4
	case 4:
		attachInterrupt(4, EBF_ISR_Handler_4, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 3
	case 3:
		attachInterrupt(3, EBF_ISR_Handler_3, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 2
	case 2:
		attachInterrupt(2, EBF_ISR_Handler_2, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 1
	case 1:
		attachInterrupt(1, EBF_ISR_Handler_1, mode);
		break;
#endif
#if EXTERNAL_NUM_INTERRUPTS > 0
	case 0:
		attachInterrupt(0, EBF_ISR_Handler_0, mode);
		break;
#endif

	default:
		break;
	}
#else
	#error Current board type is not supported
#endif

	return EBF_OK;
}

void EBF_Logic::HandleIsr(uint8_t interruptNumber)
{
	if (pHalIsr[interruptNumber] != NULL) {
		// Next process call is done from ISR
		isRunFromISR = 1;
		pHalIsr[interruptNumber]->ProcessCallback();
		isRunFromISR = 0;
	}
}

uint8_t EBF_Logic::ProcessInterrupt(EBF_HalInstance *pHalInstance)
{
	EBF_MessageQueue::MessageEntry msg;

	memset(&msg, 0, sizeof(EBF_MessageQueue::MessageEntry));
	msg.param1 = (uint32_t)pHalInstance;

	return msgQueue.AddMessage(msg);
}
#endif

#ifdef EBF_SLEEP_IMPLEMENTATION

#if defined(ARDUINO_ARCH_SAMD)
void EBF_Logic::SleepConstructor()
{
	sleepMode = EBF_SleepMode::EBF_NO_SLEEP;
	microsAddition = 0;

	// Disable all pins (input, no pullup, no input buffer)
	for (uint32_t ulPin = 0 ; ulPin < NUM_DIGITAL_PINS ; ulPin++) {
		PORT->Group[g_APinDescription[ulPin].ulPort].PINCFG[g_APinDescription[ulPin].ulPin].reg = (uint8_t)(PORT_PINCFG_RESETVALUE);
		PORT->Group[g_APinDescription[ulPin].ulPort].DIRCLR.reg = (uint32_t)(1<<g_APinDescription[ulPin].ulPin);
	}

	// reset_gclks
	for (uint8_t i = 1; i < GCLK_GEN_NUM; i++) {
		GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(i);
		while (GCLK->STATUS.bit.SYNCBUSY);

		GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(i);
		while (GCLK->STATUS.bit.SYNCBUSY);
	}

	// connect all peripherial to a dead clock
	for (byte i = 1; i < 37; i++) {
		GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(i) | GCLK_CLKCTRL_GEN(4) | GCLK_CLKCTRL_CLKEN;
		while (GCLK->STATUS.bit.SYNCBUSY);
	}
}

void RTC_Handler(void)
{
	// Disable RTC
	RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_ENABLE;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// And set the counter to 0, in case it moved on
	RTC->MODE0.COUNT.reg = 0;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// must clear flag at end
	RTC->MODE0.INTFLAG.reg |= (RTC_MODE0_INTFLAG_CMP0 | RTC_MODE0_INTFLAG_OVF);
}

uint8_t EBF_Logic::InitSleep()
{

	// Based on ArduinoLowPower library, configGCLK6() function
	// https://github.com/arduino-libraries/ArduinoLowPower/blob/master/src/samd/ArduinoLowPower.cpp
	// And on ArduinoLowPower library, "Added support for Mode 0 and Mode 1" PR
	// https://github.com/arduino-libraries/RTCZero/pull/58

	// turn on digital interface clock
	PM->APBAMASK.reg |= PM_APBAMASK_RTC;

	// Generic clock 2 will have divider = 4 (/32), with 32K crystal it gives tick about every mSec
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(2) | GCLK_GENDIV_DIV(4);
	while (GCLK->STATUS.bit.SYNCBUSY);

	// Using UltraLowPower 32K internal crystal for generic clock 2
	GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_OSCULP32K | GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_DIVSEL | GCLK_GENCTRL_RUNSTDBY);
	while (GCLK->STATUS.bit.SYNCBUSY);

	GCLK->CLKCTRL.reg = (uint32_t)((GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2 | GCLK_GENCTRL_ID(RTC_GCLK_ID)));
	while (GCLK->STATUS.bit.SYNCBUSY);

	// Disable RTC after initialization
	RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_ENABLE;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// SW reset
	RTC->MODE0.CTRL.reg |= RTC_MODE0_CTRL_SWRST;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// Configure MODE0 with prescaler=1, tick per about 1 mSec
	RTC->MODE0.CTRL.reg = RTC_MODE0_CTRL_MODE_COUNT32 | RTC_MODE0_CTRL_PRESCALER_DIV1;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// We will use overflow interrupt to wake the CPU
	RTC->MODE0.INTENSET.reg |= RTC_MODE0_INTENSET_OVF;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// Enable RTC interrupt in interrupt controller
	NVIC_EnableIRQ(RTC_IRQn);

	// Clear SW reset
	RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_SWRST;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	/* Errata: Make sure that the Flash does not power all the way down
     	* when in sleep mode. */

	NVMCTRL->CTRLB.bit.SLEEPPRM = NVMCTRL_CTRLB_SLEEPPRM_DISABLED_Val;

	// TODO: Check if other modules can be disabled to save more power

	return EBF_OK;
}

// Based on ArduinoLowPower
// https://github.com/arduino-libraries/ArduinoLowPower/blob/master/src/samd/ArduinoLowPower.cpp
uint8_t EBF_Logic::EnterSleep(uint16_t msSleep)
{
	uint32_t timerCnt;
	bool restoreUSBDevice = false;
	uint32_t apbBMask;
	uint32_t apbCMask;

	// No sleep needed
	if (sleepMode == EBF_SleepMode::EBF_NO_SLEEP) {
		return EBF_OK;
	}
	/* Errata: Make sure that the Flash does not power all the way down
     	* when in sleep mode. */

	NVMCTRL->CTRLB.bit.SLEEPPRM = NVMCTRL_CTRLB_SLEEPPRM_DISABLED_Val;

	// Count up to the overflow (-1)
	RTC->MODE0.COUNT.reg = (uint32_t)(-1) - msSleep;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// Save the sleep time for micros/millis correction
	this->sleepMs = msSleep;

	// Enable the RTC
	RTC->MODE0.CTRL.reg |= RTC_MODE0_CTRL_ENABLE;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	switch (sleepMode)
	{
	case EBF_SleepMode::EBF_SLEEP_LIGHT:
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
		PM->SLEEP.reg = 2;
		__DSB();
		__WFI();
		break;

	case EBF_SleepMode::EBF_SLEEP_DEEP:
	    // Turn down anything that will not be used during deep sleep
		SYSCTRL->OSC8M.bit.ENABLE = 0;
    	SYSCTRL->BOD33.bit.ENABLE = 0;

		// Save currently running modules setup
		apbBMask = PM->APBBMASK.reg;
		apbCMask = PM->APBCMASK.reg;

		PM->APBBMASK.bit.DMAC_ = 0;
		PM->APBBMASK.bit.USB_ = 0;
		PM->APBCMASK.reg = 0;

		if (SERIAL_PORT_USBVIRTUAL) {
			USBDevice.standby();
		} else {
			USBDevice.detach();
			restoreUSBDevice = true;
		}
		// Disable systick interrupt:  See https://www.avrfreaks.net/forum/samd21-samd21e16b-sporadically-locks-and-does-not-wake-standby-sleep-mode
		SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
		SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
		__DSB();
		__WFI();
		// Enable systick interrupt
		SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
		if (restoreUSBDevice) {
			USBDevice.attach();
		}

		// Restore currently running modules
		PM->APBCMASK.reg = apbCMask;
		PM->APBBMASK.reg = apbBMask;
		break;

	default:
		break;
	}


	// We're after the sleep
	// Disable the RTC
	RTC->MODE0.CTRL.reg &= ~RTC_MODE0_CTRL_ENABLE;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

	// Get RTC counter
	RTC->MODE0.READREQ.reg = RTC_READREQ_RREQ;
	while (RTC->MODE0.STATUS.bit.SYNCBUSY);

  	timerCnt = RTC->MODE0.COUNT.reg;
	if (timerCnt == 0) {
		// RTC timer overflow, the ISR stops and zeroes the counter, so we can be sure it will be 0 in that case
		// We slept the whole period we wanted, advance the additions counter
		microsAddition += sleepMs*1000;
	} else {
		// RTC didn't overflow yet, some other reason woke the CPU from sleep
		// Advance the additions counter with the delta
		microsAddition += (sleepMs - ((uint32_t)(-1) - timerCnt)) * 1000;
	}

	// Back to normal CPU oreration

	return EBF_OK;
}
#else
	#error Current board type is not supported
#endif

#endif
