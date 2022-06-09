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

#include <algorithm>

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Math/math_util.h"
#include "Common/UI/Context.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "UI/GamepadEmu.h"

static u32 GetButtonColor() {
	return g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080;
}

GamepadView::GamepadView(UI::LayoutParams *layoutParams) : UI::View(layoutParams) {
	lastFrameTime_ = time_now_d();
}

void GamepadView::Touch(const TouchInput &input) {
	secondsWithoutTouch_ = 0.0f;
}

void GamepadView::Update() {
	const double now = time_now_d();
	float delta = now - lastFrameTime_;
	if (delta > 0) {
		secondsWithoutTouch_ += delta;
	}
	lastFrameTime_ = now;
}

float GamepadView::GetButtonOpacity() {
	float fadeAfterSeconds = g_Config.iTouchButtonHideSeconds;
	float fadeTransitionSeconds = std::min(fadeAfterSeconds, 0.5f);
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;

	float multiplier = 1.0f;
	if (secondsWithoutTouch_ >= fadeAfterSeconds && fadeAfterSeconds > 0.0f) {
		if (secondsWithoutTouch_ >= fadeAfterSeconds + fadeTransitionSeconds) {
			multiplier = 0.0f;
		} else {
			float secondsIntoFade = secondsWithoutTouch_ - fadeAfterSeconds;
			multiplier = 1.0f - (secondsIntoFade / fadeTransitionSeconds);
		}
	}

	return opacity * multiplier;
}

void MultiTouchButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(bgImg_);
	if (image) {
		w = image->w * scale_;
		h = image->h * scale_;
	} else {
		w = 0.0f;
		h = 0.0f;
	}
}

void MultiTouchButton::Touch(const TouchInput &input) {
	GamepadView::Touch(input);
	if ((input.flags & TOUCH_DOWN) && bounds_.Contains(input.x, input.y)) {
		pointerDownMask_ |= 1 << input.id;
	}
	if (input.flags & TOUCH_MOVE) {
		if (bounds_.Contains(input.x, input.y))
			pointerDownMask_ |= 1 << input.id;
		else
			pointerDownMask_ &= ~(1 << input.id);
	}
	if (input.flags & TOUCH_UP) {
		pointerDownMask_ &= ~(1 << input.id);
	}
	if (input.flags & TOUCH_RELEASE_ALL) {
		pointerDownMask_ = 0;
	}
}

void MultiTouchButton::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	float scale = scale_;
	if (IsDown()) {
		if (g_Config.iTouchButtonStyle == 2) {
			opacity *= 1.35f;
		} else {
			scale *= 2.0f;
			opacity *= 1.15f;
		}
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t downBg = colorAlpha(0xFFFFFF, opacity * 0.5f);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	if (IsDown() && g_Config.iTouchButtonStyle == 2) {
		if (bgImg_ != bgDownImg_)
			dc.Draw()->DrawImageRotated(bgDownImg_, bounds_.centerX(), bounds_.centerY(), scale, bgAngle_ * (M_PI * 2 / 360.0f), downBg, flipImageH_);
	}

	dc.Draw()->DrawImageRotated(bgImg_, bounds_.centerX(), bounds_.centerY(), scale, bgAngle_ * (M_PI * 2 / 360.0f), colorBg, flipImageH_);

	int y = bounds_.centerY();
	// Hack round the fact that the center of the rectangular picture the triangle is contained in
	// is not at the "weight center" of the triangle.
	if (img_ == ImageID("I_TRIANGLE"))
		y -= 2.8f * scale;
	dc.Draw()->DrawImageRotated(img_, bounds_.centerX(), y, scale, angle_ * (M_PI * 2 / 360.0f), color);
}

void BoolButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;

	if (down != lastDown) {
		*value_ = down;
		UI::EventParams params{ this };
		params.a = down;
		OnChange.Trigger(params);
	}
}

void FPSLimitButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;

	if (!down && lastDown && IsDown()) {
		PSP_CoreParameter().fpsLimit = FPSLimit::NORMAL;
	} else if (down && !lastDown && PSP_CoreParameter().fpsLimit == FPSLimit::NORMAL) {
		int limit = limit_ == FPSLimit::CUSTOM1 ? g_Config.iFpsLimit1 : g_Config.iFpsLimit2;
		// Validate it actually has a setting (may this should override visible?)
		if (limit >= 0) {
			PSP_CoreParameter().fpsLimit = limit_;
		}
	}
}

