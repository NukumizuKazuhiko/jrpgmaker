#pragma once

#include <RmlUi/Core.h>
#include <RmlUi/Core/TextInputHandler.h>

#include <string_view>

namespace jrpgmaker::editor {

// Bridges SDL text-editing events to RmlUi's active text input context. It
// only owns transient composition state; committed text remains in the
// editor controller and project workspace.
class RmlUiTextInputHandler final : public Rml::TextInputHandler {
public:
    void OnActivate(Rml::TextInputContext* input_context) override;
    void OnDeactivate(Rml::TextInputContext* input_context) override;
    void OnDestroy(Rml::TextInputContext* input_context) override;

    void HandleEdit(std::string_view text, int start, int length);

private:
    Rml::TextInputContext* context_ = nullptr;
    int composition_start_ = 0;
    int composition_end_ = 0;
};

} // namespace jrpgmaker::editor
