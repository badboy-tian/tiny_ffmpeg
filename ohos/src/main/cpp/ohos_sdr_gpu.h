#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <native_buffer/native_buffer.h>
#include <native_image/native_image.h>
#include <native_window/external_window.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>
extern "C" {
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_oh.h"
}

// GPU只应用现有FFmpeg色调映射生成的三维查找表，避免另写一套HLG/PQ亮度曲线。
class OhosSdrGpu {
  static constexpr int edge = 65;
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  OHNativeWindow *outputWindow = nullptr;
  GLuint texture = 0, lut = 0, program = 0, framebuffer = 0, outputTexture = 0;
  OH_NativeImage *image = nullptr;
  std::mutex mutex;
  std::condition_variable ready;
  unsigned available = 0;

  PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bindImage = nullptr;
  static void onFrame(void *opaque) {
    auto self = static_cast<OhosSdrGpu *>(opaque);
    std::lock_guard<std::mutex> lock(self->mutex);
    ++self->available;
    self->ready.notify_one();
  }
  static GLuint shader(GLenum type, const char *source) {
    GLuint value = glCreateShader(type);
    glShaderSource(value, 1, &source, nullptr);
    glCompileShader(value);
    GLint ok = 0;
    glGetShaderiv(value, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char text[1024];
      glGetShaderInfoLog(value, sizeof(text), nullptr, text);
      av_log(nullptr, AV_LOG_ERROR, "SDR GPU shader: %s\n", text);
      glDeleteShader(value);
      return 0;
    }
    return value;
  }
  int buildLut(AVCodecParameters *par, double peak,
               std::vector<float> &pixels) {
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFrame *input = av_frame_alloc(), *output = av_frame_alloc();
    AVFilterInOut *inputs = nullptr, *outputs = nullptr;
    int rc = 0;
    auto run = [&]() -> int {
      if (!graph || !input || !output)
        return AVERROR(ENOMEM);
      graph->nb_threads = 2;
      AVFilterContext *source = nullptr, *sink = nullptr;
      std::string options = "video_size=" + std::to_string(edge * edge) + "x" +
                            std::to_string(edge) +
                            ":pix_fmt=" + std::to_string(AV_PIX_FMT_GBRPF32LE) +
                            ":time_base=1/1:pixel_aspect=1/1";
      int er =
          avfilter_graph_create_filter(&source, avfilter_get_by_name("buffer"),
                                       "in", options.c_str(), nullptr, graph);
      if (er < 0)
        return er;
      AVBufferSrcParameters *sourceParameters = av_buffersrc_parameters_alloc();
      if (!sourceParameters)
        return AVERROR(ENOMEM);
      sourceParameters->color_space = AVCOL_SPC_RGB;
      sourceParameters->color_range = AVCOL_RANGE_JPEG;
      er = av_buffersrc_parameters_set(source, sourceParameters);
      av_free(sourceParameters);
      if (er < 0)
        return er;
      if ((er = avfilter_graph_create_filter(
               &sink, avfilter_get_by_name("buffersink"), "out", nullptr,
               nullptr, graph)) < 0)
        return er;
      outputs = avfilter_inout_alloc();
      inputs = avfilter_inout_alloc();
      if (!outputs || !inputs)
        return AVERROR(ENOMEM);
      outputs->name = av_strdup("in");
      outputs->filter_ctx = source;
      outputs->pad_idx = 0;
      inputs->name = av_strdup("out");
      inputs->filter_ctx = sink;
      inputs->pad_idx = 0;
      // RGB纹理已完成YUV矩阵解码，因此输入矩阵显式设gbr；峰值与正式软件命令完全一致。
      std::string filters =
          "zscale=pin=bt2020:tin=" + std::to_string(par->color_trc) +
          ":min=gbr:rin=full:t=linear:npl=203,format=gbrpf32le,tonemap=tonemap="
          "mobius:peak=" +
          std::to_string(peak) +
          ",zscale=p=bt709:t=bt709:m=gbr:r=full:dither=none,format=gbrpf32le";
      if ((er = avfilter_graph_parse_ptr(graph, filters.c_str(), &inputs,
                                         &outputs, nullptr)) < 0)
        return er;
      if ((er = avfilter_graph_config(graph, nullptr)) < 0)
        return er;
      input->format = AV_PIX_FMT_GBRPF32LE;
      input->width = edge * edge;
      input->height = edge;
      input->color_primaries = AVCOL_PRI_BT2020;
      input->color_trc = par->color_trc;
      input->colorspace = AVCOL_SPC_RGB;
      input->color_range = AVCOL_RANGE_JPEG;
      input->pts = 0;
      if ((er = av_frame_get_buffer(input, 32)) < 0)
        return er;
      // 3D纹理X/Y/Z分别为R/G/B，FFmpeg平面顺序为G/B/R，不能交换造成偏色。
      for (int b = 0; b < edge; ++b)
        for (int g = 0; g < edge; ++g)
          for (int red = 0; red < edge; ++red) {
            int x = g * edge + red, y = b;
            reinterpret_cast<float *>(input->data[0] +
                                      y * input->linesize[0])[x] =
                float(g) / (edge - 1);
            reinterpret_cast<float *>(input->data[1] +
                                      y * input->linesize[1])[x] =
                float(b) / (edge - 1);
            reinterpret_cast<float *>(input->data[2] +
                                      y * input->linesize[2])[x] =
                float(red) / (edge - 1);
          }
      if ((er = av_buffersrc_add_frame_flags(source, input,
                                             AV_BUFFERSRC_FLAG_KEEP_REF)) < 0)
        return er;
      if ((er = av_buffersink_get_frame(sink, output)) < 0)
        return er;
      pixels.resize(edge * edge * edge * 3);
      for (int b = 0; b < edge; ++b)
        for (int g = 0; g < edge; ++g)
          for (int red = 0; red < edge; ++red) {
            int x = g * edge + red;
            size_t pos = ((b * edge + g) * edge + red) * 3;
            pixels[pos] = reinterpret_cast<float *>(output->data[2] +
                                                    b * output->linesize[2])[x];
            pixels[pos + 1] = reinterpret_cast<float *>(
                output->data[0] + b * output->linesize[0])[x];
            pixels[pos + 2] = reinterpret_cast<float *>(
                output->data[1] + b * output->linesize[1])[x];
          }
      return 0;
    };
    try {
      rc = run();
    } catch (...) {
      rc = AVERROR(ENOMEM);
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    av_frame_free(&input);
    av_frame_free(&output);
    avfilter_graph_free(&graph);
    return rc;
  }

public:
  ~OhosSdrGpu() {
    // 先关闭回调，再在拥有EGL上下文的工作线程释放纹理与窗口。
    if (image)
      OH_NativeImage_UnsetOnFrameAvailableListener(image);
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      eglMakeCurrent(display, surface, surface, context);
    if (image)
      OH_NativeImage_Destroy(&image);
    if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
    if (outputTexture)
      glDeleteTextures(1, &outputTexture);
    if (program)
      glDeleteProgram(program);
    if (lut)
      glDeleteTextures(1, &lut);
    if (texture)
      glDeleteTextures(1, &texture);
    if (display != EGL_NO_DISPLAY) {
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (surface != EGL_NO_SURFACE)
        eglDestroySurface(display, surface);
      if (context != EGL_NO_CONTEXT)
        eglDestroyContext(display, context);
      eglTerminate(display);
    }
  }
  OHNativeWindow *inputWindow() {
    return image ? OH_NativeImage_AcquireNativeWindow(image) : nullptr;
  }
  int init(OHNativeWindow *output, int width, int height,
           AVCodecParameters *par, double peak) {
    outputWindow = output;
    // 编码器默认窗口可能沿用YUV格式，EGL渲染必须申请RGBA且允许GPU写入。
    uint64_t usage = 0;
    if (OH_NativeWindow_NativeWindowHandleOpt(output, GET_USAGE, &usage) != 0 ||
        OH_NativeWindow_NativeWindowHandleOpt(output, SET_BUFFER_GEOMETRY,
                                              width, height) != 0 ||
        OH_NativeWindow_NativeWindowHandleOpt(
            output, SET_FORMAT, NATIVEBUFFER_PIXEL_FMT_RGBA_8888) != 0 ||
        OH_NativeWindow_NativeWindowHandleOpt(
            output, SET_USAGE,
            usage | NATIVEBUFFER_USAGE_HW_RENDER |
                NATIVEBUFFER_USAGE_HW_TEXTURE) != 0 ||
        OH_NativeWindow_NativeWindowHandleOpt(output, SET_TIMEOUT, 2000) != 0)
      return AVERROR(ENOSYS);
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
      return AVERROR(ENOSYS);
    EGLConfig config;
    EGLint count = 0;
    const EGLint attrs[] = {EGL_SURFACE_TYPE,
                            EGL_PBUFFER_BIT,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES3_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_ALPHA_SIZE,
                            8,
                            EGL_NONE};
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || count != 1)
      return AVERROR(ENOSYS);
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttrs);
    const EGLint pbufferAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, pbufferAttrs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context))
      return AVERROR(ENOSYS);
    // 直接请求编码缓冲区，确保时间戳在RequestBuffer前设置，避开EGL预取导致的重复首帧PTS。
    createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    bindImage = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!createImage || !destroyImage || !bindImage)
      return AVERROR(ENOSYS);
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &outputTexture);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    image = OH_NativeImage_Create(texture, GL_TEXTURE_EXTERNAL_OES);
    if (!image)
      return AVERROR(ENOSYS);
    OH_OnFrameAvailableListener listener{this, onFrame};
    if (OH_NativeImage_SetOnFrameAvailableListener(image, listener) != 0)
      return AVERROR_EXTERNAL;
    std::vector<float> values;
    int rc = buildLut(par, peak, values);
    if (rc < 0)
      return rc;
    glGenTextures(1, &lut);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, edge, edge, edge, 0, GL_RGB,
                 GL_FLOAT, values.data());
    const char *vs =
        "#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 "
        "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-"
        "1.0,0.,1.);}";
    const char *fs =
        "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : "
        "require\nprecision highp float;precision highp sampler3D;uniform "
        "samplerExternalOES source;uniform sampler3D mapping;uniform mat4 "
        "transform;uniform vec4 crop;in vec2 uv;out vec4 color;void "
        "main(){vec2 p=crop.xy+vec2(uv.x,1.-uv.y)*crop.zw;vec2 "
        "q=(transform*vec4(p,0.,1.)).xy;vec3 "
        "rgb=clamp(texture(source,q).rgb,0.,1.);vec3 "
        "value=texture(mapping,(rgb*64.+.5)/"
        "65.).rgb;color=vec4(clamp(value,0.,1.),1.);}";
    GLuint vertex = shader(GL_VERTEX_SHADER, vs),
           fragment = shader(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) {
      if (vertex)
        glDeleteShader(vertex);
      if (fragment)
        glDeleteShader(fragment);
      return AVERROR(ENOSYS);
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
      return AVERROR(ENOSYS);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "source"), 0);
    glUniform1i(glGetUniformLocation(program, "mapping"), 1);
    glViewport(0, 0, width, height);
    return glGetError() == GL_NO_ERROR ? 0 : AVERROR_EXTERNAL;
  }
  int render(int64_t timestamp, int x, int y, int width, int height,
             int sourceWidth, int sourceHeight,
             const std::function<bool()> &stopped) {
    std::unique_lock<std::mutex> lock(mutex);
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!available && !stopped()) {
      if (ready.wait_until(lock, until) == std::cv_status::timeout)
        return AVERROR(ETIMEDOUT);
    }
    if (stopped())
      return AVERROR_EXIT;
    --available;
    lock.unlock();
    // 不吞掉驱动的迟到错误，资源失效时立即让调用方走既有失败回退。
    if (glGetError() != GL_NO_ERROR)
      return AVERROR_EXTERNAL;
    int update = OH_NativeImage_UpdateSurfaceImage(image);
    if (update != 0) {
      av_log(nullptr, AV_LOG_ERROR, "SDR GPU update=%d EGL=%x\n", update,
             eglGetError());
      return AVERROR_EXTERNAL;
    }
    auto importError = glGetError();
    if (importError != GL_NO_ERROR) {
      av_log(nullptr, AV_LOG_ERROR, "SDR GPU import=%x\n", importError);
      return AVERROR_EXTERNAL;
    }
    float matrix[16];
    int transform = OH_NativeImage_GetTransformMatrixV2(image, matrix);
    if (transform != 0) {
      av_log(nullptr, AV_LOG_ERROR, "SDR GPU transform=%d\n", transform);
      return AVERROR_EXTERNAL;
    }
    // 原生矩阵处理输入方向；FBO行方向在着色器中反转，选区仍使用编码坐标。
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "transform"), 1, GL_FALSE,
                       matrix);
    glUniform4f(
        glGetUniformLocation(program, "crop"), float(x) / sourceWidth,
        // NativeImage纹理以左下为原点，编码选区以左上为原点，必须连同选区高度换算。
        float(sourceHeight - y - height) / sourceHeight,
        float(width) / sourceWidth, float(height) / sourceHeight);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut);
    // 时间戳属于申请到的缓冲区，因此先设置再申请，不能等到eglSwapBuffers才设置。
    if (OH_NativeWindow_NativeWindowHandleOpt(
            outputWindow, SET_UI_TIMESTAMP, static_cast<uint64_t>(timestamp)) !=
        0)
      return AVERROR_EXTERNAL;
    OHNativeWindowBuffer *buffer = nullptr;
    int fence = -1;
    if (OH_NativeWindow_NativeWindowRequestBuffer(outputWindow, &buffer,
                                                  &fence) != 0)
      return AVERROR_EXTERNAL;
    EGLImageKHR target = EGL_NO_IMAGE_KHR;
    auto draw = [&]() -> int {
      if (fence >= 0) {
        pollfd wait{fence, POLLIN, 0};
        if (poll(&wait, 1, 2000) <= 0)
          return AVERROR(ETIMEDOUT);
      }
      if (stopped())
        return AVERROR_EXIT;
      target = createImage(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_OHOS,
                           static_cast<EGLClientBuffer>(buffer), nullptr);
      if (target == EGL_NO_IMAGE_KHR)
        return AVERROR_EXTERNAL;
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, outputTexture);
      bindImage(GL_TEXTURE_2D, target);
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, outputTexture, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return AVERROR_EXTERNAL;
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      auto error = glGetError();
      if (error != GL_NO_ERROR) {
        av_log(nullptr, AV_LOG_ERROR, "SDR GPU FBO=%x\n", error);
        return AVERROR_EXTERNAL;
      }
      return 0;
    };
    int result = draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (target != EGL_NO_IMAGE_KHR)
      destroyImage(display, target);
    if (fence >= 0)
      close(fence);
    if (result < 0) {
      OH_NativeWindow_NativeWindowAbortBuffer(outputWindow, buffer);
      return result;
    }
    Region region{nullptr, 0};
    if (OH_NativeWindow_NativeWindowFlushBuffer(outputWindow, buffer, -1,
                                                region) != 0) {
      OH_NativeWindow_NativeWindowAbortBuffer(outputWindow, buffer);
      return AVERROR_EXTERNAL;
    }

    return 0;
  }
};
