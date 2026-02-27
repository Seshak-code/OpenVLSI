# System Includes Architecture

The `include` directory serves as the **External Dependency Interface** for the project.

## Logic: Third-Party Headers
It contains headers for libraries that are either bundled with the source or provided by the system.
-   [**glslang/**]: Headers for the GLSL to SPIR-V compiler (used by the physics backends).

## 🤖 SME Validation Checklist
- [ ] **No Local Logic**: Is project-specific code living in this folder? (Forbidden: This is for external headers only).
- [ ] **Search Paths**: Are these headers included using `<>` or `""`? (Prefer `<>` for system/bundle headers).
