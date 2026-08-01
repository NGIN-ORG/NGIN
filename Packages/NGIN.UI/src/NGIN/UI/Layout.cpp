#include <NGIN/UI/Layout.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto CustomContextFor(RuntimeNode &node, const F32 scaleFactor)
    -> CustomElementContext {
  return CustomElementContext{
      *node.custom.state,
      node.id,
      node.arrangedBounds,
      CustomInteractionState{
          .hovered = node.interaction.hovered,
          .pressed =
              node.interaction.pressed || node.interaction.keyboardPressed,
          .focused = node.interaction.focused,
          .enabled = node.properties.interaction.enabled,
      },
      scaleFactor,
  };
}

void ReportCustomError(const RuntimeNode &node, const UIError &error) noexcept {
  if (!node.properties.custom.onError) {
    return;
  }
  try {
    node.properties.custom.onError(error);
  } catch (...) {
  }
}

[[nodiscard]] auto ClampDimension(const F32 value, const F32 minimum,
                                  const F32 maximum) noexcept -> F32 {
  return std::clamp(std::max(0.0F, value), std::max(0.0F, minimum),
                    std::max(minimum, maximum));
}

[[nodiscard]] auto InnerWidth(const F32 width,
                              const Thickness &padding) noexcept -> F32 {
  return std::max(0.0F, width - padding.left - padding.right);
}

[[nodiscard]] auto InnerHeight(const F32 height,
                               const Thickness &padding) noexcept -> F32 {
  return std::max(0.0F, height - padding.top - padding.bottom);
}

[[nodiscard]] auto AlignmentOffset(const F32 available, const F32 extent,
                                   const HorizontalAlignment alignment) noexcept
    -> F32 {
  switch (alignment) {
  case HorizontalAlignment::Center:
    return (available - extent) * 0.5F;
  case HorizontalAlignment::End:
    return available - extent;
  default:
    return 0.0F;
  }
}

[[nodiscard]] auto TrackMinimum(const GridTrack &track) noexcept -> F32 {
  return std::max(0.0F, track.minimum);
}

[[nodiscard]] auto TrackMaximum(const GridTrack &track) noexcept -> F32 {
  return std::max(TrackMinimum(track), track.maximum);
}

[[nodiscard]] auto ClampTrack(const GridTrack &track, const F32 value) noexcept
    -> F32 {
  return std::clamp(std::max(0.0F, value), TrackMinimum(track),
                    TrackMaximum(track));
}

[[nodiscard]] auto TrackGapExtent(const UIntSize count, const F32 gap) noexcept
    -> F32 {
  return count > 1 ? std::max(0.0F, gap) * static_cast<F32>(count - 1) : 0.0F;
}

[[nodiscard]] auto TrackSum(const std::vector<F32> &tracks) noexcept -> F32 {
  F32 result = 0.0F;
  for (const auto track : tracks) {
    result += track;
  }
  return result;
}

[[nodiscard]] auto ResolveTracks(const std::vector<GridTrack> &definitions,
                                 const std::vector<F32> &intrinsic,
                                 const F32 available, const F32 gap)
    -> std::vector<F32> {
  std::vector<F32> result(definitions.size(), 0.0F);
  for (UIntSize index = 0; index < definitions.size(); ++index) {
    const auto &track = definitions[index];
    const auto desired = index < intrinsic.size() ? intrinsic[index] : 0.0F;
    result[index] = track.sizing == GridTrackSizing::Fixed
                        ? ClampTrack(track, track.value)
                        : ClampTrack(track, desired);
  }
  if (!std::isfinite(available)) {
    return result;
  }

  auto remaining = std::max(0.0F, available) -
                   TrackGapExtent(result.size(), gap) - TrackSum(result);
  while (remaining > 0.0001F) {
    F32 totalWeight = 0.0F;
    for (UIntSize index = 0; index < definitions.size(); ++index) {
      if (definitions[index].sizing == GridTrackSizing::Weighted &&
          result[index] < TrackMaximum(definitions[index])) {
        totalWeight += std::max(0.0F, definitions[index].value);
      }
    }
    if (totalWeight <= 0.0F) {
      break;
    }
    auto distributed = 0.0F;
    for (UIntSize index = 0; index < definitions.size(); ++index) {
      const auto &track = definitions[index];
      if (track.sizing != GridTrackSizing::Weighted ||
          result[index] >= TrackMaximum(track)) {
        continue;
      }
      const auto weight = std::max(0.0F, track.value);
      const auto share = remaining * weight / totalWeight;
      const auto grown = ClampTrack(track, result[index] + share);
      distributed += grown - result[index];
      result[index] = grown;
    }
    if (distributed <= 0.0001F) {
      break;
    }
    remaining -= distributed;
  }
  return result;
}

[[nodiscard]] auto NormalizedStart(const UIntSize requested,
                                   const UIntSize count) noexcept -> UIntSize {
  return count == 0 ? 0 : std::min(requested, count - 1);
}

[[nodiscard]] auto NormalizedSpan(const UIntSize requested,
                                  const UIntSize start,
                                  const UIntSize count) noexcept -> UIntSize {
  return count == 0 ? 0
                    : std::min(std::max<UIntSize>(1, requested), count - start);
}

[[nodiscard]] auto SpanExtent(const std::vector<F32> &tracks,
                              const UIntSize requestedStart,
                              const UIntSize requestedSpan,
                              const F32 gap) noexcept -> F32 {
  if (tracks.empty()) {
    return 0.0F;
  }
  const auto start = NormalizedStart(requestedStart, tracks.size());
  const auto span = NormalizedSpan(requestedSpan, start, tracks.size());
  F32 result = TrackGapExtent(span, gap);
  for (UIntSize index = start; index < start + span; ++index) {
    result += tracks[index];
  }
  return result;
}

void GrowIntrinsic(const std::vector<GridTrack> &definitions,
                   std::vector<F32> &intrinsic, const UIntSize requestedStart,
                   const UIntSize requestedSpan, const F32 desired,
                   const F32 gap) {
  if (definitions.empty()) {
    return;
  }
  const auto start = NormalizedStart(requestedStart, definitions.size());
  const auto span = NormalizedSpan(requestedSpan, start, definitions.size());
  auto deficit = desired - SpanExtent(intrinsic, start, span, gap);
  if (deficit <= 0.0F) {
    return;
  }

  for (const auto sizing :
       {GridTrackSizing::Automatic, GridTrackSizing::Weighted}) {
    while (deficit > 0.0001F) {
      UIntSize growable = 0;
      for (UIntSize index = start; index < start + span; ++index) {
        if (definitions[index].sizing == sizing &&
            intrinsic[index] < TrackMaximum(definitions[index])) {
          ++growable;
        }
      }
      if (growable == 0) {
        break;
      }
      const auto share = deficit / static_cast<F32>(growable);
      auto grownTotal = 0.0F;
      for (UIntSize index = start; index < start + span; ++index) {
        if (definitions[index].sizing != sizing) {
          continue;
        }
        const auto grown =
            ClampTrack(definitions[index], intrinsic[index] + share);
        grownTotal += grown - intrinsic[index];
        intrinsic[index] = grown;
      }
      if (grownTotal <= 0.0001F) {
        break;
      }
      deficit -= grownTotal;
    }
    if (deficit <= 0.0001F) {
      break;
    }
  }
}

