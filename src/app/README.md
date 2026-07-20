# Open Trader Application layer

**Namespace:** `scratcher::elements` (Elements UI), `scratcher::elements::panels` (panel content), `scratcher::imgui` (ImGui UI, legacy)

The Application layer implements

1. control over the application startup and overall application flow.
2. Implements UI management and routing using abstract UI builder, which has independent implementation for different UI engines like Elements, ImGUI or QT. Current mainstream dev is based on Cycfi Elements library.

Open Trader UI planned to be conceptually modular built from universal content panels. Everything in the UI is content panel except menu, tabs and splitters.
In other words, any dynamic set of the content panels may be combined in the UI and automatically managed by tabs, splitters, etc... with deep recursion.

While the panels creation and its internal implementation are tightly coupled to underlying UI library, it provides a common API to manage unique business logics which is called by the fully abstract Trader Cockpit implementation
