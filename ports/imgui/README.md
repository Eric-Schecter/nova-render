# ImGui overlay

Based on the imgui port at the manifest vcpkg baseline (1.91.9). The Vulkan binding uses its supported `IMGUI_IMPL_VULKAN_USE_VOLK` path and the existing volk dependency rather than linking the Vulkan loader. This preserves Nova's runtime loader discovery and SDK-free installed consumer build. Source archives and checksums are unchanged from the upstream port; other bindings are unchanged.
