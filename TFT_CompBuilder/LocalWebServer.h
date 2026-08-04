#pragma once

#include <filesystem>

void runLocalWebServer(
	int port = 8765,
	const std::filesystem::path& webRoot = "Web",
	bool openBrowser = true
);
