// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/mathml/math_padded_layout_algorithm.h"

#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/layout/mathml/math_layout_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_break_token.h"
#include "third_party/blink/renderer/core/layout/ng/ng_box_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_out_of_flow_layout_part.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/core/mathml_names.h"

namespace blink {

MathPaddedLayoutAlgorithm::MathPaddedLayoutAlgorithm(
    const NGLayoutAlgorithmParams& params)
    : NGLayoutAlgorithm(params) {}

LayoutUnit MathPaddedLayoutAlgorithm::RequestedLSpace() const {
  return std::max(LayoutUnit(),
                  ValueForLength(Style().GetMathLSpace(), LayoutUnit()));
}

LayoutUnit MathPaddedLayoutAlgorithm::RequestedVOffset() const {
  return ValueForLength(Style().GetMathPaddedVOffset(), LayoutUnit());
}

absl::optional<LayoutUnit> MathPaddedLayoutAlgorithm::RequestedAscent(
    LayoutUnit content_ascent) const {
  if (Style().GetMathBaseline().IsAuto())
    return absl::nullopt;
  return std::max(LayoutUnit(),
                  ValueForLength(Style().GetMathBaseline(), content_ascent));
}

absl::optional<LayoutUnit> MathPaddedLayoutAlgorithm::RequestedDescent(
    LayoutUnit content_descent) const {
  if (Style().GetMathPaddedDepth().IsAuto())
    return absl::nullopt;
  return std::max(LayoutUnit(), ValueForLength(Style().GetMathPaddedDepth(),
                                               content_descent));
}

void MathPaddedLayoutAlgorithm::GetContentAsAnonymousMrow(
    NGBlockNode* content) const {
  // Node() is a LayoutNGMathMLBlockWithAnonymousMrow node, which is either
  // empty or contains a single anonymous mrow child.
  if (NGLayoutInputNode child = Node().FirstChild()) {
    DCHECK(!child.NextSibling());
    DCHECK(!child.IsOutOfFlowPositioned());
    *content = To<NGBlockNode>(child);
  }
}

const NGLayoutResult* MathPaddedLayoutAlgorithm::Layout() {
  DCHECK(!BreakToken());

  NGBlockNode content = nullptr;
  GetContentAsAnonymousMrow(&content);
  LayoutUnit content_ascent, content_descent;
  BoxStrut content_margins;
  const NGLayoutResult* content_layout_result = nullptr;
  if (content) {
    NGConstraintSpace constraint_space = CreateConstraintSpaceForMathChild(
        Node(), ChildAvailableSize(), ConstraintSpace(), content);
    content_layout_result = content.Layout(constraint_space);
    const auto& content_fragment =
        To<NGPhysicalBoxFragment>(content_layout_result->PhysicalFragment());
    content_margins =
        ComputeMarginsFor(constraint_space, content.Style(), ConstraintSpace());
    NGBoxFragment fragment(ConstraintSpace().GetWritingDirection(),
                           content_fragment);
    content_ascent = content_margins.block_start +
                     fragment.FirstBaseline().value_or(fragment.BlockSize());
    content_descent =
        fragment.BlockSize() + content_margins.BlockSum() - content_ascent;
  }
  // width/height/depth attributes can override width/ascent/descent.
  LayoutUnit ascent = BorderScrollbarPadding().block_start +
                      RequestedAscent(content_ascent).value_or(content_ascent);
  container_builder_.SetBaselines(ascent);
  LayoutUnit descent =
      RequestedDescent(content_descent).value_or(content_descent) +
      BorderScrollbarPadding().block_end;
  if (content_layout_result) {
    // Need to take into account border/padding, lspace and voffset.
    LogicalOffset content_offset = {
        BorderScrollbarPadding().inline_start + RequestedLSpace(),
        (ascent - content_ascent) - RequestedVOffset()};
    container_builder_.AddResult(*content_layout_result, content_offset,
                                 content_margins);
  }

  LayoutUnit intrinsic_block_size = ascent + descent;
  LayoutUnit block_size = ComputeBlockSizeForFragment(
      ConstraintSpace(), Style(), BorderPadding(), intrinsic_block_size,
      container_builder_.InitialBorderBoxSize().inline_size);

  container_builder_.SetIntrinsicBlockSize(intrinsic_block_size);
  container_builder_.SetFragmentsTotalBlockSize(block_size);

  NGOutOfFlowLayoutPart(Node(), ConstraintSpace(), &container_builder_).Run();

  return container_builder_.ToBoxFragment();
}

MinMaxSizesResult MathPaddedLayoutAlgorithm::ComputeMinMaxSizes(
    const MinMaxSizesFloatInput&) {
  if (auto result = CalculateMinMaxSizesIgnoringChildren(
          Node(), BorderScrollbarPadding()))
    return *result;


  NGBlockNode content = nullptr;
  GetContentAsAnonymousMrow(&content);

  const auto content_result = ComputeMinAndMaxContentContributionForMathChild(
      Style(), ConstraintSpace(), content, ChildAvailableSize().block_size);

  bool depends_on_block_constraints =
      content_result.depends_on_block_constraints;
  MinMaxSizes sizes;
  sizes += content_result.sizes;

  sizes += BorderScrollbarPadding().InlineSum();
  return MinMaxSizesResult(sizes, depends_on_block_constraints);
}

}  // namespace blink
