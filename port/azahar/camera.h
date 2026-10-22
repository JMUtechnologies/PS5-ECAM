// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the 3DS's cameras (camera.cpp): a picture from azahar/camera, which both see.
#pragma once

namespace ps5azahar
{
	// Azahar's camera_name for it; its camera_config is the folder the picture is in
	inline constexpr const char* kPictureCamera = "ps5picture";
	// Its factory, once, before a game starts (Azahar makes the cameras as the game asks for them)
	void RegisterPictureCamera();
}
