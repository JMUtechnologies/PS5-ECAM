// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "frame_capture.h"
#include <map>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
namespace ps5capture
{
	// Known session facts only. Empty context remains invalid for comparison until owner-supplied.
	inline std::string Session(const Frames& frames, const std::string& system, const std::string& game,
		const std::string& build, const std::string& core, const std::map<std::string, int>& settings)
	{
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
		out.StartObject();
		auto text = [&](const char* key, const std::string& value) { out.Key(key); out.String(value.data(), value.size()); };
		out.Key("capture_schema"); out.Int(1);
		text("system", system); text("game_id", game); text("build", build); text("core_revision", core);
		text("capture_method", "DS CPU timestamp after frontend Present returns; not GPU/display timestamps");
		text("frame_kind", "presented"); text("stop_reason", frames.Reason());
		out.Key("samples"); out.Uint64(frames.Samples().size());
		out.Key("settings"); out.StartObject();
		for (const auto& [key, value] : settings) { out.Key(key.data(), key.size()); out.Int(value); }
		out.EndObject();
		for (const char* key : {"game_version", "scene", "console_model", "firmware", "hen", "cache_state"}) text(key, "");
		out.Key("context_required"); out.StartArray();
		for (const char* key : {"game_version", "scene", "console_model", "firmware", "hen", "cache_state", "verify_build_and_effective_settings"}) out.String(key);
		out.EndArray(); out.EndObject();
		return {buffer.GetString(), buffer.GetSize()};
	}
	inline bool WriteSession(const std::string& file, const std::string& json)
	{
		FILE* output = std::fopen(file.c_str(), "wx");
		if (!output) return false;
		const bool ok = std::fwrite(json.data(), 1, json.size(), output) == json.size();
		return std::fclose(output) == 0 && ok;
	}
}
