#include "SettingsPanel.hpp"

#include <NGIN/UI/CustomElement.hpp>
#include <cmath>
#include <memory>
#include <string>

namespace SettingsDemo {
namespace {
using namespace NGIN;
using namespace NGIN::UI;

struct Settings {
  State<Text::String> profile{Text::String{"Player"}};
  State<F32> volume{0.65F};
  State<F32> split{0.55F};
  State<Text::String> applied{Text::String{"Preview"}};

  void Apply() {
    Text::String status = profile.Get();
    if (status.Empty()) {
      status = "Enter a profile name";
    } else {
      status.Append(" at ");
      status.Append(std::to_string(std::lround(volume.Get() * 100)).c_str());
      status.Append("%");
    }
    static_cast<void>(applied.Set(std::move(status)));
  }
};

class LevelMeter final : public ICustomElement {
public:
  explicit LevelMeter(F32 level) : m_level(level) {}

  auto Measure(CustomElementContext &, SizeConstraints constraints)
      -> UIResult<Size> override {
    return constraints.Constrain({160, 18});
  }
  auto Paint(CustomElementContext &, PaintContext &paint)
      -> UIResult<void> override {
    auto bounds = paint.Bounds();
    paint.Fill(bounds, {0.12F, 0.18F, 0.22F, 1});
    bounds.width *= m_level;
    paint.Fill(bounds, {0.24F, 0.76F, 0.52F, 1});
    return {};
  }
  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    return SemanticProperties{
        .role = SemanticRole::ProgressBar,
        .label = "Output level",
        .value =
            Text::String{std::to_string(std::lround(m_level * 100)) + "%"}};
  }

private:
  F32 m_level;
};
} // namespace

auto SettingsPanel() -> NGIN::UI::Views::View {
  using namespace NGIN::UI::Views;
  auto model = std::make_shared<Settings>();
  NodeProperties panel{};
  panel.layout.gap = 12;
  panel.layout.padding = Thickness::Uniform(Dp{16});
  auto fields = Column(
      {
          Label("Audio settings"),
          Label("Profile"),
          TextField("Profile", Bind(model->profile)),
          Label("Master volume"),
          Slider("Master volume", Bind(model->volume),
                 {.minimum = 0, .maximum = 1, .step = 0.01F}),
          Button("Apply", [model] { model->Apply(); }),
      },
      panel);
  auto preview = Column(
      {
          Component([model] { return Label(model->applied.Get()); }),
          Component([model] {
            return Custom(std::make_shared<LevelMeter>(model->volume.Get()));
          }),
      },
      panel);
  return Views::SplitPane(Bind(model->split), std::move(fields),
                          std::move(preview));
}
} // namespace SettingsDemo
