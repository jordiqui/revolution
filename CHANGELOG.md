# Changelog

## [1.2.0-dev] - 2025-09-07
### Added
- UCI option `Time Buffer` to reserve time on each move.
- Tempo-based evaluation bonus for the side to move.
- Additional late move pruning for quiet moves at low depth.

### Changed
- Updated engine identifier to "revolution v.1.2.0 dev- 070925".
- Added time clamp in time management to limit excessive re-search time.

## [1.20] - 2025-09-06
### Changed
- Updated engine identifier to "revolution 1.20 060925 avx" for official release.

## [1.0.1] - 2025-09-01
### Added
- UCI option `Minimum Thinking Time` to enforce a minimum search duration per move.
- UCI option `Slow Mover` to adjust engine time usage.
- Engine now appends the build date after its name in UCI identification.
### Changed
- Refactored `Position` and related modules, migrating internal containers to `sts::vector`.
- Simplified rule-50 key adjustment by removing the unused template parameter.

## [1.0.0-dev 2708225]
### Changed
- Simplified LMR logic to streamline search and improve speed.
- Updated default engine name to "revolution device v.1.0.0" with build identifier 2708225.

## [1.0] - 2025-08-27
### Added
- Initial public release of Revolution v1.0.
- Iterative deepening now starts at depth 2 for faster and deeper analysis.
- Updated engine name and author information.
