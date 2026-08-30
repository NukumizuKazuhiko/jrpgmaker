#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "jrpgmaker/ui/dialog.hpp"
#include "jrpgmaker/ui/draw_list.hpp"

namespace jrpgmaker::ui {

struct RuntimeOverlayDiagnostic {
    std::string code;
};

struct RuntimeOverlayProjection {
    DrawList draw_list;
    std::vector<RuntimeOverlayDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

// Projects already-resolved runtime presentation state into backend-agnostic
// screen-space text. Domain owns dialog progression; this owner only decides
// which prompt/dialog strings are visible and where they are laid out.
[[nodiscard]] RuntimeOverlayProjection
BuildRuntimeOverlayDrawList(std::string_view prompt_text, const DialogPresentationSnapshot& dialog,
                            Rect viewport, float line_height);

} // namespace jrpgmaker::ui
