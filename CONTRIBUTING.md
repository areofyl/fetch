# Contributing

## The basics

fetch is a single-file C program. The only dependency is `-lm`. Keep it that way :)

Before submitting a PR, run `make test` and make sure everything passes.

## What makes a good PR

- **Small and focused.** One fix or one feature per PR. If your diff is 500+ lines, it's probably too big. Break it up or open an issue first to discuss scope.
- **No new dependencies.** Please don't add libraries, don't shell out to external tools, and don't link anything besides `-lm`.
- **No unnecessary refactors.** If you're not fixing a bug or adding a feature, don't move code around. "Cleaner" is subjective and adds review burden for no user-facing benefit.
- **Test your changes.** `make test` runs the C unit tests and Python regression tests. If you're adding a new feature, add a test for it.

## What will get your PR rejected

- **Scope creep.** A PR that adds a small feature buried in 200 lines of unrelated cleanup won't get merged. Split code changes up if they are unrelated.
- **Untested drive-by "fixes."** If you're changing behavior, show that it was broken before and works now.
- **Fake security patches.** Wrapping safe code in unnecessary validation to pad a PR is obvious and will be closed.

## Security

fetch has a strict policy on command execution:

- **No command execution from config files.** Config values are static strings only. Features that let users run arbitrary commands from config will not be merged.
- **Sanitize all popen inputs.** If you're adding a system info gatherer that calls an external binary, validate the input charset before interpolation. No exceptions.
- **Use `-c none` for any fastfetch calls** to ignore user configs.

## AI-generated code

If AI was involved in writing your PR, please say so! PRs that are clearly AI-generated and don't disclose it **will** be closed (regardless of code quality).

Don't use AI to generate issues, discussion posts, or governance proposals. There is absolutely **zero** tolerance for those things being AI generated. 

## Style

- C99, no C++ features
- Match the existing code style. Look at fetch.c before writing
- No over-commenting. If the code needs a paragraph to explain, simplify the code
- Keep error messages short and useful: `fprintf(stderr, "fetch: ...")` format

## Questions?

Open an issue!