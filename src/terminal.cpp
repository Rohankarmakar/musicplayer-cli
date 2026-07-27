#include "terminal.hpp"

#include <iostream>
#include <unistd.h>

namespace {
constexpr const char *kReset = "\033[0m";
constexpr const char *kCyan = "\033[38;5;51m";
constexpr const char *kPink = "\033[38;5;213m";
constexpr const char *kYellow = "\033[38;5;227m";
} // namespace

void print_banner() {
  std::cout << kCyan << "\n  ♫  musicplayer" << kPink
            << " / music in your terminal" << kReset << "\n\n";
}
void print_tracks(const std::vector<std::string> &lines) {
  for (const auto &line : lines)
    std::cout << kYellow << "  " << line << kReset << '\n';
}
void print_error(const std::string &message) {
  std::cerr << "\033[31merror: " << message << kReset << '\n';
}
void print_info(const std::string &message) {
  std::cout << kCyan << message << kReset << '\n';
}
bool terminal_is_interactive() {
  return isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
}
