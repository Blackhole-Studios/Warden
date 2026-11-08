# Warden

**An extensible, local-first personal intelligence system.**

Warden is an experimental personal assistant designed around local intelligence, modular architecture, and optional online services. Its goal is to provide a capable, adaptable assistant that can operate independently while remaining extensible through external models, APIs, and knowledge sources.

Warden is designed to run on resource-constrained hardware, including Raspberry Pi systems.

> **Status: Pre-alpha.** Warden is under active development. Most components are currently architectural concepts or works in progress.

## Philosophy

Warden is built around several principles:

- **Local-first:** Prioritize local processing and offline capabilities.
- **Modularity:** Keep individual components independent and replaceable.
- **Extensibility:** Support different models, providers, APIs, and knowledge sources.
- **Resource awareness:** Adapt to the hardware available.
- **Provider independence:** Avoid tying the system to a single AI provider.
- **User control:** Keep configuration, credentials, data, and updates under the user's control.

## Architecture

Warden is organized into specialized components, each with a defined responsibility.

| Component | Responsibility |
|---|---|
| Core | Main orchestration, context management, and request routing |
| LOCK | Core module responsible for controlling input and output during maintenance and system upgrades |
| QISKIT | Specialized local neural networks for classification, routing, and decision-making |
| Gemma | Conversational response generation |
| Corpus | Offline knowledge storage and retrieval |
| Auvi | Audio and visual interface, including STT, TTS, and hardware interaction |
| Concrete | Proactive conversation and context-aware initiation |
| Apye | External API integrations and connected services |

### Infrastructure

Infrastructure components support the operation and maintenance of Warden.

| Component | Responsibility |
|---|---|
| Deloader | System updates, deployment, and recovery |
| Model Installer | Discovery, downloading, verification, and installation of models |
| Model Manager | Tracking installed models and managing model assignments |
| Corpus Manager | Managing, updating, and indexing offline knowledge sources |
| API Manager | Managing online service providers and API configurations |
| Secret Manager | Secure storage and retrieval of credentials |

## Information Pipeline

A typical interaction is intended to follow this process:

1. **Auvi** captures user input and performs speech recognition.
2. **Core** receives the input and manages context.
3. **QISKIT** evaluates the request and determines whether external retrieval is necessary.
4. **Gemini** or another configured online provider retrieves current information when needed.
5. **Corpus** supplies relevant offline knowledge.
6. **Gemma** generates a natural-language response using the available context.
7. **Auvi** delivers the response through the appropriate interface.

Not every request requires every component. Warden is designed to avoid unnecessary model inference and external API calls.

## Offline Knowledge

Corpus is intended to provide access to locally stored reference material without requiring an internet connection.

Potential sources include:

- Wikipedia
- Wiktionary
- Technical documentation
- Programming language references
- User-provided documents

Corpus Manager will handle database discovery, downloads, verification, indexing, and updates. Knowledge sources are intended to remain independent of the retrieval interface.

## Model Support

Warden is designed to support both local and remote models.

Local models will be installed and managed independently of the main source repository. Remote models will be accessed through configurable API providers.

The model architecture is intended to support different hardware configurations without requiring changes to Core.

## Hardware

The initial development target is the **Raspberry Pi 3 Model B (1 GB RAM)**.

Future support may include:

- Raspberry Pi systems
- Linux desktop computers
- More powerful ARM systems
- x86-64 systems

Hardware detection and model compatibility will help determine which components can run on a given system.

## Repository Structure

```text
Warden/
├── warden.cpp          # Independent bootstrap and system manager
├── warden-bootstrap.cpp # Replaces Warden after its process exits
├── code/               # Replaceable application source
│   ├── core/            # Core runtime and supervisor stop-code protocol
│   ├── qiskit/
│   ├── gemma/
│   ├── auvi/
│   ├── concrete/
│   └── infrastructure/
├── docs/
├── README.md
└── LICENSE
```

The bootstrap is kept outside `code/` so application updates can replace the
application source without replacing the system manager. The intended Linux
layout keeps the persistent Git checkout and deployed application under
`/opt/warden/`, installs the `warden` command in `/usr/local/bin/`, and stores
system configuration and user data separately:

```text
/opt/warden/                 # Persistent source checkout and installed code
/opt/warden/code/            # Deployed application code
/usr/local/bin/warden        # Bootstrap/system manager executable
/usr/local/bin/warden-bootstrap # Independent Warden self-updater
/etc/warden/                 # Protected system configuration
/var/lib/warden/             # Models, corpus, user data, and logs
/etc/systemd/system/warden.service
```

Updates should stage repository changes, deploy only `code/`, build affected
components, and verify service health. The Git metadata must remain available
for future updates. Large model files and offline databases are distributed
separately from application source.

The commands `warden init` and `warden start` prepare the installed runtime
and supervise Core. `warden update --self` asks the independent bootstrap to
pull the repository and replace the Warden executable when `warden.cpp` has a
newer revision. `warden start` also checks for a newer Warden revision before
starting Core. Core signals its requested action through its process exit
status: `10` requests an update, `11` requests a restart, and `12` requests a
safe exit. `warden start` forwards an external stop request to Core and waits
for it to exit.

The initial Core runtime accepts terminal text, keeps an in-memory conversation
history, passes each turn through placeholder routing and response layers,
and prints the response. Gemma and QISKIT are not part of this first runtime.

`warden install` fetches a clean checkout, verifies the required runtime
sources, then replaces `/opt/warden` and builds the installed executables.
Configuration in `/etc/warden` and user data in `/var/lib/warden` remain
separate from that application directory.
Build fingerprints let `warden init` and `warden update` skip Warden, Core,
and bootstrap builds when their respective sources have not changed.

## Development

Warden is currently in early development. Its architecture and components are subject to change.

Contributions, experimentation, and community forks are welcome. The modular design is intended to make it possible to develop alternative implementations of individual components.

## Security

- API credentials must never be committed to the public repository.
- Downloaded models and databases should be integrity-verified.
- Updates should use staged deployment and recovery mechanisms.
- Local user data should remain separate from application source code.

## License

See [LICENSE](LICENSE) for licensing information.
