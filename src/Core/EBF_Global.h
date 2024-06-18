#ifndef __EBF_GLOBAL_H__
#define __EBF_GLOBAL_H__

#define EBF_NO_POLLING 0xFFFF
typedef void (*EBF_CallbackType)();

enum EBF_ERROR_CODE : uint8_t {
	EBF_OK = 0,

	EBF_NOT_ENOUGH_MEMORY,
	EBF_INDEX_OUT_OF_BOUNDS,
	EBF_RESOURCE_IS_IN_USE,
	EBF_NOT_INITIALIZED,
	EBF_INVALID_STATE,
};

enum EBF_SleepMode : uint8_t {
	EBF_NO_SLEEP = 0,
	EBF_SLEEP_LIGHT,
	EBF_SLEEP_DEEP = 255
};

#endif