bool FPSLimitButton::IsDown() {
	return PSP_CoreParameter().fpsLimit == limit_;
}

void RapidFireButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		__CtrlSetRapidFire(!__CtrlGetRapidFire());
	}
}

bool RapidFireButton::IsDown() {
	return __CtrlGetRapidFire();
}

void AnalogRotationButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		autoRotating_ = true;
	} else if (lastDown && !down) {
		autoRotating_ = false;
		__CtrlSetAnalogX(0.0f, 0);
		__CtrlSetAnalogY(0.0f, 0);
	}
}

void AnalogRotationButton::Update() {
	const float now = time_now_d();
	float delta = now - lastFrameTime_;
	if (delta > 0) {
		secondsWithoutTouch_ += delta;
	}
	lastFrameTime_ = now;

	if (autoRotating_) {
		float speed = clockWise_ ? -g_Config.fAnalogAutoRotSpeed : g_Config.fAnalogAutoRotSpeed;
		// Clamp to a square
		__CtrlSetAnalogX(std::min(1.0f, std::max(-1.0f, 1.42f*cosf(now*speed))), 0);
		__CtrlSetAnalogY(std::min(1.0f, std::max(-1.0f, 1.42f*sinf(now*speed))), 0);
	}
}

void PSPButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		if (g_Config.bHapticFeedback) {
			Vibrate(HAPTIC_VIRTUAL_KEY);
		}
		__CtrlButtonDown(pspButtonBit_);
	} else if (lastDown && !down) {
		__CtrlButtonUp(pspButtonBit_);
	}
}

void ComboKey::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	static const int combo[16] = {CTRL_SQUARE ,CTRL_TRIANGLE ,CTRL_CIRCLE ,CTRL_CROSS ,CTRL_UP ,CTRL_DOWN ,CTRL_LEFT ,CTRL_RIGHT ,CTRL_START ,CTRL_SELECT ,CTRL_LTRIGGER ,CTRL_RTRIGGER };
	if (down || lastDown) {
		for (int i = 0; i < 16; i++) {
			if (pspButtonBit_ & combo[i])
			{
				if (down && !lastDown) {
					if (g_Config.bHapticFeedback) {
						Vibrate(HAPTIC_VIRTUAL_KEY);
					}
					if (!toggle_) {
						__CtrlButtonDown(combo[i]);
					} else {
						if (__CtrlPeekButtons() & combo[i])
							__CtrlButtonUp(combo[i]);
						else
							__CtrlButtonDown(combo[i]);
					}
				}
				else if (lastDown && !down && !toggle_) {
					__CtrlButtonUp(combo[i]);
				}
			}
		}
	}
}

bool PSPButton::IsDown() {
	return (__CtrlPeekButtons() & pspButtonBit_) != 0;
}

PSPDpad::PSPDpad(ImageID arrowIndex, ImageID arrowDownIndex, ImageID overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams)
	: GamepadView(layoutParams), arrowIndex_(arrowIndex), arrowDownIndex_(arrowDownIndex), overlayIndex_(overlayIndex),
		scale_(scale), spacing_(spacing), dragPointerId_(-1), down_(0) {
}

void PSPDpad::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = D_pad_Radius * spacing_ * 4;
	h = D_pad_Radius * spacing_ * 4;
}

