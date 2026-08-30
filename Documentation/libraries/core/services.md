---
title: Services and scopes
description: Register singleton, scoped, and transient services with explicit dependency lifetimes.
---

# Services and scopes

NGIN.Core provides a dependency-injection container for application services.
Services can be registered by value, factory, concrete type, or interface
mapping.

## Lifetimes

| Lifetime | Meaning |
| --- | --- |
| Singleton | One instance for the application container |
| Scoped | One instance for a created scope |
| Transient | A new instance for each resolution |

Choose the lifetime from ownership semantics, not convenience. A singleton
must not capture a shorter-lived scoped service.

## Typed construction

Typed constructor injection does not require `NGIN.Reflection`. The container
can construct registered concrete types and satisfy known typed dependencies.
Use reflection only when the application needs runtime metadata-driven
construction.

## Resolution boundary

Prefer resolving dependencies at composition boundaries and passing them as
typed constructor parameters. Scattered service-locator calls make required
dependencies and failure points harder to see.

## Scopes

A scope groups services with a shared lifetime, such as one request, document,
job, or game session. Dispose of the scope only after work using its services
has completed.

## Diagnostics

Treat missing registration, cyclic dependency, invalid lifetime capture, and
factory failure as distinct errors. Preserve the dependency chain so the
failure identifies both the missing service and the consumer that required it.
