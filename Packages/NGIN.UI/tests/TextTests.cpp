#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
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

TEST_CASE("text editing composition updates remain transient and cancellable") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  REQUIRE(buffer.Reset(NGIN::Text::String{"AB"}).HasValue());
  REQUIRE(buffer.SetSelection(TextRange{1, 1}).HasValue());

  const NGIN::Text::String composed{"e\xCC\x81"};
  REQUIRE(buffer.UpdateComposition(composed, composed.Size(), 0).HasValue());
  REQUIRE(buffer.HasComposition());
  REQUIRE(buffer.Value() == NGIN::Text::String{"Ae\xCC\x81"});
  REQUIRE(buffer.State().composition == TextRange{1, 1});
  REQUIRE(buffer.State().selection == TextRange{2, 0});

  REQUIRE(buffer.UpdateComposition(NGIN::Text::String{"Z"}, 1, 0).HasValue());
  REQUIRE(buffer.Value() == NGIN::Text::String{"AZ"});
  REQUIRE(buffer.CancelComposition().HasValue());
  REQUIRE_FALSE(buffer.HasComposition());
  REQUIRE(buffer.Value() == NGIN::Text::String{"AB"});
  REQUIRE(buffer.State().selection == TextRange{1, 1});
}

TEST_CASE("text editing composition commits against its original selection") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  TextEditingBuffer buffer{segmenter};
  REQUIRE(buffer.Reset(NGIN::Text::String{"AB"}).HasValue());
  REQUIRE(buffer.SetSelection(TextRange{1, 1}).HasValue());
  REQUIRE(buffer.UpdateComposition(NGIN::Text::String{"candidate"}, 9, 0)
              .HasValue());
  REQUIRE(buffer.CommitComposition(NGIN::Text::String{"Q"}).HasValue());
  REQUIRE_FALSE(buffer.HasComposition());
  REQUIRE(buffer.Value() == NGIN::Text::String{"AQ"});
  REQUIRE(buffer.State().selection == TextRange{2, 0});

  const NGIN::Text::String multiByte{"\xC3\xA9"};
  const auto invalid = buffer.UpdateComposition(multiByte, 1, 0);
  REQUIRE_FALSE(invalid.HasValue());
  REQUIRE(invalid.Error().code == UIErrorCode::InvalidArgument);
  REQUIRE(buffer.Value() == NGIN::Text::String{"AQ"});
}

TEST_CASE("semantic text field edits bindings and retains its session") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"Hi"}};
  Composer composer;
  composer.TextField(Bind(value), segmenter, {}, "name");

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const auto firstStats = reconciler.Reconcile(composer.Declarations());
  REQUIRE(firstStats.created == 1);
  const auto field = tree.Get(tree.Root())->children.front();
  auto *node = tree.Get(field);
  REQUIRE(node != nullptr);
  REQUIRE(node->textField.editing);
  const auto retainedSession = node->textField.editing;

  Testing::TestPlatformBackend platform;
  REQUIRE(platform
              .Initialize(PlatformInitInfo{
                  .applicationName = NGIN::Text::String{"Text tests"},
              })
              .HasValue());
  InputRouter input{tree, &platform};
  REQUIRE(input.SetFocus(field));

  const auto typed = input.Route(TextInput{
      .text = NGIN::Text::String{"!"},
  });
  REQUIRE(typed.handled);
  REQUIRE(typed.layoutStateChanged);
  REQUIRE(value.Get() == NGIN::Text::String{"Hi!"});
  REQUIRE(node->textField.editing->Value() == NGIN::Text::String{"Hi!"});

  Composer recomposed;
  recomposed.TextField(Bind(value), segmenter, {}, "name");
  const auto secondStats = reconciler.Reconcile(recomposed.Declarations());
  REQUIRE(secondStats.preserved == 1);
  node = tree.Get(field);
  REQUIRE(node->textField.editing == retainedSession);
  REQUIRE(node->textField.editing->State().caretCluster == 3);
}

