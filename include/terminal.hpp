#pragma once

#include <string>
#include <vector>

void print_banner();
void print_tracks(const std::vector<std::string>& lines);
void print_error(const std::string& message);
void print_info(const std::string& message);
bool terminal_is_interactive();
