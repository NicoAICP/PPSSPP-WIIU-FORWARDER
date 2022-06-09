#pragma once

#include "InputDevice.h"
#include "Xinput.h"
#include "Core/HLE/sceCtrl.h"

class XinputDevice final : public InputDevice {
public:
	XinputDevice();
	~XinputDevice();
	virtual int UpdateState() override;

private:
	void UpdatePad(int pad, const XINPUT_STATE &state, XINPUT_VIBRATION &vibration);
	void ApplyButtons(int pad, const XINPUT_STATE &state);
	void ApplyVibration(int pad, XINPUT_VIBRATION &vibration);
	int check_delay[4]{};
	XINPUT_STATE prevState[4]{};
	XINPUT_VIBRATION prevVibration[4]{};
	double prevVibrationTime = 0.0;
	u32 prevButtons[4]{};
};
