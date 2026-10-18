#include "gui/capture.h"

#include "core/paths.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>

namespace psyight {

namespace {

// OpenGL 3.0 framebuffer objects; the system header stops at 1.1. GLFW undefines APIENTRY at
// the end of its header, so the calling convention is spelled out.
#ifdef _WIN32
#define PSYIGHT_GL_CALL __stdcall
#else
#define PSYIGHT_GL_CALL
#endif
constexpr unsigned int kGlFramebuffer = 0x8D40;
constexpr unsigned int kGlColorAttachment0 = 0x8CE0;
constexpr unsigned int kGlFramebufferComplete = 0x8CD5;

using GenFramebuffers = void(PSYIGHT_GL_CALL *)(int, unsigned int *);
using DeleteFramebuffers = void(PSYIGHT_GL_CALL *)(int, const unsigned int *);
using BindFramebuffer = void(PSYIGHT_GL_CALL *)(unsigned int, unsigned int);
using FramebufferTexture2D = void(PSYIGHT_GL_CALL *)(unsigned int, unsigned int, unsigned int,
                                                     unsigned int, int);
using CheckFramebufferStatus = unsigned int(PSYIGHT_GL_CALL *)(unsigned int);

template <typename F> F gl(const char *name) {
    return reinterpret_cast<F>(glfwGetProcAddress(name));
}

} // namespace

std::optional<Result<CaptureArgs>>
parse_capture_args(const std::vector<std::filesystem::path> &args) {
    std::vector<std::string> a;
    for (const std::filesystem::path &p : args) {
        a.push_back(path_to_utf8(p));
    }
    bool wanted = false;
    for (const std::string &s : a) {
        wanted = wanted || s == "--capture";
    }
    if (!wanted) {
        return std::nullopt;
    }
    CaptureArgs c;
    for (size_t i = 0; i < a.size(); ++i) {
        const bool has_value = i + 1 < a.size();
        if (a[i] == "--capture" && has_value) {
            c.out = args[++i];
        } else if (a[i] == "--theme" && has_value) {
            c.theme = a[++i];
        } else if (a[i] == "--audio" && has_value) {
            c.audio = args[++i];
        } else if (a[i] == "--time" && has_value) {
            c.time_s = std::atof(a[++i].c_str());
        } else if (a[i] == "--frames" && has_value) {
            c.frames = std::atoi(a[++i].c_str());
        } else {
            return Result<CaptureArgs>(Error{"capture: unexpected argument " + a[i]});
        }
    }
    if (c.out.empty() || c.audio.empty()) {
        return Result<CaptureArgs>(Error{"capture: needs --capture <out.png> and --audio <file>"});
    }
    if (c.theme != "desk" && c.theme != "show") {
        return Result<CaptureArgs>(Error{"capture: --theme must be desk or show"});
    }
    if (c.frames < 1 || c.time_s < 0.0) {
        return Result<CaptureArgs>(Error{"capture: --frames must be 1 or more, --time 0 or more"});
    }
    return Result<CaptureArgs>(c);
}

CaptureTarget::~CaptureTarget() {
    if (framebuffer_ != 0) {
        gl<DeleteFramebuffers>("glDeleteFramebuffers")(1, &framebuffer_);
    }
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
    }
}

Result<void> CaptureTarget::create(int width, int height) {
    const auto gen = gl<GenFramebuffers>("glGenFramebuffers");
    const auto attach = gl<FramebufferTexture2D>("glFramebufferTexture2D");
    const auto status = gl<CheckFramebufferStatus>("glCheckFramebufferStatus");
    if (gen == nullptr || attach == nullptr || status == nullptr) {
        return Error{"capture: no OpenGL framebuffer objects"};
    }
    width_ = width;
    height_ = height;
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gen(1, &framebuffer_);
    bind();
    attach(kGlFramebuffer, kGlColorAttachment0, GL_TEXTURE_2D, texture_, 0);
    const bool complete = status(kGlFramebuffer) == kGlFramebufferComplete;
    unbind();
    if (!complete) {
        return Error{"capture: the framebuffer is incomplete"};
    }
    return {};
}

void CaptureTarget::bind() const {
    gl<BindFramebuffer>("glBindFramebuffer")(kGlFramebuffer, framebuffer_);
}

void CaptureTarget::unbind() const { gl<BindFramebuffer>("glBindFramebuffer")(kGlFramebuffer, 0); }

std::vector<std::uint8_t> CaptureTarget::read_rgba() const {
    std::vector<std::uint8_t> bottom_up(static_cast<size_t>(width_) * height_ * 4);
    bind();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, bottom_up.data());
    unbind();
    std::vector<std::uint8_t> out(bottom_up.size());
    const size_t row = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        std::copy_n(bottom_up.data() + static_cast<size_t>(height_ - 1 - y) * row, row,
                    out.data() + static_cast<size_t>(y) * row);
    }
    for (size_t i = 3; i < out.size(); i += 4) {
        out[i] = 255; // The UI is opaque; ImGui's blending leaves partial alpha behind.
    }
    return out;
}

} // namespace psyight
