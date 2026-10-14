# ──────────────────────────────────────────────────────────────────────────
# Dependencies.cmake — Camshaft's pinned dependencies.
#
# Included after project() by the root CMakeLists.txt. Resolution (sibling
# clone or pinned commit) is FxmeDeps.cmake's.
#
# Camshaft uses FxmeFX as a source library (effect DSP and components,
# CommonAssets.cmake, FxmeModulePresets.cmake) and never runs FxmeFX's own
# CMakeLists, so it pins JUCE and FxmeTools itself, before FxmeFX. FxmeTools
# is pinned to the commit the FxmeFX pin was validated with.
# ──────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/FxmeDeps.cmake)

fxme_dependency(JUCE https://github.com/juce-framework/JUCE 8.0.12 SHALLOW)
fxme_dependency(FxmeTools https://github.com/odoare/FxmeTools
    b4bb53e78c34261951ca2400932a4dd19f88d327)
fxme_dependency(FxmeFX https://github.com/odoare/FxmeFX
    784ad846554a86ff77cc7572b0a5f70610dd686d)

add_subdirectory(${JUCE_DIR} JUCE)

# Registers the `FxmeTools` JUCE module, FxmeCore and fxmetools_attach().
include(${FXMETOOLS_DIR}/cmake/FxmeTools.cmake)
