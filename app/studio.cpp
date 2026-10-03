#include "bridge.h"
#include "dsl/parser.h"
#include "engine/audio_in.h"
#include "engine/midi_in.h"
#include "engine/pipewire_out.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "imgui_node_editor.h"
#include "imgui_stdlib.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace ed = ax::NodeEditor;
using namespace ms::studio;
namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
void render(void *user, float *l, float *r, size_t n) {
  static_cast<ms::engine::RtRunner *>(user)->render(l, r, n);
}
ImVec4 mint{.35f, .86f, .74f, 1}, amber{1, .7f, .34f, 1},
    dim{.51f, .57f, .64f, 1};
std::string number(float value) {
  std::ostringstream s;
  s << std::fixed << std::setprecision(8) << value;
  return s.str();
}
ImVec4 color(const std::string &type) {
  if (type == "transport" || type == "clock" || type == "scene")
    return amber;
  if (type == "channel" || type == "out")
    return mint;
  if (type == "quant" || type == "phrase" || type == "stepseq")
    return {.69f, .59f, .96f, 1};
  return {.4f, .67f, .93f, 1};
}
int column(const std::string &type) {
  if (type == "transport" || type == "clock" || type == "scene" ||
      type == "lfo")
    return 0;
  if (type == "quant" || type == "phrase" || type == "stepseq" ||
      type == "bern" || type == "euclid" || type == "switch")
    return 1;
  if (type == "vco" || type == "additive" || type == "noise" ||
      type == "func" || type == "adsr")
    return 2;
  if (type == "channel" || type == "mix")
    return 4;
  if (type == "out")
    return 5;
  return 3;
}
void theme() {
  ImGui::StyleColorsDark();
  auto &s = ImGui::GetStyle();
  s.WindowPadding = {16, 14};
  s.FramePadding = {9, 6};
  s.ItemSpacing = {9, 8};
  s.WindowRounding = 8;
  s.FrameRounding = 5;
  s.GrabRounding = 4;
  s.ChildRounding = 7;
  s.Colors[ImGuiCol_WindowBg] = {.055f, .067f, .085f, 1};
  s.Colors[ImGuiCol_ChildBg] = {.075f, .09f, .11f, 1};
  s.Colors[ImGuiCol_FrameBg] = {.12f, .15f, .18f, 1};
  s.Colors[ImGuiCol_Button] = {.14f, .2f, .23f, 1};
  s.Colors[ImGuiCol_ButtonHovered] = {.2f, .35f, .36f, 1};
  s.Colors[ImGuiCol_ButtonActive] = {.17f, .42f, .4f, 1};
  s.Colors[ImGuiCol_CheckMark] = mint;
  s.Colors[ImGuiCol_SliderGrab] = mint;
  s.Colors[ImGuiCol_Header] = {.15f, .26f, .28f, 1};
  s.Colors[ImGuiCol_HeaderHovered] = {.2f, .34f, .37f, 1};
  s.Colors[ImGuiCol_Text] = {.88f, .9f, .91f, 1};
  s.Colors[ImGuiCol_TextDisabled] = dim;
  auto &n = ed::GetStyle();
  n.NodePadding = {14, 10, 14, 12};
  n.NodeRounding = 8;
  n.Colors[ed::StyleColor_Bg] = {.045f, .055f, .07f, 1};
  n.Colors[ed::StyleColor_Grid] = {.27f, .34f, .4f, .2f};
  n.Colors[ed::StyleColor_NodeBg] = {.085f, .105f, .13f, 1};
  n.Colors[ed::StyleColor_NodeBorder] = {.22f, .28f, .33f, 1};
  n.Colors[ed::StyleColor_SelNodeBorder] = mint;
}
struct Editor {
  Editor(Session &s, std::string path)
      : session(s), socket_path(std::move(path)) {}
  Session &session;
  std::string socket_path, source, selected, filter, error, parameter, value,
      add_name, add_type = "vco", add_config;
  uint64_t source_revision = 0, cache_revision = 0;
  bool source_dirty = false, all_ports = false, layout = false, fit = false;
  int next_id = 1;
  std::map<std::string, int> ids;
  std::set<int> positioned;
  std::map<std::string, float> edits;
  Json interfaces = Session::interfaces(), pending;
  std::string pending_method;
  int id(const std::string &key) {
    auto [it, inserted] = ids.try_emplace(key, next_id);
    if (inserted)
      ++next_id;
    return it->second;
  }
  void queue(std::string method, Json args) {
    args["revision"] = session.revision();
    pending_method = std::move(method);
    pending = std::move(args);
  }
  void set(const std::string &module, const std::string &param,
           const std::string &v) {
    queue("set_parameter",
          {{"module", module}, {"parameter", param}, {"value", v}});
  }
  void replace(std::string text) {
    queue("replace_patch", {{"source", std::move(text)}});
  }
  const Json *info(const std::string &type) {
    for (const auto &i : interfaces)
      if (i["type"] == type)
        return &i;
    return nullptr;
  }
  void meter(const char *label, float amplitude) {
    const float db = 20 * std::log10(std::max(amplitude, 1e-6f));
    ImGui::TextColored(dim, "%s", label);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          db > 0 ? ImVec4{1, .35f, .3f, 1} : mint);
    char text[32];
    std::snprintf(text, sizeof(text), "%+.1f dBFS", db);
    ImGui::ProgressBar(std::clamp((db + 60) / 60, 0.f, 1.f), {170, 22}, text);
    ImGui::PopStyleColor();
  }
  void toolbar(const Json &state) {
    ImGui::TextColored(mint, "R A T S N E S T");
    ImGui::SameLine();
    ImGui::TextDisabled("/  LIVE PATCH STUDIO");
    ImGui::SameLine();
    const auto audio = state.value("audio", Json::object());
    ImGui::TextColored(
        audio.value("ok", false) ? mint : amber, "[%s]",
        audio.value("mode", std::string("initializing")).c_str());
    ImGui::SameLine();
    ImGui::Text("    %s%s",
                std::filesystem::path(session.path()).filename().c_str(),
                session.dirty() ? "  *" : "");
    ImGui::SameLine();
    if (ImGui::Button("Save"))
      queue("save", Json::object());
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.value("can_undo", false));
    if (ImGui::Button("Undo"))
      queue("undo", Json::object());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.value("can_redo", false));
    if (ImGui::Button("Redo"))
      queue("redo", Json::object());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fit graph"))
      fit = true;
    ImGui::SameLine();
    if (ImGui::Button("Arrange"))
      layout = true;
    ImGui::SameLine();
    ImGui::Checkbox("All ports", &all_ports);
    for (const auto &node : state["modules"])
      if (node["type"] == "transport" || node["type"] == "clock") {
        const std::string name = node["name"];
        ImGui::TextColored(amber, "TRANSPORT");
        ImGui::SameLine();
        const bool running = node["inputs"].value("run", 1.f) > .5f;
        if (ImGui::Button(running ? "Pause" : "Play"))
          set(name, "run", running ? "0" : "1");
        ImGui::SameLine();
        ImGui::Text("%.1f BPM", node["inputs"].value("bpm", 120.f));
        if (node["outputs"].contains("phrase_phase") &&
            !node["outputs"]["phrase_phase"].is_null()) {
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_PlotHistogram, amber);
          ImGui::ProgressBar(node["outputs"]["phrase_phase"].get<float>(),
                             {170, 16}, "phrase");
          ImGui::PopStyleColor();
        }
        break;
      }
    ImGui::SameLine();
    meter("L", state.value("peak_l", 0.f));
    ImGui::SameLine();
    meter("R", state.value("peak_r", 0.f));
    for (const auto &node : state["modules"])
      if (node["type"] == "scene") {
        const std::string name = node["name"];
        ImGui::TextColored(amber, "SCENES");
        ImGui::SameLine();
        const auto parsed = ms::dsl::parse(session.source());
        size_t count = 0;
        for (const auto &m : parsed.ast.modules)
          if (m.name == name)
            for (const auto &p : m.params)
              if (p.name == "bars")
                count = p.value.items.size();
        const auto &outputs = node["outputs"];
        const int current =
            outputs["index"].is_number() ? outputs["index"].get<int>() : -1;
        const int waiting =
            outputs["pending"].is_number() ? outputs["pending"].get<int>() : -1;
        for (size_t i = 0; i < count; ++i) {
          ImGui::PushID(static_cast<int>(i));
          if (current == static_cast<int>(i))
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{.2f, .4f, .34f, 1});
          const std::string label =
              std::to_string(i + 1) +
              (waiting == static_cast<int>(i) ? " queued" : "");
          if (ImGui::Button(label.c_str()))
            set(name, "request", std::to_string(i));
          if (current == static_cast<int>(i))
            ImGui::PopStyleColor();
          ImGui::PopID();
          ImGui::SameLine();
        }
        bool hold = node["inputs"].value("hold", 0.f) > .5f;
        if (ImGui::Checkbox("Hold", &hold))
          set(name, "hold", hold ? "1" : "0");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel queued"))
          set(name, "request", "-1");
        break;
      }
  }
  void inspector(const Json &state) {
    ImGui::TextColored(dim, "INSPECTOR");
    ImGui::Separator();
    const Json *chosen = nullptr;
    for (const auto &n : state["modules"])
      if (n["name"] == selected)
        chosen = &n;
    if (!chosen) {
      ImGui::TextWrapped(
          "Select a node to edit its inputs and inspect live outputs.");
      return;
    }
    const auto &n = *chosen;
    const std::string type = n["type"];
    ImGui::TextColored(color(type), "%s", selected.c_str());
    ImGui::TextDisabled("%s", type.c_str());
    bool authored = false;
    for (const auto &m : ms::dsl::parse(session.source()).ast.modules)
      if (m.name == selected)
        authored = true;
    if (!authored)
      ImGui::TextWrapped("Expanded node. Edit its parent subpatch in Source.");
    if (cache_revision != session.revision()) {
      edits.clear();
      cache_revision = session.revision();
    }
    if (ImGui::CollapsingHeader("Input bases", ImGuiTreeNodeFlags_DefaultOpen))
      for (const auto &[key, val] : n["inputs"].items()) {
        const auto full = selected + "." + key;
        auto [it, added] = edits.try_emplace(full, val.get<float>());
        (void)added;
        ImGui::PushID(key.c_str());
        ImGui::BeginDisabled(!authored);
        ImGui::SetNextItemWidth(145);
        ImGui::DragFloat("##value", &it->second, .01f, 0, 0, "%.4f");
        if (ImGui::IsItemDeactivatedAfterEdit())
          set(selected, key, number(it->second));
        ImGui::SameLine();
        ImGui::TextUnformatted(key.c_str());
        ImGui::EndDisabled();
        ImGui::PopID();
      }
    if (ImGui::CollapsingHeader("Configuration")) {
      if (const auto *schema = info(type))
        for (const auto &c : (*schema)["configs"]) {
          const auto key = c["name"].get<std::string>();
          if (ImGui::Selectable(
                  (key + "  (" + c["kind"].get<std::string>() + ")").c_str(),
                  parameter == key))
            parameter = key;
        }
      ImGui::InputText("Parameter", &parameter);
      ImGui::InputText("DSL value", &value);
      ImGui::BeginDisabled(!authored || parameter.empty() || value.empty());
      if (ImGui::Button("Apply config"))
        set(selected, parameter, value);
      ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Live outputs", ImGuiTreeNodeFlags_DefaultOpen))
      for (const auto &[key, val] : n["outputs"].items()) {
        if (val.is_number())
          ImGui::Text("%-15s % .5f", key.c_str(), val.get<double>());
        else
          ImGui::TextDisabled("%s  waiting", key.c_str());
      }
  }
  void graph(const Json &state) {
    struct Pin {
      std::string node, port;
      bool input;
    };
    std::map<int, Pin> pins;
    std::set<std::string> connected;
    for (const auto &c : state["cables"]) {
      connected.insert(c["target"].get<std::string>() + "." +
                       c["input"].get<std::string>());
    }
    ed::Begin("Patch graph");
    std::array<int, 6> rows{};
    std::map<int, int> columns;
    for (const auto &n : state["modules"])
      columns[column(n["type"].get<std::string>())] = 0;
    int next_column = 0;
    for (auto &[category, position] : columns) {
      (void)category;
      position = next_column++;
    }
    for (const auto &n : state["modules"]) {
      const std::string name = n["name"], type = n["type"];
      const int node = id("node:" + name);
      const int col = columns.at(column(type));
      if (layout || !positioned.contains(node)) {
        ed::SetNodePosition(
            node, {static_cast<float>(col * 245),
                   static_cast<float>(rows[static_cast<size_t>(col)])});
        positioned.insert(node);
      }
      int visible_inputs = 0;
      if (const auto *shape = info(type))
        for (const auto &input : (*shape)["inputs"]) {
          const std::string port = input["name"];
          if (all_ports || name == selected || visible_inputs < 2 ||
              connected.contains(name + "." + port))
            ++visible_inputs;
        }
      rows[static_cast<size_t>(col)] +=
          100 + 26 * (visible_inputs + static_cast<int>(n["outputs"].size()));
      ed::PushStyleColor(ed::StyleColor_NodeBorder, color(type));
      ed::BeginNode(node);
      ImGui::TextColored(color(type), "%s", type.c_str());
      ImGui::SameLine();
      ImGui::TextUnformatted(name.c_str());
      ImGui::Separator();
      const auto *schema = info(type);
      size_t index = 0;
      if (schema)
        for (const auto &input : (*schema)["inputs"]) {
          const std::string port = input["name"];
          const bool visible = all_ports || name == selected || index < 2 ||
                               connected.contains(name + "." + port);
          ++index;
          if (!visible)
            continue;
          const int pin = id("in:" + name + "." + port);
          pins[pin] = {name, port, true};
          ed::BeginPin(pin, ed::PinKind::Input);
          ed::PinPivotAlignment({0, .5f});
          ed::PinPivotSize({0, 0});
          ImGui::TextColored(dim, "o");
          ImGui::SameLine();
          ImGui::TextUnformatted(port.c_str());
          ed::EndPin();
        }
      ImGui::Dummy({165, 2});
      for (const auto &[port, val] : n["outputs"].items()) {
        const int pin = id("out:" + name + "." + port);
        pins[pin] = {name, port, false};
        ed::BeginPin(pin, ed::PinKind::Output);
        ed::PinPivotAlignment({1, .5f});
        ed::PinPivotSize({0, 0});
        if (val.is_number())
          ImGui::TextDisabled("% .3f", val.get<double>());
        else
          ImGui::TextDisabled("   --");
        ImGui::SameLine(85);
        ImGui::TextUnformatted(port.c_str());
        ImGui::SameLine();
        ImGui::TextColored(color(type), "o");
        ed::EndPin();
      }
      ed::EndNode();
      ed::PopStyleColor();
    }
    size_t cable_index = 0;
    for (const auto &c : state["cables"]) {
      const auto from =
          c["source"].get<std::string>() + "." + c["output"].get<std::string>();
      const auto to =
          c["target"].get<std::string>() + "." + c["input"].get<std::string>();
      ed::Link(id("link:" + std::to_string(cable_index++)), id("out:" + from),
               id("in:" + to), {.36f, .55f, .62f, .7f}, 2);
    }
    if (ed::BeginCreate(mint, 2)) {
      ed::PinId a, b;
      if (ed::QueryNewLink(&a, &b) && a && b &&
          pins.contains(static_cast<int>(a.Get())) &&
          pins.contains(static_cast<int>(b.Get()))) {
        auto p = pins.at(static_cast<int>(a.Get())),
             q = pins.at(static_cast<int>(b.Get()));
        if (p.input)
          std::swap(p, q);
        if (!p.input && q.input) {
          if (ed::AcceptNewItem())
            replace(session.source() + "\n" + p.node + "." + p.port + " -> " +
                    q.node + "." + q.port + "\n");
        } else
          ed::RejectNewItem();
      }
    }
    ed::EndCreate();
    if (ed::BeginDelete()) {
      ed::LinkId link;
      if (ed::QueryDeletedLink(&link)) {
        bool found = false;
        for (size_t i = 0; i < state["cables"].size(); ++i)
          if (id("link:" + std::to_string(i)) == static_cast<int>(link.Get())) {
            if (ed::AcceptDeletedItem())
              queue("disconnect", state["cables"][i]);
            found = true;
            break;
          }
        if (!found)
          ed::RejectDeletedItem();
      }
      ed::NodeId node;
      if (ed::QueryDeletedNode(&node)) {
        for (const auto &n : state["modules"])
          if (id("node:" + n["name"].get<std::string>()) ==
              static_cast<int>(node.Get())) {
            if (ed::AcceptDeletedItem())
              queue("remove_node", {{"module", n["name"]}});
            break;
          }
      }
    }
    ed::EndDelete();
    ed::NodeId selection;
    if (ed::GetSelectedNodes(&selection, 1) > 0)
      for (const auto &n : state["modules"]) {
        const std::string name = n["name"];
        if (id("node:" + name) == static_cast<int>(selection.Get()))
          selected = name;
      }
    if (fit) {
      ed::NavigateToContent(.2f);
      fit = false;
    }
    layout = false;
    ed::End();
  }
  void draw(const std::string &executable) {
    const auto state = session.state();
    if (!source_dirty && source_revision != session.revision()) {
      source = session.source();
      source_revision = session.revision();
    }
    auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Ratsnest", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    toolbar(state);
    ImGui::Separator();
    const float h = std::max(100.f, ImGui::GetContentRegionAvail().y - 45);
    ImGui::BeginChild("browser", {195, h}, ImGuiChildFlags_Borders);
    ImGui::TextColored(dim, "PATCH NODES");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##find", "Find a node...", &filter);
    if (ImGui::Button("+ Add module", {-1, 0}))
      ImGui::OpenPopup("Add module");
    if (ImGui::BeginPopup("Add module")) {
      ImGui::InputText("Name", &add_name);
      if (ImGui::BeginCombo("Type", add_type.c_str())) {
        for (const auto &module : interfaces) {
          const auto type = module["type"].get<std::string>();
          if (ImGui::Selectable(type.c_str(), type == add_type))
            add_type = type;
        }
        ImGui::EndCombo();
      }
      ImGui::InputText("Parameters", &add_config);
      ImGui::TextDisabled("e.g. bpm: 100, swing: 0.2");
      if (ImGui::Button("Add")) {
        replace(session.source() + "\n" + add_name + " = " + add_type + " { " +
                add_config + " }\n");
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    for (const auto &n : state["modules"]) {
      const std::string name = n["name"], type = n["type"];
      if (!filter.empty() && name.find(filter) == std::string::npos &&
          type.find(filter) == std::string::npos)
        continue;
      if (ImGui::Selectable(name.c_str(), selected == name)) {
        selected = name;
        ed::SelectNode(id("node:" + name));
        ed::NavigateToSelection();
      }
      ImGui::TextDisabled("  %s", type.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();
    const float canvas =
        std::max(240.f, ImGui::GetContentRegionAvail().x - 325);
    ImGui::BeginChild("center", {canvas, h}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("views")) {
      if (ImGui::BeginTabItem("Node graph")) {
        graph(state);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Source")) {
        if (source_dirty && source_revision != session.revision())
          ImGui::TextColored(
              amber, "Live patch changed. Refresh or merge before applying.");
        if (ImGui::Button("Apply source")) {
          pending_method = "replace_patch";
          pending = {{"source", source}, {"revision", source_revision}};
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh from live")) {
          source = session.source();
          source_revision = session.revision();
          source_dirty = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Valid edits become audible. Save writes the file.");
        if (ImGui::InputTextMultiline("##source", &source, {-1, -1},
                                      ImGuiInputTextFlags_AllowTabInput))
          source_dirty = true;
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("inspector", {0, h}, ImGuiChildFlags_Borders);
    inspector(state);
    ImGui::EndChild();
    ImGui::TextColored(error.empty() ? dim : amber, "%s",
                       error.empty() ? session.message().c_str()
                                     : error.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" | rev %llu",
                        static_cast<unsigned long long>(session.revision()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy MCP config")) {
      Json config;
      config["mcpServers"]["ratsnest"] = {{"command", executable},
                                          {"args", {"--mcp", socket_path}}};
      ImGui::SetClipboardText(config.dump(2).c_str());
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Connect an agent to this running session\n%s",
                        socket_path.c_str());
    ImGui::End();
    if (!pending_method.empty()) {
      try {
        session.call(pending_method, pending);
        error.clear();
        if (pending_method == "replace_patch") {
          source_dirty = false;
          source_revision = 0;
        }
      } catch (const std::exception &e) {
        error = e.what();
      }
      pending_method.clear();
    }
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--mcp")
      return ms::studio::mcp(argv[2]);
    if (argc < 2) {
      std::cerr << "usage: ratsnest-studio PATCH [--silent] [--headless] "
                   "[--socket PATH] [--frames N] [--screenshot FILE.ppm]\n     "
                   "  ratsnest-studio --mcp SOCKET\n";
      return 2;
    }
    bool silent = false, headless = false;
    int frames = 0;
    std::string path = argv[1], socket_path = default_socket(path), screenshot;
    for (int i = 2; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--silent")
        silent = true;
      else if (arg == "--headless")
        headless = true;
      else if (arg == "--socket" && i + 1 < argc)
        socket_path = argv[++i];
      else if (arg == "--frames" && i + 1 < argc)
        frames = std::stoi(argv[++i]);
      else if (arg == "--screenshot" && i + 1 < argc)
        screenshot = argv[++i];
      else
        throw std::runtime_error("Unknown option: " + arg);
    }
    auto session = std::make_unique<Session>(path);
    Bridge bridge(*session, socket_path);
    std::unique_ptr<ms::engine::PipewireOut> audio;
    std::unique_ptr<ms::engine::MidiIn> midi;
    std::unique_ptr<ms::engine::AudioIn> capture;
    if (!silent) {
      audio = std::make_unique<ms::engine::PipewireOut>(
          "ratsnest-studio", render, &session->runner(), nullptr, 48000);
      midi = std::make_unique<ms::engine::MidiIn>("ratsnest-studio-midi",
                                                  session->runner());
      capture = std::make_unique<ms::engine::AudioIn>("ratsnest-studio-capture",
                                                      session->runner(), 48000);
    }
    std::cerr << "Ratsnest session: " << path << "\nMCP socket: " << socket_path
              << "\n";
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    auto pump = [&] {
      if (silent)
        session->set_audio_status(
            {{"mode", "silent preview"}, {"ok", true}, {"rate", 48000}});
      else
        session->set_audio_status(
            {{"mode", audio->ok() ? "PipeWire live" : "audio offline"},
             {"ok", audio->ok()},
             {"error", audio->error()},
             {"rate", audio->observed_rate()},
             {"xruns", audio->xruns()},
             {"midi_error", midi->error()},
             {"capture_error", capture->error()}});
      if (silent) {
        float l[800], r[800];
        session->runner().render(l, r, 800);
      }
      session->poll();
      bridge.poll();
    };
    if (headless) {
      for (int f = 0; !stopped && (frames <= 0 || f < frames); ++f) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }
      return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
      throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window =
        SDL_CreateWindow("Ratsnest | Live Patch Studio", 1600, 1000,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                             SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
      throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context)
      throw std::runtime_error(SDL_GetError());
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const char *font_path = "/usr/share/fonts/noto/NotoSans-Regular.ttf";
    if (std::filesystem::exists(font_path))
      ImGui::GetIO().Fonts->AddFontFromFileTTF(font_path, 16);
    else {
      ImFontConfig font;
      font.SizePixels = 16;
      ImGui::GetIO().Fonts->AddFontDefault(&font);
    }
    ed::Config config;
    config.SettingsFile = nullptr;
    auto *editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(editor);
    theme();
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330");
    Editor ui{*session, socket_path};
    bool quit = false, request_close = false;
    int f = 0;
    while (!quit && !stopped && (frames <= 0 || f < frames)) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT)
          request_close = true;
      }
      pump();
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      ui.draw(std::filesystem::canonical("/proc/self/exe").string());
      if (request_close) {
        if (session->dirty() || ui.source_dirty)
          ImGui::OpenPopup("Unsaved changes");
        else
          quit = true;
        request_close = false;
      }
      if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Keep your live patch and source edits before closing?");
        if (ImGui::Button("Save and close")) {
          try {
            if (ui.source_dirty)
              session->call(
                  "replace_patch",
                  {{"source", ui.source}, {"revision", ui.source_revision}});
            session->call("save", {{"revision", session->revision()}});
            quit = true;
          } catch (const std::exception &e) {
            ui.error = e.what();
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and close"))
          quit = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::Render();
      int width = 0, height = 0;
      SDL_GetWindowSizeInPixels(window, &width, &height);
      glViewport(0, 0, width, height);
      glClearColor(.045f, .055f, .07f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (!screenshot.empty() && frames > 0 && f == frames - 1) {
        std::vector<unsigned char> pixels(
            static_cast<size_t>(width * height * 3));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE,
                     pixels.data());
        std::ofstream image(screenshot, std::ios::binary);
        image << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y)
          image.write(reinterpret_cast<char *>(
                          pixels.data() + static_cast<size_t>(y * width * 3)),
                      width * 3);
      }
      SDL_GL_SwapWindow(window);
      ++f;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ed::DestroyEditor(editor);
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (session->dirty())
      std::cerr << "Live edits were not saved. Use Save before closing to "
                   "persist them.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ratsnest-studio: " << e.what() << "\n";
    return 1;
  }
}
