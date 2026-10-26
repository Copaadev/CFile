# Contributing to CFile

Thank you for your interest in improving CFile. This document describes how to
set up the project and what is expected from a contribution.

## Getting started

Requirements: a C11 compiler (GCC or Clang), GNU Make and a POSIX system.

```sh
git clone https://github.com/Copaadev/CFile.git
cd CFile
make
make test
```

## Guidelines

- Keep the separation of concerns: `files.c` and `directory.c` implement
  operations and never print; `interface.c` is the only module that talks to
  the user; `utils.c` holds pure helpers.
- Every function that can fail returns a status (`0` or a negative
  `cf_status`) and sets `errno` when the reason comes from the system.
  Always check the return value of system calls.
- Use bounded string functions (`snprintf`, `memcpy` with checked lengths).
  Never write into a buffer without knowing its size.
- Destructive operations must never run without explicit confirmation and must
  never overwrite data silently.
- The code must build without warnings using the flags in the `Makefile`.
- Prefer small functions with a single responsibility.

## Tests

- Add a unit test to `tests/test_cfile.c` for every behavior you add or fix.
  Tests must only touch the private directory created with `mkdtemp()`.
- Add a scenario to `tests/smoke.sh` when you change user-visible behavior of
  the interactive shell.
- Run `make test` and `make sanitize` (AddressSanitizer and UBSan) before
  opening a pull request.

## Submitting changes

1. Fork the repository and create a topic branch.
2. Make focused commits with clear messages
   (for example `files: refuse to move onto the same inode`).
3. Update `CHANGELOG.md` and the documentation when behavior changes.
4. Open a pull request describing the motivation and how you tested it.

## Reporting bugs

Open an issue with the CFile version (`cfile --version`), your distribution,
the exact commands you ran and the output you observed.
