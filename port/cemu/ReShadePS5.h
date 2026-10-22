// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: ReShade on the Wii U side's picture. The preset and effects a PC's ReShade uses
// (ReShadeEffects.h: /data/ps5cemu/reshade, ReShadePreset.ini or presets/<title ID>.ini, Shaders/ and
// Textures/) run on the TV's picture each frame (ReShadeRuntime.h), once Cemu has drawn it into the
// swapchain image and before the GamePad's screen, the overlays and the in-game menu go over it: Cemu's
// LatteRenderTarget_copyToBackbuffer calls VulkanRenderer::PS5_PostEffects (patches/cemu), which calls
// Apply. The setting is Settings > Graphics' ReShade, or a game's own, and the in-game menu's
// (PS5Cemu_ReShade).
//
// The preset is compiled away from the renderer's thread, the first frame it is wanted, and again for
// another size or format of picture: the frames meanwhile are shown as they are. What it runs, or why
// it runs nothing, is in the boot log ("[reshade]").

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#include <cstdint>

namespace ps5reshade
{
	struct Picture
	{
		VkDevice device;
		VkPhysicalDevice physical;
		VkCommandBuffer commands; // the renderer's current one, outside a render pass
		VkImage image;			  // the swapchain image the TV's picture was drawn into
		VkFormat format;
		VkExtent2D extent;
		uint64_t titleId;		  // the game's, for its own preset
		VkImageLayout layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // image's, before and after
	};

	// Whether the setting is on for this frame
	bool Wanted();
	// The effects on the picture, once they are compiled for its size and format
	void Apply(const Picture& picture);
}
