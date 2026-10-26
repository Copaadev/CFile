# Architecture

CFile is split into four small modules plus a `main` entry point. The goal is
a strict separation between *what* is done to the file system and *how* it is
presented to the user.

```
main.c ──> interface.c ──┬──> files.c ───┐
                         ├──> directory.c ┼──> utils.c
                         └────────────────┘
```

| Module        | Responsibility                                              | Prints? |
|---------------|-------------------------------------------------------------|---------|
| `utils.c`     | Colors, size/mode/time formatting, tokenizer, path helpers  | no      |
| `files.c`     | Copy, move, rename, remove, metadata for single entries     | no      |
| `directory.c` | List, mkdir, rmdir, chdir, recursive size and search        | no      |
| `interface.c` | Banner, menu, prompt, command dispatch, confirmations       | yes     |
| `main.c`      | Command line options and start-up                           | minimal |

Because `files.c` and `directory.c` never print or read input, they are unit
tested directly (`tests/test_cfile.c`) without any terminal involved. The
interactive layer is covered end to end by `tests/smoke.sh`, which drives the
real binary through stdin.

## Error handling

Every operation returns `0` on success or a negative `cf_status`:

- `CF_ERRNO` - the reason is in `errno` (the usual case);
- `CF_ESAME`, `CF_EBADNAME`, `CF_EXDEVDIR` - conditions that have no suitable
  `errno` value.

`cf_error_string()` turns either kind into a message. The interface calls it
before printing anything else so `errno` is never clobbered.

## Safety decisions

- **No silent overwrite.** `cp` opens the target with `O_CREAT | O_EXCL`; `mv`
  and `rename` check for an existing target first. The interface reacts to
  `EEXIST` by asking the user, and only then retries with `overwrite = 1`.
- **Cleanup on failure.** A partially written copy is unlinked, and a
  cross-filesystem move removes the new copy if the original cannot be
  deleted.
- **Symbolic links are never followed** while searching, measuring sizes or
  removing entries, so a link can neither cause infinite recursion nor make a
  delete reach outside the chosen path.
- **Bounded buffers everywhere.** Paths are limited to `CF_PATH_MAX`, input
  lines to 4095 characters (longer lines are rejected and drained), and all
  string building uses `snprintf` with checked results.
- **Terminal injection.** File names are passed through `cf_sanitize()` before
  being printed, so a hostile name cannot emit escape sequences.
- **Single confirmation for batches.** `rm` and `rmdir` list everything that
  will be removed and ask once.

## Layout note

The original brief lists an `include/` directory. Headers live next to their
sources in `src/` instead: the project is an application, not a library, so
there is no public header set to separate, and keeping them together makes the
modules easier to navigate.
