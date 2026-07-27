#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Track {
    std::filesystem::path path;
    std::string title;
};

// Repository pattern: all filesystem querying lives behind this small interface.
class MusicLibrary {
public:
    explicit MusicLibrary(std::filesystem::path root);
    std::vector<Track> playlist() const;
    std::vector<Track> search(const std::string& query) const;

private:
    std::filesystem::path root_;
};

std::filesystem::path default_music_directory();