[[nodiscard]] auto InitialIntrinsic(const std::vector<GridTrack> &definitions)
    -> std::vector<F32> {
  std::vector<F32> result;
  result.reserve(definitions.size());
  for (const auto &track : definitions) {
    result.push_back(track.sizing == GridTrackSizing::Fixed
                         ? ClampTrack(track, track.value)
                         : TrackMinimum(track));
  }
  return result;
}

[[nodiscard]] auto GridDefinitions(const std::vector<GridTrack> &authored)
    -> std::vector<GridTrack> {
  return authored.empty() ? std::vector<GridTrack>{GridTrack::Weighted()}
                          : authored;
}

[[nodiscard]] auto AlignmentOffset(const F32 available, const F32 extent,
                                   const VerticalAlignment alignment) noexcept
    -> F32 {
  switch (alignment) {
  case VerticalAlignment::Center:
    return (available - extent) * 0.5F;
  case VerticalAlignment::End:
    return available - extent;
  default:
    return 0.0F;
  }
}

[[nodiscard]] auto TextFieldDisplayValue(const RuntimeNode &node)
    -> NGIN::Text::String {
  if (!node.textField.editing) {
    return {};
  }
  const auto &editing = *node.textField.editing;
  if (!node.properties.textField.password || editing.State().revealPassword) {
    return editing.Value();
  }

  NGIN::Text::String masked;
  constexpr auto bullet = "\xE2\x80\xA2";
  for (UIntSize index = 0; index < editing.Clusters().size(); ++index) {
    masked.Append(bullet);
  }
  return masked;
}

[[nodiscard]] auto
ResolveImageDestination(const Rect available, const PixelSize source,
                        const ImageFit fit,
                        const ImageAlignment alignment) noexcept -> Rect {
  if (available.width <= 0.0F || available.height <= 0.0F || source.IsEmpty()) {
    return {};
  }
  const auto sourceWidth = static_cast<F32>(source.width);
  const auto sourceHeight = static_cast<F32>(source.height);
  F32 width = sourceWidth;
  F32 height = sourceHeight;
  if (fit == ImageFit::Fill) {
    width = available.width;
    height = available.height;
  } else if (fit == ImageFit::Contain || fit == ImageFit::Cover ||
             fit == ImageFit::ScaleDown) {
    auto scale = fit == ImageFit::Cover
                     ? std::max(available.width / sourceWidth,
                                available.height / sourceHeight)
                     : std::min(available.width / sourceWidth,
                                available.height / sourceHeight);
    if (fit == ImageFit::ScaleDown) {
      scale = std::min(1.0F, scale);
    }
    width = sourceWidth * scale;
    height = sourceHeight * scale;
  }
  const auto horizontal = std::clamp(alignment.horizontal, 0.0F, 1.0F);
  const auto vertical = std::clamp(alignment.vertical, 0.0F, 1.0F);
  return Rect{
      available.x + (available.width - width) * horizontal,
      available.y + (available.height - height) * vertical,
      width,
      height,
  };
}

[[nodiscard]] auto PresentedByteOffset(const RuntimeNode &node,
                                       const UIntSize cluster) noexcept
    -> UIntSize {
  if (!node.textField.editing) {
    return 0;
  }
  if (node.properties.textField.password &&
      !node.textField.editing->State().revealPassword) {
    return cluster * 3;
  }
  return node.textField.editing->ByteOffsetForCluster(cluster);
}
} // namespace

LayoutEngine::LayoutEngine(RuntimeTree &tree) noexcept : m_tree(tree) {}

auto LayoutEngine::Perform(const SizeConstraints constraints,
                           const Rect finalBounds, const F32 scaleFactor)
    -> LayoutPassStats {
  m_stats = {};
  m_scaleFactor = scaleFactor > 0.0F ? scaleFactor : 1.0F;
  ++m_revision;
  for (auto &slot : m_tree.m_slots) {
    if (slot.occupied) {
      slot.node.text.glyphRuns.clear();
    }
  }
  static_cast<void>(Measure(m_tree.Root(), constraints));
  Arrange(m_tree.Root(), finalBounds);
  return m_stats;
}

auto LayoutEngine::Measure(const ElementHandle handle,
                           SizeConstraints constraints) -> Size {
  auto *node = m_tree.Get(handle);
  if (node == nullptr) {
    return {};
  }
  if (node->properties.visibility == ElementVisibility::Collapsed) {
    node->measuredSize = {};
    node->layoutRevision = m_revision;
    ++m_stats.measured;
    return {};
  }

  constraints.minimum.width = std::max(
      constraints.minimum.width, node->properties.layout.minimumSize.width);
  constraints.minimum.height = std::max(
      constraints.minimum.height, node->properties.layout.minimumSize.height);
  constraints.maximum.width = std::min(
      constraints.maximum.width, node->properties.layout.maximumSize.width);
  constraints.maximum.height = std::min(
      constraints.maximum.height, node->properties.layout.maximumSize.height);
  constraints.maximum.width =
      std::max(constraints.maximum.width, constraints.minimum.width);
  constraints.maximum.height =
      std::max(constraints.maximum.height, constraints.minimum.height);

  const auto desktopContainer = node->type == ElementType::Grid ||
                                node->type == ElementType::WrapPanel ||
                                node->type == ElementType::Canvas;
  auto measured = node->type == ElementType::CustomElement
                      ? MeasureCustom(*node, constraints)
                      : ((node->children.empty() && !desktopContainer) ||
                                 node->type == ElementType::Spacer
                             ? MeasureLeaf(*node, constraints)
                             : MeasureContainer(*node, constraints));
  measured = constraints.Constrain(measured);
  if (node->type == ElementType::Popup) {
    measured = {};
  }
  node = m_tree.Get(handle);
  node->measuredSize = measured;
  node->layoutRevision = m_revision;
  ++m_stats.measured;
  return measured;
}

auto LayoutEngine::MeasureCustom(RuntimeNode &node,
                                 const SizeConstraints constraints) -> Size {
  m_tree.SynchronizeCustom(node, m_scaleFactor);
  if (!node.properties.custom.element || !node.custom.state) {
    return constraints.Constrain(node.properties.layout.preferredSize);
  }

  try {
    auto context = CustomContextFor(node, m_scaleFactor);
    auto measured =
        node.properties.custom.element->Measure(context, constraints);
    if (!measured) {
      ReportCustomError(node, measured.Error());
      return constraints.Constrain(node.properties.layout.preferredSize);
    }
    return constraints.Constrain(measured.Value());
  } catch (const std::bad_alloc &) {
    ReportCustomError(node, MakeUIError(UIErrorCode::OutOfMemory,
                                        "Custom measurement allocation failed",
                                        "NGIN.UI", "ICustomElement::Measure"));
  } catch (...) {
    ReportCustomError(
        node, MakeUIError(UIErrorCode::InvalidState,
                          "Custom measurement callback threw an exception",
                          "NGIN.UI", "ICustomElement::Measure"));
  }
  return constraints.Constrain(node.properties.layout.preferredSize);
}

