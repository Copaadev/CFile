# CFile

A fast, safe and good-looking file manager for the Linux terminal, written in C11.

```
  ____ _____ _ _      
 / ___|  ___(_) | ___ 
| |   | |_  | | |/ _ \
| |___|  _| | | |  __/
 \____|_|   |_|_|\___|
```

[![build](https://github.com/Copaadev/CFile/actions/workflows/build.yml/badge.svg)](https://github.com/Copaadev/CFile/actions/workflows/build.yml)
![language](https://img.shields.io/badge/language-C11-blue)
![platform](https://img.shields.io/badge/platform-Linux-informational)
[![license](https://img.shields.io/badge/license-MIT-green)](LICENSE)

CFile is an interactive command line tool for everyday file and directory
management. It has no dependencies beyond the C standard library and POSIX,
uses no database or configuration files, and asks for confirmation before
anything destructive happens.

## Demo

The session below is real output captured from the program (colors removed).

```text

    ____ _____ _ _      
   / ___|  ___(_) | ___ 
  | |   | |_  | | |/ _ \
  | |___|  _| | | |  __/
   \____|_|   |_|_|\___|

  Professional CLI file manager  v1.0.0

  Navigation
    ls [-a] [dir]          List contents with permissions and sizes
    cd [dir|-]             Change directory ('-' = previous)
    pwd                    Print the current directory
    find <name> [dir]      Search files by name, recursively

  Files
    info <path>...         Show type, size, permissions and dates
    cp <src> <dst>         Copy a file
    mv <src> <dst>         Move a file or directory
    rename <path> <name>   Rename a file or directory
    rm <file>...           Remove files (asks for confirmation)

  Directories
    mkdir [-p] <dir>...    Create directories
    rmdir <dir>...         Remove empty directories (asks first)

  General
    clear                  Clear the screen
    help [command]         Show this menu or help for a command
    quit                   Leave CFile

  Paths accept ~ and quotes. Destructive commands always ask first.

cfile ~ > cd projects
cfile ~/projects > ls

  Directory  /home/demo/projects

  PERMISSIONS        SIZE  MODIFIED          NAME
  drwxr-xr-x            -  2026-10-26 01:28  webapp/
  -rw-r--r--     46.9 KiB  2026-10-26 01:28  archive.bin
  -rwxr-x---         21 B  2026-10-26 01:28  build.sh
  -rw-r--r--         63 B  2026-10-26 01:28  hello.c
  lrwxrwxrwx            -  2026-10-26 01:28  notes-link -> ../Documents/notes.md

  1 directory, 4 files, 47.0 KiB

cfile ~/projects > info hello.c

  hello.c

  Path         /home/demo/projects/hello.c
  Type         regular file
  Size         63 B (63 bytes)
  Permissions  -rw-r--r-- (0644)
  Owner        demo:demo
  Links        1
  Inode        573520
  Modified     2026-10-26 01:28
  Accessed     2026-10-26 01:28

cfile ~/projects > find notes ~

  -  /home/demo/Documents/notes.md
  l  /home/demo/projects/notes-link

  2 matches

cfile ~/projects > cp hello.c hello_backup.c
  [ OK ] Copied 'hello.c' -> 'hello_backup.c'
cfile ~/projects > cp hello.c hello_backup.c
  [ ?? ] 'hello_backup.c' already exists. Overwrite? [y/N] n
  [INFO] Cancelled. Nothing was changed.
cfile ~/projects > rename hello_backup.c backup.c
  [ OK ] Renamed 'hello_backup.c' to 'backup.c'
cfile ~/projects > mkdir -p webapp/assets
  [ OK ] Created directory 'webapp/assets'
cfile ~/projects > rm backup.c

    - backup.c (63 B)

  [ ?? ] Permanently remove 1 item? [y/N] y
  [ OK ] Removed 'backup.c'
cfile ~/projects > rmdir webapp

    - webapp (directory)

  [ ?? ] Permanently remove 1 item? [y/N] y
  [FAIL] rmdir: 'webapp': Directory not empty
cfile ~/projects > rm nothing.txt
  [FAIL] rm: 'nothing.txt': No such file or directory
cfile ~/projects > quit
  Goodbye.
```

## Features

- List directories with permissions, sizes and modification times; directories
  first, sorted case-insensitively, symbolic links shown with their target.
- Navigate with `cd`, `cd -` and `~` expansion.
- Create directories (optionally with parents) and remove empty ones.
- Copy, move and rename files and directories (copy handles regular files).
- Remove files with an explicit confirmation listing exactly what will go.
- Show detailed information: type, size, symbolic and octal permissions,
  owner, link count, inode and timestamps; directories include a recursive
  total size.
- Recursive, case-insensitive search by file name that never follows symbolic
  links.
- Nothing is overwritten silently: CFile asks first, and a failed copy never
  leaves a truncated file behind.
- Dark-theme ANSI interface with a blue accent. Colors are turned off
  automatically for pipes, `TERM=dumb`, `NO_COLOR` or `--no-color`.
- File names are sanitized before printing, so hostile names cannot inject
  terminal escape sequences.

## Installation

Build requirements: a C11 compiler (GCC or Clang), GNU Make and `git`.

Arch Linux:

```sh
sudo pacman -S --needed base-devel git
```

Debian / Ubuntu:

```sh
sudo apt install build-essential git
```

Fedora:

```sh
sudo dnf install gcc make git
```

Then fetch and install:

```sh
git clone https://github.com/Copaadev/CFile.git
cd CFile
make
sudo make install        # installs to /usr/local by default
```

To install somewhere else, set `PREFIX` (and optionally `DESTDIR` for
packaging):

```sh
make install PREFIX=$HOME/.local
```

Remove it again with `make uninstall` using the same `PREFIX`.

## Compilation

| Command          | Description                                                  |
|------------------|--------------------------------------------------------------|
| `make`           | Build `build/cfile`                                          |
| `make clean`     | Remove build artifacts                                       |
| `make install`   | Install the binary and the man page                          |
| `make uninstall` | Remove the installed files                                   |
| `make test`      | Build and run the unit tests and the interactive smoke test  |
| `make sanitize`  | Run the whole test suite under AddressSanitizer and UBSan    |

The build uses `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion` and
produces no warnings. Override the compiler with `make CC=clang`.

## Usage

```sh
cfile [options] [directory]
```

| Option          | Description                                  |
|-----------------|----------------------------------------------|
| `-h`, `--help`  | Show usage and exit                          |
| `-V`, `--version` | Show the version and exit                  |
| `--no-color`    | Disable ANSI colors                          |
| `--no-banner`   | Skip the banner and menu at startup          |

CFile starts an interactive session in the given directory (default: the
current one). Type `help` at any time to see the menu, or `help <command>`
for a single command. Paths accept `~`, single or double quotes, and a
backslash to escape the next character.

## Commands

| Command                  | Description                                              |
|--------------------------|----------------------------------------------------------|
| `ls [-a] [dir]`          | List contents with permissions and sizes                 |
| `cd [dir\|-]`            | Change directory (`-` returns to the previous one)       |
| `pwd`                    | Print the current directory                              |
| `find <name> [dir]`      | Search by name, recursively (first 200 matches shown)    |
| `info <path>...`         | Show type, size, permissions, owner and dates            |
| `cp <src> <dst>`         | Copy a file; asks before overwriting                     |
| `mv <src> <dst>`         | Move a file or directory; asks before overwriting        |
| `rename <path> <name>`   | Rename within the same directory; never overwrites       |
| `rm <file>...`           | Remove files after confirmation; refuses directories     |
| `mkdir [-p] <dir>...`    | Create directories; `-p` creates missing parents         |
| `rmdir <dir>...`         | Remove empty directories after confirmation              |
| `clear`                  | Clear the screen                                         |
| `help [command]`         | Show the menu or help for one command                    |
| `quit`, `exit`           | Leave CFile (end of input also works)                    |

If the destination of `cp` or `mv` is an existing directory, the entry is
placed inside it under its original name.

## Project structure

```text
CFile/
├── src/
│   ├── main.c          command line options and start-up
│   ├── interface.c/h   banner, menu, prompt, commands, confirmations
│   ├── files.c/h       copy, move, rename, remove, metadata
│   ├── directory.c/h   list, mkdir, rmdir, chdir, size, search
│   └── utils.c/h       colors, formatting, tokenizer, path helpers
├── tests/
│   ├── test_cfile.c    unit tests for the utils, files and directory modules
│   └── smoke.sh        end-to-end test of the interactive binary
├── docs/
│   ├── ARCHITECTURE.md design notes and safety decisions
│   ├── cfile.1         manual page
│   └── demo.txt        real captured session used in this README
├── .github/workflows/build.yml
├── Makefile
├── README.md
├── LICENSE
├── CONTRIBUTING.md
├── CHANGELOG.md
└── .gitignore
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the rationale behind the
module split.

## Testing

```sh
make test       # unit tests + interactive smoke test
make sanitize   # the same under AddressSanitizer and UBSan
```

The unit tests cover directory creation and removal, copying (including
permission preservation, overwrite protection and files larger than the
internal buffer), renaming, moving, removal, symbolic link handling, listing
order, recursive size, search and error conditions such as `ENOENT`, `EEXIST`,
`ENOTEMPTY` and `EISDIR`. The smoke test runs the real binary and verifies
prompts, confirmations, error messages and robustness against bad input.

Every test runs inside a private directory created with `mkdtemp()`, with
`HOME` redirected there for the smoke test. Personal files are never touched.

## Known limitations

- Linux and other POSIX systems only.
- `cp` copies regular files; recursive directory copy is not implemented.
- Moving a directory across filesystems is not supported (the error says so);
  moving regular files across filesystems works through copy and delete.
- There is no shell-style wildcard expansion; use `find` to locate entries.
- Hard-linked files are counted once per link in recursive size totals.
- An overwrite confirmation is a check at one point in time; a different
  process may still change the target before the operation runs.

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) for
the coding guidelines and the test requirements, and add an entry to
[CHANGELOG.md](CHANGELOG.md) for user-visible changes.

## License

CFile is released under the [MIT License](LICENSE).