void PSPDpad::Touch(const TouchInput &input) {
	int lastDown = down_;
	GamepadView::Touch(input);

	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPDpad::ProcessTouch(float x, float y, bool down) {
	float stick_size = spacing_ * D_pad_Radius * scale_;
	float inv_stick_size = 1.0f / stick_size;
	const float deadzone = 0.17f;

	float dx = (x - bounds_.centerX()) * inv_stick_size;
	float dy = (y - bounds_.centerY()) * inv_stick_size;
	float rad = sqrtf(dx*dx + dy*dy);
	if (rad < deadzone || rad > 2.0f)
		down = false;

	int ctrlMask = 0;
	int lastDown = down_;

	bool fourWay = g_Config.bDisableDpadDiagonals || rad < 0.7f;
	if (down) {
		if (fourWay) {
			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 4) + 0.5f)) & 3;
			switch (direction) {
			case 0: ctrlMask |= CTRL_RIGHT; break;
			case 1: ctrlMask |= CTRL_DOWN; break;
			case 2: ctrlMask |= CTRL_LEFT; break;
			case 3: ctrlMask |= CTRL_UP; break;
			}
			// 4 way pad
		} else {
			// 8 way pad
			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 8) + 0.5f)) & 7;
			switch (direction) {
			case 0: ctrlMask |= CTRL_RIGHT; break;
			case 1: ctrlMask |= CTRL_RIGHT | CTRL_DOWN; break;
			case 2: ctrlMask |= CTRL_DOWN; break;
			case 3: ctrlMask |= CTRL_DOWN | CTRL_LEFT; break;
			case 4: ctrlMask |= CTRL_LEFT; break;
			case 5: ctrlMask |= CTRL_UP | CTRL_LEFT; break;
			case 6: ctrlMask |= CTRL_UP; break;
			case 7: ctrlMask |= CTRL_UP | CTRL_RIGHT; break;
			}
		}
	}

	down_ = ctrlMask;
	int pressed = down_ & ~lastDown;
	int released = (~down_) & lastDown;
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	for (int i = 0; i < 4; i++) {
		if (pressed & dir[i]) {
			if (g_Config.bHapticFeedback) {
				Vibrate(HAPTIC_VIRTUAL_KEY);
			}
			__CtrlButtonDown(dir[i]);
		}
		if (released & dir[i]) {
			__CtrlButtonUp(dir[i]);
		}
	}
}

void PSPDpad::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	static const float xoff[4] = {1, 0, -1, 0};
	static const float yoff[4] = {0, 1, 0, -1};
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	int buttons = __CtrlPeekButtons();
	float r = D_pad_Radius * spacing_;
	for (int i = 0; i < 4; i++) {
		bool isDown = (buttons & dir[i]) != 0;

		float x = bounds_.centerX() + xoff[i] * r;
		float y = bounds_.centerY() + yoff[i] * r;
		float x2 = bounds_.centerX() + xoff[i] * (r + 10.f * scale_);
		float y2 = bounds_.centerY() + yoff[i] * (r + 10.f * scale_);
		float angle = i * M_PI / 2;
		float imgScale = isDown ? scale_ * 2 : scale_;
		float imgOpacity = opacity;

		if (isDown && g_Config.iTouchButtonStyle == 2) {
			imgScale = scale_;
			imgOpacity *= 1.35f;

			uint32_t downBg = colorAlpha(0x00FFFFFF, imgOpacity * 0.5f);
			if (arrowIndex_ != arrowDownIndex_)
				dc.Draw()->DrawImageRotated(arrowDownIndex_, x, y, imgScale, angle + PI, downBg, false);
		}

		uint32_t colorBg = colorAlpha(GetButtonColor(), imgOpacity);
		uint32_t color = colorAlpha(0xFFFFFF, imgOpacity);

		dc.Draw()->DrawImageRotated(arrowIndex_, x, y, imgScale, angle + PI, colorBg, false);
		if (overlayIndex_.isValid())
			dc.Draw()->DrawImageRotated(overlayIndex_, x2, y2, imgScale, angle + PI, color);
	}
}

PSPStick::PSPStick(ImageID bgImg, ImageID stickImg, ImageID stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams)
	: GamepadView(layoutParams), dragPointerId_(-1), bgImg_(bgImg), stickImageIndex_(stickImg), stickDownImg_(stickDownImg), stick_(stick), scale_(scale), centerX_(-1), centerY_(-1) {
	stick_size_ = 50;
}

void PSPStick::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->GetAtlas()->measureImage(bgImg_, &w, &h);
}

void PSPStick::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2) {
		opacity *= 1.35f;
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t downBg = colorAlpha(0x00FFFFFF, opacity * 0.5f);
	uint32_t color = colorAlpha(0x808080, opacity);

	if (centerX_ < 0.0f) {
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
	}

	float stickX = centerX_;
	float stickY = centerY_;

	float dx, dy;
	__CtrlPeekAnalog(stick_, &dx, &dy);

	dc.Draw()->DrawImage(bgImg_, stickX, stickY, 1.0f * scale_, colorBg, ALIGN_CENTER);
	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2 && stickDownImg_ != stickImageIndex_)
		dc.Draw()->DrawImage(stickDownImg_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_, downBg, ALIGN_CENTER);
	dc.Draw()->DrawImage(stickImageIndex_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_, colorBg, ALIGN_CENTER);
}