auto LayoutEngine::MeasureLeaf(RuntimeNode &node,
                               const SizeConstraints constraints) -> Size {
  if (node.type == ElementType::Text) {
    return MeasureText(node, constraints, node.properties.text.value);
  }
  if (node.type == ElementType::TextField ||
      node.type == ElementType::TextArea) {
    if (node.textField.editing &&
        (node.properties.text.layout != nullptr ||
         node.properties.text.glyphAtlas != nullptr)) {
      const auto measured =
          MeasureText(node, constraints, TextFieldDisplayValue(node));
      if (node.type == ElementType::TextArea) {
        const auto padding = node.properties.layout.padding;
        node.scroll.contentSize = Size{
            node.text.paragraph.size.width + padding.left + padding.right,
            node.text.paragraph.size.height + padding.top + padding.bottom};
        const auto preferred = node.properties.layout.preferredSize;
        return constraints.Constrain(Size{
            preferred.width > 0.0F ? preferred.width : measured.width,
            preferred.height > 0.0F ? preferred.height : measured.height,
        });
      }
      return measured;
    }
    node.text = {};
  }
  if (node.type == ElementType::Image) {
    return MeasureImage(node, constraints);
  }
  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               node.properties.layout.minimumSize.width),
      std::max(node.properties.layout.preferredSize.height,
               node.properties.layout.minimumSize.height),
  });
}

auto LayoutEngine::MeasureText(RuntimeNode &node,
                               const SizeConstraints constraints,
                               const NGIN::Text::String &value) -> Size {
  const auto &properties = node.properties.text;
  const auto report = [&node, &properties](const UIError &error) {
    if (properties.onError) {
      properties.onError(error);
    }
    if ((node.type == ElementType::TextField ||
         node.type == ElementType::TextArea) &&
        node.properties.textField.onError) {
      node.properties.textField.onError(error);
    }
  };
  node.text = {};
  if (properties.layout == nullptr || properties.glyphAtlas == nullptr) {
    report(
        MakeUIError(UIErrorCode::InvalidArgument,
                    "Text requires paragraph layout and glyph atlas services",
                    "NGIN.UI", "LayoutEngine::MeasureText"));
    return constraints.Constrain(node.properties.layout.preferredSize);
  }

  const auto padding = node.properties.layout.padding;
  const auto horizontalPadding = padding.left + padding.right;
  const auto verticalPadding = padding.top + padding.bottom;
  const auto maximumWidth =
      node.type == ElementType::TextArea &&
              node.properties.text.wrapping == TextWrapping::NoWrap
          ? std::numeric_limits<F32>::infinity()
          : (std::isfinite(constraints.maximum.width)
                 ? std::max(0.0F, constraints.maximum.width - horizontalPadding)
                 : constraints.maximum.width);
  ParagraphRequest request{
      .runs =
          {
              TextRun{
                  .text = value,
                  .font = properties.font,
                  .fontSize = properties.fontSize,
                  .direction = properties.direction,
                  .language = properties.language,
                  .script = properties.script,
              },
          },
      .maximumWidth = maximumWidth,
      .lineHeight = properties.lineHeight,
      .alignment = properties.alignment,
      .wrapping = properties.wrapping,
  };
  auto paragraph = properties.layout->LayoutParagraph(request);
  if (!paragraph) {
    report(paragraph.Error());
    return constraints.Constrain(node.properties.layout.preferredSize);
  }

  node.text.paragraph = std::move(paragraph).Value();
  bool glyphsValid = true;
  for (const auto &positioned : node.text.paragraph.runs) {
    Point pen{};
    for (const auto &glyph : positioned.run.glyphs) {
      auto atlasEntry = properties.glyphAtlas->ResolveGlyph(GlyphAtlasRequest{
          .fontFace = positioned.run.fontFace,
          .glyphIndex = glyph.glyphIndex,
          .fontSize = positioned.fontSize,
          .scaleFactor = m_scaleFactor,
      });
      if (!atlasEntry) {
        report(atlasEntry.Error());
        glyphsValid = false;
        break;
      }
      const auto &entry = atlasEntry.Value();
      if (entry.size.width <= 0.0F || entry.size.height <= 0.0F) {
        pen.x += glyph.advance.x;
        pen.y += glyph.advance.y;
        continue;
      }
      if (!entry.texture) {
        report(MakeUIError(UIErrorCode::InvalidState,
                           "Glyph atlas returned an invalid entry", "NGIN.UI",
                           "LayoutEngine::MeasureText"));
        glyphsValid = false;
        break;
      }

      if (node.text.glyphRuns.empty() ||
          node.text.glyphRuns.back().texture != entry.texture) {
        node.text.glyphRuns.push_back(TextGlyphRun{
            .texture = entry.texture,
        });
      }
      node.text.glyphRuns.back().glyphs.push_back(GlyphQuad{
          .destination =
              Rect{
                  positioned.origin.x + pen.x + glyph.offset.x +
                      entry.bearing.x,
                  positioned.origin.y + pen.y + glyph.offset.y +
                      entry.bearing.y,
                  entry.size.width,
                  entry.size.height,
              },
          .textureCoordinates = entry.textureCoordinates,
          .lease = entry.lease,
      });
      pen.x += glyph.advance.x;
      pen.y += glyph.advance.y;
    }
    if (!glyphsValid) {
      break;
    }
  }
  if (!glyphsValid) {
    node.text.glyphRuns.clear();
  }
  node.text.valid = glyphsValid;
  if ((node.type == ElementType::TextField ||
       node.type == ElementType::TextArea) &&
      node.textField.editing && properties.geometry != nullptr) {
    const auto &state = node.textField.editing->State();
    if (!state.selection.Empty()) {
      auto selectionRects = properties.geometry->RangeRects(
          node.text.paragraph, PresentedByteOffset(node, state.selection.start),
          PresentedByteOffset(node, state.selection.End()) -
              PresentedByteOffset(node, state.selection.start));
      if (selectionRects) {
        node.text.selectionRects = std::move(selectionRects).Value();
      } else {
        report(selectionRects.Error());
      }
    }
    if (node.textField.editing->HasComposition() &&
        !state.composition.Empty()) {
      auto compositionRects = properties.geometry->RangeRects(
          node.text.paragraph,
          PresentedByteOffset(node, state.composition.start),
          PresentedByteOffset(node, state.composition.End()) -
              PresentedByteOffset(node, state.composition.start));
      if (compositionRects) {
        node.text.compositionRects = std::move(compositionRects).Value();
      } else {
        report(compositionRects.Error());
      }
    }
    auto caret = properties.geometry->CaretRect(
        node.text.paragraph, PresentedByteOffset(node, state.caretCluster));
    if (caret) {
      node.text.caretRect = std::move(caret).Value();
      node.text.hasCaret = true;
    } else {
      report(caret.Error());
    }
  }

  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               node.text.paragraph.size.width + horizontalPadding),
      std::max(node.properties.layout.preferredSize.height,
               node.text.paragraph.size.height + verticalPadding),
  });
}

