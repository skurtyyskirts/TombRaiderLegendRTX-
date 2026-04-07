# Static Analyzer Agent Memory Index

- [project_trl_architecture.md](project_trl_architecture.md) - TRL renderer VS constant register layout and matrix upload pipeline
- [project_trl_draw_pipeline.md](project_trl_draw_pipeline.md) - Two-phase render pipeline (queue vs flush), why DIP backtraces point to flush but culling is in queue phase
- [project_trl_culling_gates.md](project_trl_culling_gates.md) - Complete map of 14 culling gates in draw pipeline, 4 patched, 10 unpatched with priority ranking
- [project_trl_streaming.md](project_trl_streaming.md) - Mesh streaming system: 94-slot Object Tracker causes draw drops at distance, not culling jumps
- [project_trl_portal_traversal.md](project_trl_portal_traversal.md) - Portal traversal root cause: negative sector bounds at 0x46D1E0/E5/EA, 15-byte patch to force fullscreen
