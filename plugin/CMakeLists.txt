# The plug-in: the tracker with its UI drawn inside a Hollow skin (skin/), and a processor that pulls its mix
# (plugin.cpp, tracker_host.cpp). Built by the top-level CMakeLists.txt with MILKYTRACKER_PLUGIN on; every format
# lands in bin/<format>.

# The tracker without a platform main: src/tracker/CMakeLists.txt's sources and its commandlib's
set(T ${PROJECT_SOURCE_DIR}/src/tracker)
add_library(tracker_core STATIC
    ${T}/AnimatedFXControl.cpp ${T}/ColorExportImport.cpp ${T}/ColorPaletteContainer.cpp ${T}/fx/Convolver.cpp
    ${T}/DialogHelp.cpp ${T}/DialogChannelSelector.cpp ${T}/DialogEQ.cpp ${T}/DialogGroupSelection.cpp
    ${T}/DialogHandlers.cpp ${T}/DialogListBox.cpp ${T}/DialogPanning.cpp ${T}/DialogQuickChooseInstrument.cpp
    ${T}/DialogResample.cpp ${T}/DialogWithValues.cpp ${T}/DialogZap.cpp ${T}/DialogSliders.cpp ${T}/fx/EQConstants.cpp
    ${T}/EditorBase.cpp ${T}/EnvelopeContainer.cpp ${T}/EnvelopeEditor.cpp ${T}/EnvelopeEditorControl.cpp
    ${T}/fx/Equalizer.cpp ${T}/FileExtProvider.cpp ${T}/FileIdentificator.cpp ${T}/GlobalColorConfig.cpp
    ${T}/InputControlListener.cpp ${T}/LogoBig.cpp ${T}/LogoSmall.cpp ${T}/ModuleEditor.cpp ${T}/PatternEditor.cpp
    ${T}/PatternEditorClipBoard.cpp ${T}/PatternEditorControl.cpp ${T}/PatternEditorControlEventListener.cpp
    ${T}/PatternEditorControlKeyboard.cpp ${T}/PatternEditorControlTransposeHandler.cpp ${T}/PatternEditorTools.cpp
    ${T}/PatternTools.cpp ${T}/Addon.cpp ${T}/PeakLevelControl.cpp ${T}/Piano.cpp ${T}/PianoControl.cpp
    ${T}/PlayerController.cpp ${T}/PlayerLogic.cpp ${T}/PlayerMaster.cpp ${T}/RecPosProvider.cpp ${T}/RecorderLogic.cpp
    ${T}/ResamplerHelper.cpp ${T}/VolumeRampHelper.cpp ${T}/SampleEditor.cpp ${T}/SampleEditorControl.cpp
    ${T}/SampleEditorControlToolHandler.cpp ${T}/SampleEditorResampler.cpp ${T}/SamplePlayer.cpp ${T}/ScopesControl.cpp
    ${T}/SectionAbout.cpp ${T}/SectionAbstract.cpp ${T}/SectionAdvancedEdit.cpp ${T}/SectionDiskMenu.cpp
    ${T}/SectionHDRecorder.cpp ${T}/SectionInstruments.cpp ${T}/SectionOptimize.cpp ${T}/SectionQuickOptions.cpp
    ${T}/SectionSamples.cpp ${T}/SectionSettings.cpp ${T}/SectionSwitcher.cpp ${T}/SectionTranspose.cpp
    ${T}/SectionUpperLeft.cpp ${T}/SystemMessage.cpp ${T}/fx/Filter.cpp ${T}/TabHeaderControl.cpp ${T}/TabManager.cpp
    ${T}/TabTitleProvider.cpp ${T}/TitlePageManager.cpp ${T}/ToolInvokeHelper.cpp ${T}/Tracker.cpp ${T}/TrackerConfig.cpp
    ${T}/TrackerInit.cpp ${T}/TrackerKeyboard.cpp ${T}/TrackerSettings.cpp ${T}/TrackerSettingsDatabase.cpp
    ${T}/TrackerShortCuts.cpp ${T}/TrackerShutDown.cpp ${T}/TrackerStartUp.cpp ${T}/TrackerUpdate.cpp ${T}/Undo.cpp
    ${T}/VRand.cpp ${T}/Zapper.cpp
    ${T}/ModuleServices.cpp ${T}/SongLengthEstimator.cpp ${T}/wav/WAVExporter.cpp ${T}/wav/WAVExportArgs.cpp
    ${T}/wav/WAVUtils.cpp ${PROJECT_SOURCE_DIR}/src/cli/CLIParser.cpp)
