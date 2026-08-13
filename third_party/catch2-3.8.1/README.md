# Catch2 3.8.1

This directory vendors the official Catch2 v3.8.1 amalgamated distribution for
offline, reproducible tests.

Source tag: `v3.8.1`

Source commit: `2b60af89e23d28eefc081bc930831ee9d45ea58b`

Verified SHA-256 values:

```text
8730587447e16531b832407bbf66959e97ad09af445adc118d2df13689dfecab  src/catch_amalgamated.hpp
d90d5101269efb3bba00729b8cf14d44a6e6d9b695f9e5c9b4a71345a3bceb99  src/catch_amalgamated.cpp
c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566  LICENSE.txt
```

`src/catch_amalgamated.hpp`, `src/catch_amalgamated.cpp`, and `LICENSE.txt`
are unmodified upstream files. `include/catch2/catch_test_macros.hpp` is a local
include-path bridge to the amalgamated header, and `src/catch_main.cpp` is the
standard local `Catch::Session` entry point used to provide
`Catch2::Catch2WithMain`.
