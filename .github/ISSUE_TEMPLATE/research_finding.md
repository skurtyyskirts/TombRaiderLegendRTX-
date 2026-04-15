---
name: Research finding
about: New reverse engineering discovery — address, culling layer, struct field, or engine behavior
title: "[RE] "
labels: research
assignees: ''
---

## Finding Summary

<!-- One sentence: what did you discover? -->

## Evidence

<!-- How did you find this? Ghidra decompile, livetools trace, dx9tracer capture, static analysis, etc. -->

| Tool | Command / Method | Output |
|------|-----------------|--------|
| | | |

## Address(es)

```
0xADDR  FunctionName — description
```

## Impact on the Project

<!-- Does this unblock a culling layer? Affect hash stability? Change how matrix recovery works? -->

- [ ] Adds a new culling layer to the map
- [ ] Fixes a known culling layer patch
- [ ] Affects hash stability
- [ ] Other:

## Suggested Patch

<!-- If you have a proposed fix: address, bytes to write, expected effect -->

```
Address:  0x______
Before:   XX XX XX XX
After:    XX XX XX XX
Effect:
```

## Reproduction

<!-- How can someone else verify this finding? Tool command, game state, etc. -->

## References

<!-- WHITEBOARD.md section, CHANGELOG entry, prior build numbers, external sources -->
