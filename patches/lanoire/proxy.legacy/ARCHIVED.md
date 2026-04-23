# Archived: legacy C d3d9 proxy

This directory is the pre-2026-04 hand-written C d3d9 proxy for LA Noire. It
hardcoded matrix registers (`c4-c6=ModelView`, `c12-c15=MVP`) that do not
match LA Noire's dynamic-register `AbstractDevice` reality, and its
concatenated-MVP capture path produced ~176k/frame "singular view basis
rejected" errors in `ffp_proxy.log`.

Superseded by the canonical C++20 remix-comp-proxy port in
[../src/](../src/), which captures separate World / View / Projection via
LA Noire-specific hooks on `SetCameraMatrices`, `SetShaderConstant`, and
`GetConstantByName`. See
[../../../patches/lanoire/findings.txt](../findings.txt) for the reverse
engineering, and the plan at
`~/.claude/plans/review-this-workspace-and-nifty-mist.md`.

Kept only for reference and rollback. Do not edit, build, or deploy from
here.
