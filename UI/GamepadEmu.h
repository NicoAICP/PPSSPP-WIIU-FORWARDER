// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Common/Input/InputState.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Core/CoreParameter.h"

class GamepadView : public UI::View {
public:
	GamepadView(UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	bool Key(const KeyInput &input) override {
		return false;
	}
	void Update() override;

protected:
	virtual float GetButtonOpacity();

	double lastFrameTime_;
	float secondsWithoutTouch_ = 0.0;
};

class MultiTouchButton : public GamepadView {
public:
	MultiTouchButton(ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: GamepadView(layoutParams), scale_(scale), bgImg_(bgImg), bgDownImg_(bgDownImg), img_(img) {
	}

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	virtual bool IsDown() { return pointerDownMask_ != 0; }
	// chainable
	MultiTouchButton *FlipImageH(bool flip) { flipImageH_ = flip; return this; }
	MultiTouchButton *SetAngle(float angle) { angle_ = angle; bgAngle_ = angle; return this; }
	MultiTouchButton *SetAngle(float angle, float bgAngle) { angle_ = angle; bgAngle_ = bgAngle; return this; }

protected:
	uint32_t pointerDownMask_ = 0;
	float scale_;

private:
	ImageID bgImg_;
	ImageID bgDownImg_;
	ImageID img_;
	float bgAngle_ = 0.0f;
	float angle_ = 0.0f;
	bool flipImageH_ = false;
};

class BoolButton : public MultiTouchButton {
public:
	BoolButton(bool *value, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), value_(value) {

	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override { return *value_; }

	UI::Event OnChange;

private:
	bool *value_;
};

class FPSLimitButton : public MultiTouchButton {
public:
	FPSLimitButton(FPSLimit limit, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), limit_(limit) {

	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override;

private:
	FPSLimit limit_;
};

class RapidFireButton : public MultiTouchButton {
public:
	RapidFireButton(ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams) {
	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override;
};

class AnalogRotationButton : public MultiTouchButton {
public:
	AnalogRotationButton(bool clockWise, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), clockWise_(clockWise) {
	}
	void Touch(const TouchInput &input) override;
	void Update() override;

private:
	bool autoRotating_ = false;
	bool clockWise_;
};

class PSPButton : public MultiTouchButton {
public:
	PSPButton(int pspButtonBit, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit) {
	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override;

private:
	int pspButtonBit_;
};

class PSPDpad : public GamepadView {
public:
	PSPDpad(ImageID arrowIndex, ImageID arrowDownIndex, ImageID overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	void ProcessTouch(float x, float y, bool down);
	ImageID arrowIndex_;
	ImageID arrowDownIndex_;
	ImageID overlayIndex_;

	float scale_;
	float spacing_;

	int dragPointerId_;
	int down_;
};

class PSPStick : public GamepadView {
public:
	PSPStick(ImageID bgImg, ImageID stickImg, ImageID stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

protected:
	int dragPointerId_;
	ImageID bgImg_;
	ImageID stickImageIndex_;
	ImageID stickDownImg_;

	int stick_;
	float stick_size_;
	float scale_;

	float centerX_;
	float centerY_;

private:
	void ProcessTouch(float x, float y, bool down);
};

class PSPCustomStick : public PSPStick {
public:
	PSPCustomStick(ImageID bgImg, ImageID stickImg, ImageID stickDownImg, float scale, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;

private:
	void ProcessTouch(float x, float y, bool down);

	float posX_ = 0.0f;
	float posY_ = 0.0f;
};

//initializes the layout from Config. if a default layout does not exist,
//it sets up default values
void InitPadLayout(float xres, float yres, float globalScale = 1.15f);
UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause);

const int D_pad_Radius = 50;
const int baseActionButtonSpacing = 60;

class ComboKey : public MultiTouchButton {
public:
	ComboKey(int pspButtonBit, bool toggle, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit), toggle_(toggle) {
	}
	void Touch(const TouchInput &input) override;
private:
	int pspButtonBit_;
	bool toggle_;
};
