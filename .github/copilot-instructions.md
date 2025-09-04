## ReactOS Architecture and Development Guide

This guide provides essential information for AI coding agents to effectively contribute to the ReactOS project. ReactOS is an open-source operating system aiming for binary compatibility with Microsoft Windows applications and drivers.

### Big Picture Architecture

ReactOS follows the Windows NT architecture. Understanding its main components is crucial:

- **Kernel (`ntoskrnl/`):** This is the core of the operating system, handling fundamental services like memory management (`mm/`), process and thread management (`ps/`), I/O (`io/`), and the executive services (`ex/`). It's located in the `ntoskrnl/` directory.
- **Windows Subsystem (`win32ss/`):** This component implements the Win32 API, which is the primary API for Windows applications. It includes the kernel-mode device driver (`win32k.sys`) that manages windowing, graphics (GDI), and user input.
- **Hardware Abstraction Layer (`hal/`):** The HAL provides an abstraction layer between the kernel and the hardware, allowing ReactOS to be portable across different hardware platforms.
- **Drivers (`drivers/`):** Device drivers for filesystems, networking, storage, and more are located in the `drivers/` directory.
- **User-Mode DLLs (`dll/`):** Many user-mode libraries are based on or synchronized with the [Wine project](https://www.winehq.org/). These provide implementations of standard Windows DLLs. Pay attention to `media/doc/WINESYNC.txt` for files that are synced with Wine.
- **Applications (`base/applications/`):** These are the user-facing applications that are part of the ReactOS base installation, like the command prompt, calculator, and notepad.

### Developer Workflows

**Building the System:**
The primary method for building ReactOS is using the ReactOS Build Environment (RosBE).

1.  **Configure the build:** Run `configure.cmd` (on Windows) or `./configure.sh` (on Linux/macOS) in your build directory.
2.  **Build:** Use `ninja` to build the entire project or `ninja <modulename>` to build a specific module. For example, `ninja ntoskrnl`.
3.  **Create a bootable ISO:** Run `ninja bootcd` to create a `bootcd.iso` image for testing in a VM.

**Testing:**
- Automated tests are run and their results are available on the [Web Test Manager](https://reactos.org/testman/).
- When fixing a bug, try to find a failing test that demonstrates the issue or create a new one.
- Bugs are tracked in the [ReactOS JIRA](https://jira.reactos.org/). Look for issues labeled `starter-project` if you're new.

### Coding Conventions and Patterns

ReactOS has a strict coding style documented in `CODING_STYLE.md`. Key points include:

- **Formatting:**
    - Line width is limited to 100 characters.
    - Use 4 spaces for indentation, not tabs.
    - Braces (`{` and `}`) go on their own lines (Allman style).
- **Naming:**
    - Use `PascalCase` for function and variable names (e.g., `MyFunctionName`).
    - Do not use Hungarian notation unless it's for Win32 API consistency.
    - Avoid abbreviations.
- **Types:**
    - Use NT types (e.g., `PVOID`, `ULONG_PTR`) whenever possible to ensure 64-bit compatibility. Avoid fixed-size types unless absolutely necessary.
- **Control Structures:**
    - Do not use "Yoda conditions": `if (MyVariable == 1)` is correct, `if (1 == MyVariable)` is not.
    - Prefer linear code flow with early exits over deeply nested `if` statements. `goto` is acceptable for cleanup paths.
- **Headers:**
    - Use `#pragma once` instead of include guards.
- **Comments:**
    - Use Doxygen-style comments for API documentation.
- **Legal:**
    - All contributions must be original work. You must affirm that you have not seen any proprietary Microsoft Windows source code.

### Contribution Process

- **Pull Requests:** The preferred method for submitting changes is via GitHub Pull Requests.
- **Commit Messages:** Follow the format in the `.gitmessage` template. Use your real name and email address.
- **Scope:** Keep pull requests small and focused on a single issue. Do not mix formatting changes with functional code changes in the same commit.