auto LayoutEngine::MeasureImage(RuntimeNode &node,
                                const SizeConstraints constraints) -> Size {
  node.image = {};
  const auto &properties = node.properties.image;
  const auto report = [&properties](const UIError &error) {
    if (properties.onError) {
      properties.onError(error);
    }
  };
  if (!properties.resource || properties.resolver == nullptr) {
    report(MakeUIError(UIErrorCode::InvalidArgument,
                       "Image requires a logical resource and image resolver",
                       "NGIN.UI", "LayoutEngine::MeasureImage"));
    return constraints.Constrain(node.properties.layout.preferredSize);
  }

  auto resolved = properties.resolver->Resolve(properties.resource);
  if (!resolved) {
    report(resolved.Error());
    node.image.loadState = properties.resource->State();
  } else {
    node.image.texture = resolved.Value().texture;
    node.image.sourceSize = resolved.Value().size;
    node.image.loadState = resolved.Value().state;
    node.image.valid = resolved.Value().state == ImageLoadState::Ready &&
                       static_cast<bool>(resolved.Value().texture);
  }
  const auto natural =
      node.image.sourceSize.IsEmpty()
          ? node.properties.layout.preferredSize
          : Size{static_cast<F32>(node.image.sourceSize.width),
                 static_cast<F32>(node.image.sourceSize.height)};
  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width, natural.width),
      std::max(node.properties.layout.preferredSize.height, natural.height),
  });
}

auto LayoutEngine::MeasureContainer(RuntimeNode &node,
                                    const SizeConstraints constraints) -> Size {
  if (node.type == ElementType::Grid) {
    return MeasureGrid(node, constraints);
  }
  if (node.type == ElementType::WrapPanel) {
    return MeasureWrapPanel(node, constraints);
  }
  if (node.type == ElementType::Canvas) {
    return MeasureCanvas(node, constraints);
  }
  const auto padding = node.properties.layout.padding;
  const auto horizontalPadding = padding.left + padding.right;
  const auto verticalPadding = padding.top + padding.bottom;
  const auto availableWidth =
      std::isfinite(constraints.maximum.width)
          ? InnerWidth(constraints.maximum.width, padding)
          : constraints.maximum.width;
  const auto availableHeight =
      std::isfinite(constraints.maximum.height)
          ? InnerHeight(constraints.maximum.height, padding)
          : constraints.maximum.height;

  if (node.type == ElementType::ListView &&
      node.properties.virtualizedList.controller != nullptr) {
    auto &controller = *node.properties.virtualizedList.controller;
    F32 contentWidth = 0.0F;
    for (const auto childHandle : node.children) {
      const auto childSize =
          Measure(childHandle,
                  SizeConstraints{.maximum = Size{availableWidth,
                                                  controller.ItemExtent()}});
      contentWidth = std::max(contentWidth, childSize.width);
    }
    const auto viewportHeight =
        std::isfinite(availableHeight)
            ? availableHeight
            : std::min(controller.TotalExtent(),
                       controller.Diagnostics().viewportExtent);
    return constraints.Constrain(Size{
        std::max(node.properties.layout.preferredSize.width,
                 contentWidth + padding.left + padding.right),
        std::max(node.properties.layout.preferredSize.height,
                 viewportHeight + padding.top + padding.bottom),
    });
  }

  F32 contentWidth = 0.0F;
  F32 contentHeight = 0.0F;
  UIntSize flowChildCount = 0;

  for (const auto childHandle : node.children) {
    SizeConstraints childConstraints{};
    childConstraints.maximum = Size{availableWidth, availableHeight};
    if (node.type == ElementType::ScrollView ||
        node.type == ElementType::ListView) {
      if (node.properties.scroll.horizontal) {
        childConstraints.maximum.width = std::numeric_limits<F32>::infinity();
      }
      if (node.properties.scroll.vertical) {
        childConstraints.maximum.height = std::numeric_limits<F32>::infinity();
      }
    } else if (node.type == ElementType::Column) {
      childConstraints.maximum.height = std::numeric_limits<F32>::infinity();
    } else if (node.type == ElementType::Row) {
      childConstraints.maximum.width = std::numeric_limits<F32>::infinity();
    }

    const auto childSize = Measure(childHandle, childConstraints);
    const auto *child = m_tree.Get(childHandle);
    if (child != nullptr &&
        (child->type == ElementType::Popup ||
         child->properties.visibility == ElementVisibility::Collapsed)) {
      continue;
    }
    ++flowChildCount;
    switch (node.type) {
    case ElementType::Column:
      contentWidth = std::max(contentWidth, childSize.width);
      contentHeight += childSize.height;
      break;
    case ElementType::Row:
      contentWidth += childSize.width;
      contentHeight = std::max(contentHeight, childSize.height);
      break;
    default:
      contentWidth = std::max(contentWidth, childSize.width);
      contentHeight = std::max(contentHeight, childSize.height);
      break;
    }
  }

  const auto gapCount = flowChildCount == 0 ? 0 : flowChildCount - 1;
  if (node.type == ElementType::Column) {
    contentHeight += node.properties.layout.gap * static_cast<F32>(gapCount);
  } else if (node.type == ElementType::Row) {
    contentWidth += node.properties.layout.gap * static_cast<F32>(gapCount);
  }

  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               contentWidth + horizontalPadding),
      std::max(node.properties.layout.preferredSize.height,
               contentHeight + verticalPadding),
  });
}

auto LayoutEngine::MeasureGrid(RuntimeNode &node,
                               const SizeConstraints constraints) -> Size {
  const auto padding = node.properties.layout.padding;
  const auto availableWidth =
      std::isfinite(constraints.maximum.width)
          ? InnerWidth(constraints.maximum.width, padding)
          : constraints.maximum.width;
  const auto availableHeight =
      std::isfinite(constraints.maximum.height)
          ? InnerHeight(constraints.maximum.height, padding)
          : constraints.maximum.height;
  const auto columns = GridDefinitions(node.properties.grid.columns);
  const auto rows = GridDefinitions(node.properties.grid.rows);
  auto columnIntrinsic = InitialIntrinsic(columns);
  auto rowIntrinsic = InitialIntrinsic(rows);
  const auto columnGap = std::max(0.0F, node.properties.grid.columnGap);
  const auto rowGap = std::max(0.0F, node.properties.grid.rowGap);

  for (const auto childHandle : node.children) {
    const auto childSize = Measure(
        childHandle,
        SizeConstraints{.maximum = Size{availableWidth, availableHeight}});
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr || child->type == ElementType::Popup ||
        child->properties.visibility == ElementVisibility::Collapsed) {
      continue;
    }
    const auto &placement = child->properties.gridPlacement;
    GrowIntrinsic(columns, columnIntrinsic, placement.column,
                  placement.columnSpan, childSize.width, columnGap);
  }

  auto resolvedColumns =
      ResolveTracks(columns, columnIntrinsic, availableWidth, columnGap);
  for (const auto childHandle : node.children) {
    const auto *before = m_tree.Get(childHandle);
    if (before == nullptr || before->type == ElementType::Popup ||
        before->properties.visibility == ElementVisibility::Collapsed) {
      continue;
    }
    const auto placement = before->properties.gridPlacement;
    const auto width = SpanExtent(resolvedColumns, placement.column,
                                  placement.columnSpan, columnGap);
    const auto childSize = Measure(
        childHandle, SizeConstraints{.maximum = Size{width, availableHeight}});
    GrowIntrinsic(rows, rowIntrinsic, placement.row, placement.rowSpan,
                  childSize.height, rowGap);
  }
  auto resolvedRows =
      ResolveTracks(rows, rowIntrinsic, availableHeight, rowGap);
  node.grid.columnIntrinsic = std::move(columnIntrinsic);
  node.grid.rowIntrinsic = std::move(rowIntrinsic);
  node.grid.resolvedColumns = std::move(resolvedColumns);
  node.grid.resolvedRows = std::move(resolvedRows);

  const auto desired = Size{
      TrackSum(node.grid.resolvedColumns) +
          TrackGapExtent(node.grid.resolvedColumns.size(), columnGap) +
          padding.left + padding.right,
      TrackSum(node.grid.resolvedRows) +
          TrackGapExtent(node.grid.resolvedRows.size(), rowGap) + padding.top +
          padding.bottom,
  };
  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width, desired.width),
      std::max(node.properties.layout.preferredSize.height, desired.height),
  });
}

