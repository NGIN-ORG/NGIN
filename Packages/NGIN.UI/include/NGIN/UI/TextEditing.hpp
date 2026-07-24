#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Text.hpp>

#include <vector>

namespace NGIN::UI {
struct TextRange final {
  UIntSize start{0};
  UIntSize length{0};

  [[nodiscard]] constexpr auto End() const noexcept -> UIntSize {
    return start + length;
  }

  [[nodiscard]] constexpr auto Empty() const noexcept -> bool {
    return length == 0;
  }

  [[nodiscard]] constexpr auto
  operator<=>(const TextRange &) const noexcept = default;
};

struct TextEditingState final {
  TextRange selection{};
  TextRange composition{};
  UIntSize caretCluster{0};
  F32 preferredCaretX{0.0F};
  bool revealPassword{false};
};

class TextEditingBuffer final {
public:
  explicit TextEditingBuffer(IGraphemeSegmenter &segmenter) noexcept;

  [[nodiscard]] auto Value() const noexcept -> const NGIN::Text::String &;
  [[nodiscard]] auto State() const noexcept -> const TextEditingState &;
  [[nodiscard]] auto Clusters() const noexcept
      -> const std::vector<GraphemeCluster> &;
  [[nodiscard]] auto SelectedText() const -> NGIN::Text::String;
  [[nodiscard]] auto HasComposition() const noexcept -> bool;
  [[nodiscard]] auto ByteOffsetForCluster(UIntSize cluster) const noexcept
      -> UIntSize;
  [[nodiscard]] auto ClusterForByteOffset(UIntSize byteOffset) const noexcept
      -> UIntSize;

  auto Reset(NGIN::Text::String value) -> UIResult<void>;
  auto SetSelection(TextRange selection) -> UIResult<void>;
  void SelectAll() noexcept;
  auto MoveCaretTo(UIntSize cluster, bool extendSelection = false)
      -> UIResult<void>;
  auto MoveCaretVertically(UIntSize cluster, F32 preferredX,
                           bool extendSelection = false) -> UIResult<void>;
  auto ReplaceSelection(const NGIN::Text::String &text) -> UIResult<void>;
  auto DeleteBackward() -> UIResult<void>;
  auto DeleteForward() -> UIResult<void>;
  auto UpdateComposition(const NGIN::Text::String &text,
                         UIntSize selectionStartByte,
                         UIntSize selectionLengthByte) -> UIResult<void>;
  auto CommitComposition(const NGIN::Text::String &text) -> UIResult<void>;
  auto CancelComposition() noexcept -> UIResult<void>;

private:
  [[nodiscard]] auto ByteOffset(UIntSize cluster) const noexcept -> UIntSize;
  [[nodiscard]] auto ClusterAtOrAfterByte(UIntSize byteOffset) const noexcept
      -> UIntSize;
  auto ReplaceRange(TextRange range, const NGIN::Text::String &text)
      -> UIResult<void>;
  auto CommitCandidate(NGIN::Text::String candidate, UIntSize desiredCaretByte)
      -> UIResult<void>;
  auto SegmentAndValidate(const NGIN::Text::String &value)
      -> UIResult<std::vector<GraphemeCluster>>;

  IGraphemeSegmenter *m_segmenter{nullptr};
  NGIN::Text::String m_value{};
  std::vector<GraphemeCluster> m_clusters{};
  TextEditingState m_state{};
  UIntSize m_selectionAnchor{0};
  bool m_compositionActive{false};
  NGIN::Text::String m_compositionBaseValue{};
  std::vector<GraphemeCluster> m_compositionBaseClusters{};
  TextRange m_compositionBaseSelection{};
  UIntSize m_compositionStartByte{0};
  UIntSize m_compositionEndByte{0};
};
} // namespace NGIN::UI
