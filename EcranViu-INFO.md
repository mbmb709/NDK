# Ecran Viu v3

Build isolated on the `ecranviu-apk-build-2026` branch. Android 9+.

V3 changes: configurable screen-off inactivity `hh:mm:ss` (default 00:01:00); configurable post-inactivity random wait `hh:mm:ss` minimum and maximum (defaults 00:05:00 and 00:10:00), accepting 00:00:00. Selected settings persist. GIF one-cycle fix retained.

Run GitHub Actions `Build EcranViu APK` and download `EcranViu-APK` artifact.

Note: Android battery policy, fullscreen restrictions and exact alarm permissions may delay or prevent automatic wakeups. The debug APK may not install as an update on top of an APK signed in a different CI run.
