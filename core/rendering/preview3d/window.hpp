#pragma once
#include "renderer.hpp"
#include "game_model.hpp"
#include <external/xdraw/xui/xui.hpp>
#include <external/nlohmann/json.hpp>
#include <core/features/changer/changer.hpp>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace nemesis::preview3d {
inline constexpr std::uintptr_t window_id = 0x4e454d3350525657ull;
inline std::filesystem::path utf8_path(const std::string& value) { return std::filesystem::u8path(value); }
inline std::string path_text(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}
inline std::filesystem::path data_directory() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
    if (!size || size >= 32768) throw std::runtime_error("LOCALAPPDATA is unavailable");
    return std::filesystem::path(buffer) / L"nemesis" / L"preview";
}
inline com_ptr<ID3D11ShaderResourceView> load_albedo(ID3D11Device* device, const std::filesystem::path& path) {
    if (path.empty()) return {};
    if (std::filesystem::file_size(path) > 64ull * 1024 * 1024) throw std::runtime_error("Texture file exceeds 64 MiB");
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) check_hr(init, "Initialize WIC/COM");
    struct com_scope { bool owned; ~com_scope() { if (owned) CoUninitialize(); } } scope{SUCCEEDED(init)};
    com_ptr<IWICImagingFactory> factory;
    check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())), "Create WIC factory");
    com_ptr<IWICBitmapDecoder> decoder;
    check_hr(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()), "Decode albedo image");
    com_ptr<IWICBitmapFrameDecode> frame;
    check_hr(decoder->GetFrame(0, frame.GetAddressOf()), "Read albedo frame");
    UINT width{}, height{};
    check_hr(frame->GetSize(&width, &height), "Read albedo dimensions");
    if (!width || !height || width > 8192 || height > 8192 || static_cast<std::uint64_t>(width) * height > 16777216)
        throw std::runtime_error("Texture limit: 8192 per side and 16 million pixels");
    com_ptr<IWICFormatConverter> converter;
    check_hr(factory->CreateFormatConverter(converter.GetAddressOf()), "Create RGBA converter");
    check_hr(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert albedo to RGBA");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    check_hr(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()), "Read albedo pixels");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
    com_ptr<ID3D11Texture2D> texture;
    com_ptr<ID3D11ShaderResourceView> view;
    check_hr(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()), "Upload albedo");
    check_hr(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()), "Create albedo SRV");
    return view;
}

class preview_window final : public xui::overlay {
    int def_{}, paint_{};
    std::string name_, obj_path_, texture_path_, loaded_obj_, loaded_texture_;
    std::string status_ = "Loading CS2 game model...";
    std::filesystem::path config_path_;
    std::function<void()> native_inspect_;
    mesh mesh_;
    std::unique_ptr<renderer> renderer_;
    camera camera_;
    std::optional<surface_anchor> anchor_;
    nlohmann::json saved_;
    bool fullscreen_{}, place_{}, drag_anchor_{}, orbit_{}, drag_window_{}, dirty_{}, first_{true};
    bool autoload_{true};
    bool loaded_from_game_{true};
    xui::rect window_{};
    float grab_x_{}, grab_y_{};

