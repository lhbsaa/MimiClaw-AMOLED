# Contributing

Thanks for your interest in MimiClaw-AMOLED. This project welcomes focused, high-quality contributions that improve reliability, performance, or documentation.

## About This Project

MimiClaw-AMOLED is a derivative work of [MimiClaw](https://github.com/memovai/mimiclaw) with AMOLED display support for T-Display-S3 AMOLED hardware.

## Scope

We accept contributions in these areas:

- **Display driver improvements** - QSPI, frame buffer, graphics
- **UI/UX enhancements** - New pages, animations, touch support
- **Hardware support** - Battery, touch, sensors
- Core firmware features and bug fixes
- Documentation, examples, and diagrams
- Build and tooling improvements

## Before You Start

- Search existing issues and discussions to avoid duplication.
- Open a short proposal for large or risky changes.
- Keep changes small and reviewable when possible.

## Development Setup

- Install ESP-IDF v5.5+.
- Target hardware: LILYGO T-Display-S3 AMOLED 1.91"
- Build targets are in `idf.py`.
- Default config lives in `main/mimi_secrets.h.example`.

## Branching and Commits

- Use a short, descriptive branch name.
- Keep commit history clean and focused.
- Suggested commit style: `docs: ...`, `fix: ...`, `feat: ...`.

## Pull Requests

- Describe the problem and the solution clearly.
- Include testing steps and results.
- Update documentation when behavior changes.

## Code Style

- Match existing naming and formatting.
- Prefer clarity over cleverness.
- Avoid large refactors mixed with functional changes.

## Tests

- Add or update tests when behavior changes.
- If tests are not available, explain why and how you validated the change.

## Documentation

- Keep README and docs in sync with behavior changes.
- Add concise examples for new features.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