auto LayoutEngine::MeasureWrapPanel(RuntimeNode &node,
                                    const SizeConstraints constraints) -> Size {
  const auto padding = node.properties.layout.padding;
  const auto horizontal =
      node.properties.wrapPanel.orientation == WrapOrientation::Horizontal;
  const auto availableWidth =
      std::isfinite(constraints.maximum.width)
          ? InnerWidth(constraints.maximum.width, padding)
          : constraints.maximum.width;
  const auto availableHeight =
      std::isfinite(constraints.maximum.height)
          ? InnerHeight(constraints.maximum.height, padding)
          : constraints.maximum.height;
  const auto availableMain = horizontal ? availableWidth : availableHeight;
  const auto itemGap = std::max(0.0F, node.properties.wrapPanel.itemGap);
  const auto lineGap = std::max(0.0F, node.properties.wrapPanel.lineGap);
  node.wrapPanel.lines.clear();

  for (const auto childHandle : node.children) {
    const auto childSize = Measure(
        childHandle,
        SizeConstraints{.maximum = Size{availableWidth, availableHeight}});
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr || child->type == ElementType::Popup ||
        child->properties.visibility == ElementVisibility::Collapsed) {
      continue;
    }
    const auto childMain = horizontal ? childSize.width : childSize.height;
    const auto childCross = horizontal ? childSize.height : childSize.width;
    const auto startsLine =
        node.wrapPanel.lines.empty() ||
        (!node.wrapPanel.lines.back().children.empty() &&
         std::isfinite(availableMain) &&
         node.wrapPanel.lines.back().mainExtent + itemGap + childMain >
             availableMain);
    if (startsLine) {
      node.wrapPanel.lines.emplace_back();
    }
    auto &line = node.wrapPanel.lines.back();
    if (!line.children.empty()) {
      line.mainExtent += itemGap;
    }
    line.children.push_back(childHandle);
    line.mainExtent += childMain;
    line.crossExtent = std::max(line.crossExtent, childCross);
  }

  F32 contentMain = 0.0F;
  F32 contentCross = 0.0F;
  for (const auto &line : node.wrapPanel.lines) {
    contentMain = std::max(contentMain, line.mainExtent);
    contentCross += line.crossExtent;
  }
  contentCross += TrackGapExtent(node.wrapPanel.lines.size(), lineGap);
  const Size content = horizontal ? Size{contentMain, contentCross}
                                  : Size{contentCross, contentMain};
  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               content.width + padding.left + padding.right),
      std::max(node.properties.layout.preferredSize.height,
               content.height + padding.top + padding.bottom),
  });
}

auto LayoutEngine::MeasureCanvas(RuntimeNode &node,
                                 const SizeConstraints constraints) -> Size {
  const auto padding = node.properties.layout.padding;
  const auto availableWidth =
      std::isfinite(constraints.maximum.width)
          ? InnerWidth(constraints.maximum.width, padding)
          : constraints.maximum.width;
  const auto availableHeight =
      std::isfinite(constraints.maximum.height)
          ? InnerHeight(constraints.maximum.height, padding)
          : constraints.maximum.height;
  Size content{};
  for (const auto childHandle : node.children) {
    const auto childSize = Measure(
        childHandle,
        SizeConstraints{.maximum = Size{availableWidth, availableHeight}});
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr || child->type == ElementType::Popup ||
        child->properties.visibility == ElementVisibility::Collapsed ||
        !child->properties.canvasPlacement.contributesToDesiredSize) {
      continue;
    }
    content.width =
        std::max(content.width,
                 std::max(0.0F, child->properties.canvasPlacement.offset.x) +
                     childSize.width);
    content.height =
        std::max(content.height,
                 std::max(0.0F, child->properties.canvasPlacement.offset.y) +
                     childSize.height);
  }
  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               content.width + padding.left + padding.right),
      std::max(node.properties.layout.preferredSize.height,
               content.height + padding.top + padding.bottom),
  });
}

void LayoutEngine::Arrange(const ElementHandle handle, const Rect finalBounds) {
  auto *node = m_tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  if (node->properties.visibility == ElementVisibility::Collapsed) {
    node->arrangedBounds = {};
    node->layoutRevision = m_revision;
    ++m_stats.arranged;
    return;
  }
  node->arrangedBounds = finalBounds;
  node->layoutRevision = m_revision;
  ++m_stats.arranged;
  if (node->type == ElementType::TextArea) {
    const auto padding = node->properties.layout.padding;
    node->scroll.viewportSize = Size{InnerWidth(finalBounds.width, padding),
                                     InnerHeight(finalBounds.height, padding)};
    node->scroll.offset.x =
        std::clamp(node->scroll.offset.x, 0.0F,
                   std::max(0.0F, node->scroll.contentSize.width -
                                      node->scroll.viewportSize.width));
    node->scroll.offset.y =
        std::clamp(node->scroll.offset.y, 0.0F,
                   std::max(0.0F, node->scroll.contentSize.height -
                                      node->scroll.viewportSize.height));
    if (node->interaction.focused && node->text.hasCaret) {
      const auto caretLeft = padding.left + node->text.caretRect.x;
      const auto caretRight =
          caretLeft + std::max(1.0F, node->text.caretRect.width);
      const auto caretTop = padding.top + node->text.caretRect.y;
      const auto caretBottom = caretTop + node->text.caretRect.height;
      if (caretLeft < node->scroll.offset.x) {
        node->scroll.offset.x = caretLeft;
      } else if (caretRight >
                 node->scroll.offset.x + node->scroll.viewportSize.width) {
        node->scroll.offset.x = caretRight - node->scroll.viewportSize.width;
      }
      if (caretTop < node->scroll.offset.y) {
        node->scroll.offset.y = caretTop;
      } else if (caretBottom >
                 node->scroll.offset.y + node->scroll.viewportSize.height) {
        node->scroll.offset.y = caretBottom - node->scroll.viewportSize.height;
      }
      node->scroll.offset.x =
          std::clamp(node->scroll.offset.x, 0.0F,
                     std::max(0.0F, node->scroll.contentSize.width -
                                        node->scroll.viewportSize.width));
      node->scroll.offset.y =
          std::clamp(node->scroll.offset.y, 0.0F,
                     std::max(0.0F, node->scroll.contentSize.height -
                                        node->scroll.viewportSize.height));
    }
  } else if (node->type == ElementType::Image && node->image.valid) {
    const auto padding = node->properties.layout.padding;
    node->image.destination = ResolveImageDestination(
        Rect{finalBounds.x + padding.left, finalBounds.y + padding.top,
             InnerWidth(finalBounds.width, padding),
             InnerHeight(finalBounds.height, padding)},
        node->image.sourceSize, node->properties.image.fit,
        node->properties.image.alignment);
  }
  if (node->type == ElementType::CustomElement && node->custom.state &&
      node->properties.custom.element) {
    try {
      auto context = CustomContextFor(*node, m_scaleFactor);
      auto arranged = node->properties.custom.element->Arrange(
          context, Size{finalBounds.width, finalBounds.height});
      if (!arranged) {
        ReportCustomError(*node, arranged.Error());
      }
    } catch (const std::bad_alloc &) {
      ReportCustomError(*node,
                        MakeUIError(UIErrorCode::OutOfMemory,
                                    "Custom arrangement allocation failed",
                                    "NGIN.UI", "ICustomElement::Arrange"));
    } catch (...) {
      ReportCustomError(
          *node, MakeUIError(UIErrorCode::InvalidState,
                             "Custom arrangement callback threw an exception",
                             "NGIN.UI", "ICustomElement::Arrange"));
    }
    m_tree.SynchronizeCustom(*node, m_scaleFactor);
  }
  ArrangeChildren(*node);
}

