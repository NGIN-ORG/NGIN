#include <NGIN/UI/TextEditing.hpp>

#include <algorithm>
#include <vector>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto Utf8Boundaries(const NGIN::Text::String &value)
    -> UIResult<std::vector<bool>> {
  const auto size = value.Size();
  std::vector<bool> boundaries(size + 1, false);
  boundaries[0] = true;

  UIntSize offset = 0;
  while (offset < size) {
    const auto first = static_cast<UInt8>(value[offset]);
    UIntSize length = 0;
    UInt32 codePoint = 0;
    UInt32 minimum = 0;
    if (first <= 0x7FU) {
      length = 1;
      codePoint = first;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      length = 2;
      codePoint = first & 0x1FU;
      minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      length = 3;
      codePoint = first & 0x0FU;
      minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      length = 4;
      codePoint = first & 0x07U;
      minimum = 0x10000U;
    } else {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Text contains invalid UTF-8", "NGIN.UI",
                         "TextEditingBuffer::Validate");
    }

    if (length > size - offset) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Text contains truncated UTF-8", "NGIN.UI",
                         "TextEditingBuffer::Validate");
    }

    for (UIntSize index = 1; index < length; ++index) {
      const auto continuation = static_cast<UInt8>(value[offset + index]);
      if ((continuation & 0xC0U) != 0x80U) {
        return MakeUIError(UIErrorCode::InvalidArgument,
                           "Text contains invalid UTF-8 continuation bytes",
                           "NGIN.UI", "TextEditingBuffer::Validate");
      }
      codePoint = (codePoint << 6U) | (continuation & 0x3FU);
    }

    if (codePoint < minimum || codePoint > 0x10FFFFU ||
        (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Text contains an invalid Unicode scalar value",
                         "NGIN.UI", "TextEditingBuffer::Validate");
    }

    offset += length;
    boundaries[offset] = true;
  }

  return boundaries;
}

[[nodiscard]] auto ClusterAtByte(const std::vector<GraphemeCluster> &clusters,
                                 const UIntSize byteOffset) noexcept
    -> UIntSize {
  for (UIntSize index = 0; index < clusters.size(); ++index) {
    const auto &cluster = clusters[index];
    if (byteOffset <= cluster.byteOffset ||
        byteOffset < cluster.byteOffset + cluster.byteLength) {
      return index;
    }
  }
  return clusters.size();
}

[[nodiscard]] auto
ClusterPositionAtOrAfterByte(const std::vector<GraphemeCluster> &clusters,
                             const UIntSize byteOffset) noexcept -> UIntSize {
  for (UIntSize index = 0; index < clusters.size(); ++index) {
    const auto &cluster = clusters[index];
    if (byteOffset <= cluster.byteOffset) {
      return index;
    }
    if (byteOffset <= cluster.byteOffset + cluster.byteLength) {
      return index + 1;
    }
  }
  return clusters.size();
}
} // namespace

TextEditingBuffer::TextEditingBuffer(IGraphemeSegmenter &segmenter) noexcept
    : m_segmenter(&segmenter) {}

auto TextEditingBuffer::Value() const noexcept -> const NGIN::Text::String & {
  return m_value;
}

auto TextEditingBuffer::State() const noexcept -> const TextEditingState & {
  return m_state;
}

auto TextEditingBuffer::Clusters() const noexcept
    -> const std::vector<GraphemeCluster> & {
  return m_clusters;
}

auto TextEditingBuffer::SelectedText() const -> NGIN::Text::String {
  const auto start = ByteOffset(m_state.selection.start);
  return m_value.Substr(start, ByteOffset(m_state.selection.End()) - start);
}

auto TextEditingBuffer::HasComposition() const noexcept -> bool {
  return m_compositionActive;
}

auto TextEditingBuffer::Reset(NGIN::Text::String value) -> UIResult<void> {
  auto clusters = SegmentAndValidate(value);
  if (!clusters) {
    return std::move(clusters).Error();
  }

  m_value = std::move(value);
  m_clusters = std::move(clusters).Value();
  m_state.selection = TextRange{m_clusters.size(), 0};
  m_state.composition = {};
  m_state.caretCluster = m_clusters.size();
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = m_state.caretCluster;
  m_compositionActive = false;
  m_compositionBaseValue = {};
  m_compositionBaseClusters.clear();
  m_compositionBaseSelection = {};
  m_compositionStartByte = 0;
  m_compositionEndByte = 0;
  return {};
}

auto TextEditingBuffer::SetSelection(const TextRange selection)
    -> UIResult<void> {
  if (selection.start > m_clusters.size() ||
      selection.length > m_clusters.size() - selection.start) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Text selection is outside the editing buffer",
                       "NGIN.UI", "TextEditingBuffer::SetSelection");
  }

  m_state.selection = selection;
  m_state.caretCluster = selection.End();
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = selection.start;
  return {};
}

