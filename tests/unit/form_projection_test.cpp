#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/editor/form_projection.hpp"

TEST_CASE("form projection exposes adapter metadata and current values", "[editor]") {
    const jrpgmaker::project::DocumentAdapter adapter{
        .type_id = "test.document",
        .fields = {{"/name", "string", "editor.test.name", "text", true, false},
                   {"/missing", "integer", "editor.test.missing", "number", false, true}}};
    const auto projection = jrpgmaker::editor::BuildFormProjection(
        adapter, nlohmann::json{{"name", "sample"}});

    REQUIRE(projection.document_id == "test.document");
    REQUIRE(projection.fields.size() == 2);
    REQUIRE(projection.fields[0].value == "sample");
    REQUIRE(projection.fields[0].label_key == "editor.test.name");
    REQUIRE(projection.fields[1].value.is_null());
    REQUIRE(projection.fields[1].read_only);
}
