*This project has been created as part of the 42 curriculum by ksw.*

> ⚠️ Replace `ksw` above with your real 42 login(s) before submitting.

# Webserv

A small **HTTP/1.1 web server written in C++98**, built from scratch using only
low-level system calls (sockets, `poll()`, `fork()`, `execve()`, pipes).
It can serve a static website, handle file uploads, run CGI scripts, and be
configured through an NGINX-inspired configuration file — all driven by a
**single non-blocking event loop**.

## Description

The goal of this project is to understand, at the protocol level, how a web
server works: how it accepts TCP connections, parses raw HTTP requests, routes
them to files or programs, and writes back well-formed HTTP responses — without
ever blocking on a single client.

Key properties required by the subject and implemented here:

- **One** `poll()` for *all* I/O (listening sockets, client sockets, CGI pipes).
- Fully **non-blocking**: a slow or malicious client can never freeze the server.
- Methods: **GET**, **POST**, **DELETE**.
- **Static file serving**, **directory listing (autoindex)**, **file upload**.
- **CGI** execution based on file extension (e.g. `.py`, `.php`).
- Multiple **`host:port`** listeners and per-route configuration.
- Accurate **HTTP status codes** and customizable **error pages**.
- The server **never crashes** and never leaks file descriptors.

## Instructions

### Build

```bash
make            # builds the ./webserv executable
make re         # rebuild from scratch
make clean      # remove object files
make fclean     # remove objects + executable
```

> Requires a **Linux/Unix** toolchain with `c++` (g++/clang++). The code uses
> POSIX socket APIs and will not build on native Windows. Use Linux, WSL, or a
> 42 machine.

### Run

```bash
./webserv [configuration file]
# if no file is given, ./config/default.conf is used
./webserv config/default.conf
```

Then open a browser at `http://localhost:8080/`.

## Project layout

```
webserv/
├── Makefile
├── include/        # class declarations (.hpp) — read these first
├── src/            # implementations (.cpp)
├── config/         # example configuration files
├── www/            # the static website used for testing
└── docs/           # in-depth beginner guide (also mirrored to Notion)
```

A guided **"where do I start reading the code?"** walkthrough lives in
[`docs/NOTION_webserv.md`](docs/NOTION_webserv.md).

## Resources

- RFC 7230–7235 (HTTP/1.1) — the authoritative protocol definition.
- RFC 3875 — the CGI/1.1 interface specification.
- [MDN: An overview of HTTP](https://developer.mozilla.org/en-US/docs/Web/HTTP/Overview)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- `man` pages: `socket`, `bind`, `listen`, `accept`, `poll`, `recv`, `send`,
  `fork`, `execve`, `pipe`, `dup2`, `waitpid`.
- NGINX, used as a reference implementation to compare header/answer behaviour.

### Use of AI

AI (Claude) was used to **scaffold the project structure, write the extensive
explanatory comments, and produce the beginner documentation** in `docs/`. Every
design decision (single-`poll` architecture, the connection state machine, the
CGI integration) is documented so it can be explained and defended during
peer-evaluation.
