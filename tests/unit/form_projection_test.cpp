#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/editor/form_projection.hpp"

TEST_CASE("form projection exposes adapter metadata and current values", "[editor]") {
    const jrpgmaker::project::DocumentAdapter adapter{
        .type_id = "test.document",
        .fields = {{"/name", "string", "editor.test.name", "text", true, false},
                   {"/missing", "integer", "editor.test.missing", "number", false, true}},
        .validate = {},
        .normalize_edit = {}};
    const auto projection = jrpgmaker::editor::BuildFormProjection(
        adapter, nlohmann::json{{"name", "sample"}});

    REQUIRE(projection.document_id == "test.document");
    REQUIRE(projection.fields.size() == 2);
    REQUIRE(projection.fields[0].value == "sample");
    REQUIRE(projection.fields[0].label_key == "editor.test.name");
    REQUIRE(projection.fields[1].value.is_null());
    REQUIRE(projection.fields[1].read_only);
}

TEST_CASE("workspace preview exposes only structured diagnosis metrics", "[editor]") {
    const jrpgmaker::project::DiagnosticSet diagnosis{.diagnostics = {},
                                                       .event_count = 2,
                                                       .interaction_count = 1,
                                                       .collision_count = 3,
                                                       .navigation_width = 8,
                                                       .navigation_height = 6,
                                                       .camera_region_count = 2};
    const auto preview = jrpgmaker::editor::BuildWorkspacePreview(diagnosis);
    REQUIRE(preview.valid);
    REQUIRE(preview.diagnostics.empty());
    REQUIRE(preview.metrics.size() == 6);
    REQUIRE(preview.metrics.front().label_key == "editor.preview.event_count");
    REQUIRE(preview.metrics.front().value == 2);
}

TEST_CASE("workspace preview preserves diagnostics", "[editor]") {
    const jrpgmaker::project::DiagnosticSet diagnosis{
        .diagnostics = {{"project.workspace.invalid", "project.json"}}};
    const auto preview = jrpgmaker::editor::BuildWorkspacePreview(diagnosis);
    REQUIRE_FALSE(preview.valid);
    REQUIRE(preview.metrics.empty());
    REQUIRE(preview.diagnostics.size() == 1);
}
