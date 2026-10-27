// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a path's own status, a link's rather than its target's (std::filesystem::symlink_status),
// where lstat is refused. ShadowMountPlus 1.7 mounts /data into an app without a HEN with lstat refused
// (EPERM) and stat answered: there, stat's, as nothing on /data is a link (app/system_layout.h's Status,
// for a struct stat). Elsewhere a link is still told apart. C++17, as melonDS's build includes it.
#pragma once
#include <filesystem>
#include <system_error>

namespace ps5files
{
	inline std::filesystem::file_status OwnStatus(const std::filesystem::path& path, std::error_code& ec)
	{
		const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
		if (ec != std::errc::operation_not_permitted)
			return status;
		return std::filesystem::status(path, ec);
	}
}
