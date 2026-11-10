# Glucoid Studio

![Glucoid Studio](packaging/fi.reijo.glucoidstudio.png)

Glucoid Studio is a local glucose and treatment-entry history viewer for the Linux KDE Plasma desktop. The interface language is selected in Settings: **English (default)** or **Finnish**. It is a separate application from [Glucoid](https://github.com/reijokin-sys/Glucoid), the Plasma panel widget.

#### Features

- A purple and mint-green G icon based on the author’s hand-drawn sketch.
- Summary: time-weighted mean glucose, GMI, time in range, variability, data coverage, a nighttime mean and a daily profile.
- Daily analysis across seven time periods, including 00–06: glucose means and recorded insulin-to-carbohydrate ratios.
- A current-day chart, a daily profile and a scrollable four-day comparison. Curve portions outside the configured target range are red.
- Local history and backups. Carbohydrate and rapid-insulin totals are available in the expandable calculation details.
- One active data source: Juggluco or your own Nightscout server.
- The version number is shown next to the application name.

Juggluco network access requires a working Linux WireGuard interface and the phone's numeric tunnel address. Studio binds the connection to that interface and does not fall back to an unprotected route. USB/ADB is an alternative. Independent Nightscout access uses HTTPS/API v3 and does not require WireGuard. Read credentials are stored in KDE Wallet. Import runs on startup and every five minutes while the application is open.

The application describes recorded data; it cannot guarantee that source data is correct. GMI is an estimate derived from sensor readings, not a laboratory HbA1c measurement. Its reference color is also shown for a one-week period; short or incomplete periods remain marked as indicative. Historical, clinically unvalidated research tools remain available separately with `--research`; they must not be used for independent medication decisions.

#### New in version 1.6.0

- The interface is bilingual: **English (default)** or **Finnish** in
  Settings, also switched live. User data, entry names and connection
  settings are unchanged when the language changes.
- The first normal import covers 183 days. Subsequent updates revisit the
  last 72 hours; after a longer outage, the entire missing interval is fetched.
- Nightscout modification timestamps also bring in edits to older events.
  A separate **Check full history** button covers older Juggluco edits.
- Import storage, summaries and backups run in the background in the main
  window. The selected tab has a clear mint-green highlight.
- **Update data** stays grey during work and turns green only
  after the import, calculations and backup succeed. Errors do not show success.

#### Installation

The release `.deb` targets **Ubuntu/Kubuntu 26.04 amd64**, Qt 6.10 and KDE/Kirigami 6. It uses distribution-provided libraries rather than bundled copies.

```sh
sudo apt install ./glucoid-studio_1.6.0_amd64.deb
glucoid-studio
```

The application also appears in the desktop application menu. Choose the source and connection type in Settings. Studio does not install or configure a WireGuard tunnel. USB import requires `adb` to be installed separately and USB debugging to be approved on the device.

Data is stored below the user's XDG application-data directory (normally `~/.local/share/glucoid-studio/live/`). Demo mode, `--demo`, uses a separate demo directory. Override the data directory with `--data-dir`. Secrets are not stored in the connection settings file. Reports and backups are local; a backup on the same disk does not protect against disk failure.

```sh
sudo apt remove glucoid-studio
```

Removal preserves the user's history and wallet credentials.

#### Building from source

Requirements: a C++20 compiler, CMake, Ninja, SQLite3, Qt 6.10 (Core, Gui, Widgets, Quick, QuickControls2, Network, DBus, Test, Graphs, Concurrent and LinguistTools), KF6Wallet, KF6I18n, the Kirigami 6 QML modules, Python 3 and OpenSSL for the tests. Exact runtime dependencies are recorded in the `.deb` metadata. Translation sources live in `i18n/` and are compiled into the application.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr/local
```

Most tests use temporary synthetic datasets. Local HTTP/TLS tests need permission to open localhost sockets. The security laboratory's network-namespace drivers are separate from normal use; they do not modify a user's phone or tunnel.

Developers can set a local data root with the `STUDIO_LOCAL_DATA_ROOT` CMake option. The public package uses the XDG default.

#### Calculation policy

Glucose is weighted by measured duration. Stream data takes priority; sensor history fills only gaps, without double counting overlapping time. Missing time is not treated as zero, and the last reading is not extended across a long gap. The technical maximum gap is 20 minutes; covered time is also limited by the series' observed sampling interval.

Daily analysis first calculates a time-weighted mean for each day's time period, then averages those daily means with equal weight for each measured day. Front-page means weight all covered time in the selected reporting period. Low or high readings and carbohydrate/insulin entries do not exclude nights or days from descriptive glucose analysis. The 00–06 interval is a clock-time window, not verified sleep time.

The insulin-to-carbohydrate ratio describes recorded amounts, not a dose recommendation. It averages daily ratios for days with a positive carbohydrate amount and a rapid-insulin entry. A missing entry is not zero.

#### License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
