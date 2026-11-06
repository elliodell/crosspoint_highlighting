#pragma once

#include <memory>

class Epub;
class Section;
class GfxRenderer;
struct ReaderRenderSpec;

namespace highlights {
// Caller owns the render lock. Extends the five-page window to whole outer
// paragraphs within each chapter; saves complete exports, never a truncated window.
bool capture(const std::shared_ptr<Epub>& epub, Section& current, int spine, GfxRenderer& renderer,
             const ReaderRenderSpec& spec);
}  // namespace highlights
