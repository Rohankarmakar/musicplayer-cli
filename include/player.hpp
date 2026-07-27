#pragma once

#include "music_library.hpp"

#include <memory>
#include <string>
#include <sys/types.h>

// Strategy pattern: changing the command-line playback program does not affect UI code.
class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual std::string name() const = 0;
    virtual pid_t start(const std::string& file, double start_seconds = 0.0) const = 0;
};

std::unique_ptr<AudioBackend> make_audio_backend();
void play_with_visualizer(const AudioBackend& backend, const MusicLibrary& library,
                          const std::vector<Track>& playlist, const Track& track);
