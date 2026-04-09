# web_vision_pro

Industrial vision system for paper machine inspection using Basler GigE cameras, Qt5 GUI, OpenCV processing, and Pylon 6.x API.

## Project Structure

```
src/
├── core/          # CameraManager, EventController, EventDatabase
├── gui/           # Qt widgets and windows
├── processing/    # OpenCV algorithms
├── config/        # Configuration handling
└── communication/ # External interfaces
```

## Build

```bash
mkdir -p build && cd build
cmake ..
make
```

## Conventions

- C++17, CMake 3.10+, Qt5, OpenCV 4.x, Pylon 6.x
- Classes: `PascalCase`, member vars: `snake_case_`, constants: `UPPER_SNAKE_CASE`
- Indentation: 4 spaces, K&R braces
- Use `nullptr`, not `NULL`
- Commit format: `type: description` (types: feat, fix, docs, style, refactor, test, chore, docker)