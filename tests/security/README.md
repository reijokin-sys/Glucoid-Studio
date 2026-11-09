# Security test fixtures

These tests use synthetic data and isolated localhost servers. They do not
operate a user's phone, production service or WireGuard tunnel.

`ctest --test-dir build -R studio-security-audit --output-on-failure` runs
the Transport/Importer/Store checks, including a bounded timeout case.

The Python network-namespace laboratories require a separately created,
isolated Linux network namespace. They reject the host network namespace.
They create and stop only resources belonging to their test run. Do not use
production addresses, credentials or device identifiers in these test drivers.
