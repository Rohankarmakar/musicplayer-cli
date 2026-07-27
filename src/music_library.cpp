#include "music_library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <stdexcept>

namespace {
const std::set<std::string> kAudioExtensions = {
    ".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac", ".wma"
};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::filesystem::path song_list_path() {
    if (const char* state = std::getenv("XDG_STATE_HOME"))
        return std::filesystem::path(state) / "ripple" / "song-list.txt";
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local" / "state" / "ripple" / "song-list.txt";
    return std::filesystem::temp_directory_path() / "ripple-song-list.txt";
}

std::vector<Track> scan_tracks(const std::filesystem::path& root) {
    std::vector<Track> tracks;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) { error.clear(); continue; }
        if (!entry.is_regular_file(error) || error) { error.clear(); continue; }
        const auto extension = lower(entry.path().extension().string());
        if (kAudioExtensions.count(extension))
            tracks.push_back({entry.path(), entry.path().filename().string()});
    }
    std::sort(tracks.begin(), tracks.end(), [](const Track& a, const Track& b) {
        return lower(a.title) == lower(b.title) ? a.path.string() < b.path.string() : lower(a.title) < lower(b.title);
    });
    return tracks;
}
}

MusicLibrary::MusicLibrary(std::filesystem::path root) : root_(std::move(root)) {}

std::vector<Track> MusicLibrary::playlist() const {
    if (!std::filesystem::is_directory(root_))
        throw std::runtime_error("Music directory does not exist: " + root_.string());

    const auto modified = std::filesystem::last_write_time(root_).time_since_epoch().count();
    const std::string stamp = std::to_string(static_cast<long long>(modified));
    const auto cache = song_list_path();
    std::ifstream input(cache);
    std::string cached_stamp;
    if (input && std::getline(input, cached_stamp) && cached_stamp == stamp) {
        std::vector<Track> cached_tracks;
        std::string path;
        while (std::getline(input, path)) {
            if (!path.empty() && std::filesystem::is_regular_file(path))
                cached_tracks.push_back({path, std::filesystem::path(path).filename().string()});
        }
        return cached_tracks;
    }

    const auto tracks = scan_tracks(root_);
    std::error_code error;
    std::filesystem::create_directories(cache.parent_path(), error);
    std::ofstream output(cache, std::ios::trunc);
    if (output) {
        output << stamp << '\n';
        for (const auto& track : tracks) output << track.path.string() << '\n';
    }
    return tracks;
}

std::vector<Track> MusicLibrary::search(const std::string& query) const {
    const auto needle = lower(query);
    std::vector<Track> matches;
    for (const auto& track : playlist())
        if (lower(track.title).find(needle) != std::string::npos) matches.push_back(track);
    return matches;
}

std::filesystem::path default_music_directory() {
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Music";
    return std::filesystem::current_path();
}
