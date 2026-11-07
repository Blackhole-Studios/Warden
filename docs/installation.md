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

/usr/local/bin/warden       # Installed bootstrap executable
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
warden install
warden update
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
  preserving configuration and user data.
- Complete removal also deletes configuration, models, corpus, logs, and other
  Warden data, and requires explicit confirmation.

## Implementation status

`warden.cpp` provides the documented CLI, persistent checkout, staged code
deployment, executable installation, status reporting, and normal or complete
uninstall. Complete removal requires the exact confirmation phrase
`REMOVE WARDEN DATA`.

There is no build command configured, so updates deploy code without compiling
it. A service restart is attempted only when a service unit is installed.
Neither a unit nor a runnable Core entry point exists yet, so installation
reports that no service was installed and updates report that restart was
skipped. Configuration files are not generated until their schemas are
defined.
