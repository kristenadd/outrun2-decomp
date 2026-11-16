#pragma once
#include <string>
namespace outrun::mac {
struct Paths { std::string saves; };
// Creates the macOS save folder (Application Support). Caches live in the game folder.
bool user_paths(Paths& paths,std::string& error);
// The default game folder: the game inside the application bundle
// (Contents/Resources/Game, or one folder inside it), else the folder that
// holds the application (or the executable's folder when it is not in a bundle).
std::string application_folder();
// True when path is inside the application bundle: nothing is written there
// (a prepared EXE image would change the signed bundle).
bool inside_application_bundle(const std::string& path);
// Fallback when the application is not in a game folder.
bool choose_retail_directory(std::string& path);
}
