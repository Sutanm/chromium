#pragma allow_unsafe_buffers
// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/342213636): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "content/renderer/pepper/ppapi_preferences_builder.h"

#include "gpu/config/gpu_feature_info.h"
#include "ppapi/shared_impl/ppapi_preferences.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"

namespace content {

ppapi::Preferences PpapiPreferencesBuilder::Build(
    const blink::web_pref::WebPreferences& prefs,
    const gpu::GpuFeatureInfo& gpu_feature_info) {
  ppapi::Preferences ppapi_prefs;
  ppapi_prefs.standard_font_family_map = prefs.standard_font_family_map;
  ppapi_prefs.fixed_font_family_map = prefs.fixed_font_family_map;
  ppapi_prefs.serif_font_family_map = prefs.serif_font_family_map;
  ppapi_prefs.sans_serif_font_family_map = prefs.sans_serif_font_family_map;
  ppapi_prefs.default_font_size = prefs.default_font_size;
  ppapi_prefs.default_fixed_font_size = prefs.default_fixed_font_size;
  ppapi_prefs.number_of_cpu_cores = prefs.number_of_cpu_cores;
  // PPAPI revival: when Chromium removed Flash it left these three hardcoded to
  // false. Flash reads them through PPB_Flash::GetSetting
  // (PP_FLASHSETTING_3DENABLED / PP_FLASHSETTING_STAGE3DENABLED) *before* it
  // creates a Context3D, so a false answer makes every Flash build fall back to
  // its software renderer ("driverInfo = Software Hw_disabled=unavailable")
  // without ever calling PPB_Graphics3D. Derive them from the real GPU status,
  // like the pre-removal implementation did via
  // WebPreferences::flash_3d_enabled / flash_stage3d_enabled.
  const bool gpu_accelerated =
      gpu_feature_info.status_values[gpu::GPU_FEATURE_TYPE_ACCELERATED_GL] ==
      gpu::kGpuFeatureStatusEnabled;
  // 与 Chromium 87 的取值一致（flash_3d_enabled / flash_stage3d_enabled /
  // flash_stage3d_baseline_enabled 三者同源）：Flash 的 2D 与 Stage3D 都走 GPU。
  // 早期版本这里只放开 Stage3D，是因为 2D-GPU 会白屏；白屏的真正原因是
  // 3D SwapBuffersACK 断流 + GPU 进程崩溃（见 ppb_graphics_3d_impl.cc 的
  // RequestSwapAckAfterPresentation 与 gles2_cmd_decoder_passthrough.cc 的
  // external framebuffer 销毁修复），两处修好后 2D-GPU 也正常。
  ppapi_prefs.is_3d_supported = gpu_accelerated;
  ppapi_prefs.is_stage3d_supported = gpu_accelerated;
  ppapi_prefs.is_stage3d_baseline_supported = gpu_accelerated;
  ppapi_prefs.is_accelerated_video_decode_enabled =
      (prefs.accelerated_video_decode_enabled &&
       (gpu_feature_info
            .status_values[gpu::GPU_FEATURE_TYPE_ACCELERATED_VIDEO_DECODE] ==
        gpu::kGpuFeatureStatusEnabled));
  return ppapi_prefs;
}

}  // namespace content
