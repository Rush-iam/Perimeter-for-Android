# XPrmLib is also used at runtime by generated game parameter sections.  The
# standalone XPrm executable and script processing are host-side development
# tools, so Android omits only the latter.  The option is supplied only by the
# Android wrapper; normal desktop defaults remain unchanged.
ADD_SUBDIRECTORY("XPrm")
if(NOT (ANDROID AND PERIMETER_ANDROID_SKIP_DEVELOPMENT_TARGETS))
    ADD_SUBDIRECTORY("Scripts")
endif()
ADD_SUBDIRECTORY("XTool")
ADD_SUBDIRECTORY("PluginMAX")
ADD_SUBDIRECTORY("Terra")
ADD_SUBDIRECTORY("tx3d")
ADD_SUBDIRECTORY("Render")
ADD_SUBDIRECTORY("Sound")
ADD_SUBDIRECTORY("Network")
ADD_SUBDIRECTORY("Util")
ADD_SUBDIRECTORY("Units")
ADD_SUBDIRECTORY("UserInterface")
ADD_SUBDIRECTORY("Physics")
ADD_SUBDIRECTORY("AI")
ADD_SUBDIRECTORY("GeoLogic")
ADD_SUBDIRECTORY("HT")
ADD_SUBDIRECTORY("Integrations")

IF(PERIMETER_EXODUS)
    ADD_SUBDIRECTORY("Exodus")
ENDIF()

if(NOT OPTION_FINAL_VERSION)
    ADD_SUBDIRECTORY("TriggerEditor")
ENDIF()

IF(ANDROID)
    add_library(perimeter SHARED)
ELSE()
    add_executable(perimeter ${PERIMETER_EXE_FLAGS})
ENDIF()

ADD_SUBDIRECTORY("Game")
