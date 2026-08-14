# Third-Party Notices

Solid v0.1's optional `Liquid::Lua` component vendors the official Lua 5.4.8 sources in `third_party/lua-5.4.8`.

## Lua 5.4.8

Project: Lua

Source: <https://www.lua.org/ftp/lua-5.4.8.tar.gz>

License: MIT
Copyright: Copyright © 1994–2025 Lua.org, PUC-Rio

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Liquid itself is not licensed under the Lua license. See [LICENSE-NOTICE.md](LICENSE-NOTICE.md).
The complete upstream license is also installed with the package at
`share/Liquid/third_party/lua-5.4.8/LICENSE`.

## Catch2 3.8.1

Project: Catch2

Source: <https://github.com/catchorg/Catch2/releases/tag/v3.8.1>

License: Boost Software License 1.0
Use: build-time test dependency only; it is not part of the installed Liquid package

The official amalgamated source and header are vendored for reproducible offline
test builds. The complete upstream license is preserved at
`third_party/catch2-3.8.1/LICENSE.txt`.
