#pragma once
#include "render.hpp"
namespace k22 {
class GLBackend {
public:
    GLBackend(SDL_Window*,const RenderOptions&);
    ~GLBackend();
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
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
