#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "jrpgmaker/ui/text.hpp"
#include "jrpgmaker/ui/widget.hpp"

using jrpgmaker::ui::Font;
using jrpgmaker::ui::List;
using jrpgmaker::ui::NineSlice;
using jrpgmaker::ui::Padding;
using jrpgmaker::ui::Panel;
using jrpgmaker::ui::Rect;
using jrpgmaker::ui::SliceNine;
using jrpgmaker::ui::TextBlock;
using jrpgmaker::ui::Widget;

namespace {

bool NearlyEqual(float a, float b) {
    return std::abs(a - b) < 1e-4f;
}

std::filesystem::path FindCjkFont() {
#if defined(_WIN32)
    const std::filesystem::path windows = "C:/Windows/Fonts/msgothic.ttc";
    if (std::filesystem::exists(windows)) {
        return windows;
    }
#elif defined(__APPLE__)
    for (const char* candidate :
         {"/System/Library/Fonts/STHeiti Light.ttc", "/System/Library/Fonts/STHeiti Medium.ttc"}) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
#else
    for (const char* candidate : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                  "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
                                  "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf"}) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
#endif
    return {};
}

} // namespace

TEST_CASE("widget tree owns children and tracks parent", "[ui][widget]") {
    class Leaf final : public Widget {
    public:
        explicit Leaf(std::uint32_t id) : Widget(id) {}
        Rect Layout(const Rect& available) override {
            set_rect(available);
            return available;
        }
    };

    auto root = std::make_unique<Leaf>(1u);
    auto child = std::make_unique<Leaf>(2u);
    Widget* child_ptr = child.get();
    root->AddChild(std::move(child));

    REQUIRE(root->id() == 1u);
    REQUIRE(root->children().size() == 1u);
    REQUIRE(child_ptr->parent() == root.get());
    REQUIRE(child_ptr->visible());
    child_ptr->set_visible(false);
    REQUIRE_FALSE(child_ptr->visible());
}

TEST_CASE("nine-slice splits a rect into the expected regions", "[ui][widget][nineslice]") {
    const Rect outer{0.0f, 0.0f, 100.0f, 80.0f};
    const NineSlice slice{.left = 10.0f, .right = 20.0f, .top = 5.0f, .bottom = 15.0f};
    const auto regions = SliceNine(outer, slice);

    // Row-major indices: 0..2 top, 3..5 middle, 6..8 bottom.
    REQUIRE(NearlyEqual(regions[0].width, 10.0f));
    REQUIRE(NearlyEqual(regions[0].height, 5.0f));
    REQUIRE(NearlyEqual(regions[2].width, 20.0f));  // right slice
    REQUIRE(NearlyEqual(regions[6].height, 15.0f)); // bottom slice
    REQUIRE(NearlyEqual(regions[4].width, 70.0f));  // center = 100-10-20
    REQUIRE(NearlyEqual(regions[4].height, 60.0f)); // center = 80-5-15
    // Center region position sits at the left/top slice offsets.
    REQUIRE(NearlyEqual(regions[4].x, 10.0f));
    REQUIRE(NearlyEqual(regions[4].y, 5.0f));
    // Regions tile the outer rect without gaps or overlaps.
    float total_area = 0.0f;
    for (const auto& region : regions) {
        total_area += region.width * region.height;
    }
    REQUIRE(NearlyEqual(total_area, outer.width * outer.height));
}

TEST_CASE("nine-slice clamps slices larger than the target", "[ui][widget][nineslice]") {
    const Rect outer{0.0f, 0.0f, 30.0f, 30.0f};
    const NineSlice slice{.left = 50.0f, .right = 50.0f, .top = 10.0f, .bottom = 10.0f};
    const auto regions = SliceNine(outer, slice);

    // Oversized left+right collapse the center width to zero; the side regions
    // split the full width and the geometry stays non-overlapping.
    float total_area = 0.0f;
    for (const auto& region : regions) {
        REQUIRE(region.width >= 0.0f);
        REQUIRE(region.height >= 0.0f);
        total_area += region.width * region.height;
    }
    REQUIRE(NearlyEqual(total_area, outer.width * outer.height));
}

