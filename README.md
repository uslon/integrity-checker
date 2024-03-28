# Integrity checker

Ensures the integrity of files within a specified directory by examining their consistency through the computation of their CRC32 values.

## Usage:
Build:
```
make
```

Use:
```
./integrity_checker -time_interval <seconds to wait> -directory <directory to observe>
```

Additionaly, arguments may be passed via environment variables. Set respective values for `time_interval` and `directory`.