void TextEditingBuffer::SelectAll() noexcept {
  m_state.selection = TextRange{0, m_clusters.size()};
  m_state.caretCluster = m_clusters.size();
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = 0;
}

auto TextEditingBuffer::MoveCaretTo(const UIntSize cluster,
                                    const bool extendSelection)
    -> UIResult<void> {
  if (cluster > m_clusters.size()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Caret is outside the editing buffer", "NGIN.UI",
                       "TextEditingBuffer::MoveCaretTo");
  }

  if (!extendSelection) {
    m_selectionAnchor = cluster;
    m_state.selection = TextRange{cluster, 0};
  } else if (cluster < m_selectionAnchor) {
    m_state.selection = TextRange{cluster, m_selectionAnchor - cluster};
  } else {
    m_state.selection =
        TextRange{m_selectionAnchor, cluster - m_selectionAnchor};
  }
  m_state.caretCluster = cluster;
  m_state.preferredCaretX = 0.0F;
  return {};
}

auto TextEditingBuffer::ReplaceSelection(const NGIN::Text::String &text)
    -> UIResult<void> {
  return ReplaceRange(m_state.selection, text);
}

auto TextEditingBuffer::DeleteBackward() -> UIResult<void> {
  if (!m_state.selection.Empty()) {
    return ReplaceSelection({});
  }
  if (m_state.caretCluster == 0) {
    return {};
  }

  const auto caret = m_state.caretCluster;
  return ReplaceRange(TextRange{caret - 1, 1}, {});
}

auto TextEditingBuffer::DeleteForward() -> UIResult<void> {
  if (!m_state.selection.Empty()) {
    return ReplaceSelection({});
  }
  if (m_state.caretCluster >= m_clusters.size()) {
    return {};
  }

  return ReplaceRange(TextRange{m_state.caretCluster, 1}, {});
}

auto TextEditingBuffer::UpdateComposition(const NGIN::Text::String &text,
                                          const UIntSize selectionStartByte,
                                          const UIntSize selectionLengthByte)
    -> UIResult<void> {
  if (text.Empty()) {
    return CancelComposition();
  }
  if (selectionStartByte > text.Size() ||
      selectionLengthByte > text.Size() - selectionStartByte) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "IME selection is outside the composition text",
                       "NGIN.UI", "TextEditingBuffer::UpdateComposition");
  }
  auto compositionBoundaries = Utf8Boundaries(text);
  if (!compositionBoundaries) {
    return std::move(compositionBoundaries).Error();
  }
  if (!compositionBoundaries.Value()[selectionStartByte] ||
      !compositionBoundaries
           .Value()[selectionStartByte + selectionLengthByte]) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "IME selection does not use UTF-8 boundaries", "NGIN.UI",
                       "TextEditingBuffer::UpdateComposition");
  }

  if (!m_compositionActive) {
    m_compositionBaseValue = m_value;
    m_compositionBaseClusters = m_clusters;
    m_compositionBaseSelection = m_state.selection;
    m_compositionStartByte = ByteOffset(m_state.selection.start);
    m_compositionEndByte = ByteOffset(m_state.selection.End());
  }

  auto candidate = m_compositionBaseValue;
  candidate.Replace(m_compositionStartByte,
                    m_compositionEndByte - m_compositionStartByte, text.View());
  auto clusters = SegmentAndValidate(candidate);
  if (!clusters) {
    return std::move(clusters).Error();
  }

  const auto compositionEndByte = m_compositionStartByte + text.Size();
  const auto compositionStartCluster =
      ClusterAtByte(clusters.Value(), m_compositionStartByte);
  const auto compositionEndCluster =
      ClusterPositionAtOrAfterByte(clusters.Value(), compositionEndByte);
  const auto selectionStartCluster = ClusterPositionAtOrAfterByte(
      clusters.Value(), m_compositionStartByte + selectionStartByte);
  const auto selectionEndCluster = ClusterPositionAtOrAfterByte(
      clusters.Value(),
      m_compositionStartByte + selectionStartByte + selectionLengthByte);

  m_value = std::move(candidate);
  m_clusters = std::move(clusters).Value();
  m_state.composition = TextRange{
      compositionStartCluster, compositionEndCluster - compositionStartCluster};
  m_state.selection = TextRange{selectionStartCluster,
                                selectionEndCluster - selectionStartCluster};
  m_state.caretCluster = selectionEndCluster;
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = selectionStartCluster;
  m_compositionActive = true;
  return {};
}

