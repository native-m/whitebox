#pragma once

#include <imgui.h>

#define IMGUI_STYLE_DEF_VEC2(_name, _var)          \
  struct _name {                                   \
    static constexpr ImGuiStyleVar var = _var;     \
    ImVec2 value{};                                \
    inline _name() {                               \
    }                                              \
    inline _name(float x, float y) : value(x, y) { \
    }                                              \
    inline _name(const ImVec2& v) : value(v) {     \
    }                                              \
  };

#define IMGUI_STYLE_DEF_FLOAT(_name, _var)     \
  struct _name {                               \
    static constexpr ImGuiStyleVar var = _var; \
    float value{};                             \
    inline _name() {                           \
    }                                          \
    inline _name(float v) : value(v) {         \
    }                                          \
  };

IMGUI_STYLE_DEF_VEC2(ImStyleWindowPadding, ImGuiStyleVar_WindowPadding);
IMGUI_STYLE_DEF_VEC2(ImStyleFramePadding, ImGuiStyleVar_FramePadding);
IMGUI_STYLE_DEF_VEC2(ImStyleItemSpacing, ImGuiStyleVar_ItemSpacing);
IMGUI_STYLE_DEF_VEC2(ImStyleItemInnerSpacing, ImGuiStyleVar_ItemInnerSpacing);
IMGUI_STYLE_DEF_VEC2(ImStyleItemCellPadding, ImGuiStyleVar_CellPadding);

IMGUI_STYLE_DEF_FLOAT(ImStyleFrameRounding, ImGuiStyleVar_FrameRounding);
IMGUI_STYLE_DEF_FLOAT(ImStyleFrameBorderSize, ImGuiStyleVar_FrameBorderSize);

template<typename... Args>
struct __ImGuiStyleVarWrapper {
  static constexpr int num_args = sizeof...(Args);
  __ImGuiStyleVarWrapper(Args&&... args) {
    (push_style_var(std::decay_t<decltype(args)>::var, args.value), ...);
  }

  ~__ImGuiStyleVarWrapper() {
    ImGui::PopStyleVar(num_args);
  }

  static void push_style_var(ImGuiStyleVar var, const ImVec2& v) {
    if (v.y == -1.0f) {
      ImGui::PushStyleVarX(var, v.x);
    } else if (v.x == -1.0f) {
      ImGui::PushStyleVarY(var, v.y);
    } else [[likely]] {
      ImGui::PushStyleVar(var, v);
    }
  }

  static void push_style_var(ImGuiStyleVar var, float v) {
    ImGui::PushStyleVar(var, v);
  }
};

template<typename... Args>
__ImGuiStyleVarWrapper(Args&&... args) -> __ImGuiStyleVarWrapper<Args...>;

#define IMGUI_MACRO_UNPACK(...)    __VA_ARGS__
#define IMGUI_STYLE_BLOCK_NAME_(x) __imstylevar_##x
#define IMGUI_STYLE_BLOCK_NAME(x)  IMGUI_STYLE_BLOCK_NAME_(x)
#define IMGUI_STYLE_BLOCK_CTOR(x)  __ImGuiStyleVarWrapper IMGUI_MACRO_UNPACK x
#define IMGUI_STYLE_BLOCK(x)       if (const auto IMGUI_STYLE_BLOCK_NAME(__COUNTER__) = IMGUI_STYLE_BLOCK_CTOR(x); true)