TEST_CASE("panel lays children into the padded content area", "[ui][widget][panel]") {
    class Fixed final : public Widget {
    public:
        explicit Fixed(std::uint32_t id) : Widget(id) {}
        Rect Layout(const Rect& available) override {
            set_rect(available);
            return available;
        }
    };

    Panel panel(1u, NineSlice{5.0f, 5.0f, 5.0f, 5.0f}, Padding{4.0f, 6.0f, 8.0f, 2.0f});
    auto child = std::make_unique<Fixed>(2u);
    Fixed* child_ptr = child.get();
    panel.AddChild(std::move(child));

    const Rect area = panel.Layout(Rect{10.0f, 20.0f, 100.0f, 50.0f});

    REQUIRE(NearlyEqual(area.width, 100.0f)); // panel fills available
    REQUIRE(NearlyEqual(area.height, 50.0f));
    // Content area is inset by the padding.
    REQUIRE(NearlyEqual(panel.content_rect().x, 14.0f));
    REQUIRE(NearlyEqual(panel.content_rect().y, 28.0f));
    REQUIRE(NearlyEqual(panel.content_rect().width, 90.0f));  // 100-4-6
    REQUIRE(NearlyEqual(panel.content_rect().height, 40.0f)); // 50-8-2
    // The child was laid out into the content area.
    REQUIRE(NearlyEqual(child_ptr->rect().x, 14.0f));
    REQUIRE(NearlyEqual(child_ptr->rect().y, 28.0f));
}

TEST_CASE("list stacks visible children vertically", "[ui][widget][list]") {
    class FixedHeight final : public Widget {
    public:
        FixedHeight(std::uint32_t id, float height) : Widget(id), height_(height) {}
        Rect Layout(const Rect& available) override {
            const Rect result{available.x, available.y, 0.0f, height_};
            set_rect(result);
            return result;
        }

    private:
        float height_;
    };

    List list(1u, 4.0f);
    auto a = std::make_unique<FixedHeight>(2u, 10.0f);
    auto b = std::make_unique<FixedHeight>(3u, 20.0f);
    auto hidden = std::make_unique<FixedHeight>(4u, 100.0f);
    hidden->set_visible(false);
    Widget* a_ptr = a.get();
    Widget* b_ptr = b.get();
    list.AddChild(std::move(a));
    list.AddChild(std::move(b));
    list.AddChild(std::move(hidden));

    const Rect area = list.Layout(Rect{0.0f, 0.0f, 200.0f, 0.0f});

    // a at y=0 (10px), 4px gap, b at y=14 (20px). Total = 10 + 4 + 20 = 34.
    REQUIRE(NearlyEqual(a_ptr->rect().y, 0.0f));
    REQUIRE(NearlyEqual(b_ptr->rect().y, 14.0f));
    REQUIRE(NearlyEqual(area.height, 34.0f));
    REQUIRE(NearlyEqual(area.width, 200.0f));
}

TEST_CASE("text block lays out CJK text and reports its height", "[ui][widget][textblock]") {
    const auto font_path = FindCjkFont();
    if (font_path.empty()) {
        SKIP("no CJK system font found on this host");
    }

    Font font;
    REQUIRE(font.Load(font_path.string()));

    TextBlock block(1u, &font, 24u);
    block.SetText("\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf" // konnichiwa
                  "\xe3\x80\x82");                                               // kuten
    const Rect area = block.Layout(Rect{0.0f, 0.0f, 200.0f, 0.0f});

    REQUIRE(block.rect().width > 0.0f);
    REQUIRE(block.text_layout().height > 0.0f);
    REQUIRE_FALSE(block.text_layout().lines.empty());
    REQUIRE(NearlyEqual(area.width, 200.0f)); // fills available width
}

TEST_CASE("text block with no text collapses to zero size", "[ui][widget][textblock]") {
    Font font;
    TextBlock block(1u, &font, 24u);
    const Rect area = block.Layout(Rect{0.0f, 0.0f, 200.0f, 0.0f});
    REQUIRE(NearlyEqual(area.width, 0.0f));
    REQUIRE(NearlyEqual(area.height, 0.0f));
}

TEST_CASE("text block with unusable font metrics collapses to zero size",
          "[ui][widget][textblock]") {
    Font font;
    TextBlock unloaded(1u, &font, 24u);
    unloaded.SetText("text");
    const Rect unloaded_area = unloaded.Layout(Rect{4.0f, 8.0f, 200.0f, 40.0f});
    REQUIRE(NearlyEqual(unloaded_area.width, 0.0f));
    REQUIRE(NearlyEqual(unloaded_area.height, 0.0f));

    TextBlock zero_height(2u, &font, 0u);
    zero_height.SetText("text");
    const Rect zero_height_area = zero_height.Layout(Rect{4.0f, 8.0f, 200.0f, 40.0f});
    REQUIRE(NearlyEqual(zero_height_area.width, 0.0f));
    REQUIRE(NearlyEqual(zero_height_area.height, 0.0f));
}