void LayoutEngine::ArrangeChildren(RuntimeNode &node) {
  const auto padding = node.properties.layout.padding;
  const Rect content{
      node.arrangedBounds.x + padding.left,
      node.arrangedBounds.y + padding.top,
      InnerWidth(node.arrangedBounds.width, padding),
      InnerHeight(node.arrangedBounds.height, padding),
  };
  F32 cursorX = content.x;
  F32 cursorY = content.y;

  if (node.type == ElementType::Grid) {
    ArrangeGrid(node, content);
    return;
  }
  if (node.type == ElementType::WrapPanel) {
    ArrangeWrapPanel(node, content);
    return;
  }
  if (node.type == ElementType::Canvas) {
    ArrangeCanvas(node, content);
    return;
  }

  if (node.type == ElementType::Popup) {
    const auto viewportRight = content.x + content.width;
    const auto viewportBottom = content.y + content.height;
    const auto &popup = node.properties.popup;
    auto anchor = popup.anchor;
    if (!popup.anchorIdentifier.Empty()) {
      const auto anchorHandle =
          m_tree.FindBySemanticIdentifier(popup.anchorIdentifier);
      if (const auto *anchorNode = m_tree.Get(anchorHandle);
          anchorNode != nullptr &&
          anchorNode->properties.visibility == ElementVisibility::Visible) {
        anchor = anchorNode->arrangedBounds;
      }
    }
    const auto gap = std::max(0.0F, popup.gap);
    node.popup.contentBounds = {};
    bool hasContent = false;
    for (const auto childHandle : node.children) {
      const auto *child = m_tree.Get(childHandle);
      if (child == nullptr ||
          child->properties.visibility == ElementVisibility::Collapsed) {
        continue;
      }

      const auto width = std::min(content.width, child->measuredSize.width);
      const auto height = std::min(content.height, child->measuredSize.height);
      F32 x = content.x + (content.width - width) * 0.5F;
      F32 y = content.y + (content.height - height) * 0.5F;

      switch (popup.placement) {
      case PopupPlacement::BelowStart:
      case PopupPlacement::BelowEnd: {
        x = popup.placement == PopupPlacement::BelowStart
                ? anchor.x
                : anchor.x + anchor.width - width;
        y = anchor.y + anchor.height + gap;
        const auto above = anchor.y - gap - height;
        if (y + height > viewportBottom && above >= content.y) {
          y = above;
        }
        break;
      }
      case PopupPlacement::AboveStart:
      case PopupPlacement::AboveEnd: {
        x = popup.placement == PopupPlacement::AboveStart
                ? anchor.x
                : anchor.x + anchor.width - width;
        y = anchor.y - gap - height;
        const auto below = anchor.y + anchor.height + gap;
        if (y < content.y && below + height <= viewportBottom) {
          y = below;
        }
        break;
      }
      case PopupPlacement::Center:
        break;
      }

      x = std::clamp(x, content.x, std::max(content.x, viewportRight - width));
      y = std::clamp(y, content.y,
                     std::max(content.y, viewportBottom - height));
      const Rect childBounds{x, y, width, height};
      Arrange(childHandle, childBounds);
      if (!hasContent) {
        node.popup.contentBounds = childBounds;
        hasContent = true;
      } else {
        const auto left = std::min(node.popup.contentBounds.x, childBounds.x);
        const auto top = std::min(node.popup.contentBounds.y, childBounds.y);
        const auto right = std::max(node.popup.contentBounds.x +
                                        node.popup.contentBounds.width,
                                    childBounds.x + childBounds.width);
        const auto bottom = std::max(node.popup.contentBounds.y +
                                         node.popup.contentBounds.height,
                                     childBounds.y + childBounds.height);
        node.popup.contentBounds = Rect{left, top, right - left, bottom - top};
      }
    }
    return;
  }

  if (node.type == ElementType::ScrollView ||
      node.type == ElementType::ListView) {
    if (node.type == ElementType::ListView &&
        node.properties.virtualizedList.controller != nullptr) {
      auto &controller = *node.properties.virtualizedList.controller;
      node.scroll.viewportSize = Size{content.width, content.height};
      node.scroll.contentSize = Size{content.width, controller.TotalExtent()};
      if (const auto anchored = controller.TakePendingScrollOffset();
          anchored) {
        node.scroll.offset.y = *anchored;
      }
      node.scroll.offset.x = 0.0F;
      node.scroll.offset.y =
          std::clamp(node.scroll.offset.y, 0.0F,
                     std::max(0.0F, node.scroll.contentSize.height -
                                        node.scroll.viewportSize.height));
      static_cast<void>(controller.UpdateViewport(
          node.scroll.offset.y, node.scroll.viewportSize.height));

      for (const auto childHandle : node.children) {
        const auto *child = m_tree.Get(childHandle);
        if (child == nullptr) {
          continue;
        }
        if (child->properties.visibility == ElementVisibility::Collapsed) {
          Arrange(childHandle, {});
          continue;
        }
        if (!child->properties.virtualizedItem.enabled) {
          Arrange(childHandle, {});
          continue;
        }
        const auto itemY =
            content.y +
            static_cast<F32>(child->properties.virtualizedItem.sourceIndex) *
                controller.ItemStride() -
            node.scroll.offset.y;
        Arrange(childHandle,
                Rect{content.x, itemY, content.width, controller.ItemExtent()});
      }

      auto diagnostics = controller.Diagnostics();
      diagnostics.element = node.id;
      diagnostics.realizedNodeCount = node.children.size();
      m_stats.virtualizedLists.push_back(std::move(diagnostics));
      return;
    }

    Size contentSize{};
    for (const auto childHandle : node.children) {
      if (const auto *child = m_tree.Get(childHandle); child != nullptr) {
        if (child->type == ElementType::Popup ||
            child->properties.visibility == ElementVisibility::Collapsed) {
          continue;
        }
        contentSize.width =
            std::max(contentSize.width, child->measuredSize.width);
        contentSize.height =
            std::max(contentSize.height, child->measuredSize.height);
      }
    }
    if (!node.properties.scroll.horizontal) {
      contentSize.width = content.width;
    }
    if (!node.properties.scroll.vertical) {
      contentSize.height = content.height;
    }
    node.scroll.viewportSize = Size{content.width, content.height};
    node.scroll.contentSize = contentSize;
    node.scroll.offset.x =
        std::clamp(node.scroll.offset.x, 0.0F,
                   std::max(0.0F, contentSize.width - content.width));
    node.scroll.offset.y =
        std::clamp(node.scroll.offset.y, 0.0F,
                   std::max(0.0F, contentSize.height - content.height));

    for (const auto childHandle : node.children) {
      const auto *child = m_tree.Get(childHandle);
      if (child == nullptr) {
        continue;
      }
      if (child->properties.visibility == ElementVisibility::Collapsed) {
        Arrange(childHandle, {});
        continue;
      }
      if (child->type == ElementType::Popup) {
        const auto *root = m_tree.Get(m_tree.Root());
        Arrange(childHandle,
                root != nullptr ? root->arrangedBounds : node.arrangedBounds);
        continue;
      }
      const auto width = node.properties.scroll.horizontal
                             ? child->measuredSize.width
                             : content.width;
      const auto height = node.properties.scroll.vertical
                              ? child->measuredSize.height
                              : content.height;
      Arrange(childHandle,
              Rect{content.x - node.scroll.offset.x,
                   content.y - node.scroll.offset.y, width, height});
    }
    return;
  }

  F32 measuredMainAxis = 0.0F;
  F32 totalGrow = 0.0F;
  F32 totalShrinkWeight = 0.0F;
  UIntSize flowChildCount = 0;
  if (node.type == ElementType::Row || node.type == ElementType::Column) {
    for (const auto childHandle : node.children) {
      const auto *child = m_tree.Get(childHandle);
      if (child == nullptr || child->type == ElementType::Popup ||
          child->properties.visibility == ElementVisibility::Collapsed) {
        continue;
      }
      ++flowChildCount;
      const auto basis = node.type == ElementType::Row
                             ? child->measuredSize.width
                             : child->measuredSize.height;
      measuredMainAxis += basis;
      totalGrow += std::max(0.0F, child->properties.layout.flexGrow);
      totalShrinkWeight +=
          basis * std::max(0.0F, child->properties.layout.flexShrink);
    }
    if (flowChildCount > 1) {
      measuredMainAxis +=
          node.properties.layout.gap * static_cast<F32>(flowChildCount - 1);
    }
  }
  const auto availableMainAxis =
      node.type == ElementType::Row ? content.width : content.height;
  const auto remainingMainAxis = availableMainAxis - measuredMainAxis;

  for (const auto childHandle : node.children) {
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr) {
      continue;
    }
    if (child->properties.visibility == ElementVisibility::Collapsed) {
      Arrange(childHandle, {});
      continue;
    }
    if (child->type == ElementType::Popup) {
      const auto *root = m_tree.Get(m_tree.Root());
      Arrange(childHandle,
              root != nullptr ? root->arrangedBounds : node.arrangedBounds);
      continue;
    }

    auto childWidth = ResolveChildWidth(*child, content.width);
    auto childHeight = ResolveChildHeight(*child, content.height);
    F32 x = content.x;
    F32 y = content.y;

    if (node.type == ElementType::Column) {
      const auto basis = child->measuredSize.height;
      childHeight = basis;
      if (remainingMainAxis > 0.0F && totalGrow > 0.0F) {
        childHeight += remainingMainAxis *
                       std::max(0.0F, child->properties.layout.flexGrow) /
                       totalGrow;
      } else if (remainingMainAxis < 0.0F && totalShrinkWeight > 0.0F) {
        childHeight += remainingMainAxis * basis *
                       std::max(0.0F, child->properties.layout.flexShrink) /
                       totalShrinkWeight;
      }
      childHeight = ClampDimension(childHeight,
                                   child->properties.layout.minimumSize.height,
                                   child->properties.layout.maximumSize.height);
      y = cursorY;
      x += AlignmentOffset(content.width, childWidth,
                           child->properties.layout.horizontalAlignment);
      cursorY += childHeight + node.properties.layout.gap;
    } else if (node.type == ElementType::Row) {
      const auto basis = child->measuredSize.width;
      childWidth = basis;
      if (remainingMainAxis > 0.0F && totalGrow > 0.0F) {
        childWidth += remainingMainAxis *
                      std::max(0.0F, child->properties.layout.flexGrow) /
                      totalGrow;
      } else if (remainingMainAxis < 0.0F && totalShrinkWeight > 0.0F) {
        childWidth += remainingMainAxis * basis *
                      std::max(0.0F, child->properties.layout.flexShrink) /
                      totalShrinkWeight;
      }
      childWidth =
          ClampDimension(childWidth, child->properties.layout.minimumSize.width,
                         child->properties.layout.maximumSize.width);
      x = cursorX;
      y += AlignmentOffset(content.height, childHeight,
                           child->properties.layout.verticalAlignment);
      cursorX += childWidth + node.properties.layout.gap;
    } else {
      x += AlignmentOffset(content.width, childWidth,
                           child->properties.layout.horizontalAlignment);
      y += AlignmentOffset(content.height, childHeight,
                           child->properties.layout.verticalAlignment);
    }

    Arrange(childHandle, Rect{x, y, childWidth, childHeight});
  }
}