    xui::rect bounds() const {
        const auto [sw, sh] = xdraw::viewport_size();
        if (fullscreen_) return {0, 0, static_cast<float>(sw), static_cast<float>(sh)};
        auto r = window_;
        r.w = std::min(r.w, std::max(240.0f, static_cast<float>(sw) - 16));
        r.h = std::min(r.h, std::max(260.0f, static_cast<float>(sh) - 16));
        r.x = std::clamp(r.x, 0.0f, std::max(0.0f, sw - r.w));
        r.y = std::clamp(r.y, 0.0f, std::max(0.0f, sh - r.h));
        return r;
    }
    xui::rect viewport() const {
        const auto r = bounds(); return {r.x + 12, r.y + 146, std::max(16.0f, r.w - 24), std::max(16.0f, r.h - 220)};
    }
    std::optional<hit> pick(float x, float y) const {
        const auto v = viewport();
        if (!v.contains(x, y) || mesh_.vertices.empty()) return std::nullopt;
        const float nx = 2 * (x - v.x) / v.w - 1, ny = 1 - 2 * (y - v.y) / v.h;
        return raycast(mesh_, camera_.eye(), camera_.ray(nx, ny, v.w / v.h));
    }
    std::optional<vec2> marker() const {
        if (!anchor_) return std::nullopt;
        const auto v = viewport();
        const auto p = anchor_position(mesh_, *anchor_);
        const auto projected = camera_.project(p, v.w / v.h);
        if (!projected || std::abs(projected->x) > 1 || std::abs(projected->y) > 1) return std::nullopt;
        const auto to = p - camera_.eye();
        const auto nearest = raycast(mesh_, camera_.eye(), unit(to));
        if (nearest && nearest->distance < length(to) - 0.002f) return std::nullopt;
        return vec2{v.x + (projected->x + 1) * 0.5f * v.w, v.y + (1 - projected->y) * 0.5f * v.h};
    }
    void restore_anchor() {
        if (!saved_.is_object() || saved_.value("mesh_fingerprint", std::uint64_t{}) != mesh_.fingerprint) {
            if (saved_.contains("anchor") && !saved_["anchor"].is_null()) status_ += " Saved anchor ignored: model geometry changed.";
            return;
        }
        const auto a = saved_.value("anchor", nlohmann::json{});
        if (!a.is_object()) return;
        const auto triangle = a.at("triangle").get<std::uint64_t>();
        const float u = a.at("u").get<float>(), v = a.at("v").get<float>();
        if (triangle >= mesh_.vertices.size() / 3 || !std::isfinite(u) || !std::isfinite(v) || u < 0 || v < 0 || u + v > 1)
            throw std::runtime_error("Saved anchor is invalid");
        anchor_ = surface_anchor{static_cast<std::uint32_t>(triangle), u, v};
    }
    void load(bool force_live = false) {
        try {
            mesh model;
            std::string source_info;

            if (force_live) {
                if (try_capture_from_game(def_, model)) {
                    source_info = "Live in-hand CS2 weapon";
                    loaded_from_game_ = true;
                } else {
                    throw std::runtime_error("Live capture unavailable. Weapon must be held in-game.");
                }
            } else if (!obj_path_.empty() && std::filesystem::is_regular_file(utf8_path(obj_path_))) {
                const auto path = utf8_path(obj_path_);
                if (std::filesystem::file_size(path) > 32ull * 1024 * 1024) throw std::runtime_error("OBJ exceeds 32 MiB");
                std::ifstream file(path, std::ios::binary);
                if (!file) throw std::runtime_error("Cannot open OBJ");
                model = read_obj(file);
                source_info = "Custom local OBJ";
                loaded_from_game_ = false;
            } else {
                // Primary path: Load model directly from CS2 game database / live scene
                if (try_capture_from_game(def_, model)) {
                    source_info = "Captured live from CS2";
                    loaded_from_game_ = true;
                } else {
                    model = generate_weapon_mesh(def_, name_);
                    source_info = "CS2 game model (" + name_ + ")";
                    loaded_from_game_ = true;
                }
            }

            // Texture: check custom path or query skin image directly from CS2 VPK
            com_ptr<ID3D11ShaderResourceView> albedo;
            if (!texture_path_.empty() && std::filesystem::is_regular_file(utf8_path(texture_path_))) {
                albedo = load_albedo(xdraw::device(), utf8_path(texture_path_));
                source_info += " + Custom texture";
            } else {
                const auto* skin_img = features::changer::g_econ_item_system.get_skin_image(
                    static_cast<std::int16_t>(def_), paint_
                );
                if (skin_img && skin_img->srv) {
                    albedo = skin_img->srv;
                    source_info += " + Game skin texture";
                }
            }

            auto gpu = std::make_unique<renderer>(xdraw::device());
            gpu->upload(model, albedo.Get());

            const bool same = mesh_.fingerprint == model.fingerprint && !mesh_.vertices.empty();
            mesh_ = std::move(model);
            renderer_ = std::move(gpu);
            loaded_obj_ = obj_path_;
            loaded_texture_ = texture_path_;

            if (!same) { anchor_.reset(); camera_ = camera{}; }
            dirty_ = true;
            status_ = "Loaded " + std::to_string(mesh_.vertices.size() / 3) + " triangles [" + source_info + "]. Ready.";

            if (!same) {
                try { restore_anchor(); } catch (const std::exception& error) { status_ += " " + std::string(error.what()); }
            }
        } catch (const std::exception& error) {
            status_ = std::string("Load failed: ") + error.what();
            if (renderer_) status_ += " Previous model retained.";
        }
    }
    void save() {
        try {
            if (mesh_.vertices.empty()) throw std::runtime_error("Load a model before saving");
            if (config_path_.empty()) throw std::runtime_error("Preview data directory is unavailable");
            nlohmann::json data = {
                {"version", 1},
                {"def_index", def_},
                {"paint_kit", paint_},
                {"name", name_},
                {"source", loaded_from_game_ ? "cs2_game" : "custom_obj"},
                {"obj", loaded_obj_},
                {"texture", loaded_texture_},
                {"mesh_fingerprint", mesh_.fingerprint},
                {"coordinate_system", "CS2 weapon model normalized coordinate space"},
                {"anchor", nullptr}
            };
            if (anchor_) {
                const auto p = anchor_position(mesh_, *anchor_) * mesh_.radius + mesh_.center;
                const auto n = anchor_normal(mesh_, *anchor_);
                data["anchor"] = {
                    {"triangle", anchor_->triangle},
                    {"u", anchor_->u},
                    {"v", anchor_->v},
                    {"position", {p.x, p.y, p.z}},
                    {"normal", {n.x, n.y, n.z}}
                };
            }
            std::filesystem::create_directories(config_path_.parent_path());
            auto temp = config_path_; temp += L".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out << data.dump(2); out.flush();
                if (!out) throw std::runtime_error("Cannot write preview JSON");
            }
            if (!MoveFileExW(temp.c_str(), config_path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace preview JSON, Win32=" + std::to_string(GetLastError()));
            saved_ = std::move(data); dirty_ = false;
            status_ = "Saved: " + path_text(config_path_);
        } catch (const std::exception& error) { status_ = std::string("Save failed: ") + error.what(); }
    }
    bool button(xdraw::draw_list& dl, const xui::rect& r, std::string_view label, const xui::style& style, bool active = false) {
        const auto& input = xui::ctx().input;
        const bool hovered = input.in_rect(r);
        const auto bg = active ? style.button_active : hovered ? style.button_hovered : style.button_bg;
        dl.rect_filled(r.x, r.y, r.w, r.h, bg, xdraw::corner_radius{4});
        const auto text = xui::truncate(label, r.w - 8);
        const auto [tw, th] = xdraw::measure_text(text);
        dl.text(r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, text, active ? xdraw::color{20, 24, 30, 255} : style.text);
        return hovered && input.mouse_clicked;
    }
public:
    preview_window(int def, int paint, std::string name, std::function<void()> native)
        : overlay(window_id, {}), def_(def), paint_(paint), name_(std::move(name)), native_inspect_(std::move(native)) {
        const auto [sw, sh] = xdraw::viewport_size();
        window_ = {std::max(8.0f, (sw - 1040.0f) * 0.5f), std::max(8.0f, (sh - 740.0f) * 0.5f), 1040, 740};
        try {
            const auto root = data_directory();
            config_path_ = root / (std::to_string(def) + "_" + std::to_string(paint) + ".json");
            if (std::filesystem::is_regular_file(config_path_)) {
                if (std::filesystem::file_size(config_path_) > 65536) throw std::runtime_error("Saved preview JSON exceeds 64 KiB");
                std::ifstream input(config_path_, std::ios::binary); input >> saved_;
                if (saved_.at("version") == 1 && saved_.at("def_index") == def && saved_.at("paint_kit") == paint) {
                    obj_path_ = saved_.value("obj", std::string{});
                    texture_path_ = saved_.value("texture", std::string{});
                }
            }
        } catch (const std::exception& error) { status_ = error.what(); saved_ = nlohmann::json{}; }
    }
    bool hit_test(float, float) const override { return !m_closed; } // Modal, no click-through.
    bool process_input(const xui::input_state& input) override {
        if (m_closing) { force_close(); return true; }
        if (first_) return true;
        for (const auto key : input.key_presses()) {
            if (key == VK_ESCAPE) { force_close(); return true; }
            if (key == VK_F11) fullscreen_ = !fullscreen_;
        }
        if (!input.mouse_down) { drag_anchor_ = false; drag_window_ = false; }
        if (!input.rmb_down) orbit_ = false;
        const auto v = viewport(), r = bounds();
        if (input.mouse_clicked && !fullscreen_ && xui::rect{r.x, r.y, r.w, 32}.contains(input.mouse_x, input.mouse_y)) {
            drag_window_ = true; grab_x_ = input.mouse_x - r.x; grab_y_ = input.mouse_y - r.y;
        }
        if (drag_window_ && input.mouse_down) { window_.x = input.mouse_x - grab_x_; window_.y = input.mouse_y - grab_y_; }
        if (input.in_rect(v)) {
            if (input.rmb_clicked) orbit_ = true;
            if (input.scroll_delta != 0) {
                camera_.distance = std::clamp(camera_.distance * std::exp(-std::clamp(input.scroll_delta, -10.0f, 10.0f) * 0.12f), 1.15f, 25.0f);
            }
            if (input.mouse_clicked) {
                const auto p = marker();
                const bool on_marker = p && std::hypot(input.mouse_x - p->x, input.mouse_y - p->y) <= 12.0f;
                if (place_ || on_marker) {
                    if (const auto hit = pick(input.mouse_x, input.mouse_y)) {
                        anchor_ = hit->anchor; drag_anchor_ = true; place_ = false; dirty_ = true;
                    }
                }
            }
        }
        if (orbit_ && input.rmb_down && !drag_anchor_) {
            camera_.yaw = std::remainder(camera_.yaw + input.mouse_delta_x() * 0.008f, 6.28318530718f);
            camera_.pitch = std::clamp(camera_.pitch + input.mouse_delta_y() * 0.008f, -1.45f, 1.45f);
        }
        if (drag_anchor_ && input.mouse_down) {
            if (const auto hit = pick(input.mouse_x, input.mouse_y)) { anchor_ = hit->anchor; dirty_ = true; }
        }
        return true;
    }
    void render(const xui::style& style, const xui::input_state&) override {
        if (m_closing || m_closed) { force_close(); return; }
        if (first_) { first_ = false; if (autoload_) load(); }
        const auto r = bounds(), v = viewport();
        auto& dl = xdraw::get(xdraw::layer::top);
        const auto [sw, sh] = xdraw::viewport_size();
        dl.push_clip_absolute(0, 0, static_cast<float>(sw), static_cast<float>(sh));
        dl.rect_filled(0, 0, static_cast<float>(sw), static_cast<float>(sh), xdraw::color{0, 0, 0, 160});
        dl.rect_filled(r.x, r.y, r.w, r.h, xdraw::color{19, 24, 32, 255}, xdraw::corner_radius{10});
        dl.push_clip(r.x, r.y, r.w, r.h);
        const auto title = "3D / " + name_ + " / def " + std::to_string(def_) + " / paint " + std::to_string(paint_) + (dirty_ ? " *" : "");
        dl.text(r.x + 12, r.y + 10, xui::truncate(title, r.w - 24), style.text);

        constexpr int buttons = 9;
        const float bw = (r.w - 24 - 6 * (buttons - 1)) / buttons;
        auto control = [&](int i, std::string_view label, bool active = false) {
            return button(dl, {r.x + 12 + i * (bw + 6), r.y + 36, bw, 26}, label, style, active);
        };
        if (control(0, "Load (Game)")) load(false);
        if (control(1, "Capture live")) load(true);
        if (control(2, "Reset view")) camera_ = camera{};
        if (control(3, "Place anchor", place_)) place_ = !place_;
        if (control(4, "Save")) save();
        if (control(5, "Clear anchor")) { anchor_.reset(); dirty_ = true; }
        if (control(6, fullscreen_ ? "Window" : "Full screen")) fullscreen_ = !fullscreen_;
        const bool native = control(7, "CS2 inspect"), close = control(8, "Close");
        if (native || close) {
            force_close();
            xui::ctx().active_text_input = xui::null_id;
            xui::ctx().input.mouse_clicked = false;
            dl.pop_clip(); dl.pop_clip();
            if (native && native_inspect_) native_inspect_();
            return;
        }

        // Model source status and optional overrides
        auto& c = xui::ctx();
        const auto previous_overlay = c.inside_overlay;
        c.inside_overlay = m_id;
        xui::window_state ws{}; ws.title = "##nemesis_preview_paths"; ws.bounds = r; ws.is_child = true;
        c.windows.push_back(std::move(ws));
        xui::push_id(m_id); xui::draw::push_layer(xdraw::layer::top);

        dl.text(r.x + 12, r.y + 79, "Model source", style.text_dim);
        const std::string source_badge = loaded_from_game_ ? ("CS2 Game Model: " + name_) : "Custom local OBJ";
        dl.text(r.x + 115, r.y + 79, source_badge, loaded_from_game_ ? xdraw::color{70, 232, 175, 255} : style.text);

        dl.text(r.x + 12, r.y + 113, "Custom OBJ (opt)", style.text_dim);
        xui::layout::set_cursor(115, 106);
        xui::text_input("##preview_obj", obj_path_, 4096, "Optional custom OBJ file override (empty = automatic CS2 game model)");

        xui::draw::pop_layer(); xui::pop_id(); c.windows.pop_back(); c.inside_overlay = previous_overlay;

        dl.rect_filled(v.x, v.y, v.w, v.h, xdraw::color{9, 12, 16, 255});
        dl.push_clip(v.x, v.y, v.w, v.h);
        if (renderer_) {
            try {
                if (renderer_->device() != xdraw::device()) load();
                if (renderer_ && renderer_->device() == xdraw::device()) {
                    const float scale = std::min(1.0f, 4096.0f / std::max(v.w, v.h));
                    auto* image = renderer_->render(camera_, static_cast<UINT>(v.w * scale), static_cast<UINT>(v.h * scale));
                    if (image) dl.image(v.x, v.y, v.w, v.h, image);
                    if (const auto p = marker()) {
                        dl.circle_filled(p->x, p->y, 6, xdraw::color{70, 232, 175, 255});
                        dl.circle(p->x, p->y, 10, xdraw::color{225, 255, 245, 255}, 1.5f);
                    }
                }
            } catch (const std::exception& error) { status_ = std::string("Render failed: ") + error.what(); }
        } else {
            dl.text(v.x + 20, v.y + 20, "Loading 3D model from CS2 game database...", style.text_dim);
        }
        dl.pop_clip();
        dl.text(r.x + 12, r.bottom() - 64, xui::truncate(status_, r.w - 24), style.text);
        dl.text(r.x + 12, r.bottom() - 44, xui::truncate("RMB: orbit | Wheel: zoom | Place anchor + LMB: attach to surface | Drag marker: move | Esc: close", r.w - 24), style.text_dim);
        std::string footer = "CS2 Game Model. Charm anchor position calculated directly on weapon mesh surface.";
        if (anchor_) {
            const auto p = anchor_position(mesh_, *anchor_) * mesh_.radius + mesh_.center;
            footer = "Anchor: " + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) + " | Triangle " + std::to_string(anchor_->triangle) + " (u=" + std::to_string(anchor_->u) + ", v=" + std::to_string(anchor_->v) + ")";
        }
        dl.text(r.x + 12, r.bottom() - 24, xui::truncate(footer, r.w - 24), style.text_dim);
        dl.pop_clip(); dl.pop_clip();
    }
};

inline void open(int def, int paint, std::string name, std::function<void()> native_inspect) {
    if (xui::overlays::find(window_id)) return;
    xui::ctx().active_text_input = xui::null_id;
    xui::overlays::add(std::make_unique<preview_window>(def, paint, std::move(name), std::move(native_inspect)));
}
inline void on_menu_visibility(bool open) {
    if (!open) if (auto* window = xui::overlays::find(window_id)) window->force_close();
}
inline void shutdown() {
    if (auto* window = xui::overlays::find(window_id)) window->force_close();
    xui::overlays::sweep(); // Destroy the preview GPU resources before releasing the host device.
}
}
