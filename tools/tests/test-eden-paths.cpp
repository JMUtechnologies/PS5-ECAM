// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual separately-patched Eden header beside the host launcher's selection.
#include "app/paths.h"
#include "storage_paths.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
static void Put(const fs::path& p, const std::string& value) {
    fs::create_directories(p.parent_path()); std::ofstream(p) << value;
}
static void Check(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(label);
    std::cout << "PASS  " << label << '\n';
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    const fs::path shared = ECAM_SHARED_DATA, target = shared.parent_path() / "eden", legacy = shared / "eden";
    if (mode != "fresh") Put(legacy / "user/save/data", "original save");
    if (mode == "activated") Put(target / ecam_layout::kMarker, ecam_layout::kVersion);
    if (mode == "invalid") Put(target / ecam_layout::kMarker, "incomplete");
    Check(Eden::DataDir() == target.string(), "sandbox fallback does not inspect or pin inaccessible legacy paths");
    Eden::FilesystemAccessStatus() = 0;
    const auto selected = mode == "fresh" || mode == "activated" ? target : legacy;
    Check(Eden::DataDir() == selected.string(), "separate Eden executable chooses the expected data root after elevation");
    Check(ps5paths::Eden() == Eden::DataDir(), "launcher and separate executable select the same folder");
    Check(ps5paths::EdenRequestFile() == Eden::DataDir() + "/launch-request.txt" && ps5paths::EdenSessionFile() == Eden::DataDir() + "/ecam-session.txt" && ps5paths::EdenResultFile() == Eden::DataDir() + "/ecam-result.txt", "launch requests, loading sessions and menu results agree across both programs");
    Check(Eden::ConfigDir() == selected.string() + "/config" && Eden::UserDir() == selected.string() + "/user", "Eden settings and saves follow that folder");
    Check(Eden::LogsDir() == selected.string() + "/logs" && Eden::CoversDir() == selected.string() + "/covers", "Eden diagnostics and covers follow that folder");
    if (mode != "fresh") Check(fs::exists(legacy / "user/save/data"), "legacy Switch save remains untouched");
}
