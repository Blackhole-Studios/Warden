# Warden Linux Installation Architecture

## Responsibilities

- `warden.cpp` is the independent bootstrap and system manager. It installs,
  updates, reports status for, and uninstalls Warden.
- Warden application components live under the replaceable `code/` tree.
- Core runs the assistant and manages its components.

Keeping the bootstrap outside `code/` lets application updates replace the
application without replacing the mechanism that performs those updates.

## Source and installed layout

The GitHub repository contains `warden.cpp`, `code/`, and project documentation.
The installed system is intended to use:

```text
/opt/warden/
├── source/                 # Persistent Git checkout and update metadata
└── code/                   # Deployed application code

/usr/local/bin/warden       # Installed system manager
/usr/local/bin/warden-bootstrap # Independent self-updater
/etc/warden/
├── controls.json           # Protected system controls
└── config.json
/var/lib/warden/
├── models/
├── corpus/
├── user-data/
└── logs/
/etc/systemd/system/warden.service
```

Configuration and user data stay outside the source checkout. Updates must not
overwrite either. Keep the Git metadata in the persistent checkout; do not
deploy by copying a temporary clone and discarding its `.git` directory.

## CLI

The installed command is intended to provide:

```text
warden init
warden start
warden install
warden update [--self]
warden uninstall
warden status
warden version
warden help
```

The bootstrap performs initial installation. Once installed,
`/usr/local/bin/warden` remains independent of the replaceable application
tree and handles maintenance commands.

## Update flow

1. Fetch and validate repository changes in the persistent checkout.
2. Stage the updated `code/` tree.
3. Build affected application components.
4. Deploy the staged code under `/opt/warden/code/`.
5. Restart affected systemd services and check their health.

The installed executable must not be stored inside the tree being replaced.
Deployment should avoid modifying protected configuration and user data.

## Uninstall behavior

- Normal uninstall removes the executable, application code, and service while
  preserving configuration and user data. It also removes the bootstrap and
  Core binaries, then removes empty `/opt/warden/` directories.
- Complete removal also deletes configuration, models, corpus, logs, and other
  Warden data, and requires explicit confirmation.

## Implementation status

`warden.cpp` provides the documented CLI, persistent checkout, staged code
deployment, status reporting, uninstall, Core build, and Core supervision.
`warden install` fetches and validates a clean checkout in staging before it
removes and recreates `/opt/warden`. An incomplete checkout is rejected before
the current installation is removed. The command then deploys `code/` and
compiles Warden, the bootstrap, and Core from the fresh checkout.
`warden init` creates required directories, compiles the Core entry point and
its input, context, routing, response, and output managers to
`/opt/warden/bin/core`, and builds the separate
`warden-bootstrap` executable from `warden-bootstrap.cpp`.
`warden start` replaces itself with the bootstrap checker before starting Core.
The checker pulls the checkout and compares the installed source revision with
`warden.cpp` at the checkout revision. If Warden is unchanged or the checkout
is older, it relaunches the current executable. If `warden.cpp` is newer, it
compiles and installs the new executable, runs `warden init`, then relaunches
it. `warden update --self` replaces its process with the bootstrap and returns
only after the bootstrap has completed and relaunched the installed Warden.
This keeps the command synchronous so a following command cannot race the
replacement. Run it with root privileges because the bootstrap replaces
`/usr/local/bin/warden`.
The bootstrap records the `warden.cpp` Git blob it installed, so unchanged
startup checks do not recompile Warden. `warden init` fingerprints Core and
bootstrap sources and skips builds when those files and their installed
binaries are unchanged. `warden update` deploys `code/` only when that tree
changed in Git and rebuilds Core only when its source fingerprint changed.
Core's build discovers all `.cpp` files beneath `code/core/`, so new Core
modules are included without maintaining a separate source list.

Core uses exit code `10` to request an update, `11` to request a restart, and
`12` to request a safe exit. The current terminal Core accepts text, stores
user and assistant turns in in-memory context, uses a placeholder route and
response, and returns `12` for `exit`, EOF, or a shutdown signal. Application
updates rebuild Core only when its source files change. The bootstrap removes
`/usr/local/bin/warden.old` at the beginning of its next run. A systemd unit is
still not present, so installation does not register a boot service.
Configuration files are not generated until their schemas are defined.
