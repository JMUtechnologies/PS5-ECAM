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
// The app installed on a USB drive, as ShadowMountPlus records it, with the launcher's data folder
// on that drive when data is true
static fs::path InstallOnDrive(bool data) {
    const fs::path drive = fs::path(ECAM_DRIVES) / "usb0", app = drive / "homebrew/PPSA99360";
    Put(app / "eboot.bin", "eboot");
    Put(ECAM_MOUNT_RECORD, app.string() + "/\n");
    if (data) fs::create_directories(drive / ecam_layout::kDriveData);
    return drive;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    const fs::path shared = ECAM_SHARED_DATA, target = shared.parent_path() / "eden", legacy = shared / "eden";
    if (mode.rfind("drive", 0) == 0 || mode == "image") {
        // the app on a USB drive: the Switch's folder beside the launcher's data there, unless the
        // console's has the Switch side's files already
        const fs::path drive = InstallOnDrive(mode != "drive-no-data"), onDrive = drive / "eden";
        if (mode == "image") fs::remove(ECAM_MOUNT_RECORD);
        if (mode == "drive-scaffold")
            for (const char* folder : {"roms", "updates", "keys", "firmware", "mods", "user", "config"})
                fs::create_directories(target / folder);
        if (mode == "drive-console") Put(target / "user/save/data", "console save");
        if (mode == "drive-legacy") Put(legacy / "user/save/data", "legacy save");
        if (mode == "drive-activated") {
            Put(target / "user/save/data", "console save");
            Put(onDrive / ecam_layout::kMarker, ecam_layout::kVersion);
        }
        Eden::FilesystemAccessStatus() = 0;
        ps5paths::DecideSystemRoots();
        const fs::path expected = mode == "drive-console" ? target : mode == "drive-legacy" ? legacy :
            mode == "drive-no-data" || mode == "image" ? target : onDrive;
        Check(ps5paths::Eden() == expected.string() && Eden::DataDir() == expected.string(),
            mode == "drive-console" || mode == "drive-legacy" ? "the console's Switch files stay in use with the app on a drive" :
            mode == "drive-no-data" ? "an app on a drive with its data in /data keeps the Switch's there" :
            mode == "image" ? "an app mounted from an image keeps the Switch's in /data" :
            mode == "drive-scaffold" ? "the console's empty Switch folders do not keep the Switch's off the app's drive" :
            mode == "drive-activated" ? "an activated Switch folder on the drive wins" :
            "a first start on a drive puts the Switch's beside the launcher's data there");
        Check(ps5paths::EdenRequestFile() == Eden::DataDir() + "/launch-request.txt" && ps5paths::EdenSessionFile() == Eden::DataDir() + "/ecam-session.txt",
            "launch requests and loading sessions agree across both programs");
        if (expected == onDrive) {
            Check(ecam_layout::Activated(onDrive), "the Switch's folder chosen on the drive is marked");
            Put(target / "user/save/data", "a file put on the console later");
            Check(ecam_layout::Eden(shared.string(), ecam_layout::DriveData()) == onDrive.string(), "a file put on the console later does not take the Switch side back");
        } else
            Check(!fs::exists(onDrive), "nothing is made on the drive for the Switch");
        if (mode == "drive-console") Check(fs::exists(target / "user/save/data"), "console Switch save remains untouched");
        return 0;
    }
    if (mode != "fresh") Put(legacy / "user/save/data", "original save");
    if (mode == "activated") Put(target / ecam_layout::kMarker, ecam_layout::kVersion);
    if (mode == "invalid") Put(target / ecam_layout::kMarker, "incomplete");
    Check(Eden::DataDir() == target.string(), "sandbox fallback does not inspect or pin inaccessible legacy paths");
    Eden::FilesystemAccessStatus() = 0;
    const auto selected = mode == "fresh" || mode == "activated" ? target : legacy;
    Check(Eden::DataDir() == selected.string(), "separate Eden executable chooses the expected data root after elevation");
    Check(ps5paths::Eden() == Eden::DataDir(), "launcher and separate executable select the same folder");
    ps5paths::DecideSystemRoots();
    Check(ps5paths::Eden() == Eden::DataDir(), "and after the launcher decides its folders");
    Check(ps5paths::EdenRequestFile() == Eden::DataDir() + "/launch-request.txt" && ps5paths::EdenSessionFile() == Eden::DataDir() + "/ecam-session.txt" && ps5paths::EdenResultFile() == Eden::DataDir() + "/ecam-result.txt", "launch requests, loading sessions and menu results agree across both programs");
    Check(Eden::ConfigDir() == selected.string() + "/config" && Eden::UserDir() == selected.string() + "/user", "Eden settings and saves follow that folder");
    Check(Eden::LogsDir() == selected.string() + "/logs" && Eden::CoversDir() == selected.string() + "/covers", "Eden diagnostics and covers follow that folder");
    if (mode != "fresh") Check(fs::exists(legacy / "user/save/data"), "legacy Switch save remains untouched");
}
