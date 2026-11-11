// SPDX-License-Identifier: GPL-3.0-or-later
// Written by tools/render-tokens.py from port/ui/tokens.json: the UI's colours, sizes, type and
// motion. Colours are RGBA, R in the lowest byte; the launcher's theme
// (ui/theme.h) sets the colours it draws with, which start as tokens.json's.

#pragma once

#include "text.h"

#include <cstdint>

namespace ui::tokens
{
	inline constexpr uint32_t kInk0Default = 0xff0d0705; // #05070d
	inline constexpr uint32_t kInk1Default = 0xff1b0f0a; // #0a0f1b
	inline constexpr uint32_t kInk2Default = 0xff271811; // #111827
	inline constexpr uint32_t kGlassDefault = 0x0effffff; // #ffffff0e
	inline constexpr uint32_t kGlass2Default = 0x16ffffff; // #ffffff16
	inline constexpr uint32_t kGlassEdgeDefault = 0x1affffff; // #ffffff1a
	inline constexpr uint32_t kLineDefault = 0x17ffffff; // #ffffff17
	inline constexpr uint32_t kScrimDefault = 0x9e0a0503; // #03050a9e
	inline constexpr uint32_t kTextDefault = 0xfffbf7f5; // #f5f7fb
	inline constexpr uint32_t kText2Default = 0xb3fbf7f5; // #f5f7fbb3
	inline constexpr uint32_t kText3Default = 0x75fbf7f5; // #f5f7fb75
	inline constexpr uint32_t kWiiuDefault = 0xffffa95a; // #5aa9ff
	inline constexpr uint32_t kWiiuStrongDefault = 0xffe87f2f; // #2f7fe8
	inline constexpr uint32_t kWiiuInkDefault = 0xff331a07; // #071a33
	inline constexpr uint32_t kN3dsDefault = 0xff3fb6f4; // #f4b63f
	inline constexpr uint32_t kN3dsStrongDefault = 0xff1b96d9; // #d9961b
	inline constexpr uint32_t kN3dsInkDefault = 0xff021a2a; // #2a1a02
	inline constexpr uint32_t kNdsDefault = 0xff57d97e; // #7ed957
	inline constexpr uint32_t kNdsStrongDefault = 0xff38af4c; // #4caf38
	inline constexpr uint32_t kNdsInkDefault = 0xff06260c; // #0c2606
	inline constexpr uint32_t kNxDefault = 0xff4f43f2; // #f2434f
	inline constexpr uint32_t kNxStrongDefault = 0xff2c1fc8; // #c81f2c
	inline constexpr uint32_t kNxInkDefault = 0xff07052a; // #2a0507
	inline constexpr uint32_t kGoodDefault = 0xffa3d63d; // #3dd6a3
	inline constexpr uint32_t kWarnDefault = 0xff47b5ff; // #ffb547
	inline constexpr uint32_t kBadDefault = 0xff7272ff; // #ff7272
	inline constexpr uint32_t kFocusDefault = 0xffffffff; // #ffffff

	inline uint32_t kInk0 = kInk0Default;
	inline uint32_t kInk1 = kInk1Default;
	inline uint32_t kInk2 = kInk2Default;
	inline uint32_t kGlass = kGlassDefault;
	inline uint32_t kGlass2 = kGlass2Default;
	inline uint32_t kGlassEdge = kGlassEdgeDefault;
	inline uint32_t kLine = kLineDefault;
	inline uint32_t kScrim = kScrimDefault;
	inline uint32_t kText = kTextDefault;
	inline uint32_t kText2 = kText2Default;
	inline uint32_t kText3 = kText3Default;
	inline uint32_t kWiiu = kWiiuDefault;
	inline uint32_t kWiiuStrong = kWiiuStrongDefault;
	inline uint32_t kWiiuInk = kWiiuInkDefault;
	inline uint32_t kN3ds = kN3dsDefault;
	inline uint32_t kN3dsStrong = kN3dsStrongDefault;
	inline uint32_t kN3dsInk = kN3dsInkDefault;
	inline uint32_t kNds = kNdsDefault;
	inline uint32_t kNdsStrong = kNdsStrongDefault;
	inline uint32_t kNdsInk = kNdsInkDefault;
	inline uint32_t kNx = kNxDefault;
	inline uint32_t kNxStrong = kNxStrongDefault;
	inline uint32_t kNxInk = kNxInkDefault;
	inline uint32_t kGood = kGoodDefault;
	inline uint32_t kWarn = kWarnDefault;
	inline uint32_t kBad = kBadDefault;
	inline uint32_t kFocus = kFocusDefault;

	inline constexpr float kGrid = 8.0f;
	inline constexpr float kSafeX = 96.0f;
	inline constexpr float kSafeY = 60.0f;
	inline constexpr float kBarTop = 44.0f;
	inline constexpr float kBarHeight = 56.0f;
	inline constexpr float kHintsBottom = 44.0f;

	inline constexpr float kRadiusChip = 12.0f;
	inline constexpr float kRadiusCard = 20.0f;
	inline constexpr float kRadiusCover = 14.0f;
	inline constexpr float kRadiusSheet = 28.0f;
	inline constexpr float kRadiusPill = 999.0f;

	inline constexpr TextStyle kDisplayStyle{72.0f, Weight::Bold, 1.04f, 0.0f, false, false};
	inline constexpr TextStyle kTitleStyle{44.0f, Weight::SemiBold, 1.12f, 0.0f, false, false};
	inline constexpr TextStyle kHeadingStyle{32.0f, Weight::SemiBold, 1.2f, 0.0f, false, false};
	inline constexpr TextStyle kBodyStyle{26.0f, Weight::Regular, 1.45f, 0.0f, false, false};
	inline constexpr TextStyle kLabelStyle{24.0f, Weight::Medium, 1.3f, 0.0f, false, false};
	inline constexpr TextStyle kCaptionStyle{20.0f, Weight::Regular, 1.35f, 0.0f, false, false};
	inline constexpr TextStyle kOverlineStyle{18.0f, Weight::SemiBold, 1.0f, 3.5f, true, false};

	inline constexpr float kFocusOmega = 20.0f;
	inline constexpr float kScrollOmega = 16.0f;
	inline constexpr float kSheetOmega = 13.0f;
	inline constexpr float kSheetOutSeconds = 0.180f;
	inline constexpr float kScreenSeconds = 0.420f;
	inline constexpr float kStaggerSeconds = 0.040f;
	inline constexpr float kTextOutSeconds = 0.120f;
	inline constexpr float kTextInSeconds = 0.200f;
	inline constexpr float kAmbientSeconds = 0.600f;
	inline constexpr float kRestSeconds = 0.150f;
	inline constexpr float kToggleSeconds = 0.220f;
	inline constexpr float kLaunchSeconds = 0.420f;
	inline constexpr float kReducedSeconds = 0.150f;
	inline constexpr float kBreathSeconds = 2.400f;
}
