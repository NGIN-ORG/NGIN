---
title: NGIN.Reflection C++ API
description: Public registration, descriptor, value, invocation, adapter, and module ABI symbols.
---

# NGIN.Reflection C++ API

**Header:** `<NGIN/Reflection/Reflection.hpp>`  
**Namespace:** `NGIN::Reflection`  
**Target:** `NGIN::Reflection`  
**Source:** [Dependencies/NGIN/NGIN.Reflection/include/NGIN/Reflection](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Reflection/include/NGIN/Reflection)

## Registration

| Symbol | Header | Role |
| --- | --- | --- |
| `ModuleRegistration` | `ModuleInit.hpp` | Owns one pending registration transaction |
| `TypeBuilder<T>` | `TypeBuilder.hpp` | Declares fields, properties, methods, constructors, bases, enums, and attributes |
| `Describe<T>` | `Registry.hpp` | User specialization hook for explicit metadata |
| `GetType`, registry lookup functions | `Registry.hpp` | Resolve committed descriptors |

## Descriptors

| Symbol | Represents |
| --- | --- |
| `Type` | identity, kind, construction, members, bases, enum values, attributes |
| `Field`, `Property` | checked data access |
| `Method`, `Function` | signature metadata and invocation |
| `Constructor` | construction and injectable parameter bindings |
| `Base`, `EnumValue`, `AttributeView` | relationships and supporting metadata |

## Values, instances, and adapters

`Value` owns a reflected value. `ValueView` and `ConstValueView` borrow one.
`InstanceRef` and `ConstInstanceRef` borrow object storage. `SequenceAdapter`,
`TupleAdapter`, `VariantAdapter`, `OptionalLikeAdapter`, `MapAdapter`, and
`FlatHashMapAdapter` expose checked collection operations.

Descriptors are snapshot-backed handles. Imported-module descriptors and
instances must not outlive the module state that guarantees their validity.
Operations report `Error`/`ErrorCode` rather than relying on unchecked casts.

See the [Reflection API guide](../../../api/reflection.md) for registration and
invocation flows.

