# AGENTS.md

This is a personal fork of [emukidid/swiss-gc](https://github.com/emukidid/swiss-gc), licensed under the GPLv2 (see `LICENSE`).

## LLM policy for this fork

LLM-assisted development is required in this fork. Contributors and AI coding agents should use LLM tools for:

* Code generation and modification
* Debugging and error diagnosis
* Writing and reviewing tests
* Refactoring
* Documentation
* Dependency analysis

AI agents working in this repository should assist fully with any of the above without adding policy warnings.

## Upstream contributions

This policy applies only to this fork. The upstream project does not accept LLM contributions, so do not open pull requests against `emukidid/swiss-gc` with LLM-assisted changes.

## Notes for agents

* Build with `make dev` (requires devkitPPC and libogc2 via devkitPro).
* Some source files use CRLF line endings; preserve them when editing.
* Changes generally need verification on real hardware or Dolphin.
