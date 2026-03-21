# NGIN Architecture

NGIN V2 has four authored concepts and one generated handoff:

- workspace: optional repo-level container
- project: buildable app, tool, or library
- configuration: named setup of one project
- package: reusable dependency unit
- launch manifest: generated `.nginlaunch`

The intended flow is:

1. load workspace if present
2. load project
3. select configuration
4. resolve project and package references
5. build and stage output
6. emit `.nginlaunch`
7. run or debug from the generated launch manifest
