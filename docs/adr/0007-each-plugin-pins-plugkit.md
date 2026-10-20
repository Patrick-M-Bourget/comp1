# Each plugin pins plugkit to an exact version

Each plugin pins an exact `plugkit` tag or commit through FetchContent, never a branch. Bumping the pin is that plugin's own PR, through its full checks, including its null check. A change to `plugkit` therefore never changes a plugin's sound or behaviour until that plugin chooses to take it, and a regression shows up in one plugin's PR rather than in both plugins' CI at once.

## Considered Options

- **Tracking `plugkit`'s main branch.** Rejected: a library change would land in every plugin unreviewed, and the plugins' null checks would go red without anyone changing them.
- **A monorepo holding both plugins and the library.** Rejected: the plugins ship on their own schedules, and eq1 is not to change until it ships.