auto TextEditingBuffer::CommitComposition(const NGIN::Text::String &text)
    -> UIResult<void> {
  if (!m_compositionActive) {
    return ReplaceSelection(text);
  }

  auto candidate = m_compositionBaseValue;
  candidate.Replace(m_compositionStartByte,
                    m_compositionEndByte - m_compositionStartByte, text.View());
  auto clusters = SegmentAndValidate(candidate);
  if (!clusters) {
    return std::move(clusters).Error();
  }

  m_value = std::move(candidate);
  m_clusters = std::move(clusters).Value();
  const auto caret = ClusterAtOrAfterByte(m_compositionStartByte + text.Size());
  m_state.selection = TextRange{caret, 0};
  m_state.composition = {};
  m_state.caretCluster = caret;
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = caret;
  m_compositionActive = false;
  m_compositionBaseValue = {};
  m_compositionBaseClusters.clear();
  m_compositionBaseSelection = {};
  m_compositionStartByte = 0;
  m_compositionEndByte = 0;
  return {};
}

auto TextEditingBuffer::CancelComposition() noexcept -> UIResult<void> {
  if (!m_compositionActive) {
    return {};
  }

  m_value = std::move(m_compositionBaseValue);
  m_clusters = std::move(m_compositionBaseClusters);
  m_state.selection = m_compositionBaseSelection;
  m_state.composition = {};
  m_state.caretCluster = m_compositionBaseSelection.End();
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = m_compositionBaseSelection.start;
  m_compositionActive = false;
  m_compositionBaseValue = {};
  m_compositionBaseClusters.clear();
  m_compositionBaseSelection = {};
  m_compositionStartByte = 0;
  m_compositionEndByte = 0;
  return {};
}

auto TextEditingBuffer::ByteOffset(const UIntSize cluster) const noexcept
    -> UIntSize {
  return cluster < m_clusters.size() ? m_clusters[cluster].byteOffset
                                     : m_value.Size();
}

auto TextEditingBuffer::ClusterAtOrAfterByte(
    const UIntSize byteOffset) const noexcept -> UIntSize {
  return ClusterPositionAtOrAfterByte(m_clusters, byteOffset);
}

auto TextEditingBuffer::ReplaceRange(const TextRange range,
                                     const NGIN::Text::String &text)
    -> UIResult<void> {
  const auto startByte = ByteOffset(range.start);
  const auto endByte = ByteOffset(range.End());
  auto candidate = m_value;
  candidate.Replace(startByte, endByte - startByte, text.View());
  return CommitCandidate(std::move(candidate), startByte + text.Size());
}

auto TextEditingBuffer::CommitCandidate(NGIN::Text::String candidate,
                                        const UIntSize desiredCaretByte)
    -> UIResult<void> {
  auto clusters = SegmentAndValidate(candidate);
  if (!clusters) {
    return std::move(clusters).Error();
  }

  m_value = std::move(candidate);
  m_clusters = std::move(clusters).Value();
  const auto caret = ClusterAtOrAfterByte(desiredCaretByte);
  m_state.selection = TextRange{caret, 0};
  m_state.composition = {};
  m_state.caretCluster = caret;
  m_state.preferredCaretX = 0.0F;
  m_selectionAnchor = caret;
  m_compositionActive = false;
  m_compositionBaseValue = {};
  m_compositionBaseClusters.clear();
  m_compositionBaseSelection = {};
  m_compositionStartByte = 0;
  m_compositionEndByte = 0;
  return {};
}

auto TextEditingBuffer::SegmentAndValidate(const NGIN::Text::String &value)
    -> UIResult<std::vector<GraphemeCluster>> {
  auto boundaries = Utf8Boundaries(value);
  if (!boundaries) {
    return std::move(boundaries).Error();
  }

  auto clusters = m_segmenter->Segment(value);
  if (!clusters) {
    return std::move(clusters).Error();
  }

  UIntSize expectedOffset = 0;
  for (const auto &cluster : clusters.Value()) {
    if (cluster.byteOffset != expectedOffset || cluster.byteLength == 0 ||
        cluster.byteLength > value.Size() - cluster.byteOffset ||
        !boundaries.Value()[cluster.byteOffset] ||
        !boundaries.Value()[cluster.byteOffset + cluster.byteLength]) {
      return MakeUIError(UIErrorCode::InvalidState,
                         "Grapheme segmenter returned an invalid partition",
                         "NGIN.UI", "TextEditingBuffer::Segment");
    }
    expectedOffset += cluster.byteLength;
  }

  if (expectedOffset != value.Size()) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Grapheme segmenter did not cover the complete text",
                       "NGIN.UI", "TextEditingBuffer::Segment");
  }

  return std::move(clusters).Value();
}
} // namespace NGIN::UI
