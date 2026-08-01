#include <NGIN/UI/Animation.hpp>

#include <algorithm>
#include <cmath>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto Cubic(const F32 first, const F32 second, const F32 value)
    noexcept -> F32 {
  const auto inverse = 1.0F - value;
  return 3.0F * inverse * inverse * value * first +
         3.0F * inverse * value * value * second + value * value * value;
}

[[nodiscard]] auto CubicDerivative(const F32 first, const F32 second,
                                   const F32 value) noexcept -> F32 {
  const auto inverse = 1.0F - value;
  return 3.0F * inverse * inverse * first +
         6.0F * inverse * value * (second - first) +
         3.0F * value * value * (1.0F - second);
}

[[nodiscard]] auto EvaluateBezier(const F32 progress, const F32 x1,
                                  const F32 y1, const F32 x2,
                                  const F32 y2) noexcept -> F32 {
  auto parameter = progress;
  for (UInt32 iteration = 0; iteration < 8; ++iteration) {
    const auto error = Cubic(x1, x2, parameter) - progress;
    const auto derivative = CubicDerivative(x1, x2, parameter);
    if (std::abs(error) <= 0.00001F || std::abs(derivative) <= 0.00001F) {
      break;
    }
    parameter = std::clamp(parameter - error / derivative, 0.0F, 1.0F);
  }

  auto lower = 0.0F;
  auto upper = 1.0F;
  for (UInt32 iteration = 0; iteration < 12; ++iteration) {
    const auto x = Cubic(x1, x2, parameter);
    if (std::abs(x - progress) <= 0.00001F) {
      break;
    }
    if (x < progress) {
      lower = parameter;
    } else {
      upper = parameter;
    }
    parameter = (lower + upper) * 0.5F;
  }
  return Cubic(y1, y2, parameter);
}
} // namespace

namespace Detail {
struct EasingCurveAccess final {
  [[nodiscard]] static auto Evaluate(const EasingCurve &curve,
                                     const F32 progress,
                                     bool &valid) noexcept -> F32 {
    valid = true;
    const auto value = std::clamp(progress, 0.0F, 1.0F);
    F32 result = value;
    switch (curve.m_kind) {
    case BuiltInEasingCurve::Linear:
      result = value;
      break;
    case BuiltInEasingCurve::Standard:
      result = value * value * (3.0F - 2.0F * value);
      break;
    case BuiltInEasingCurve::EaseIn:
      result = value * value * value;
      break;
    case BuiltInEasingCurve::EaseOut: {
      const auto inverse = 1.0F - value;
      result = 1.0F - inverse * inverse * inverse;
      break;
    }
    case BuiltInEasingCurve::EaseInOut:
      result = value < 0.5F
                   ? 4.0F * value * value * value
                   : 1.0F -
                         std::pow(-2.0F * value + 2.0F, 3.0F) * 0.5F;
      break;
    case BuiltInEasingCurve::CubicBezier:
      result = EvaluateBezier(value, curve.m_x1, curve.m_y1, curve.m_x2,
                              curve.m_y2);
      break;
    case BuiltInEasingCurve::Steps: {
      const auto steps = static_cast<F32>(std::max(UInt32{1}, curve.m_stepCount));
      result = curve.m_stepPosition == StepPosition::Start
                   ? std::ceil(value * steps) / steps
                   : std::floor(value * steps) / steps;
      if (value >= 1.0F) {
        result = 1.0F;
      }
      break;
    }
    case BuiltInEasingCurve::Custom:
      if (!curve.m_custom) {
        valid = false;
        return value;
      }
      try {
        result = curve.m_custom->Evaluate(value);
      } catch (...) {
        valid = false;
        return value;
      }
      break;
    }
    if (!std::isfinite(result)) {
      valid = false;
      return value;
    }
    return result;
  }
};

auto EvaluateEasingCurve(const EasingCurve &curve, const F32 progress,
                         bool &valid) noexcept -> F32 {
  return EasingCurveAccess::Evaluate(curve, progress, valid);
}
} // namespace Detail

auto EasingCurve::Linear() noexcept -> EasingCurve {
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::Linear;
  return result;
}

auto EasingCurve::Standard() noexcept -> EasingCurve { return {}; }

auto EasingCurve::EaseIn() noexcept -> EasingCurve {
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::EaseIn;
  return result;
}

auto EasingCurve::EaseOut() noexcept -> EasingCurve {
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::EaseOut;
  return result;
}

auto EasingCurve::EaseInOut() noexcept -> EasingCurve {
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::EaseInOut;
  return result;
}

auto EasingCurve::CubicBezier(const F32 x1, const F32 y1, const F32 x2,
                              const F32 y2) noexcept -> EasingCurve {
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::CubicBezier;
  result.m_x1 = std::isfinite(x1) ? std::clamp(x1, 0.0F, 1.0F) : 0.0F;
  result.m_y1 = std::isfinite(y1) ? y1 : 0.0F;
  result.m_x2 = std::isfinite(x2) ? std::clamp(x2, 0.0F, 1.0F) : 1.0F;
  result.m_y2 = std::isfinite(y2) ? y2 : 1.0F;
  return result;
}

auto EasingCurve::Steps(const UInt32 count,
                        const StepPosition position) noexcept -> EasingCurve {
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::Steps;
  result.m_stepCount = std::clamp(count, UInt32{1}, UInt32{10'000});
  result.m_stepPosition = position;
  return result;
}

auto EasingCurve::Custom(std::shared_ptr<const IEasingCurve> curve) noexcept
    -> EasingCurve {
  if (!curve) {
    return Standard();
  }
  EasingCurve result;
  result.m_kind = BuiltInEasingCurve::Custom;
  result.m_custom = std::move(curve);
  return result;
}

auto EasingCurve::Evaluate(const F32 progress) const noexcept -> F32 {
  bool valid = true;
  return Detail::EasingCurveAccess::Evaluate(*this, progress, valid);
}

auto EasingCurve::Kind() const noexcept -> BuiltInEasingCurve { return m_kind; }

auto EasingCurve::Name() const noexcept -> std::string_view {
  switch (m_kind) {
  case BuiltInEasingCurve::Linear:
    return "Linear";
  case BuiltInEasingCurve::Standard:
    return "Standard";
  case BuiltInEasingCurve::EaseIn:
    return "EaseIn";
  case BuiltInEasingCurve::EaseOut:
    return "EaseOut";
  case BuiltInEasingCurve::EaseInOut:
    return "EaseInOut";
  case BuiltInEasingCurve::CubicBezier:
    return "CubicBezier";
  case BuiltInEasingCurve::Steps:
    return "Steps";
  case BuiltInEasingCurve::Custom:
    return m_custom ? m_custom->Name() : std::string_view{"Custom"};
  }
  return "Unknown";
}

auto EasingCurve::IsCustom() const noexcept -> bool {
  return m_kind == BuiltInEasingCurve::Custom;
}

auto operator==(const EasingCurve &left, const EasingCurve &right) noexcept
    -> bool {
  return left.m_kind == right.m_kind && left.m_x1 == right.m_x1 &&
         left.m_y1 == right.m_y1 && left.m_x2 == right.m_x2 &&
         left.m_y2 == right.m_y2 &&
         left.m_stepCount == right.m_stepCount &&
         left.m_stepPosition == right.m_stepPosition &&
         left.m_custom == right.m_custom;
}

auto ApplyEasing(const EasingCurve &curve, const F32 progress) noexcept -> F32 {
  return curve.Evaluate(progress);
}
} // namespace NGIN::UI
