# Deployment and execution plans

Typed plans are immutable execution contracts derived from the Composition
Graph. They contain no authored XML nodes.

## StagePlan

StagePlan combines the product artifact, active Plugin artifacts, runtime
files, assets, notices, symbols, and project files. Each input records owner,
reason, source, destination, and provenance. Destination collisions are errors
unless policy explicitly permits an identical replacement. Plugin staging does
not imply runtime loading.

## RunPlan

RunPlan selects one Executable Run and binds its product or package Tool to a
staged executable. It retains ordered arguments, working directory, ordinary
environment values, external secret references, and prerequisites. Secret
contents never enter a plan or generated file.

## TestPlan

TestPlan binds one Test registration to the staged Executable. It preserves
ordered arguments, environment, timeout, and dependency instances in Test
context. A Test is a registration, not a product kind.

## BenchmarkPlan

BenchmarkPlan binds one Benchmark registration and additionally preserves
repetitions and warm-up settings. `ngin benchmark` is distinct from Test
execution and benchmarks are not parallelized by default.

## PublishPlan

PublishPlan selects one Folder, Archive, or Installer declaration, expands
portable output placeholders, and classifies staged inputs. NGIN adds generated
CPS component metadata for the published Executable or Library. Backend fields
such as CPack variables are derived after the plan boundary.

Each plan has its own SHA-256 identity covering the Composition Identity,
adapter identity and version, and canonical plan content. Equivalent inputs
therefore serialize deterministically without conflating dependency-lock,
composition, and execution identities.
