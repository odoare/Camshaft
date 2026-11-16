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

fxme_dependency(JUCE https://github.com/juce-framework/JUCE 9.0.4 SHALLOW)
fxme_dependency(FxmeTools https://github.com/odoare/FxmeTools
    a2f76522918b31974b7618374f8a9478aa9a1450)
fxme_dependency(FxmeFX https://github.com/odoare/FxmeFX
    110ffeba9be29df795f457ee387bfc044a73b5da)
# CLAP format (fxmetools_add_clap). Its last tag predates JUCE 9 support:
# pinned to a commit. Brings the clap and clap-helpers submodules along.
fxme_dependency(ClapJuceExtensions https://github.com/free-audio/clap-juce-extensions
    7adee3a1bd4684d4caa5601100e364abccff4b4d
    SIBLING clap-juce-extensions)

add_subdirectory(${JUCE_DIR} JUCE)

# Registers the `FxmeTools` JUCE module, FxmeCore, fxmetools_attach() and
# fxmetools_add_clap().
include(${FXMETOOLS_DIR}/cmake/FxmeTools.cmake)
