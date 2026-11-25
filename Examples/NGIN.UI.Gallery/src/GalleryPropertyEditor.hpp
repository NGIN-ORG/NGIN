#pragma once

#include <NGIN/UIGallery/Gallery.hpp>

namespace NGIN::UIGallery {
struct PropertyEditorPage final {};

auto RegisterPropertyEditor(UI::PageRegistry &registry,
                            UI::PageRegistrationOptions options,
                            GalleryViewModel &gallery)
    -> UI::UIResult<void>;
} // namespace NGIN::UIGallery
