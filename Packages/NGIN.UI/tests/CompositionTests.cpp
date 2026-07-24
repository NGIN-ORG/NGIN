#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

#include <array>

namespace {
[[nodiscard]] auto Child(const NGIN::UI::RuntimeTree &tree,
                         const NGIN::UI::ElementHandle parent,
                         const NGIN::UIntSize index)
    -> const NGIN::UI::RuntimeNode * {
  const auto *node = tree.Get(parent);
  REQUIRE(node != nullptr);
  REQUIRE(index < node->children.size());
  return tree.Get(node->children[index]);
}
} // namespace

TEST_CASE("composer produces balanced nested declarations with RAII scopes") {
  using namespace NGIN::UI;

  Composer composer;
  composer.Column(
      [&] {
        composer.Leaf(ElementType::Text, "title");
        composer.Row([&] {
          composer.Leaf(ElementType::Button, "cancel");
          composer.Leaf(ElementType::Button, "save");
        });
      },
      "settings");

  REQUIRE(composer.IsBalanced());
  REQUIRE(composer.Declarations().size() == 1);
  REQUIRE(composer.Declarations().front().type == ElementType::Column);
  REQUIRE(composer.Declarations().front().children.size() == 2);
  REQUIRE(composer.Declarations().front().children[1].children.size() == 2);
}

TEST_CASE("static children preserve identity by parent type and ordinal") {
  using namespace NGIN::UI;

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array initial{
      ElementDeclaration{ElementType::Text},
      ElementDeclaration{ElementType::Button},
  };
  const auto first = reconciler.Reconcile(initial);
  REQUIRE(first.created == 2);
  REQUIRE(tree.LiveCount() == 3);

  const auto textHandle = Child(tree, tree.Root(), 0)->handle;
  const auto buttonHandle = Child(tree, tree.Root(), 1)->handle;
  const auto textId = Child(tree, tree.Root(), 0)->id;
  const auto buttonId = Child(tree, tree.Root(), 1)->id;

  const auto second = reconciler.Reconcile(initial);
  REQUIRE(second.preserved == 2);
  REQUIRE(Child(tree, tree.Root(), 0)->handle == textHandle);
  REQUIRE(Child(tree, tree.Root(), 1)->handle == buttonHandle);
  REQUIRE(Child(tree, tree.Root(), 0)->id == textId);
  REQUIRE(Child(tree, tree.Root(), 1)->id == buttonId);
}

TEST_CASE("keyed sibling reordering preserves runtime identity") {
  using namespace NGIN::UI;

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array firstOrder{
      ElementDeclaration{ElementType::Button, "first"},
      ElementDeclaration{ElementType::Button, "second"},
      ElementDeclaration{ElementType::Button, "third"},
  };
  REQUIRE(reconciler.Reconcile(firstOrder).created == 3);

  const auto first = Child(tree, tree.Root(), 0)->handle;
  const auto second = Child(tree, tree.Root(), 1)->handle;
  const auto third = Child(tree, tree.Root(), 2)->handle;

  const std::array secondOrder{
      ElementDeclaration{ElementType::Button, "third"},
      ElementDeclaration{ElementType::Button, "first"},
      ElementDeclaration{ElementType::Button, "second"},
  };
  const auto stats = reconciler.Reconcile(secondOrder);

  REQUIRE(stats.preserved == 3);
  REQUIRE(stats.created == 0);
  REQUIRE(stats.removed == 0);
  REQUIRE(Child(tree, tree.Root(), 0)->handle == third);
  REQUIRE(Child(tree, tree.Root(), 1)->handle == first);
  REQUIRE(Child(tree, tree.Root(), 2)->handle == second);
}

TEST_CASE("changed key or incompatible type replaces the runtime subtree") {
  using namespace NGIN::UI;

  RuntimeTree tree;
  Reconciler reconciler{tree};

  ElementDeclaration branch{ElementType::Column, "branch"};
  branch.children.emplace_back(ElementType::Text, "child");
  const std::array initial{branch};
  REQUIRE(reconciler.Reconcile(initial).created == 2);
  const auto oldBranch = Child(tree, tree.Root(), 0)->handle;
  const auto oldChild = Child(tree, oldBranch, 0)->handle;

  ElementDeclaration replacement{ElementType::Row, "branch"};
  replacement.children.emplace_back(ElementType::Text, "child");
  const std::array changed{replacement};
  const auto stats = reconciler.Reconcile(changed);

  REQUIRE(stats.created == 2);
  REQUIRE(stats.removed == 2);
  REQUIRE_FALSE(tree.IsAlive(oldBranch));
  REQUIRE_FALSE(tree.IsAlive(oldChild));
  REQUIRE(Child(tree, tree.Root(), 0)->handle != oldBranch);
}

TEST_CASE("removed slots are reused with a new generation and element id") {
  using namespace NGIN::UI;

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array initial{
      ElementDeclaration{ElementType::Button, "old"},
  };
  reconciler.Reconcile(initial);
  const auto oldHandle = Child(tree, tree.Root(), 0)->handle;
  const auto oldId = Child(tree, tree.Root(), 0)->id;

  const std::array replacement{
      ElementDeclaration{ElementType::Button, "new"},
  };
  reconciler.Reconcile(replacement);
  const auto *newNode = Child(tree, tree.Root(), 0);

  REQUIRE(newNode->handle.index == oldHandle.index);
  REQUIRE(newNode->handle.generation != oldHandle.generation);
  REQUIRE(newNode->id != oldId);
  REQUIRE_FALSE(tree.IsAlive(oldHandle));
}
