#pragma once

namespace wb {
extern bool g_show_project_dialog;
void project_info_dialog();
bool popup_confirm(const char* str, const char* msg);
}