if(SYNTH)
  target_sources(tracker_core PRIVATE ${T}/Synth.cpp ${T}/synth/SynthFM.cpp ${T}/Synths.cpp)
endif()
target_include_directories(tracker_core PUBLIC ${T} ${T}/wav ${PROJECT_BINARY_DIR}/src/tracker ${PROJECT_SOURCE_DIR}/src/cli)
target_link_libraries(tracker_core PUBLIC fx milkyplay ppui osinterface compression)
if(ZLIB_FOUND)
  target_link_libraries(tracker_core PUBLIC ${ZLIB_LIBRARIES})
endif()
if(WIN32)
  target_link_libraries(tracker_core PUBLIC comdlg32)
endif()

# The 32-bit DXi's DirectShow base classes (hollow_strmbase, made in this folder by hollow_add_plugin) need windows.h's
# min and max, which the tracker's NOMINMAX (the top-level CMakeLists.txt) takes away, so here it is the tracker's alone.
remove_definitions(-DNOMINMAX)
if(WIN32)
  target_compile_definitions(tracker_core PUBLIC NOMINMAX)
endif()

# The files' name (MilkyTrackerVST.clap, .vst3, .dll, .component, .exe); hosts list it and title it by plugin.cpp's
# name, MilkyTracker.vst, which a file name with a dot in it would muddle (MilkyTracker.vst.vst3)
set(binary MilkyTrackerVST)
hollow_add_plugin(milkytracker
  NAME "${binary}"
  DISPLAY_NAME "MilkyTracker.vst"
  SKIN "${CMAKE_CURRENT_SOURCE_DIR}/skin"
  SOURCES plugin.cpp tracker_host.cpp
  BUNDLE_ID studio.musica.milkytracker
  VERSION 0.1.0
  VENDOR musica.studio
  AU_MANUFACTURER Msst
  AU_SUBTYPE MkTr
  WINDOWS_ICON "${PROJECT_SOURCE_DIR}/resources/pictures/carton.ico"
  MACOS_ICON "${PROJECT_SOURCE_DIR}/resources/pictures/carton.icns"
  BIN "${PROJECT_SOURCE_DIR}/bin")
target_link_libraries(milkytracker_lib PRIVATE tracker_core)

# The processor driven as a host drives it, on one of the songs MilkyTracker ships (check_plugin.cpp)
add_executable(check_plugin check_plugin.cpp)
target_link_libraries(check_plugin PRIVATE milkytracker_lib)
enable_testing()
add_test(NAME plugin COMMAND check_plugin "${PROJECT_SOURCE_DIR}/resources/music/milky.xm")

# The editor driven as a mouse and a keyboard drive it, over the real skin and processor (check_gui.cpp)
add_executable(check_gui check_gui.cpp)
target_link_libraries(check_gui PRIVATE milkytracker_lib)
add_test(NAME gui COMMAND check_gui "${PROJECT_SOURCE_DIR}/resources/music/milky.xm")
set_tests_properties(gui PROPERTIES ENVIRONMENT "MT_GUI_SHOTS=${CMAKE_BINARY_DIR}/shots/gui")

# Each built format loaded as a host loads it, with an audio thread and a window of the platform's own (check_host.cpp),
# and Steinberg's VST3 validator; both on the hosting classes of the VST3 SDK that clap-wrapper fetched, which the
# 64-bit builds have. The 32-bit build checks its VST2 alone.
set(xm "${PROJECT_SOURCE_DIR}/resources/music/milky.xm")
set(bin "${PROJECT_SOURCE_DIR}/bin")
add_executable(check_host check_host.cpp)
target_include_directories(check_host PRIVATE "${PROJECT_SOURCE_DIR}/hollow/third_party")
target_compile_features(check_host PRIVATE cxx_std_17)
if(TARGET clap)
  target_link_libraries(check_host PRIVATE clap)
  target_compile_definitions(check_host PRIVATE MT_CLAP_HOST)