void PSPStick::Touch(const TouchInput &input) {
	GamepadView::Touch(input);
	if (input.flags & TOUCH_RELEASE_ALL) {
		dragPointerId_ = -1;
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
		__CtrlSetAnalogX(0.0f, stick_);
		__CtrlSetAnalogY(0.0f, stick_);
		return;
	}
	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			if (g_Config.bAutoCenterTouchAnalog) {
				centerX_ = input.x;
				centerY_ = input.y;
			} else {
				centerX_ = bounds_.centerX();
				centerY_ = bounds_.centerY();
			}
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			centerX_ = bounds_.centerX();
			centerY_ = bounds_.centerY();
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPStick::ProcessTouch(float x, float y, bool down) {
	if (down && centerX_ >= 0.0f) {
		float inv_stick_size = 1.0f / (stick_size_ * scale_);

		float dx = (x - centerX_) * inv_stick_size;
		float dy = (y - centerY_) * inv_stick_size;
		// Do not clamp to a circle! The PSP has nearly square range!

		// Old code to clamp to a circle
		// float len = sqrtf(dx * dx + dy * dy);
		// if (len > 1.0f) {
		//	dx /= len;
		//	dy /= len;
		//}

		// Still need to clamp to a square
		dx = std::min(1.0f, std::max(-1.0f, dx));
		dy = std::min(1.0f, std::max(-1.0f, dy));

		__CtrlSetAnalogX(dx, stick_);
		__CtrlSetAnalogY(-dy, stick_);
	} else {
		__CtrlSetAnalogX(0.0f, stick_);
		__CtrlSetAnalogY(0.0f, stick_);
	}
}

PSPCustomStick::PSPCustomStick(ImageID bgImg, ImageID stickImg, ImageID stickDownImg, float scale, UI::LayoutParams *layoutParams)
	: PSPStick(bgImg, stickImg, stickDownImg, -1, scale, layoutParams) {
}

void PSPCustomStick::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2) {
		opacity *= 1.35f;
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t downBg = colorAlpha(0x00FFFFFF, opacity * 0.5f);
	uint32_t color = colorAlpha(0x808080, opacity);

	if (centerX_ < 0.0f) {
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
	}

	float stickX = centerX_;
	float stickY = centerY_;

	float dx, dy;
	dx = posX_;
	dy = -posY_;

	dc.Draw()->DrawImage(bgImg_, stickX, stickY, 1.0f * scale_, colorBg, ALIGN_CENTER);
	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2 && stickDownImg_ != stickImageIndex_)
		dc.Draw()->DrawImage(stickDownImg_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_, downBg, ALIGN_CENTER);
	dc.Draw()->DrawImage(stickImageIndex_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_, colorBg, ALIGN_CENTER);
}

