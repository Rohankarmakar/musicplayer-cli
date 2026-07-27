#include "player.hpp"

#include "terminal.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

namespace {
bool available(const char* program) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string paths(path), part;
    for (std::size_t at = 0, next; at <= paths.size(); at = next + 1) {
        next = paths.find(':', at);
        part = paths.substr(at, next == std::string::npos ? next : next - at);
        if (access((part.empty() ? "." : part).append("/").append(program).c_str(), X_OK) == 0) return true;
        if (next == std::string::npos) break;
    }
    return false;
}

std::optional<double> audio_duration_seconds(const std::string& file) {
    if (!available("ffprobe")) return std::nullopt;
    int output[2];
    if (pipe(output) != 0) return std::nullopt;
    const pid_t child = fork();
    if (child < 0) {
        close(output[0]);
        close(output[1]);
        return std::nullopt;
    }
    if (child == 0) {
        dup2(output[1], STDOUT_FILENO);
        close(output[0]);
        close(output[1]);
        execlp("ffprobe", "ffprobe", "-v", "error", "-show_entries", "format=duration",
               "-of", "default=noprint_wrappers=1:nokey=1", file.c_str(), nullptr);
        _exit(127);
    }
    close(output[1]);
    std::string value;
    char buffer[64];
    ssize_t count;
    while ((count = read(output[0], buffer, sizeof(buffer))) > 0) value.append(buffer, static_cast<std::size_t>(count));
    close(output[0]);
    int status = 0;
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {}
    try {
        const double duration = std::stod(value);
        return duration > 0.0 && std::isfinite(duration) ? std::optional<double>(duration) : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string format_time(std::optional<double> seconds) {
    if (!seconds) return "--:--";
    const auto total = static_cast<unsigned long>(std::max(0.0, std::floor(*seconds)));
    const auto minutes = total / 60;
    const auto seconds_part = total % 60;
    std::ostringstream formatted;
    formatted << std::setfill('0');
    if (minutes >= 60)
        formatted << minutes / 60 << ':' << std::setw(2) << minutes % 60 << ':' << std::setw(2) << seconds_part;
    else
        formatted << std::setw(2) << minutes << ':' << std::setw(2) << seconds_part;
    return formatted.str();
}

class ProcessBackend final : public AudioBackend {
public:
    ProcessBackend(std::string executable, std::string label) : executable_(std::move(executable)), label_(std::move(label)) {}
    std::string name() const override { return label_; }
    pid_t start(const std::string& file, double start_seconds = 0.0) const override {
        const pid_t child = fork();
        if (child < 0) throw std::runtime_error("could not start audio player");
        if (child == 0) {
            // Each supported backend is deliberately executed without a shell.
            // The visualizer owns stdin for its search, discover, and stop controls.
            // Disable each backend's terminal input so it cannot consume those keys.
            const std::string position = std::to_string(start_seconds);
            if (executable_ == "mpv") {
                const std::string start = "--start=" + position;
                execlp("mpv", "mpv", "--no-video", "--really-quiet", "--input-terminal=no", start.c_str(), file.c_str(), nullptr);
            }
            if (executable_ == "ffplay")
                execlp("ffplay", "ffplay", "-ss", position.c_str(), "-nodisp", "-autoexit", "-nostdin", "-loglevel", "quiet", file.c_str(), nullptr);
            // mpg123 seeks by MPEG frames; 38 frames per second is a close estimate for its fallback mode.
            const std::string frames = std::to_string(static_cast<unsigned>(start_seconds * 38.0));
            execlp("mpg123", "mpg123", "-q", "--skip", frames.c_str(), file.c_str(), nullptr);
            _exit(127);
        }
        return child;
    }
private:
    std::string executable_, label_;
};

class SingleKeyInput {
public:
    SingleKeyInput() { enable(); }
    ~SingleKeyInput() { disable(); }

    void enable() {
        if (enabled_ || tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        termios current = saved_;
        current.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        current.c_cc[VMIN] = 0;
        current.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &current) == 0) enabled_ = true;
    }

    void disable() {
        if (enabled_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        enabled_ = false;
    }

private:
    termios saved_{};
    bool enabled_ = false;
};

std::optional<std::string> pressed_key() {
    static std::string pending;
    fd_set input;
    FD_ZERO(&input);
    FD_SET(STDIN_FILENO, &input);
    timeval timeout{};
    if (select(STDIN_FILENO + 1, &input, nullptr, nullptr, &timeout) > 0) {
        char keys[8];
        const ssize_t count = read(STDIN_FILENO, keys, sizeof(keys));
        if (count > 0) pending.append(keys, static_cast<std::size_t>(count));
    }
    if (pending.empty()) return std::nullopt;
    if (pending.front() == '\033') {
        // Arrow keys can be sent as ESC [ C/D, ESC O C/D (application cursor mode),
        // or a CSI sequence such as ESC [ 1 ; 5 C/D when modifiers are held.
        if (pending.size() < 2) return std::nullopt;
        if (pending[1] == 'O') {
            if (pending.size() < 3) return std::nullopt;
            const char final = pending[2];
            pending.erase(0, 3);
            if (final == 'C' || final == 'D') return std::string("\033[") + final;
            return std::string{};
        }
        if (pending[1] == '[') {
            std::size_t final_at = 2;
            while (final_at < pending.size() && (pending[final_at] < '@' || pending[final_at] > '~')) ++final_at;
            if (final_at == pending.size()) return std::nullopt;
            const char final = pending[final_at];
            pending.erase(0, final_at + 1);
            if (final == 'C' || final == 'D') return std::string("\033[") + final;
            return std::string{};
        }
        pending.erase(0, 1);
        return std::string{};
    }
    const std::string key(1, pending.front());
    pending.erase(0, 1);
    return key;
}

void stop_process(pid_t child) {
    if (child <= 0) return;
    kill(child, SIGTERM);
    int status = 0;
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {}
}

std::optional<Track> choose_track(const MusicLibrary& library, bool discover) {
    std::cout << "\033[2J\033[H";
    print_banner();
    std::string query;
    if (!discover) {
        std::cout << "Search for a track (leave blank to return): ";
        std::getline(std::cin, query);
        if (query.empty()) return std::nullopt;
    }
    const auto tracks = library.search(query);
    if (tracks.empty()) {
        print_info("No tracks found. Press Enter to return to playback.");
        std::getline(std::cin, query);
        return std::nullopt;
    }
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < tracks.size(); ++i)
        lines.push_back("[" + std::to_string(i + 1) + "] " + tracks[i].title);
    print_tracks(lines);
    std::cout << "\nChoose a track (1-" << tracks.size() << ", Enter to cancel): ";
    std::string selection;
    std::getline(std::cin, selection);
    if (selection.empty()) return std::nullopt;
    try {
        const auto choice = std::stoul(selection);
        if (choice == 0 || choice > tracks.size()) throw std::out_of_range("selection");
        return tracks[choice - 1];
    } catch (const std::exception&) {
        print_error("please choose a number from the displayed list");
        std::cout << "Press Enter to return to playback.";
        std::getline(std::cin, selection);
        return std::nullopt;
    }
}
}