endif()
if(APPLE)
  set_source_files_properties(check_host.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(check_host PRIVATE "-framework Cocoa" "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio")
elseif(UNIX)
  find_package(X11 REQUIRED)
  target_link_libraries(check_host PRIVATE X11::X11 ${CMAKE_DL_LIBS} pthread)
endif()
if(TARGET base-sdk-vst3)
  get_target_property(sdk base-sdk-vst3 INTERFACE_INCLUDE_DIRECTORIES)
  list(GET sdk 0 sdk)
  set(h "${sdk}/public.sdk/source/vst/hosting")
  file(GLOB sdk_base "${sdk}/base/source/*.cpp" "${sdk}/base/thread/source/*.cpp" "${sdk}/pluginterfaces/base/*.cpp"
       "${sdk}/public.sdk/source/common/*.cpp")
  if(WIN32)
    set(module "${h}/module_win32.cpp")
  elseif(APPLE)
    set(module "${h}/module_mac.mm" "${sdk}/public.sdk/source/common/threadchecker_mac.mm")
    set_source_files_properties(${module} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  else()
    set(module "${h}/module_linux.cpp")
  endif()
  add_library(vst3_hosting STATIC ${sdk_base} ${module} "${h}/connectionproxy.cpp" "${h}/eventlist.cpp" "${h}/hostclasses.cpp"
      "${h}/module.cpp" "${h}/parameterchanges.cpp" "${h}/pluginterfacesupport.cpp" "${h}/plugprovider.cpp" "${h}/processdata.cpp"
      "${sdk}/public.sdk/source/vst/vstinitiids.cpp" "${sdk}/public.sdk/source/vst/utility/stringconvert.cpp"
      "${sdk}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp")
  target_include_directories(vst3_hosting PUBLIC "${sdk}")
  target_compile_definitions(vst3_hosting PUBLIC RELEASE=1)
  target_compile_features(vst3_hosting PUBLIC cxx_std_17)
  if(APPLE)
    target_link_libraries(vst3_hosting PUBLIC "-framework Cocoa")
  elseif(UNIX)
    target_link_libraries(vst3_hosting PUBLIC ${CMAKE_DL_LIBS} pthread)
  endif()
  target_link_libraries(check_host PRIVATE vst3_hosting)
  target_compile_definitions(check_host PRIVATE MT_VST3_HOST)

  set(v "${sdk}/public.sdk/samples/vst-hosting/validator/source")
  file(GLOB_RECURSE suite "${sdk}/public.sdk/source/vst/testsuite/*.cpp")
  file(GLOB hosting_tests "${h}/test/*.cpp")
  add_executable(steinberg_validator "${v}/main.cpp" "${v}/usediids.cpp" "${v}/validator.cpp" ${suite} ${hosting_tests}
      "${sdk}/public.sdk/source/main/moduleinit.cpp" "${sdk}/public.sdk/source/vst/utility/testing.cpp"
      "${sdk}/public.sdk/source/vst/vstpresetfile.cpp")
  target_link_libraries(steinberg_validator PRIVATE vst3_hosting)
  add_test(NAME vst3_validator COMMAND steinberg_validator "${bin}/VST3/${binary}.vst3")
  add_test(NAME host_vst3 COMMAND check_host vst3 "${bin}/VST3/${binary}.vst3" "${xm}" "${CMAKE_BINARY_DIR}/shots")
endif()
if(TARGET clap)
  add_test(NAME host_clap COMMAND check_host clap "${bin}/CLAP/${binary}.clap" "${xm}" "${CMAKE_BINARY_DIR}/shots")
endif()
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  add_test(NAME host_vst2 COMMAND check_host vst2 "${bin}/VST2 32-bit and DXi/${binary}.dll" "${xm}" "${CMAKE_BINARY_DIR}/shots")
elseif(WIN32)
  add_test(NAME host_vst2 COMMAND check_host vst2 "${bin}/VST2/${binary}.dll" "${xm}" "${CMAKE_BINARY_DIR}/shots")
  add_test(NAME host_standalone COMMAND check_host standalone "${bin}/Standalone/${binary}.exe" "${CMAKE_BINARY_DIR}/shots")
elseif(APPLE)
  add_test(NAME host_vst2 COMMAND check_host vst2 "${bin}/VST2/${binary}.vst" "${xm}" "${CMAKE_BINARY_DIR}/shots")
  add_test(NAME host_au COMMAND check_host au "${bin}/AU/${binary}.component" "${xm}" "${CMAKE_BINARY_DIR}/shots")
else()
  add_test(NAME host_vst2 COMMAND check_host vst2 "${bin}/VST2/${binary}.so" "${xm}" "${CMAKE_BINARY_DIR}/shots")
  add_test(NAME host_standalone COMMAND check_host standalone "${bin}/Standalone/${binary}" "${CMAKE_BINARY_DIR}/shots")
endif()
