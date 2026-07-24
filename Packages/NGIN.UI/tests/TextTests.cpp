#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/TextEditing.hpp>

namespace {
class TestGraphemeSegmenter final : public NGIN::UI::IGraphemeSegmenter {
public:
  bool fail{false};

  auto Segment(const NGIN::Text::String &text) noexcept
      -> NGIN::UI::UIResult<std::vector<NGIN::UI::GraphemeCluster>> override {
    if (fail) {
      return NGIN::UI::MakeUIError(NGIN::UI::UIErrorCode::TextShapingFailed,
                                   "Injected grapheme segmentation failure",
                                   "NGIN.UI.Tests",
                                   "TestGraphemeSegmenter::Segment");
    }

    std::vector<NGIN::UI::GraphemeCluster> clusters;
    NGIN::UIntSize offset = 0;
    while (offset < text.Size()) {
      const auto first = static_cast<NGIN::UInt8>(text[offset]);
      NGIN::UIntSize length = 1;
      NGIN::UInt32 codePoint = first;
      if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        codePoint = first & 0x1FU;
      } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        codePoint = first & 0x0FU;
      } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        codePoint = first & 0x07U;
      }
      for (NGIN::UIntSize index = 1; index < length; ++index) {
        codePoint = (codePoint << 6U) |
                    (static_cast<NGIN::UInt8>(text[offset + index]) & 0x3FU);
      }

      const bool combiningMark = codePoint >= 0x0300U && codePoint <= 0x036FU;
      if (combiningMark && !clusters.empty()) {
        clusters.back().byteLength += length;
      } else {
        clusters.push_back({.byteOffset = offset, .byteLength = length});
      }
      offset += length;
    }
    return clusters;
  }
};

class BrokenGraphemeSegmenter final : public NGIN::UI::IGraphemeSegmenter {
public:
  auto Segment(const NGIN::Text::String &) noexcept
      -> NGIN::UI::UIResult<std::vector<NGIN::UI::GraphemeCluster>> override {
    return std::vector<NGIN::UI::GraphemeCluster>{
        {.byteOffset = 1, .byteLength = 1},
    };
  }
};
} // namespace

TEST_CASE("text editing uses grapheme clusters for caret and deletion") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  REQUIRE(buffer
              .Reset(NGIN::Text::String{"Ae\xCC\x81"
                                        "B"})
              .HasValue());
  REQUIRE(buffer.Clusters().size() == 3);
  REQUIRE(buffer.Clusters()[1].byteLength == 3);
  REQUIRE(buffer.State().caretCluster == 3);

  REQUIRE(buffer.MoveCaretTo(2).HasValue());
  REQUIRE(buffer.DeleteBackward().HasValue());
  REQUIRE(buffer.Value() == NGIN::Text::String{"AB"});
  REQUIRE(buffer.State().selection == TextRange{1, 0});
  REQUIRE(buffer.State().caretCluster == 1);

  REQUIRE(buffer.DeleteForward().HasValue());
  REQUIRE(buffer.Value() == NGIN::Text::String{"A"});
  REQUIRE(buffer.State().caretCluster == 1);
}

TEST_CASE("text editing replacement and selection remain cluster indexed") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  REQUIRE(buffer
              .Reset(NGIN::Text::String{"Ae\xCC\x81"
                                        "B"})
              .HasValue());
  REQUIRE(buffer.SetSelection(TextRange{1, 1}).HasValue());
  REQUIRE(buffer.ReplaceSelection(NGIN::Text::String{"Z"}).HasValue());
  REQUIRE(buffer.Value() == NGIN::Text::String{"AZB"});
  REQUIRE(buffer.State().caretCluster == 2);

  REQUIRE(buffer.MoveCaretTo(3).HasValue());
  REQUIRE(buffer.MoveCaretTo(1, true).HasValue());
  REQUIRE(buffer.State().selection == TextRange{1, 2});
  REQUIRE(buffer.State().caretCluster == 1);

  buffer.SelectAll();
  REQUIRE(buffer.State().selection == TextRange{0, 3});
}

TEST_CASE("text editing rejects malformed UTF-8 transactionally") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  REQUIRE(buffer.Reset(NGIN::Text::String{"safe"}).HasValue());
  REQUIRE(buffer.MoveCaretTo(2).HasValue());

  const NGIN::Text::String malformed{"\xC0\xAF", 2};
  const auto result = buffer.ReplaceSelection(malformed);
  REQUIRE_FALSE(result.HasValue());
  REQUIRE(result.Error().code == UIErrorCode::InvalidArgument);
  REQUIRE(buffer.Value() == NGIN::Text::String{"safe"});
  REQUIRE(buffer.State().caretCluster == 2);
  REQUIRE(buffer.State().selection == TextRange{2, 0});
}

TEST_CASE("text editing validates grapheme partitions before mutation") {
  using namespace NGIN::UI;

  BrokenGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  const auto result = buffer.Reset(NGIN::Text::String{"A"});
  REQUIRE_FALSE(result.HasValue());
  REQUIRE(result.Error().code == UIErrorCode::InvalidState);
  REQUIRE(buffer.Value().Empty());
  REQUIRE(buffer.Clusters().empty());
}

TEST_CASE("text editing deletion is transactional when segmentation fails") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  REQUIRE(buffer.Reset(NGIN::Text::String{"safe"}).HasValue());
  REQUIRE(buffer.MoveCaretTo(2).HasValue());
  segmenter.fail = true;

  const auto result = buffer.DeleteBackward();
  REQUIRE_FALSE(result.HasValue());
  REQUIRE(result.Error().code == UIErrorCode::TextShapingFailed);
  REQUIRE(buffer.Value() == NGIN::Text::String{"safe"});
  REQUIRE(buffer.State().selection == TextRange{2, 0});
  REQUIRE(buffer.State().caretCluster == 2);
}
