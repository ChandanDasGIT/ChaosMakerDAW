Copy-me files. Nothing in this folder is built. Copy a file to its real place, rename it, then replace `Component` or
`ExampleEffect` with your own name. CMake finds new `.cpp`, `.qml` and `_test.cpp` files by itself.

| New thing | Copy | To |
| --- | --- | --- |
| Backend part | `component.h`, `component.cpp` | `backend/abc/abc.h`, `backend/abc/abc.cpp` |
| Effect or instrument | `exampleeffect.h`, `exampleeffect.cpp` | `backend/plugins/abc/abc.h`, `backend/plugins/abc/abc.cpp` |
| Test for a backend part | `component_test.cpp` | `tests/abc/abc_test.cpp` |
| Test for a plugin | `exampleeffect_test.cpp` | `tests/plugins/abc_test.cpp` |
| Wrapper | `examplecontroller.h`, `examplecontroller.cpp` | `wrapper/abcWrap/abccontroller.h`, `.cpp` |
| QML view | `ExampleView.qml` | `frontend/abc/AbcView.qml` |

The name of a QML file must start with a capital letter and be unique across all of `frontend/`.
In a test, the first word of `TEST` must match the file name: `abc_test.cpp` uses `TEST(abc, ...)`.
