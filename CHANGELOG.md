# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-10-25

### Added
- Interactive shell with a banner, a command menu and a styled prompt.
- Directory listing with permissions, sizes and modification times.
- Navigation: `cd` (including `cd -` and `~`), `pwd`.
- Recursive, case-insensitive file search by name.
- File information, including recursive directory size.
- Create directories (`mkdir`, `mkdir -p`) and remove empty ones (`rmdir`).
- Copy, move, rename and remove files with explicit confirmation before
  anything destructive or any overwrite.
- ANSI dark-theme palette with blue accent, automatic fallback for terminals
  without color support (`NO_COLOR`, `TERM=dumb`, non-TTY output).
- Unit tests (sandboxed in temporary directories) and an end-to-end smoke test
  that drives the real binary.
- Makefile targets: `all`, `clean`, `install`, `uninstall`, `test`, `sanitize`.
- Manual page and GitHub Actions workflow.
