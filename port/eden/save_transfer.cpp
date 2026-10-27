// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_transfer.h"
#include "standalone/ryujinx_saves.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace ps5eden::transfer
{
	namespace
	{
		namespace fs = std::filesystem;
		struct Budget { uint64_t bytes = 0; size_t files = 0; };
		constexpr uint64_t kBytes = 4ull << 30;
		void CheckPath(const fs::path& path)
		{
			// Check ancestors too: copying through a symlinked account folder is not safe.
			for (auto at = fs::absolute(path).lexically_normal(); !at.empty(); at = at.parent_path())
			{
				std::error_code error;
				const auto status = fs::symlink_status(at, error);
				if (!error && fs::is_symlink(status)) throw std::runtime_error("A save path contains a symbolic link: " + path.string());
				if (error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("Cannot inspect save path", at, error);
				if (at == at.root_path()) break;
			}
		}
		bool Within(const fs::path& child, const fs::path& parent)
		{
			const auto relative = fs::absolute(child).lexically_normal().lexically_relative(fs::absolute(parent).lexically_normal());
			return !relative.empty() && *relative.begin() != "..";
		}
		void Copy(const fs::path& from, const fs::path& to, Budget& budget, unsigned depth = 0)
		{
			if (depth > 32) throw std::runtime_error("Save directory is too deep.");
			CheckPath(from);
			if (!fs::is_directory(from)) throw std::runtime_error("Save source is not a directory.");
			fs::create_directories(to);
			std::error_code error;
			const auto entries = Eden::RyujinxSaves::ListFolder(from, error);
			if (error) throw fs::filesystem_error("Cannot list save", from, error);
			for (const auto& entry : entries)
			{
				const auto status = entry.symlink_status();
				if (fs::is_symlink(status)) throw std::runtime_error("Symbolic links are not imported/exported.");
				const auto target = to / entry.path().filename();
				if (fs::is_directory(status)) Copy(entry.path(), target, budget, depth + 1);
				else if (fs::is_regular_file(status))
				{
					const auto size = entry.file_size();
					if (++budget.files > 100000 || size > kBytes - budget.bytes) throw std::runtime_error("Save exceeds the transfer limit (100000 files / 4 GiB).");
					budget.bytes += size;
					fs::copy_file(entry.path(), target);
					if (fs::file_size(target) != size) throw std::runtime_error("Save changed or a copy was incomplete.");
				}
				else throw std::runtime_error("Save contains an unsupported file type.");
			}
		}
		fs::path Unique(const fs::path& parent)
		{
			CheckPath(parent);
			fs::create_directories(parent);
			const auto time = std::chrono::system_clock::now().time_since_epoch();
			const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
			for (int i = 0; i < 1000; i++)
			{
				const fs::path path = parent / (std::to_string(stamp) + "-" + std::to_string(i));
				std::error_code error;
				if (fs::create_directory(path, error)) return path;
				if (error && error != std::errc::file_exists) throw fs::filesystem_error("Cannot stage save", path, error);
			}
			throw std::runtime_error("Cannot allocate a unique save-transfer directory.");
		}
	}

	std::vector<Source> Discover(const std::string& root, uint64_t title, std::string& error)
	{
		try
		{
			if ((title >> 56) != 1) throw std::runtime_error("A real Switch title ID is required.");
			CheckPath(root);
			std::vector<Eden::RyujinxSaves::Save> saves;
			if (const auto data = Eden::RyujinxSaves::DataRoot(root); !data.empty())
			{
				const auto index = Eden::RyujinxSaves::IndexPath(data);
				CheckPath(index);
				if (fs::file_size(index) > (16u << 20)) throw std::runtime_error("Ryujinx save index is too large.");
				saves = Eden::RyujinxSaves::FindSaves(root, title, error);
			}
			else saves = Eden::RyujinxSaves::FindFolderSaves(root, title, error);
			std::vector<Source> result;
			for (const auto& save : saves)
			{
				CheckPath(save.folder);
				result.push_back({save.kind == Eden::RyujinxSaves::Kind::Account ? Kind::Account : Kind::Device, save.folder.string()});
			}
			if (!result.empty()) error.clear();
			return result;
		}
		catch (const std::exception& failure) { error = failure.what(); return {}; }
	}

	bool Import(const std::vector<Source>& sources, const std::string& account, const std::string& device,
		const std::string& recovery, std::string& kept, std::string& error)
	{
		struct Move { fs::path staged, target, before; bool old = false, installed = false; };
		std::vector<Move> moves;
		fs::path work;
		kept.clear();
		try
		{
			if (sources.empty() || sources.size() > 2) throw std::runtime_error("Choose one account and/or device save.");
			CheckPath(account); CheckPath(device); CheckPath(recovery);
			if (Within(account, device) || Within(device, account) || Within(recovery, account) || Within(recovery, device))
				throw std::runtime_error("Save destinations and recovery directory overlap.");
			for (const auto& source : sources)
			{
				if (Within(account, source.folder) || Within(device, source.folder) || Within(source.folder, account) || Within(source.folder, device) ||
					Within(recovery, source.folder) || Within(source.folder, recovery)) throw std::runtime_error("Source overlaps a destination or recovery directory.");
			}
			work = Unique(recovery);
			Budget budget;
			for (const auto& source : sources)
			{
				const bool accountSave = source.kind == Kind::Account;
				const fs::path target = accountSave ? account : device;
				if (std::any_of(moves.begin(), moves.end(), [&](const auto& move) { return move.target == target; })) throw std::runtime_error("Duplicate save type.");
				const auto name = accountSave ? "account" : "device";
				Move move{work / (std::string(name) + ".staged"), target, work / (std::string(name) + ".before")};
				const size_t before = budget.files;
				Copy(source.folder, move.staged, budget);
				if (budget.files == before) throw std::runtime_error("Empty saves are not imported.");
				moves.push_back(std::move(move));
			}
			// Nothing in the destination is changed until every source is staged successfully.
			for (auto& move : moves)
			{
				CheckPath(move.target);
				fs::create_directories(move.target.parent_path());
				if (fs::exists(move.target)) { fs::rename(move.target, move.before); move.old = true; }
				fs::rename(move.staged, move.target); move.installed = true;
			}
			kept = work.string();
			error.clear();
			return true;
		}
		catch (const std::exception& failure)
		{
			error = failure.what();
			bool rollback = true;
			for (auto it = moves.rbegin(); it != moves.rend(); ++it)
			{
				std::error_code ec;
				if (it->installed) fs::remove_all(it->target, ec);
				if (ec) rollback = false;
				if (it->old) { ec.clear(); fs::rename(it->before, it->target, ec); if (ec) rollback = false; }
			}
			if (!work.empty())
			{
				if (rollback) { std::error_code ec; fs::remove_all(work, ec); }
				else { kept = work.string(); error += " Recovery is incomplete; retain " + kept; }
			}
			return false;
		}
	}

	bool Export(const std::string& account, const std::string& device, const std::string& target, std::string& error)
	{
		fs::path work;
		try
		{
			CheckPath(target);
			if (fs::exists(target)) throw std::runtime_error("Export destination already exists.");
			if (Within(target, account) || Within(target, device) || Within(account, target) || Within(device, target)) throw std::runtime_error("Export and source overlap.");
			work = Unique(fs::path(target).parent_path());
			Budget budget;
			for (const auto& entry : {std::pair{account, "account"}, std::pair{device, "device"}})
				if (fs::is_directory(entry.first)) Copy(entry.first, work / entry.second, budget);
			if (!budget.files) throw std::runtime_error("No save files to export.");
			fs::rename(work, target);
			error.clear();
			return true;
		}
		catch (const std::exception& failure)
		{
			error = failure.what();
			if (!work.empty()) { std::error_code ec; fs::remove_all(work, ec); }
			return false;
		}
	}
}