void LayoutEngine::ArrangeGrid(RuntimeNode &node, const Rect content) {
  const auto columns = GridDefinitions(node.properties.grid.columns);
  const auto rows = GridDefinitions(node.properties.grid.rows);
  const auto columnGap = std::max(0.0F, node.properties.grid.columnGap);
  const auto rowGap = std::max(0.0F, node.properties.grid.rowGap);
  node.grid.resolvedColumns = ResolveTracks(columns, node.grid.columnIntrinsic,
                                            content.width, columnGap);
  node.grid.resolvedRows =
      ResolveTracks(rows, node.grid.rowIntrinsic, content.height, rowGap);

  std::erase_if(m_stats.grids, [&node](const GridLayoutDiagnostics &item) {
    return item.element == node.id;
  });
  m_stats.grids.push_back(GridLayoutDiagnostics{
      .element = node.id,
      .columns = node.grid.resolvedColumns,
      .rows = node.grid.resolvedRows,
  });

  for (const auto childHandle : node.children) {
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr) {
      continue;
    }
    if (child->properties.visibility == ElementVisibility::Collapsed) {
      Arrange(childHandle, {});
      continue;
    }
    if (child->type == ElementType::Popup) {
      const auto *root = m_tree.Get(m_tree.Root());
      Arrange(childHandle,
              root != nullptr ? root->arrangedBounds : node.arrangedBounds);
      continue;
    }
    const auto &placement = child->properties.gridPlacement;
    const auto column =
        NormalizedStart(placement.column, node.grid.resolvedColumns.size());
    const auto row =
        NormalizedStart(placement.row, node.grid.resolvedRows.size());
    F32 cellX = content.x;
    for (UIntSize index = 0; index < column; ++index) {
      cellX += node.grid.resolvedColumns[index] + columnGap;
    }
    F32 cellY = content.y;
    for (UIntSize index = 0; index < row; ++index) {
      cellY += node.grid.resolvedRows[index] + rowGap;
    }
    const auto cellWidth = SpanExtent(node.grid.resolvedColumns, column,
                                      placement.columnSpan, columnGap);
    const auto cellHeight =
        SpanExtent(node.grid.resolvedRows, row, placement.rowSpan, rowGap);
    const auto width = ResolveChildWidth(*child, cellWidth);
    const auto height = ResolveChildHeight(*child, cellHeight);
    Arrange(childHandle,
            Rect{cellX + AlignmentOffset(
                             cellWidth, width,
                             child->properties.layout.horizontalAlignment),
                 cellY + AlignmentOffset(
                             cellHeight, height,
                             child->properties.layout.verticalAlignment),
                 width, height});
  }
}

