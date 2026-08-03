# Dependencies

`Dependencies/NGIN/` contains first-party library source owned by the NGIN
project. `Dependencies/ThirdParty/` contains upstream source used by package
wrappers and library implementations.

Public NGIN packages are described under [`Packages/`](../Packages). Change a
package wrapper when the work concerns exposure or build integration; change a
first-party source tree when the implementation belongs to that library.

Do not edit third-party source unless a task explicitly requires a vendored
upstream change. Preserve its licenses and notices.