void PSPCustomStick::Touch(const TouchInput &input) {
	GamepadView::Touch(input);
	if (input.flags & TOUCH_RELEASE_ALL) {
		dragPointerId_ = -1;
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
		posX_ = 0.0f;
		posY_ = 0.0f;
		return;
	}
	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			if (g_Config.bAutoCenterTouchAnalog) {
				centerX_ = input.x;
				centerY_ = input.y;
			} else {
				centerX_ = bounds_.centerX();
				centerY_ = bounds_.centerY();
			}
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			centerX_ = bounds_.centerX();
			centerY_ = bounds_.centerY();
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPCustomStick::ProcessTouch(float x, float y, bool down) {
	static const int button[16] = {CTRL_LTRIGGER, CTRL_RTRIGGER, CTRL_SQUARE, CTRL_TRIANGLE, CTRL_CIRCLE, CTRL_CROSS, CTRL_UP, CTRL_DOWN, CTRL_LEFT, CTRL_RIGHT, CTRL_START, CTRL_SELECT};

	if (down && centerX_ >= 0.0f) {
		float inv_stick_size = 1.0f / (stick_size_ * scale_);

		float dx = (x - centerX_) * inv_stick_size;
		float dy = (y - centerY_) * inv_stick_size;

		dx = std::min(1.0f, std::max(-1.0f, dx));
		dy = std::min(1.0f, std::max(-1.0f, dy));

		if (g_Config.iRightAnalogRight != 0) {
			if (dx > 0.5f)
				__CtrlButtonDown(button[g_Config.iRightAnalogRight-1]);
			else
				__CtrlButtonUp(button[g_Config.iRightAnalogRight-1]);
		}
		if (g_Config.iRightAnalogLeft != 0) {
			if (dx < -0.5f)
				__CtrlButtonDown(button[g_Config.iRightAnalogLeft-1]);
			else
				__CtrlButtonUp(button[g_Config.iRightAnalogLeft-1]);
		}
		if (g_Config.iRightAnalogUp != 0) {
			if (dy < -0.5f)
				__CtrlButtonDown(button[g_Config.iRightAnalogUp-1]);
			else
				__CtrlButtonUp(button[g_Config.iRightAnalogUp-1]);
		}
		if (g_Config.iRightAnalogDown != 0) {
			if (dy > 0.5f)
				__CtrlButtonDown(button[g_Config.iRightAnalogDown-1]);
			else
				__CtrlButtonUp(button[g_Config.iRightAnalogDown-1]);
		}
		if (g_Config.iRightAnalogPress != 0)
			__CtrlButtonDown(button[g_Config.iRightAnalogPress-1]);

		posX_ = dx;
		posY_ = dy;

	} else {
		if (g_Config.iRightAnalogUp != 0)
			__CtrlButtonUp(button[g_Config.iRightAnalogUp-1]);
		if (g_Config.iRightAnalogDown != 0)
			__CtrlButtonUp(button[g_Config.iRightAnalogDown-1]);
		if (g_Config.iRightAnalogLeft != 0)
			__CtrlButtonUp(button[g_Config.iRightAnalogLeft-1]);
		if (g_Config.iRightAnalogRight != 0)
			__CtrlButtonUp(button[g_Config.iRightAnalogRight-1]);
		if (g_Config.iRightAnalogPress != 0)
			__CtrlButtonUp(button[g_Config.iRightAnalogPress-1]);

		posX_ = 0.0f;
		posY_ = 0.0f;
	}
}

void InitPadLayout(float xres, float yres, float globalScale) {
	const float scale = globalScale;
	const int halfW = xres / 2;

	auto initTouchPos = [=](ConfigTouchPos &touch, float x, float y) {
		if (touch.x == -1.0f || touch.y == -1.0f) {
			touch.x = x / xres;
			touch.y = y / yres;
			touch.scale = scale;
		}
	};

	// PSP buttons (triangle, circle, square, cross)---------------------
	// space between the PSP buttons (triangle, circle, square and cross)
	if (g_Config.fActionButtonSpacing < 0) {
		g_Config.fActionButtonSpacing = 1.0f;
	}

	// Position of the circle button (the PSP circle button). It is the farthest to the left
	float Action_button_spacing = g_Config.fActionButtonSpacing * baseActionButtonSpacing;
	int Action_button_center_X = xres - Action_button_spacing * 2;
	int Action_button_center_Y = yres - Action_button_spacing * 2;
	if (g_Config.touchRightAnalogStick.show) {
		Action_button_center_Y -= 150 * scale;
	}
	initTouchPos(g_Config.touchActionButtonCenter, Action_button_center_X, Action_button_center_Y);

	//D-PAD (up down left right) (aka PSP cross)----------------------------
	//radius to the D-pad
	// TODO: Make configurable

	int D_pad_X = 2.5 * D_pad_Radius * scale;
	int D_pad_Y = yres - D_pad_Radius * scale;
	if (g_Config.touchAnalogStick.show) {
		D_pad_Y -= 200 * scale;
	}
	initTouchPos(g_Config.touchDpad, D_pad_X, D_pad_Y);

	//analog stick-------------------------------------------------------
	//keep the analog stick right below the D pad
	int analog_stick_X = D_pad_X;
	int analog_stick_Y = yres - 80 * scale;
	initTouchPos(g_Config.touchAnalogStick, analog_stick_X, analog_stick_Y);

	//right analog stick-------------------------------------------------
	//keep the right analog stick right below the face buttons
	int right_analog_stick_X = Action_button_center_X;
	int right_analog_stick_Y = yres - 80 * scale;
	initTouchPos(g_Config.touchRightAnalogStick, right_analog_stick_X, right_analog_stick_Y);

	//select, start, throttle--------------------------------------------
	//space between the bottom keys (space between select, start and un-throttle)
	float bottom_key_spacing = 100;
	if (dp_xres < 750) {
		bottom_key_spacing *= 0.8f;
	}

	int start_key_X = halfW + bottom_key_spacing * scale;
	int start_key_Y = yres - 60 * scale;
	initTouchPos(g_Config.touchStartKey, start_key_X, start_key_Y);

	int select_key_X = halfW;
	int select_key_Y = yres - 60 * scale;
	initTouchPos(g_Config.touchSelectKey, select_key_X, select_key_Y);

	int unthrottle_key_X = halfW - bottom_key_spacing * scale;
	int unthrottle_key_Y = yres - 60 * scale;
	initTouchPos(g_Config.touchUnthrottleKey, unthrottle_key_X, unthrottle_key_Y);

	initTouchPos(g_Config.touchSpeed1Key, unthrottle_key_X, unthrottle_key_Y - 60 * scale);
	initTouchPos(g_Config.touchSpeed2Key, unthrottle_key_X + bottom_key_spacing * scale, unthrottle_key_Y - 60 * scale);
	initTouchPos(g_Config.touchRapidFireKey, unthrottle_key_X + 2*bottom_key_spacing * scale, unthrottle_key_Y - 60 * scale);
	initTouchPos(g_Config.touchAnalogRotationCCWKey, unthrottle_key_X, unthrottle_key_Y - 120 * scale);
	initTouchPos(g_Config.touchAnalogRotationCWKey, unthrottle_key_X + bottom_key_spacing * scale, unthrottle_key_Y - 120 * scale);

	// L and R------------------------------------------------------------
	// Put them above the analog stick / above the buttons to the right.
	// The corners were very hard to reach..

	int l_key_X = 60 * scale;
	int l_key_Y = yres - 380 * scale;
	initTouchPos(g_Config.touchLKey, l_key_X, l_key_Y);

	int r_key_X = xres - 60 * scale;
	int r_key_Y = l_key_Y;
	initTouchPos(g_Config.touchRKey, r_key_X, r_key_Y);

	//Combo key
	int combo_key_X = halfW + bottom_key_spacing * scale * 1.2f;
	int combo_key_Y = yres / 2;
	initTouchPos(g_Config.touchCombo0, combo_key_X, combo_key_Y);

	int combo1_key_X = halfW + bottom_key_spacing * scale * 2.2f;
	int combo1_key_Y = yres / 2;
	initTouchPos(g_Config.touchCombo1, combo1_key_X, combo1_key_Y);

	int combo2_key_X = halfW + bottom_key_spacing * scale * 3.2f;
	int combo2_key_Y = yres / 2;
	initTouchPos(g_Config.touchCombo2, combo2_key_X, combo2_key_Y);

	int combo3_key_X = halfW + bottom_key_spacing * scale * 1.2f;
	int combo3_key_Y = yres / 3;
	initTouchPos(g_Config.touchCombo3, combo3_key_X, combo3_key_Y);

	int combo4_key_X = halfW + bottom_key_spacing * scale * 2.2f;
	int combo4_key_Y = yres / 3;
	initTouchPos(g_Config.touchCombo4, combo4_key_X, combo4_key_Y);
}

UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause) {
	using namespace UI;

	AnchorLayout *root = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	if (!g_Config.bShowTouchControls) {
		return root;
	}

	struct ButtonOffset {
		float x;
		float y;
	};
	auto buttonLayoutParams = [=](const ConfigTouchPos &touch, ButtonOffset off = { 0, 0 }) {
		return new AnchorLayoutParams(touch.x * xres + off.x, touch.y * yres + off.y, NONE, NONE, true);
	};

	// Space between the PSP buttons (traingle, circle, square and cross)
	const float actionButtonSpacing = g_Config.fActionButtonSpacing * baseActionButtonSpacing;
	// Position of the circle button (the PSP circle button).  It is the farthest to the right.
	ButtonOffset circleOffset{ actionButtonSpacing, 0.0f };
	ButtonOffset crossOffset{ 0.0f, actionButtonSpacing };
	ButtonOffset triangleOffset{ 0.0f, -actionButtonSpacing };
	ButtonOffset squareOffset{ -actionButtonSpacing, 0.0f };

	const int halfW = xres / 2;

	const ImageID roundImage = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");

	const ImageID rectImage = g_Config.iTouchButtonStyle ? ImageID("I_RECT_LINE") : ImageID("I_RECT");
	const ImageID shoulderImage = g_Config.iTouchButtonStyle ? ImageID("I_SHOULDER_LINE") : ImageID("I_SHOULDER");
	const ImageID dirImage = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");
	const ImageID stickImage = g_Config.iTouchButtonStyle ? ImageID("I_STICK_LINE") : ImageID("I_STICK");
	const ImageID stickBg = g_Config.iTouchButtonStyle ? ImageID("I_STICK_BG_LINE") : ImageID("I_STICK_BG");
	static const ImageID comboKeyImages[5] = { ImageID("I_1"), ImageID("I_2"), ImageID("I_3"), ImageID("I_4"), ImageID("I_5") };

	auto addPSPButton = [=](int buttonBit, ImageID bgImg, ImageID bgDownImg, ImageID img, const ConfigTouchPos &touch, ButtonOffset off = { 0, 0 }) -> PSPButton * {
		if (touch.show) {
			return root->Add(new PSPButton(buttonBit, bgImg, bgDownImg, img, touch.scale, buttonLayoutParams(touch, off)));
		}
		return nullptr;
	};
	auto addComboKey = [=](int buttonBit, bool toggle, ImageID bgImg, ImageID bgDownImg, ImageID img, const ConfigTouchPos &touch) -> ComboKey * {
		if (touch.show) {
			return root->Add(new ComboKey(buttonBit, toggle, bgImg, bgDownImg, img, touch.scale, buttonLayoutParams(touch)));
		}
		return nullptr;
	};
	auto addBoolButton = [=](bool *value, ImageID bgImg, ImageID bgDownImg, ImageID img, const ConfigTouchPos &touch) -> BoolButton * {
		if (touch.show) {
			return root->Add(new BoolButton(value, bgImg, bgDownImg, img, touch.scale, buttonLayoutParams(touch)));
		}
		return nullptr;
	};
	auto addFPSLimitButton = [=](FPSLimit value, ImageID bgImg, ImageID bgDownImg, ImageID img, const ConfigTouchPos &touch) -> FPSLimitButton * {
		if (touch.show) {
			return root->Add(new FPSLimitButton(value, bgImg, bgDownImg, img, touch.scale, buttonLayoutParams(touch)));
		}
		return nullptr;
	};

	if (!System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON) || g_Config.bShowTouchPause) {
		root->Add(new BoolButton(pause, roundImage, ImageID("I_ROUND"), ImageID("I_ARROW"), 1.0f, new AnchorLayoutParams(halfW, 20, NONE, NONE, true)))->SetAngle(90);
	}

	// touchActionButtonCenter.show will always be true, since that's the default.
	if (g_Config.bShowTouchCircle)
		addPSPButton(CTRL_CIRCLE, roundImage, ImageID("I_ROUND"), ImageID("I_CIRCLE"), g_Config.touchActionButtonCenter, circleOffset);
	if (g_Config.bShowTouchCross)
		addPSPButton(CTRL_CROSS, roundImage, ImageID("I_ROUND"), ImageID("I_CROSS"), g_Config.touchActionButtonCenter, crossOffset);
	if (g_Config.bShowTouchTriangle)
		addPSPButton(CTRL_TRIANGLE, roundImage, ImageID("I_ROUND"), ImageID("I_TRIANGLE"), g_Config.touchActionButtonCenter, triangleOffset);
	if (g_Config.bShowTouchSquare)
		addPSPButton(CTRL_SQUARE, roundImage, ImageID("I_ROUND"), ImageID("I_SQUARE"), g_Config.touchActionButtonCenter, squareOffset);

	addPSPButton(CTRL_START, rectImage, ImageID("I_RECT"), ImageID("I_START"), g_Config.touchStartKey);
	addPSPButton(CTRL_SELECT, rectImage, ImageID("I_RECT"), ImageID("I_SELECT"), g_Config.touchSelectKey);

	BoolButton *unthrottle = addBoolButton(&PSP_CoreParameter().unthrottle, rectImage, ImageID("I_RECT"), ImageID("I_ARROW"), g_Config.touchUnthrottleKey);
	if (unthrottle) {
		unthrottle->SetAngle(180.0f);
		unthrottle->OnChange.Add([](UI::EventParams &e) {
			if (e.a && coreState == CORE_STEPPING) {
				Core_EnableStepping(false);
			}
			return UI::EVENT_DONE;
		});
	}

	if (g_Config.touchRapidFireKey.show) {
		auto rapidFire = root->Add(new RapidFireButton(rectImage, ImageID("I_RECT"), ImageID("I_ARROW"), g_Config.touchRapidFireKey.scale, buttonLayoutParams(g_Config.touchRapidFireKey)));
		rapidFire->SetAngle(90.0f, 180.0f);
	}

	if (g_Config.touchAnalogRotationCWKey.show) {
		auto analogRotationCC = root->Add(new AnalogRotationButton(true, rectImage, ImageID("I_RECT"), ImageID("I_ARROW"), g_Config.touchAnalogRotationCWKey.scale, buttonLayoutParams(g_Config.touchAnalogRotationCWKey)));
		analogRotationCC->SetAngle(190.0f, 180.0f);
	}

	if (g_Config.touchAnalogRotationCCWKey.show) {
		auto analogRotationCCW = root->Add(new AnalogRotationButton(false, rectImage, ImageID("I_RECT"), ImageID("I_ARROW"), g_Config.touchAnalogRotationCCWKey.scale, buttonLayoutParams(g_Config.touchAnalogRotationCCWKey)));
		analogRotationCCW->SetAngle(350.0f, 180.0f);
	}

	FPSLimitButton *speed1 = addFPSLimitButton(FPSLimit::CUSTOM1, rectImage, ImageID("I_RECT"), ImageID("I_ARROW"), g_Config.touchSpeed1Key);
	if (speed1)
		speed1->SetAngle(170.0f, 180.0f);
	FPSLimitButton *speed2 = addFPSLimitButton(FPSLimit::CUSTOM2, rectImage, ImageID("I_RECT"), ImageID("I_ARROW"), g_Config.touchSpeed2Key);
	if (speed2)
		speed2->SetAngle(190.0f, 180.0f);

	addPSPButton(CTRL_LTRIGGER, shoulderImage, ImageID("I_SHOULDER"), ImageID("I_L"), g_Config.touchLKey);
	PSPButton *rTrigger = addPSPButton(CTRL_RTRIGGER, shoulderImage, ImageID("I_SHOULDER"), ImageID("I_R"), g_Config.touchRKey);
	if (rTrigger)
		rTrigger->FlipImageH(true);

	if (g_Config.touchDpad.show)
		root->Add(new PSPDpad(dirImage, ImageID("I_DIR"), ImageID("I_ARROW"), g_Config.touchDpad.scale, g_Config.fDpadSpacing, buttonLayoutParams(g_Config.touchDpad)));

	if (g_Config.touchAnalogStick.show)
		root->Add(new PSPStick(stickBg, stickImage, ImageID("I_STICK"), 0, g_Config.touchAnalogStick.scale, buttonLayoutParams(g_Config.touchAnalogStick)));

	if (g_Config.touchRightAnalogStick.show) {
		if (g_Config.bRightAnalogCustom)
			root->Add(new PSPCustomStick(stickBg, stickImage, ImageID("I_STICK"), g_Config.touchRightAnalogStick.scale, buttonLayoutParams(g_Config.touchRightAnalogStick)));
		else
			root->Add(new PSPStick(stickBg, stickImage, ImageID("I_STICK"), 1, g_Config.touchRightAnalogStick.scale, buttonLayoutParams(g_Config.touchRightAnalogStick)));
	}

	addComboKey(g_Config.iCombokey0, g_Config.bComboToggle0, roundImage, ImageID("I_ROUND"), comboKeyImages[0], g_Config.touchCombo0);
	addComboKey(g_Config.iCombokey1, g_Config.bComboToggle1, roundImage, ImageID("I_ROUND"), comboKeyImages[1], g_Config.touchCombo1);
	addComboKey(g_Config.iCombokey2, g_Config.bComboToggle2, roundImage, ImageID("I_ROUND"), comboKeyImages[2], g_Config.touchCombo2);
	addComboKey(g_Config.iCombokey3, g_Config.bComboToggle3, roundImage, ImageID("I_ROUND"), comboKeyImages[3], g_Config.touchCombo3);
	addComboKey(g_Config.iCombokey4, g_Config.bComboToggle4, roundImage, ImageID("I_ROUND"), comboKeyImages[4], g_Config.touchCombo4);

	return root;
}