TEST_CASE("text field composition changes binding only when committed") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"A"}};
  Composer composer;
  composer.TextField(Bind(value), segmenter);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto field = tree.Get(tree.Root())->children.front();
  InputRouter input{tree};
  REQUIRE(input.SetFocus(field));

  const NGIN::Text::String candidate{"e\xCC\x81"};
  const auto composing = input.Route(TextComposition{
      .text = candidate,
      .selectionStart = candidate.Size(),
  });
  REQUIRE(composing.handled);
  REQUIRE(composing.layoutStateChanged);
  REQUIRE(value.Get() == NGIN::Text::String{"A"});
  REQUIRE(tree.Get(field)->textField.editing->HasComposition());
  REQUIRE(tree.Get(field)->textField.editing->Value() ==
          NGIN::Text::String{"Ae\xCC\x81"});

  const auto session = tree.Get(field)->textField.editing;
  Composer recomposed;
  recomposed.TextField(Bind(value), segmenter);
  static_cast<void>(reconciler.Reconcile(recomposed.Declarations()));
  REQUIRE(tree.Get(field)->textField.editing == session);
  REQUIRE(tree.Get(field)->textField.editing->HasComposition());
  REQUIRE(tree.Get(field)->textField.editing->Value() ==
          NGIN::Text::String{"Ae\xCC\x81"});

  const auto committed = input.Route(TextInput{
      .text = NGIN::Text::String{"E"},
  });
  REQUIRE(committed.handled);
  REQUIRE(value.Get() == NGIN::Text::String{"AE"});
  REQUIRE_FALSE(tree.Get(field)->textField.editing->HasComposition());
}

TEST_CASE("text field restores composition when commit validation fails") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"A"}};
  auto validated = Bind(value).WithValidation(
      [](const NGIN::Text::String &candidate) -> UIResult<void> {
        if (candidate.Size() > 1) {
          return MakeUIError(UIErrorCode::InvalidArgument, "Text is too long",
                             "NGIN.UI.Tests", "Validate");
        }
        return {};
      });
  Composer composer;
  composer.TextField(std::move(validated), segmenter);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto field = tree.Get(tree.Root())->children.front();
  InputRouter input{tree};
  REQUIRE(input.SetFocus(field));
  REQUIRE(input
              .Route(TextComposition{
                  .text = NGIN::Text::String{"x"},
                  .selectionStart = 1,
              })
              .handled);

  const auto rejected = input.Route(TextInput{
      .text = NGIN::Text::String{"long"},
  });
  REQUIRE(rejected.handled);
  REQUIRE(value.Get() == NGIN::Text::String{"A"});
  REQUIRE(tree.Get(field)->textField.editing->HasComposition());
  REQUIRE(tree.Get(field)->textField.editing->Value() ==
          NGIN::Text::String{"Ax"});
}

TEST_CASE("text field focus coordinates platform IME lifecycle") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"text"}};
  Composer composer;
  composer.TextField(Bind(value), segmenter);
  composer.Button([] {});

  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto field = tree.Get(tree.Root())->children[0];
  const auto button = tree.Get(tree.Root())->children[1];
  tree.Get(field)->arrangedBounds = Rect{1.0F, 2.0F, 30.0F, 10.0F};

  Testing::TestPlatformBackend platform;
  REQUIRE(platform
              .Initialize(PlatformInitInfo{
                  .applicationName = NGIN::Text::String{"Text tests"},
              })
              .HasValue());
  auto created = platform.CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"IME"},
      .title = NGIN::Text::String{"IME"},
  });
  REQUIRE(created.HasValue());

  InputRouter input{tree, &platform, created.Value(), 2.0F};
  REQUIRE(input.SetFocus(field));
  REQUIRE(platform.Windows().front().textInputActive);
  REQUIRE(platform.Windows().front().textInputRect == PixelRect{2, 4, 60, 20});

  REQUIRE(
      tree.Get(field)
          ->textField.editing->UpdateComposition(NGIN::Text::String{"x"}, 1, 0)
          .HasValue());
  REQUIRE(input.SetFocus(button));
  REQUIRE_FALSE(platform.Windows().front().textInputActive);
  REQUIRE_FALSE(tree.Get(field)->textField.editing->HasComposition());
  REQUIRE(tree.Get(field)->textField.editing->Value() ==
          NGIN::Text::String{"text"});

  REQUIRE(input.SetFocus(field));
  REQUIRE(platform.Windows().front().textInputActive);
  Composer empty;
  static_cast<void>(reconciler.Reconcile(empty.Declarations()));
  input.Synchronize();
  REQUIRE_FALSE(platform.Windows().front().textInputActive);
}

