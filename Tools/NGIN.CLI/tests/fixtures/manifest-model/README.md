# Manifest model golden fixtures

These fixtures are the Milestone 0 semantic contract inputs. They intentionally
use only the new grammar and are not compatibility samples.

Until the new parser is implemented, no current-schema test loads them. Each
implementation milestone promotes the relevant fixture into focused executable
tests. `semantic-inventories.json` records facts that resolution must produce;
it is deliberately narrower and more stable than a full serialized graph
snapshot.

Old syntax belongs only in explicitly named rejection fixtures added alongside
the parser. Do not add a legacy parser or conversion fallback to make old
fixtures pass.