void LayoutEngine::ArrangeWrapPanel(RuntimeNode &node, const Rect content) {
  const auto horizontal =
      node.properties.wrapPanel.orientation == WrapOrientation::Horizontal;
  const auto availableMain = horizontal ? content.width : content.height;
  const auto itemGap = std::max(0.0F, node.properties.wrapPanel.itemGap);
  const auto lineGap = std::max(0.0F, node.properties.wrapPanel.lineGap);
  node.wrapPanel.lines.clear();
  for (const auto childHandle : node.children) {
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr || child->type == ElementType::Popup ||
        child->properties.visibility == ElementVisibility::Collapsed) {
      continue;
    }
    const auto childMain =
        horizontal ? child->measuredSize.width : child->measuredSize.height;
    const auto childCross =
        horizontal ? child->measuredSize.height : child->measuredSize.width;
    const auto startsLine =
        node.wrapPanel.lines.empty() ||
        (!node.wrapPanel.lines.back().children.empty() &&
         node.wrapPanel.lines.back().mainExtent + itemGap + childMain >
             availableMain);
    if (startsLine) {
      node.wrapPanel.lines.emplace_back();
    }
    auto &line = node.wrapPanel.lines.back();
    if (!line.children.empty()) {
      line.mainExtent += itemGap;
    }
    line.children.push_back(childHandle);
    line.mainExtent += childMain;
    line.crossExtent = std::max(line.crossExtent, childCross);
  }

  F32 crossCursor = horizontal ? content.y : content.x;
  for (const auto &line : node.wrapPanel.lines) {
    auto gap = itemGap;
    F32 mainCursor = horizontal ? content.x : content.y;
    const auto free = std::max(0.0F, availableMain - line.mainExtent);
    switch (node.properties.wrapPanel.lineAlignment) {
    case WrapLineAlignment::Center:
      mainCursor += free * 0.5F;
      break;
    case WrapLineAlignment::End:
      mainCursor += free;
      break;
    case WrapLineAlignment::SpaceBetween:
      if (line.children.size() > 1) {
        gap += free / static_cast<F32>(line.children.size() - 1);
      }
      break;
    case WrapLineAlignment::Start:
      break;
    }
    for (const auto childHandle : line.children) {
      const auto *child = m_tree.Get(childHandle);
      if (child == nullptr) {
        continue;
      }
      const auto childMain =
          horizontal ? child->measuredSize.width : child->measuredSize.height;
      const auto childCross =
          horizontal ? child->measuredSize.height : child->measuredSize.width;
      const auto crossAlignment =
          horizontal
              ? AlignmentOffset(line.crossExtent, childCross,
                                child->properties.layout.verticalAlignment)
              : AlignmentOffset(line.crossExtent, childCross,
                                child->properties.layout.horizontalAlignment);
      Arrange(childHandle, horizontal
                               ? Rect{mainCursor, crossCursor + crossAlignment,
                                      childMain, childCross}
                               : Rect{crossCursor + crossAlignment, mainCursor,
                                      childCross, childMain});
      mainCursor += childMain + gap;
    }
    crossCursor += line.crossExtent + lineGap;
  }

  std::erase_if(m_stats.wrapPanels,
                [&node](const WrapPanelLayoutDiagnostics &item) {
                  return item.element == node.id;
                });
  WrapPanelLayoutDiagnostics diagnostics{
      .element = node.id,
      .orientation = node.properties.wrapPanel.orientation,
  };
  diagnostics.lines.reserve(node.wrapPanel.lines.size());
  for (const auto &line : node.wrapPanel.lines) {
    diagnostics.lines.push_back(WrapLineDiagnostics{
        .itemCount = line.children.size(),
        .mainExtent = line.mainExtent,
        .crossExtent = line.crossExtent,
    });
  }
  m_stats.wrapPanels.push_back(std::move(diagnostics));

  for (const auto childHandle : node.children) {
    const auto *child = m_tree.Get(childHandle);
    if (child != nullptr &&
        child->properties.visibility == ElementVisibility::Collapsed) {
      Arrange(childHandle, {});
    } else if (child != nullptr && child->type == ElementType::Popup) {
      const auto *root = m_tree.Get(m_tree.Root());
      Arrange(childHandle,
              root != nullptr ? root->arrangedBounds : node.arrangedBounds);
    }
  }
}

void LayoutEngine::ArrangeCanvas(RuntimeNode &node, const Rect content) {
  for (const auto childHandle : node.children) {
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr) {
      continue;
    }
    if (child->properties.visibility == ElementVisibility::Collapsed) {
      Arrange(childHandle, {});
      continue;
    }
    if (child->type == ElementType::Popup) {
      const auto *root = m_tree.Get(m_tree.Root());
      Arrange(childHandle,
              root != nullptr ? root->arrangedBounds : node.arrangedBounds);
      continue;
    }
    const auto offset = child->properties.canvasPlacement.offset;
    Arrange(childHandle,
            Rect{content.x + offset.x, content.y + offset.y,
                 child->measuredSize.width, child->measuredSize.height});
  }
}

auto LayoutEngine::ResolveChildWidth(const RuntimeNode &child,
                                     const F32 available) const noexcept
    -> F32 {
  if (child.properties.layout.horizontalAlignment ==
      HorizontalAlignment::Stretch) {
    return ClampDimension(available, child.properties.layout.minimumSize.width,
                          child.properties.layout.maximumSize.width);
  }
  return std::min(available, child.measuredSize.width);
}

auto LayoutEngine::ResolveChildHeight(const RuntimeNode &child,
                                      const F32 available) const noexcept
    -> F32 {
  if (child.properties.layout.verticalAlignment == VerticalAlignment::Stretch) {
    return ClampDimension(available, child.properties.layout.minimumSize.height,
                          child.properties.layout.maximumSize.height);
  }
  return std::min(available, child.measuredSize.height);
}
} // namespace NGIN::UI
