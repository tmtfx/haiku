# Custom Haiku OS Fork

This repository contains a custom fork of the **Haiku OS** operating system. The project includes native kernel and add-on integrations, including custom graphics drivers and video capture components.

---

## 📜 License and Legal Notes

Due to the integration of third-party drivers released under the **GNU General Public License (GPL)**, this repository and its related distributed binary builds / ISO images are released as a whole under the **GPL v2** license.

* **Haiku Source Code:** Individual source files inherited directly from the original Haiku project remain individually under the **MIT** license (with their respective copyright notices intact).
* **Graphics and Video Drivers:** Some of the added/modified driver components are released under the **GPL v2** license.
* **ISO Images / Binary Distribution:** The final compiled work falls entirely under the terms of the GPL v2 license.

For further details, please refer to the [`LICENSE.md`](LICENSE.md) file.

---

## 🛠 Build Instructions

To compile the system and generate the custom ISO image, ensure you have set up Haiku's standard cross-compilation environment and the `jam` build tool.
This fork is specifically intended for x86_64 instruction-set cpus.

### 1. Build Environment Configuration

```bash
cd haiku
./configure --build-cross-tools x86_64 ../buildtools
