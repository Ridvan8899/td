//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundType.h"

#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"

namespace td {

static string get_color_hex_string(int32 color) {
  string result;
  for (int i = 20; i >= 0; i -= 4) {
    result += "0123456789abcdef"[(color >> i) & 0xF];
  }
  return result;
}

static bool is_valid_color(int32 color) {
  return 0 <= color && color <= 0xFFFFFF;
}

static bool is_valid_rotation_angle(int32 rotation_angle) {
  return 0 <= rotation_angle && rotation_angle < 360 && rotation_angle % 45 == 0;
}

BackgroundFill::BackgroundFill(const telegram_api::wallPaperSettings *settings) {
  if (settings == nullptr) {
    return;
  }

  auto flags = settings->flags_;
  if ((flags & telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK) != 0) {
    top_color_ = settings->background_color_;
    if (!is_valid_color(top_color_)) {
      LOG(ERROR) << "Receive " << to_string(*settings);
      top_color_ = 0;
    }
  }
  if ((flags & telegram_api::wallPaperSettings::FOURTH_BACKGROUND_COLOR_MASK) != 0 ||
      (flags & telegram_api::wallPaperSettings::THIRD_BACKGROUND_COLOR_MASK) != 0) {
    bottom_color_ = settings->second_background_color_;
    if (!is_valid_color(bottom_color_)) {
      LOG(ERROR) << "Receive " << to_string(*settings);
      bottom_color_ = 0;
    }
    third_color_ = settings->third_background_color_;
    if (!is_valid_color(third_color_)) {
      LOG(ERROR) << "Receive " << to_string(*settings);
      third_color_ = 0;
    }
    if ((flags & telegram_api::wallPaperSettings::FOURTH_BACKGROUND_COLOR_MASK) != 0) {
      fourth_color_ = settings->fourth_background_color_;
      if (!is_valid_color(fourth_color_)) {
        LOG(ERROR) << "Receive " << to_string(*settings);
        fourth_color_ = 0;
      }
    }
  } else if ((flags & telegram_api::wallPaperSettings::SECOND_BACKGROUND_COLOR_MASK) != 0) {
    bottom_color_ = settings->second_background_color_;
    if (!is_valid_color(bottom_color_)) {
      LOG(ERROR) << "Receive " << to_string(*settings);
      bottom_color_ = 0;
    }

    rotation_angle_ = settings->rotation_;
    if (!is_valid_rotation_angle(rotation_angle_)) {
      LOG(ERROR) << "Receive " << to_string(*settings);
      rotation_angle_ = 0;
    }
  }
}

Result<BackgroundFill> BackgroundFill::get_background_fill(const td_api::BackgroundFill *fill) {
  if (fill == nullptr) {
    return Status::Error(400, "Background fill info must be non-empty");
  }
  switch (fill->get_id()) {
    case td_api::backgroundFillSolid::ID: {
      auto solid = static_cast<const td_api::backgroundFillSolid *>(fill);
      if (!is_valid_color(solid->color_)) {
        return Status::Error(400, "Invalid solid fill color value");
      }
      return BackgroundFill(solid->color_);
    }
    case td_api::backgroundFillGradient::ID: {
      auto gradient = static_cast<const td_api::backgroundFillGradient *>(fill);
      if (!is_valid_color(gradient->top_color_)) {
        return Status::Error(400, "Invalid top gradient color value");
      }
      if (!is_valid_color(gradient->bottom_color_)) {
        return Status::Error(400, "Invalid bottom gradient color value");
      }
      if (!is_valid_rotation_angle(gradient->rotation_angle_)) {
        return Status::Error(400, "Invalid rotation angle value");
      }
      return BackgroundFill(gradient->top_color_, gradient->bottom_color_, gradient->rotation_angle_);
    }
    case td_api::backgroundFillFreeformGradient::ID: {
      auto freeform = static_cast<const td_api::backgroundFillFreeformGradient *>(fill);
      if (freeform->colors_.size() != 3 && freeform->colors_.size() != 4) {
        return Status::Error(400, "Wrong number of gradient colors");
      }
      for (auto &color : freeform->colors_) {
        if (!is_valid_color(color)) {
          return Status::Error(400, "Invalid freeform gradient color value");
        }
      }
      return BackgroundFill(freeform->colors_[0], freeform->colors_[1], freeform->colors_[2],
                            freeform->colors_.size() == 3 ? -1 : freeform->colors_[3]);
    }
    default:
      UNREACHABLE();
      return {};
  }
}

Result<BackgroundFill> BackgroundFill::get_background_fill(Slice name) {
  name = name.substr(0, name.find('#'));

  Slice parameters;
  auto parameters_pos = name.find('?');
  if (parameters_pos != Slice::npos) {
    parameters = name.substr(parameters_pos + 1);
    name = name.substr(0, parameters_pos);
  }

  auto get_color = [](Slice color_string) -> Result<int32> {
    auto r_color = hex_to_integer_safe<uint32>(color_string);
    if (r_color.is_error() || color_string.size() > 6) {
      return Status::Error(400, "WALLPAPER_INVALID");
    }
    return static_cast<int32>(r_color.ok());
  };

  size_t hyphen_pos = name.find('-');
  if (name.find('~') < name.size()) {
    vector<Slice> color_strings = full_split(name, '~');
    CHECK(color_strings.size() >= 2);
    if (color_strings.size() == 2) {
      hyphen_pos = color_strings[0].size();
    } else {
      if (color_strings.size() > 4) {
        return Status::Error(400, "WALLPAPER_INVALID");
      }

      TRY_RESULT(first_color, get_color(color_strings[0]));
      TRY_RESULT(second_color, get_color(color_strings[1]));
      TRY_RESULT(third_color, get_color(color_strings[2]));
      int32 fourth_color = -1;
      if (color_strings.size() == 4) {
        TRY_RESULT_ASSIGN(fourth_color, get_color(color_strings[3]));
      }
      return BackgroundFill(first_color, second_color, third_color, fourth_color);
    }
  }

  if (hyphen_pos < name.size()) {
    TRY_RESULT(top_color, get_color(name.substr(0, hyphen_pos)));
    TRY_RESULT(bottom_color, get_color(name.substr(hyphen_pos + 1)));
    int32 rotation_angle = 0;

    Slice prefix("rotation=");
    if (begins_with(parameters, prefix)) {
      rotation_angle = to_integer<int32>(parameters.substr(prefix.size()));
      if (!is_valid_rotation_angle(rotation_angle)) {
        rotation_angle = 0;
      }
    }

    return BackgroundFill(top_color, bottom_color, rotation_angle);
  }

  TRY_RESULT(color, get_color(name));
  return BackgroundFill(color);
}

string BackgroundFill::get_link(bool is_first) const {
  switch (get_type()) {
    case BackgroundFill::Type::Solid:
      return get_color_hex_string(top_color_);
    case BackgroundFill::Type::Gradient:
      return PSTRING() << get_color_hex_string(top_color_) << '-' << get_color_hex_string(bottom_color_)
                       << (is_first ? '?' : '&') << "rotation=" << rotation_angle_;
    case BackgroundFill::Type::FreeformGradient: {
      SliceBuilder sb;
      sb << get_color_hex_string(top_color_) << '~' << get_color_hex_string(bottom_color_) << '~'
         << get_color_hex_string(third_color_);
      if (fourth_color_ != -1) {
        sb << '~' << get_color_hex_string(fourth_color_);
      }
      return sb.as_cslice().str();
    }
    default:
      UNREACHABLE();
      return string();
  }
}

static bool is_valid_intensity(int32 intensity) {
  return -100 <= intensity && intensity <= 100;
}

int64 BackgroundFill::get_id() const {
  CHECK(is_valid_color(top_color_));
  CHECK(is_valid_color(bottom_color_));
  switch (get_type()) {
    case Type::Solid:
      return static_cast<int64>(top_color_) + 1;
    case Type::Gradient:
      CHECK(is_valid_rotation_angle(rotation_angle_));
      return (rotation_angle_ / 45) * 0x1000001000001 + (static_cast<int64>(top_color_) << 24) + bottom_color_ +
             (1 << 24) + 1;
    case Type::FreeformGradient: {
      CHECK(is_valid_color(third_color_));
      CHECK(fourth_color_ == -1 || is_valid_color(fourth_color_));
      const uint64 mul = 123456789;
      uint64 result = static_cast<uint64>(top_color_);
      result = result * mul + static_cast<uint64>(bottom_color_);
      result = result * mul + static_cast<uint64>(third_color_);
      result = result * mul + static_cast<uint64>(fourth_color_);
      return static_cast<int64>(result % 0x8000008000008);
    }
    default:
      UNREACHABLE();
      return 0;
  }
}

bool BackgroundFill::is_dark() const {
  switch (get_type()) {
    case Type::Solid:
      return (top_color_ & 0x808080) == 0;
    case Type::Gradient:
      return (top_color_ & 0x808080) == 0 && (bottom_color_ & 0x808080) == 0;
    case Type::FreeformGradient:
      return (top_color_ & 0x808080) == 0 && (bottom_color_ & 0x808080) == 0 && (third_color_ & 0x808080) == 0 &&
             (fourth_color_ == -1 || (fourth_color_ & 0x808080) == 0);
    default:
      UNREACHABLE();
      return 0;
  }
}

bool BackgroundFill::is_valid_id(int64 id) {
  return 0 < id && id < 0x8000008000008;
}

bool operator==(const BackgroundFill &lhs, const BackgroundFill &rhs) {
  return lhs.top_color_ == rhs.top_color_ && lhs.bottom_color_ == rhs.bottom_color_ &&
         lhs.rotation_angle_ == rhs.rotation_angle_ && lhs.third_color_ == rhs.third_color_ &&
         lhs.fourth_color_ == rhs.fourth_color_;
}

string BackgroundType::get_mime_type() const {
  CHECK(has_file());
  return type_ == Type::Pattern ? "image/png" : "image/jpeg";
}

void BackgroundType::apply_parameters_from_link(Slice name) {
  const auto query = parse_url_query(name);

  is_blurred_ = false;
  is_moving_ = false;
  auto modes = full_split(query.get_arg("mode"), ' ');
  for (auto &mode : modes) {
    if (type_ != Type::Pattern && to_lower(mode) == "blur") {
      is_blurred_ = true;
    }
    if (to_lower(mode) == "motion") {
      is_moving_ = true;
    }
  }

  if (type_ == Type::Pattern) {
    intensity_ = -101;
    auto intensity_arg = query.get_arg("intensity");
    if (!intensity_arg.empty()) {
      intensity_ = to_integer<int32>(intensity_arg);
    }
    if (!is_valid_intensity(intensity_)) {
      intensity_ = 50;
    }

    auto bg_color = query.get_arg("bg_color");
    if (!bg_color.empty()) {
      auto r_fill = BackgroundFill::get_background_fill(
          PSLICE() << url_encode(bg_color) << "?rotation=" << url_encode(query.get_arg("rotation")));
      if (r_fill.is_ok()) {
        fill_ = r_fill.move_as_ok();
      }
    }
  }
}

string BackgroundType::get_link() const {
  string mode;
  if (is_blurred_) {
    mode = "blur";
  }
  if (is_moving_) {
    if (!mode.empty()) {
      mode += '+';
    }
    mode += "motion";
  }

  switch (type_) {
    case Type::Wallpaper: {
      if (!mode.empty()) {
        return PSTRING() << "mode=" << mode;
      }
      return string();
    }
    case Type::Pattern: {
      string link = PSTRING() << "intensity=" << intensity_ << "&bg_color=" << fill_.get_link(false);
      if (!mode.empty()) {
        link += "&mode=";
        link += mode;
      }
      return link;
    }
    case Type::Fill:
      return fill_.get_link(true);
    default:
      UNREACHABLE();
      return string();
  }
}

BackgroundFill BackgroundType::get_background_fill() {
  CHECK(type_ == Type::Fill);
  return fill_;
}

bool operator==(const BackgroundType &lhs, const BackgroundType &rhs) {
  return lhs.type_ == rhs.type_ && lhs.is_blurred_ == rhs.is_blurred_ && lhs.is_moving_ == rhs.is_moving_ &&
         lhs.intensity_ == rhs.intensity_ && lhs.fill_ == rhs.fill_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType &type) {
  string_builder << "type ";
  switch (type.type_) {
    case BackgroundType::Type::Wallpaper:
      string_builder << "Wallpaper";
      break;
    case BackgroundType::Type::Pattern:
      string_builder << "Pattern";
      break;
    case BackgroundType::Type::Fill:
      string_builder << "Fill";
      break;
    default:
      UNREACHABLE();
      break;
  }
  return string_builder << '[' << type.get_link() << ']';
}

Result<BackgroundType> BackgroundType::get_background_type(const td_api::BackgroundType *background_type) {
  if (background_type == nullptr) {
    return Status::Error(400, "Type must be non-empty");
  }

  switch (background_type->get_id()) {
    case td_api::backgroundTypeWallpaper::ID: {
      auto wallpaper_type = static_cast<const td_api::backgroundTypeWallpaper *>(background_type);
      return BackgroundType(wallpaper_type->is_blurred_, wallpaper_type->is_moving_);
    }
    case td_api::backgroundTypePattern::ID: {
      auto pattern_type = static_cast<const td_api::backgroundTypePattern *>(background_type);
      TRY_RESULT(background_fill, BackgroundFill::get_background_fill(pattern_type->fill_.get()));
      if (!is_valid_intensity(pattern_type->intensity_)) {
        return Status::Error(400, "Wrong intensity value");
      }
      return BackgroundType(pattern_type->is_moving_, std::move(background_fill), pattern_type->intensity_);
    }
    case td_api::backgroundTypeFill::ID: {
      auto fill_type = static_cast<const td_api::backgroundTypeFill *>(background_type);
      TRY_RESULT(background_fill, BackgroundFill::get_background_fill(fill_type->fill_.get()));
      return BackgroundType(std::move(background_fill));
    }
    default:
      UNREACHABLE();
      return BackgroundType();
  }
}

BackgroundType::BackgroundType(bool is_fill, bool is_pattern,
                               telegram_api::object_ptr<telegram_api::wallPaperSettings> settings) {
  if (is_fill) {
    type_ = Type::Fill;
    CHECK(settings != nullptr);
    fill_ = BackgroundFill(settings.get());
  } else if (is_pattern) {
    type_ = Type::Pattern;
    if (settings) {
      fill_ = BackgroundFill(settings.get());
      is_moving_ = (settings->flags_ & telegram_api::wallPaperSettings::MOTION_MASK) != 0;
      if ((settings->flags_ & telegram_api::wallPaperSettings::INTENSITY_MASK) != 0) {
        intensity_ = settings->intensity_;
        if (!is_valid_intensity(intensity_)) {
          LOG(ERROR) << "Receive " << to_string(settings);
          intensity_ = 50;
        }
      }
    }
  } else {
    type_ = Type::Wallpaper;
    if (settings) {
      is_blurred_ = (settings->flags_ & telegram_api::wallPaperSettings::BLUR_MASK) != 0;
      is_moving_ = (settings->flags_ & telegram_api::wallPaperSettings::MOTION_MASK) != 0;
    }
  }
}

td_api::object_ptr<td_api::BackgroundFill> BackgroundFill::get_background_fill_object() const {
  switch (get_type()) {
    case BackgroundFill::Type::Solid:
      return td_api::make_object<td_api::backgroundFillSolid>(top_color_);
    case BackgroundFill::Type::Gradient:
      return td_api::make_object<td_api::backgroundFillGradient>(top_color_, bottom_color_, rotation_angle_);
    case BackgroundFill::Type::FreeformGradient: {
      vector<int32> colors{top_color_, bottom_color_, third_color_, fourth_color_};
      if (colors.back() == -1) {
        colors.pop_back();
      }
      return td_api::make_object<td_api::backgroundFillFreeformGradient>(std::move(colors));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::BackgroundType> BackgroundType::get_background_type_object() const {
  switch (type_) {
    case Type::Wallpaper:
      return td_api::make_object<td_api::backgroundTypeWallpaper>(is_blurred_, is_moving_);
    case Type::Pattern:
      return td_api::make_object<td_api::backgroundTypePattern>(fill_.get_background_fill_object(), intensity_,
                                                                is_moving_);
    case Type::Fill:
      return td_api::make_object<td_api::backgroundTypeFill>(fill_.get_background_fill_object());
    default:
      UNREACHABLE();
      return nullptr;
  }
}

telegram_api::object_ptr<telegram_api::wallPaperSettings> BackgroundType::get_input_wallpaper_settings() const {
  CHECK(has_file());

  int32 flags = 0;
  if (is_blurred_) {
    flags |= telegram_api::wallPaperSettings::BLUR_MASK;
  }
  if (is_moving_) {
    flags |= telegram_api::wallPaperSettings::MOTION_MASK;
  }
  switch (fill_.get_type()) {
    case BackgroundFill::Type::FreeformGradient:
      if (fill_.fourth_color_ != -1) {
        flags |= telegram_api::wallPaperSettings::FOURTH_BACKGROUND_COLOR_MASK;
      }
      flags |= telegram_api::wallPaperSettings::THIRD_BACKGROUND_COLOR_MASK;
      // fallthrough
    case BackgroundFill::Type::Gradient:
      flags |= telegram_api::wallPaperSettings::SECOND_BACKGROUND_COLOR_MASK;
      // fallthrough
    case BackgroundFill::Type::Solid:
      flags |= telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK;
      break;
    default:
      UNREACHABLE();
  }
  if (intensity_ != 0) {
    flags |= telegram_api::wallPaperSettings::INTENSITY_MASK;
  }
  return telegram_api::make_object<telegram_api::wallPaperSettings>(
      flags, false /*ignored*/, false /*ignored*/, fill_.top_color_, fill_.bottom_color_, fill_.third_color_,
      fill_.fourth_color_, intensity_, fill_.rotation_angle_);
}

}  // namespace td