TEST_CASE("text field clipboard and keyboard commands are cluster aware") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"Ae\xCC\x81"
                                                     "B"}};
  Composer composer;
  composer.TextField(Bind(value), segmenter);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto field = tree.Get(tree.Root())->children.front();

  Testing::TestPlatformBackend platform;
  REQUIRE(platform
              .Initialize(PlatformInitInfo{
                  .applicationName = NGIN::Text::String{"Text tests"},
              })
              .HasValue());
  InputRouter input{tree, &platform};
  REQUIRE(input.SetFocus(field));

  const auto command = static_cast<NGIN::UInt32>(KeyModifierFlags::Control);
  REQUIRE(input
              .Route(KeyChanged{
                  .logicalKey = static_cast<NGIN::UInt32>('A'),
                  .state = KeyState::Pressed,
                  .modifiers = command,
              })
              .handled);
  REQUIRE(input
              .Route(KeyChanged{
                  .logicalKey = static_cast<NGIN::UInt32>('C'),
                  .state = KeyState::Pressed,
                  .modifiers = command,
              })
              .handled);
  REQUIRE(platform.ClipboardText() == value.Get());

  REQUIRE(platform.SetClipboardText(NGIN::Text::String{"Paste"}).HasValue());
  REQUIRE(input
              .Route(KeyChanged{
                  .logicalKey = static_cast<NGIN::UInt32>('V'),
                  .state = KeyState::Pressed,
                  .modifiers = command,
              })
              .handled);
  REQUIRE(value.Get() == NGIN::Text::String{"Paste"});

  REQUIRE(
      input
          .Route(KeyChanged{
              .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Backspace),
              .state = KeyState::Pressed,
          })
          .handled);
  REQUIRE(value.Get() == NGIN::Text::String{"Past"});
}

TEST_CASE("text field rolls back edits rejected by binding validation") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"safe"}};
  UIError reported{};
  bool errorReported = false;
  auto validated = Bind(value).WithValidation(
      [](const NGIN::Text::String &candidate) -> UIResult<void> {
        if (candidate.Size() > 4) {
          return MakeUIError(UIErrorCode::InvalidArgument, "Text is too long",
                             "NGIN.UI.Tests", "Validate");
        }
        return {};
      });
  NodeProperties properties{};
  properties.textField.onError = [&](const UIError &error) {
    reported = error;
    errorReported = true;
  };

  Composer composer;
  composer.TextField(std::move(validated), segmenter, properties);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto field = tree.Get(tree.Root())->children.front();
  InputRouter input{tree};
  REQUIRE(input.SetFocus(field));

  const auto result = input.Route(TextInput{
      .text = NGIN::Text::String{"!"},
  });
  REQUIRE(result.handled);
  REQUIRE(result.callbackInvoked);
  REQUIRE(errorReported);
  REQUIRE(reported.code == UIErrorCode::InvalidArgument);
  REQUIRE(value.Get() == NGIN::Text::String{"safe"});
  REQUIRE(tree.Get(field)->textField.editing->Value() ==
          NGIN::Text::String{"safe"});
  REQUIRE(tree.Get(field)->textField.editing->State().caretCluster == 4);
}

TEST_CASE("password text fields omit values from semantic output") {
  using namespace NGIN::UI;

  TestGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"secret"}};
  NodeProperties properties{};
  properties.textField.password = true;
  properties.semantics.label = NGIN::Text::String{"Password"};

  Composer composer;
  composer.TextField(Bind(value), segmenter, properties);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto field = tree.Get(tree.Root())->children.front();

  const auto semantics = BuildSemanticTree(tree);
  const auto *semantic = semantics.FindByOwner(tree.Get(field)->id);
  REQUIRE(semantic != nullptr);
  REQUIRE(semantic->role == SemanticRole::TextBox);
  REQUIRE(semantic->label == NGIN::Text::String{"Password"});
  REQUIRE(semantic->value.Empty());
}
