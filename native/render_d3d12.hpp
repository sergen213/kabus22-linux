#pragma once
#include "render.hpp"
namespace k22 {
// Native Direct3D 12 backend (Windows desktop and Xbox UWP Dev Mode): GLBackend's interface and semantics, rendering the
// same image (docs/XBOX.md, native/shaders/d3d12).
class D3D12Backend {
public:
    D3D12Backend(SDL_Window*,const RenderOptions&);
    ~D3D12Backend();
    void draw(const RenderFrame&,WindowMetrics);
    void capture(std::string_view,WindowMetrics) const;
    void drain();
    void invalidate(WindowMode);
    void release_scene(std::span<const Model* const> keep = {});
    void purge_model(const Model*);
    void purge_image(const Image*);
    int shader_version() const;
    std::string description() const;
    PresentationState presentation_state() const;
    const RenderOptions& options() const;
    bool reconfigure(const RenderOptions&); // Renderer::reconfigure contract, `next` already admitted for d3d12.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
