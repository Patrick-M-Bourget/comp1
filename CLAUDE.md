# comp1

Real-time compressor plugin (VST3/AU/AAX/CLAP), the sibling of eq1, modeled on FabFilter Pro-C 3 with compact and expanded views, built with JUCE 9. The spec is issue #1. Use the vocabulary in `GLOSSARY.md`; respect the decisions in `docs/adr/`.
Follow `CODING_STANDARDS.md`: plugkit's shared standards, then comp1's own rules.
Checks run through plugkit's `scripts/check.sh` at the `PLUGKIT_TAG` that `plugkit.conf` pins, from comp1's root: comp1 has no copy. CI runs the same stages (`.github/workflows/ci.yml`).

GitHub account: `Patrick-M-Bourget`. Run `gh auth status` before any `git push`.

## Agent skills

### Issue tracker

Issues and specs live in GitHub Issues for Patrick-M-Bourget/comp1, via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