std::unique_ptr<AudioBackend> make_audio_backend() {
    if (available("mpv")) return std::make_unique<ProcessBackend>("mpv", "mpv");
    if (available("ffplay")) return std::make_unique<ProcessBackend>("ffplay", "ffplay");
    if (available("mpg123")) return std::make_unique<ProcessBackend>("mpg123", "mpg123");
    throw std::runtime_error("no audio backend found; install mpv (recommended): sudo dnf install mpv");
}

void play_with_visualizer(const AudioBackend& backend, const MusicLibrary& library,
                          const std::vector<Track>& playlist, const Track& track) {
    if (playlist.empty()) return;
    pid_t child = backend.start(track.path.string());
    if (!terminal_is_interactive()) { int status; waitpid(child, &status, 0); return; }
    std::cout << "\033[?25l\033[2J\033[H";
    SingleKeyInput input;
    Track current = track;
    auto playlist_index = std::find_if(playlist.begin(), playlist.end(), [&current](const Track& item) {
        return item.path == current.path;
    });
    std::size_t current_index = playlist_index == playlist.end()
                                  ? 0 : static_cast<std::size_t>(playlist_index - playlist.begin());
    bool repeat_current = false;
    auto started_at = std::chrono::steady_clock::now();
    double start_offset = 0.0;
    auto total_duration = audio_duration_seconds(current.path.string());
    const std::array<int, 8> colors{51, 45, 39, 93, 129, 171, 207, 227};
    constexpr int kWaveRows = 12;
    constexpr int kWaveColumns = 64;
    constexpr double kWaveCenter = (kWaveRows - 1) / 2.0;
    constexpr double kWaveAmplitude = 4.5;
    constexpr double kWaveCycles = 2.0;
    constexpr double kPi = 3.14159265358979323846;
    unsigned frame = 0;
    int status = 0;
    while (true) {
        if (waitpid(child, &status, WNOHANG) == child) {
            if (!repeat_current) current_index = (current_index + 1) % playlist.size();
            current = playlist[current_index];
            child = backend.start(current.path.string());
            frame = 0;
            start_offset = 0.0;
            started_at = std::chrono::steady_clock::now();
            total_duration = audio_duration_seconds(current.path.string());
            continue;
        }
        std::cout << "\033[H";
        std::cout << "\033[38;5;51mNow playing\033[0m  " << current.title << "\n";
        std::cout << "\033[2mbackend: " << backend.name()
                  << "  •  [b/f] seek −/+5 sec  [s] search  [d] discover  [r] repeat: "
                  << (repeat_current ? "on" : "off") << "  [x] stop  [q] quit\033[0m\n\n";
        for (int row = 0; row < kWaveRows; ++row) {
            for (int column = 0; column < kWaveColumns; ++column) {
                const double phase = (static_cast<double>(column) / kWaveColumns * kWaveCycles * 2.0 * kPi)
                                   + static_cast<double>(frame) * 0.18;
                const int waveRow = static_cast<int>(std::lround(kWaveCenter - kWaveAmplitude * std::sin(phase)));
                if (row == waveRow) {
                    std::cout << "\033[38;5;" << colors[(column / 8 + frame / 7) % colors.size()] << "m●";
                } else if (row > waveRow) {
                    std::cout << "\033[38;5;" << colors[(column / 8 + frame / 7) % colors.size()] << "m·";
                } else {
                    std::cout << ' ';
                }
            }
            std::cout << "\033[0m\n";
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
        const double position = start_offset + elapsed;
        const auto remaining = total_duration ? std::optional<double>(std::max(0.0, *total_duration - position)) : std::nullopt;
        std::cout << "\033[2mTime remaining: " << format_time(remaining)
                  << " / " << format_time(total_duration) << "\033[0m\033[K\n";
        std::cout.flush();
        if (const auto key = pressed_key()) {
            if (key->find_first_of("bBfF") != std::string::npos) {
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
                const double delta = key->find_first_of("fF") != std::string::npos ? 5.0 : -5.0;
                start_offset = std::max(0.0, start_offset + elapsed + delta);
                stop_process(child);
                child = backend.start(current.path.string(), start_offset);
                started_at = std::chrono::steady_clock::now();
            } else if (key->find_first_of("xXqQ") != std::string::npos) {
                stop_process(child);
                std::cout << "\033[0m\033[?25h\nPlayback stopped.\n";
                return;
            } else if (key->find_first_of("rR") != std::string::npos) {
                repeat_current = !repeat_current;
            } else if (key->find_first_of("sSdD") != std::string::npos) {
                input.disable();
                const auto selected = choose_track(library, key->find_first_of("dD") != std::string::npos);
                input.enable();
                if (selected) {
                    stop_process(child);
                    current = *selected;
                    const auto selected_index = std::find_if(playlist.begin(), playlist.end(), [&current](const Track& item) {
                        return item.path == current.path;
                    });
                    if (selected_index != playlist.end())
                        current_index = static_cast<std::size_t>(selected_index - playlist.begin());
                    child = backend.start(current.path.string());
                    frame = 0;
                    start_offset = 0.0;
                    started_at = std::chrono::steady_clock::now();
                    total_duration = audio_duration_seconds(current.path.string());
                }
                std::cout << "\033[2J\033[H";
            }
        }
        ++frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }
    std::cout << "\033[0m\033[?25h\n";
}
