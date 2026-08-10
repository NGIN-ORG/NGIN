# Hello.Hosted

The smallest application hosted by `NGIN.Core`. It registers a static C++
module, stages configuration, and declares the runtime module in the project
manifest.

Read [`Hello.Hosted.nginproj`](Hello.Hosted.nginproj),
[`src/main.cpp`](src/main.cpp), and [`config/app.cfg`](config/app.cfg).

```bash
ngin validate --project Examples/Hello.Hosted/Hello.Hosted.nginproj --configuration Debug
ngin build --project Examples/Hello.Hosted/Hello.Hosted.nginproj --configuration Debug --output build/manual/Hello.Hosted
ngin run --project Examples/Hello.Hosted/Hello.Hosted.nginproj --configuration Debug --output build/manual/Hello.Hosted
```

Expected application output:

```text
Hello.Hosted completed successfully
```

C++ registers the module implementation; the manifest describes the runtime
shape used by project tooling. Dynamic plugins are intentionally outside this
example.
