# Third-party notices

This file records external works this project builds on or takes inspiration from,
and their licences. The project itself is licensed under LGPL 2.1 (see LICENSE).

## Data composition engine — architecture adapted from onebase (MIT)

The data-composition ("СКД"-style) report engine under
`src/engine/backend/dataComposer/` is an **independent C++ implementation**. Its
architecture — the recursive bucket-by-grouping-field build, per-group aggregation
into a subtotal map, the group/result tree shape, and the separation of an immutable
schema from user settings — is **adapted from the report engine of the onebase
project** (Go), used here under the terms of its MIT licence. No onebase source code
is copied; only the design/algorithm was studied and re-implemented over this
project's own value, number and spreadsheet types.

onebase: https://github.com/ivanarama/onebase

```
MIT License

Copyright (c) 2026 Иван Титов (Ivan Titov)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## XLSX / DOCX export

The `.xlsx` writer under `src/engine/backend/export/` is an independent
implementation of the **public** ECMA‑376 (Office Open XML) file format. It is not
derived from any third-party spreadsheet library.
