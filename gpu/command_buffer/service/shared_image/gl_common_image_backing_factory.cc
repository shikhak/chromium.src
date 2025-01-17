// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shared_image/gl_common_image_backing_factory.h"

#include <algorithm>
#include <list>

#include "base/feature_list.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/service/service_utils.h"
#include "gpu/command_buffer/service/shared_image/shared_image_format_service_utils.h"
#include "gpu/config/gpu_preferences.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gl/gl_version_info.h"
#include "ui/gl/progress_reporter.h"

namespace gpu {
///////////////////////////////////////////////////////////////////////////////
// GLCommonImageBackingFactory

namespace {
// Kill switch for allowing using core ES3 format types for half float format.
BASE_FEATURE(kAllowEs3F16CoreTypeForGlSi,
             "AllowEs3F16CoreTypeForGlSi",
             base::FEATURE_ENABLED_BY_DEFAULT);
}  // namespace

GLCommonImageBackingFactory::GLCommonImageBackingFactory(
    uint32_t supported_usages,
    const GpuPreferences& gpu_preferences,
    const GpuDriverBugWorkarounds& workarounds,
    const gles2::FeatureInfo* feature_info,
    gl::ProgressReporter* progress_reporter)
    : SharedImageBackingFactory(supported_usages),
      use_passthrough_(gpu_preferences.use_passthrough_cmd_decoder &&
                       gles2::PassthroughCommandDecoderSupported()),
      workarounds_(workarounds),
      use_webgpu_adapter_(gpu_preferences.use_webgpu_adapter),
      progress_reporter_(progress_reporter) {
  gl::GLApi* api = gl::g_current_gl_context;
  api->glGetIntegervFn(GL_MAX_TEXTURE_SIZE, &max_texture_size_);
  // Ensure max_texture_size_ is less than INT_MAX so that gfx::Rect and friends
  // can be used to accurately represent all valid sub-rects, with overflow
  // cases, clamped to INT_MAX, always invalid.
  max_texture_size_ = std::min(max_texture_size_, INT_MAX - 1);

  texture_usage_angle_ = feature_info->feature_flags().angle_texture_usage;
  bool enable_texture_storage =
      feature_info->feature_flags().ext_texture_storage;
  const gles2::Validators* validators = feature_info->validators();
  for (auto format : viz::SinglePlaneFormat::kAll) {
    // BG5_565 is not supported for historical reasons.
    if (format == viz::SinglePlaneFormat::kBGR_565) {
      continue;
    }
    const GLFormatDesc format_desc = ToGLFormatDescOverrideHalfFloatType(
        format, /*plane_index=*/0,
        feature_info->feature_flags().angle_rgbx_internal_format,
        feature_info->oes_texture_float_available());
    const GLuint image_internal_format = format_desc.image_internal_format;
    const GLenum gl_format = format_desc.data_format;
    CHECK_NE(gl_format, static_cast<GLenum>(GL_ZERO));
    const GLenum gl_type = format_desc.data_type;

    // kRGBA_F16 is a core part of ES3.
    const bool at_least_es3 =
        gl::g_current_gl_version->IsAtLeastGLES(3, 0) &&
        base::FeatureList::IsEnabled(kAllowEs3F16CoreTypeForGlSi);
    const bool supports_data_type =
        (gl_type == GL_HALF_FLOAT && at_least_es3) ||
        validators->pixel_type.IsValid(gl_type);
    const bool supports_internal_format =
        (image_internal_format == GL_RGBA16F && at_least_es3) ||
        validators->texture_internal_format.IsValid(image_internal_format);

    const bool uncompressed_format_valid =
        supports_internal_format &&
        validators->texture_format.IsValid(gl_format);
    const bool compressed_format_valid =
        validators->compressed_texture_format.IsValid(image_internal_format);

    if (!(uncompressed_format_valid || compressed_format_valid) ||
        !supports_data_type) {
      continue;
    }

    FormatInfo& info = supported_formats_[format].emplace_back();
    info.is_compressed = compressed_format_valid;
    info.gl_format = gl_format;
    info.gl_type = gl_type;
    info.swizzle =
        gles2::TextureManager::GetCompatibilitySwizzle(feature_info, gl_format);
    info.image_internal_format = gles2::TextureManager::AdjustTexInternalFormat(
        feature_info, image_internal_format, gl_type);
    info.storage_internal_format = format_desc.storage_internal_format;
    info.adjusted_format =
        gles2::TextureManager::AdjustTexFormat(feature_info, gl_format);

    if (enable_texture_storage && !info.is_compressed &&
        validators->texture_internal_format_storage.IsValid(
            info.storage_internal_format)) {
      // GL_ALPHA8 requires EXT_texture_storage even with ES3. We should not
      // rely on validating command decoder logic that allows GL_ALPHA8, but
      // working around here for now until proper fix.
      if (info.storage_internal_format == GL_ALPHA8 && use_passthrough_) {
        continue;
      }

      info.supports_storage = true;
      info.adjusted_storage_internal_format =
          gles2::TextureManager::AdjustTexStorageFormat(
              feature_info, info.storage_internal_format);
    }
  }
}

GLCommonImageBackingFactory::~GLCommonImageBackingFactory() = default;

std::vector<GLCommonImageBackingFactory::FormatInfo>
GLCommonImageBackingFactory::GetFormatInfo(
    viz::SharedImageFormat format) const {
  auto iter = supported_formats_.find(format);
  if (iter == supported_formats_.end()) {
    return {};
  }
  return iter->second;
}

bool GLCommonImageBackingFactory::CanCreateTexture(
    viz::SharedImageFormat format,
    const gfx::Size& size,
    base::span<const uint8_t> pixel_data,
    GLenum target) {
  auto iter = supported_formats_.find(format);
  if (iter == supported_formats_.end()) {
    DVLOG(2) << "CreateSharedImage: unsupported format";
    return false;
  }

  if (size.width() < 1 || size.height() < 1 ||
      size.width() > max_texture_size_ || size.height() > max_texture_size_) {
    DVLOG(2) << "CreateSharedImage: invalid size: " << size.ToString()
             << ", max_texture_size_=" << max_texture_size_;
    return false;
  }

  // If we have initial data to upload, ensure it is sized appropriately.
  if (!pixel_data.empty()) {
    DCHECK_EQ(iter->second.size(), 1u);
    const FormatInfo& format_info = iter->second[0];

    if (format_info.is_compressed) {
      const char* error_message = "unspecified";
      if (!gles2::ValidateCompressedTexDimensions(
              target, /*level=*/0, size.width(), size.height(), /*depth=*/1,
              format_info.image_internal_format, &error_message)) {
        DVLOG(2) << "CreateSharedImage: "
                    "ValidateCompressedTexDimensionsFailed with error: "
                 << error_message;
        return false;
      }

      GLsizei bytes_required = 0;
      if (!gles2::GetCompressedTexSizeInBytes(
              /*function_name=*/nullptr, size.width(), size.height(),
              /*depth=*/1, format_info.image_internal_format, &bytes_required,
              /*error_state=*/nullptr)) {
        DVLOG(2) << "CreateSharedImage: Unable to compute required size for "
                    "initial texture upload.";
        return false;
      }

      if (bytes_required < 0 ||
          pixel_data.size() != static_cast<size_t>(bytes_required)) {
        DVLOG(2) << "CreateSharedImage: Initial data does not have expected "
                    "size.";
        return false;
      }
    } else {
      uint32_t bytes_required;
      uint32_t unpadded_row_size = 0u;
      uint32_t padded_row_size = 0u;
      if (!gles2::GLES2Util::ComputeImageDataSizes(
              size.width(), size.height(), /*depth=*/1, format_info.gl_format,
              format_info.gl_type, /*alignment=*/4, &bytes_required,
              &unpadded_row_size, &padded_row_size)) {
        LOG(ERROR) << "CreateSharedImage: Unable to compute required size for "
                      "initial texture upload.";
        return false;
      }

      // The GL spec, used in the computation for required bytes in the function
      // above, assumes no padding is required for the last row in the image.
      // But the client data does include this padding, so we add it for the
      // data validation check here.
      uint32_t padding = padded_row_size - unpadded_row_size;
      bytes_required += padding;
      if (pixel_data.size() != bytes_required) {
        LOG(ERROR) << "CreateSharedImage: Initial data does not have expected "
                      "size.";
        return false;
      }
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// GLCommonImageBackingFactory::FormatInfo

GLCommonImageBackingFactory::FormatInfo::FormatInfo() = default;
GLCommonImageBackingFactory::FormatInfo::FormatInfo(const FormatInfo& other) =
    default;
GLCommonImageBackingFactory::FormatInfo::~FormatInfo() = default;

}  // namespace gpu
