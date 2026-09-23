cmake_minimum_required(VERSION 3.15)

project(mdJucePlugin VERSION ${CMAKE_PROJECT_VERSION})

execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	OUTPUT_VARIABLE MDMM_DIAGNOSTICS_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT MDMM_DIAGNOSTICS_REVISION)
	set(MDMM_DIAGNOSTICS_REVISION "source-archive")
endif()
execute_process(COMMAND git diff --quiet HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	RESULT_VARIABLE MDMM_DIAGNOSTICS_DIRTY ERROR_QUIET)
if(MDMM_DIAGNOSTICS_DIRTY)
	string(APPEND MDMM_DIAGNOSTICS_REVISION "-modified")
endif()
add_compile_definitions(MDMM_DIAGNOSTICS_REVISION="${MDMM_DIAGNOSTICS_REVISION}")

set(SOURCES
	mdCombinedEditor.cpp mdCombinedEditor.h
	mdCombinedProcessor.cpp mdCombinedProcessor.h
	mdController.cpp mdController.h
	mdEditor.cpp mdEditor.h
	mdFrontPanelPresentation.h
	mdLcdGesture.h
	mdLcdInteractionModel.cpp mdLcdInteractionModel.h
	mdLcdViewport.h
	mdMaschineNihiaClient.cpp mdMaschineNihiaClient.h
	mdMaschineNihiaProtocol.cpp mdMaschineNihiaProtocol.h
	mdMaschineScreenRenderer.cpp mdMaschineScreenRenderer.h
	mdMaschineController.cpp mdMaschineController.h
	mdMaschineBankPreview.h mdMaschineRecordedSteps.h
	mdMaschineSectionPlayback.h mdMaschineStripLeds.h
	mdPixelPerfectPanel.cpp mdPixelPerfectPanel.h
	mdPanelAffordances.h
	mdPluginEditorState.cpp mdPluginEditorState.h
	mdProductSkinPolicy.h
	mdProductSkins.h
	mdPluginProcessor.cpp mdPluginProcessor.h
	mdStorageImage.cpp mdStorageImage.h
	mdPluginEntry.cpp

	parameterDescriptions_md.json parameterDescriptions_mm.json

	skins/mdDefault/mdDefault.rml
	skins/mdDefault/mdKnobs.rcss
	skins/mdDefault/mdDefault.rcss
	skins/mmSfx60/mmSfx60.rml
	skins/mmSfx60/mmKnobAtlas.svg
	skins/mmSfx60/mmKnobs.rcss
	skins/mmSfx60/mmSfx60.rcss
	mdSettingsPanelFeel.cpp mdSettingsPanelFeel.h
	mdSettingsAudioInput.cpp mdSettingsAudioInput.h
)

set(COMMON_ASSETS
	"../../../jucePluginData/elektronKnobBlankAtlas.png"
	"../../../jucePluginData/elektronMasterKnobAtlas.png"
	"../../../jucePluginData/elektronButtonBevelAtlas.png"
	"../../../jucePluginData/elektronLedLens.png"
	"../../../jucePluginData/elektronLedLensSoft.png"
	"../../../jucePluginData/elektronPlayTriangle.png"
	"parameterDescriptions_md.json"
	"parameterDescriptions_mm.json"
	"../../../jucePluginData/elektron_panel.rcss"
	"../../../jucePluginData/elektron_materials.rcss")
file(GLOB MD_SKIN_ASSETS CONFIGURE_DEPENDS
	"skins/mdDefault/*.png" "skins/mdDefault/*.rml" "skins/mdDefault/*.rcss"
	"skins/mdDefault/*.ttf" "skins/mdDefault/*.lua")
file(GLOB MM_SKIN_ASSETS CONFIGURE_DEPENDS
	"skins/mmSfx60/*.png" "skins/mmSfx60/*.rml" "skins/mmSfx60/*.rcss"
	"skins/mmSfx60/*.ttf" "skins/mmSfx60/*.lua")

juce_add_binary_data(mdJucePlugin_BinaryData SOURCES
	${COMMON_ASSETS} "tus_settings_gui_Machinedrum.rml"
	"tus_settings_dspaudio_Machinedrum.rml" ${MD_SKIN_ASSETS})
juce_add_binary_data(mmJucePlugin_BinaryData SOURCES
	${COMMON_ASSETS} "tus_settings_gui_Monomachine.rml"
	"tus_settings_dspaudio_Monomachine.rml" ${MM_SKIN_ASSETS})
juce_add_binary_data(mdmmJucePlugin_BinaryData SOURCES
	${COMMON_ASSETS}
	"tus_settings_gui_Machinedrum.rml" "tus_settings_dspaudio_Machinedrum.rml"
	"tus_settings_gui_Monomachine.rml" "tus_settings_dspaudio_Monomachine.rml"
	${MD_SKIN_ASSETS} ${MM_SKIN_ASSETS})

createJucePlugin(mdJucePlugin "Gearmulator MD" TRUE "Tmdr" mdJucePlugin_BinaryData mdLib)
createJucePlugin(mmJucePlugin "Gearmulator MM" TRUE "Tmno" mmJucePlugin_BinaryData mdLib)
createJucePlugin(mdmmJucePlugin "Maschine MD-MM" TRUE "Tmdm" mdmmJucePlugin_BinaryData mdLib)
# Retain the existing audio-device/settings file across the product rename.
if(TARGET mdmmJucePlugin_Standalone)
	target_compile_definitions(mdmmJucePlugin_Standalone PRIVATE
		JUCE_STANDALONE_SETTINGS_NAME="Gearmulator MD-MM")
endif()
target_compile_definitions(mdmmJucePlugin PRIVATE
	MD_JUCEPLUGIN_COMBINED=1
	JUCE_STANDALONE_PLUGIN_DEFAULT_BUFFER_SIZE=512)

if(APPLE)
	find_library(MD_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
	foreach(plugin_target mdJucePlugin mmJucePlugin mdmmJucePlugin)
		target_link_libraries(${plugin_target} PRIVATE ${MD_COREFOUNDATION_FRAMEWORK})
	endforeach()
endif()

# MD/MM expose an optional stereo input to plug-in hosts. Starting the
# standalone with that input active makes CoreAudio combine the built-in input
# and output devices on common Mac setups, which can consume the real-time
# deadline. Keep the input selectable, but start new standalone configurations
# with output only.
foreach(plugin_target mdJucePlugin mmJucePlugin mdmmJucePlugin)
	target_compile_definitions(${plugin_target} PUBLIC
		JUCE_STANDALONE_PLUGIN_AUTO_OPEN_AUDIO_INPUT=0
		JUCE_VST3_EMULATE_MIDI_PC_WITH_PROGRAMS=1)
endforeach()

# JUCE can populate bundle resources after its first signing pass on macOS.
# Re-sign the completed bundles so hosts and Finder do not reject their final
# on-disk contents. Prefer an installed Developer ID for local builds: an
# ad-hoc signature changes identity whenever the executable changes, which
# makes macOS ask for microphone/network permissions again after every build.
if(APPLE)
	set(GEARMULATOR_LOCAL_CODESIGN_IDENTITY "" CACHE STRING
		"Identity used to sign local Gearmulator bundles; use '-' to force ad-hoc signing")
	set(_gearmulator_codesign_identity "${GEARMULATOR_LOCAL_CODESIGN_IDENTITY}")
	if(NOT _gearmulator_codesign_identity)
		execute_process(
			COMMAND /usr/bin/security find-identity -v -p codesigning
			OUTPUT_VARIABLE _gearmulator_codesign_identities
			ERROR_QUIET)
		string(REGEX MATCH
			"\"Developer ID Application: [^\"]+\""
			_gearmulator_codesign_match "${_gearmulator_codesign_identities}")
		if(_gearmulator_codesign_match)
			string(REGEX REPLACE "^\"|\"$" ""
				_gearmulator_codesign_identity "${_gearmulator_codesign_match}")
		endif()
	endif()
	if(NOT _gearmulator_codesign_identity)
		set(_gearmulator_codesign_identity "-")
	endif()
	message(STATUS "Local Gearmulator code-signing identity: ${_gearmulator_codesign_identity}")
	if(NOT _gearmulator_codesign_identity STREQUAL "-")
		string(REGEX REPLACE ".*\\(([A-Z0-9]+)\\)$" "\\1"
			_gearmulator_codesign_team "${_gearmulator_codesign_identity}")
	endif()

	foreach(plugin_target
		mdJucePlugin_VST3 mmJucePlugin_VST3
		mdmmJucePlugin_VST3
		mdJucePlugin_AU mmJucePlugin_AU
		mdmmJucePlugin_AU
		mdJucePlugin_Standalone mmJucePlugin_Standalone
		mdmmJucePlugin_Standalone)
		if(TARGET ${plugin_target})
			if(_gearmulator_codesign_identity STREQUAL "-")
				add_custom_command(TARGET ${plugin_target} POST_BUILD
					COMMAND /usr/bin/codesign --force --deep --sign -
						"$<TARGET_BUNDLE_DIR:${plugin_target}>"
					COMMENT "Ad-hoc signing completed ${plugin_target} bundle"
					VERBATIM)
			else()
				# TARGET POST_BUILD is an Xcode shell phase and can run before
				# Xcode's own CodeSign phase. Configure that final phase too, so
				# it cannot replace the stable signature with "Sign to Run Locally".
				set_target_properties(${plugin_target} PROPERTIES
					XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${_gearmulator_codesign_identity}"
					XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
					XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO"
					XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${_gearmulator_codesign_team}"
					XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
					XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
						"${CMAKE_CURRENT_SOURCE_DIR}/macos-local.entitlements")
				add_custom_command(TARGET ${plugin_target} POST_BUILD
					COMMAND /usr/bin/codesign --force --deep --options runtime
						--entitlements "${CMAKE_CURRENT_SOURCE_DIR}/macos-local.entitlements"
						--sign "${_gearmulator_codesign_identity}"
						"$<TARGET_BUNDLE_DIR:${plugin_target}>"
					COMMENT "Stable signing completed ${plugin_target} bundle"
					VERBATIM)
			endif()
		endif()
	endforeach()
endif()

# Build an independent MM product from the shared editor/controller code.
target_compile_definitions(mmJucePlugin PRIVATE MD_JUCEPLUGIN_MONOMACHINE=1)

if(BUILD_TESTING)
	juce_add_plugin(mdProgramChangeProbe
		COMPANY_NAME "Gearmulator" IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE
		NEEDS_MIDI_OUTPUT FALSE COPY_PLUGIN_AFTER_BUILD FALSE
		PLUGIN_MANUFACTURER_CODE GmPv PLUGIN_CODE Tmpc FORMATS VST3
		PRODUCT_NAME "Gearmulator MIDI Program Probe")
	target_sources(mdProgramChangeProbe PRIVATE ../../../pluginTester/programChange/programChangeProbe.cpp)
	removeJuceDependencies(mdProgramChangeProbe)
	target_link_libraries(mdProgramChangeProbe PRIVATE juce_plugin_modules)
	target_compile_definitions(mdProgramChangeProbe PUBLIC
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_VST3_EMULATE_MIDI_PC_WITH_PROGRAMS=1)
	add_test(NAME mdVst3ProgramChangeTest COMMAND vst3ProgramChangeTest
		$<GENEX_EVAL:$<TARGET_PROPERTY:mdProgramChangeProbe_VST3,JUCE_PLUGIN_ARTEFACT_FILE>> --fixture)
	set_tests_properties(mdVst3ProgramChangeTest PROPERTIES LABELS "PluginTest;MIDI")
	juce_add_plugin(mdProgramChangeLegacyProbe
		COMPANY_NAME "Gearmulator" IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE
		NEEDS_MIDI_OUTPUT FALSE COPY_PLUGIN_AFTER_BUILD FALSE
		PLUGIN_MANUFACTURER_CODE GmPv PLUGIN_CODE Tmpl FORMATS VST3
		PRODUCT_NAME "Gearmulator Legacy MIDI Program Probe")
	target_sources(mdProgramChangeLegacyProbe PRIVATE ../../../pluginTester/programChange/programChangeProbe.cpp)
	removeJuceDependencies(mdProgramChangeLegacyProbe)
	target_link_libraries(mdProgramChangeLegacyProbe PRIVATE juce_plugin_modules)
	target_compile_definitions(mdProgramChangeLegacyProbe PUBLIC JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	add_dependencies(mdProgramChangeProbe_VST3 mdProgramChangeLegacyProbe_VST3 vst3ProgramChangeTest)
	add_test(NAME mdVst3ProgramChangeOptOutTest COMMAND vst3ProgramChangeTest
		$<GENEX_EVAL:$<TARGET_PROPERTY:mdProgramChangeLegacyProbe_VST3,JUCE_PLUGIN_ARTEFACT_FILE>> --legacy)
	set_tests_properties(mdVst3ProgramChangeOptOutTest PROPERTIES LABELS "PluginTest;MIDI")
	# Check the shipping targets as well as the probes: a probe's independent
	# opt-in must not hide a missing definition on either production wrapper.
	foreach(plugin_target mdJucePlugin mmJucePlugin)
		# The deep JUCE sanitizer build disables shipping plugin formats.
		if(TARGET ${plugin_target}_VST3)
			add_test(NAME ${plugin_target}_VST3ProgramChangeTest COMMAND vst3ProgramChangeTest
				$<GENEX_EVAL:$<TARGET_PROPERTY:${plugin_target}_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>)
			set_tests_properties(${plugin_target}_VST3ProgramChangeTest PROPERTIES
				LABELS "PluginTest;MIDI"
				TIMEOUT 30
				ENVIRONMENT "GEARMULATOR_DATA_ROOT=${CMAKE_CURRENT_BINARY_DIR}/program-metadata/${plugin_target}")
		endif()
	endforeach()

	add_executable(mdLcdInteractionModelTest
		mdLcdInteractionModelTest.cpp mdLcdInteractionModel.cpp)
	target_link_libraries(mdLcdInteractionModelTest PRIVATE mdLib)
	add_test(NAME mdLcdInteractionModelTest COMMAND mdLcdInteractionModelTest)
	set_tests_properties(mdLcdInteractionModelTest PROPERTIES
		LABELS "UnitTest;LcdInteraction")
	set_property(TARGET mdLcdInteractionModelTest PROPERTY FOLDER "Elektron/test")
	set(MD_LCD_TEST_CORPUS_DIR "" CACHE PATH
		"Optional private MD/MM LCD screenshot corpus used by the exhaustive classifier replay")
	if(MD_LCD_TEST_CORPUS_DIR)
		add_executable(mdLcdInteractionCorpusTest
			mdLcdInteractionCorpusTest.cpp mdLcdInteractionModel.cpp)
		target_link_libraries(mdLcdInteractionCorpusTest PRIVATE mdLib)
		target_compile_definitions(mdLcdInteractionCorpusTest PRIVATE
			MD_LCD_TEST_CORPUS_DIR="${MD_LCD_TEST_CORPUS_DIR}")
		add_test(NAME mdLcdInteractionCorpusTest COMMAND mdLcdInteractionCorpusTest)
		set_tests_properties(mdLcdInteractionCorpusTest PROPERTIES
			LABELS "LcdInteraction;LocalCorpus")
		set_property(TARGET mdLcdInteractionCorpusTest PROPERTY FOLDER "Elektron/test")
	endif()

	add_executable(mdLcdEditorPointerTest mdLcdEditorPointerTest.cpp)
	target_include_directories(mdLcdEditorPointerTest PRIVATE
		"${CMAKE_SOURCE_DIR}/source" "${CMAKE_SOURCE_DIR}/source/elektron/md")
	target_link_libraries(mdLcdEditorPointerTest PRIVATE mdJucePlugin
		jucePluginEditorLib juceRmlUi juceUiLib juce_plugin_modules mdLib)
	add_test(NAME mdLcdEditorPointerTest COMMAND mdLcdEditorPointerTest)
	set_tests_properties(mdLcdEditorPointerTest PROPERTIES
		LABELS "IntegrationTest;LcdInteraction;PointerInput")
	set_property(TARGET mdLcdEditorPointerTest PROPERTY FOLDER "Elektron/test")

	add_executable(mmLcdEditorPointerTest mdLcdEditorPointerTest.cpp)
	target_compile_definitions(mmLcdEditorPointerTest PRIVATE MD_LCD_POINTER_TEST_MM=1)
	target_include_directories(mmLcdEditorPointerTest PRIVATE
		"${CMAKE_SOURCE_DIR}/source" "${CMAKE_SOURCE_DIR}/source/elektron/md")
	target_link_libraries(mmLcdEditorPointerTest PRIVATE mmJucePlugin
		jucePluginEditorLib juceRmlUi juceUiLib juce_plugin_modules mdLib)
	add_test(NAME mmLcdEditorPointerTest COMMAND mmLcdEditorPointerTest)
	set_tests_properties(mmLcdEditorPointerTest PROPERTIES
		LABELS "IntegrationTest;LcdInteraction;PointerInput")
	set_property(TARGET mmLcdEditorPointerTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdStandaloneRendererPolicyTest mdStandaloneRendererPolicyTest.cpp)
	add_test(NAME mdStandaloneRendererPolicyTest COMMAND mdStandaloneRendererPolicyTest)
	set_tests_properties(mdStandaloneRendererPolicyTest PROPERTIES
		LABELS "UnitTest;Rendering")
	set_property(TARGET mdStandaloneRendererPolicyTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdPanelRenderingTest mdPanelRenderingTest.cpp mdPixelPerfectPanel.cpp)
	target_link_libraries(mdPanelRenderingTest PRIVATE juceRmlUi juceUiLib juce_plugin_modules juce::juce_opengl)
	target_compile_definitions(mdPanelRenderingTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	add_test(NAME mdPanelRenderingTest COMMAND mdPanelRenderingTest)
	set_tests_properties(mdPanelRenderingTest PROPERTIES LABELS "UnitTest;Rendering")
	set_property(TARGET mdPanelRenderingTest PROPERTY FOLDER "Elektron/test")

	juce_add_plugin(mdAudioProbePlugin
		COMPANY_NAME "Gearmulator"
		IS_SYNTH TRUE
		NEEDS_MIDI_INPUT TRUE
		NEEDS_MIDI_OUTPUT FALSE
		IS_MIDI_EFFECT FALSE
		COPY_PLUGIN_AFTER_BUILD FALSE
		PLUGIN_MANUFACTURER_CODE GmPv
		PLUGIN_CODE Taip
		FORMATS VST3
		PRODUCT_NAME "Gearmulator Audio I-O Probe")
	target_sources(mdAudioProbePlugin PRIVATE mdAudioProbePlugin.cpp)
	removeJuceDependencies(mdAudioProbePlugin)
	target_link_libraries(mdAudioProbePlugin PRIVATE
		jucePluginLib juce_plugin_modules synthLib)
	target_compile_definitions(mdAudioProbePlugin PUBLIC
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

	add_executable(mdAudioIoLayoutTest mdAudioIoLayoutTest.cpp)
	# Exercise the portable measurement tools in the existing platform CI gate.
	add_dependencies(mdAudioIoLayoutTest latency_host mdLatencyFixture)
	target_link_libraries(mdAudioIoLayoutTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_compile_definitions(mdAudioIoLayoutTest PRIVATE
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		JUCE_STANDALONE_PLUGIN_AUTO_OPEN_AUDIO_INPUT=0)
	set_property(TARGET mdAudioIoLayoutTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdAudioIoLayoutTest COMMAND mdAudioIoLayoutTest)
	set_tests_properties(mdAudioIoLayoutTest PROPERTIES LABELS "UnitTest;AudioIo")

	foreach(plugin_target mdJucePlugin_VST3 mmJucePlugin_VST3)
		if(TARGET ${plugin_target} AND TARGET pluginTester)
			add_test(NAME ${plugin_target}AudioIoTest
				COMMAND $<TARGET_FILE:pluginTester> -verify-audio-buses
					-blocks 16 -plugin $<TARGET_FILE:${plugin_target}>)
			set_tests_properties(${plugin_target}AudioIoTest PROPERTIES
				LABELS "PluginTest;AudioIo")
		endif()
	endforeach()
	if(TARGET mdAudioProbePlugin_VST3 AND TARGET pluginTester)
		add_test(NAME mdAudioProbePluginVST3IdentityTest
			COMMAND $<TARGET_FILE:pluginTester> -verify-audio-identity
				-blocks 0 -samplerate 48000 -blocksize 64
				-plugin $<TARGET_FILE:mdAudioProbePlugin_VST3>)
		set_tests_properties(mdAudioProbePluginVST3IdentityTest PROPERTIES
			LABELS "PluginTest;AudioIo")
	endif()

	add_executable(mdFrontPanelPresentationTest mdFrontPanelPresentationTest.cpp)
	target_link_libraries(mdFrontPanelPresentationTest PRIVATE mdLib)
	add_test(NAME mdFrontPanelPresentationTests COMMAND mdFrontPanelPresentationTest)
	set_tests_properties(mdFrontPanelPresentationTests PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdFrontPanelPresentationTest PROPERTY FOLDER "Elektron")

	add_executable(mdMaschineScreenRendererTest
		mdMaschineScreenRendererTest.cpp mdMaschineScreenRenderer.cpp)
	add_executable(mdCombinedMidiRouterTest mdCombinedMidiRouterTest.cpp)
	target_link_libraries(mdCombinedMidiRouterTest PRIVATE mdLib)
	add_test(NAME mdCombinedMidiRouterTest COMMAND mdCombinedMidiRouterTest)
	set_tests_properties(mdCombinedMidiRouterTest PROPERTIES LABELS "UnitTest;Maschine")
	add_executable(mdMaschineNihiaProtocolTest
		mdMaschineNihiaProtocolTest.cpp mdMaschineNihiaProtocol.cpp
		mdMaschineScreenRenderer.cpp)
	target_link_libraries(mdMaschineNihiaProtocolTest PRIVATE mdLib)
	add_test(NAME mdMaschineNihiaProtocolTest COMMAND mdMaschineNihiaProtocolTest)
	set_tests_properties(mdMaschineNihiaProtocolTest PROPERTIES
		LABELS "UnitTest;Maschine")
	set_property(TARGET mdMaschineNihiaProtocolTest PROPERTY FOLDER "Elektron/test")
	if(APPLE)
		add_executable(mdMaschineHardwareProbe EXCLUDE_FROM_ALL
			mdMaschineHardwareProbe.cpp mdMaschineNihiaClient.cpp
			mdMaschineNihiaProtocol.cpp mdMaschineScreenRenderer.cpp)
		target_link_libraries(mdMaschineHardwareProbe PRIVATE mdLib
			${MD_COREFOUNDATION_FRAMEWORK})
		set_property(TARGET mdMaschineHardwareProbe PROPERTY FOLDER "Elektron/test")
	endif()
	target_link_libraries(mdMaschineScreenRendererTest PRIVATE mdLib)
	add_test(NAME mdMaschineScreenRendererTest COMMAND mdMaschineScreenRendererTest)
	set_tests_properties(mdMaschineScreenRendererTest PROPERTIES
		LABELS "UnitTest;Maschine")
	set_property(TARGET mdMaschineScreenRendererTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdAutomationParameterTest mdAutomationParameterTest.cpp)
	target_link_libraries(mdAutomationParameterTest PRIVATE
		jucePluginLib mdLib juce_plugin_modules)
	target_compile_definitions(mdAutomationParameterTest PRIVATE
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		MD_AUTOMATION_PARAMETER_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
	set_property(TARGET mdAutomationParameterTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdAutomationParameterTest COMMAND mdAutomationParameterTest)

	function(add_md_automation_firmware_test TARGET_NAME SOURCE_FILE)
		add_executable(${TARGET_NAME} ${SOURCE_FILE} mdAutomationTestSupport.h)
		target_link_libraries(${TARGET_NAME} PRIVATE
			mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
			juce::juce_opengl)
		target_include_directories(${TARGET_NAME} PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/../../..)
		target_compile_definitions(${TARGET_NAME} PRIVATE
			JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
		set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Elektron/test")
	endfunction()

	add_md_automation_firmware_test(mdSysexLifecycleTest mdSysexLifecycleTest.cpp)
	add_test(NAME mdSysexLifecycleTest COMMAND mdSysexLifecycleTest)
	set_tests_properties(mdSysexLifecycleTest PROPERTIES
		LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 600)

	add_md_automation_firmware_test(mdAutomationFirmwareTest
		mdAutomationFirmwareTest.cpp)
	add_test(NAME mdAutomationFirmwareTest COMMAND mdAutomationFirmwareTest)
	set_tests_properties(mdAutomationFirmwareTest PROPERTIES
		LABELS "Integration;FirmwareTest"
		SKIP_RETURN_CODE 77)

	add_md_automation_firmware_test(mdProgramChangeFirmwareTest
		mdProgramChangeFirmwareTest.cpp)
	add_test(NAME mdProgramChangeFirmwareTest COMMAND mdProgramChangeFirmwareTest)
	set_tests_properties(mdProgramChangeFirmwareTest PROPERTIES
		LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	add_md_automation_firmware_test(mdAutomationRobustnessTest
		mdAutomationRobustnessTest.cpp)
	add_test(NAME mdAutomationRobustnessTest COMMAND mdAutomationRobustnessTest)
	set_tests_properties(mdAutomationRobustnessTest PROPERTIES
		LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
	add_test(NAME mdAutomationArchitectureTest COMMAND mdAutomationRobustnessTest
		--architecture-only)
	set_tests_properties(mdAutomationArchitectureTest PROPERTIES
		LABELS "Unit" TIMEOUT 60)

	add_md_automation_firmware_test(mdAutomationSoakTest
		mdAutomationSoakTest.cpp)
	add_test(NAME mdAutomationSoakTest COMMAND mdAutomationSoakTest
		--soak-writes=256 --multi-writes=128)
	set_tests_properties(mdAutomationSoakTest PROPERTIES
		LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	option(MD_AUTOMATION_REGISTER_FULL_SOAK_TEST
		"Register the full MD/MM automation soak with CTest" OFF)
	if(MD_AUTOMATION_REGISTER_FULL_SOAK_TEST)
		add_test(NAME mdAutomationFullSoakTest COMMAND mdAutomationSoakTest
			--soak-writes=8192 --multi-writes=2048)
		set_tests_properties(mdAutomationFullSoakTest PROPERTIES
			LABELS "Integration;FirmwareTest;Soak"
			SKIP_RETURN_CODE 77 TIMEOUT 900)
	endif()

	add_executable(mdProjectStateRestoreTest mdProjectStateRestoreTest.cpp)
	target_link_libraries(mdProjectStateRestoreTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_include_directories(mdProjectStateRestoreTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdProjectStateRestoreTest PRIVATE
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	# Keep the restored-audio NaN/Inf checks effective in optimized test builds.
	if(MSVC)
		target_compile_options(mdProjectStateRestoreTest PRIVATE /fp:precise)
	else()
		target_compile_options(mdProjectStateRestoreTest PRIVATE -fno-fast-math)
	endif()
	add_test(NAME mdProjectStateRestoreTest COMMAND mdProjectStateRestoreTest)
	set_tests_properties(mdProjectStateRestoreTest PROPERTIES
		LABELS "Integration;FirmwareTest"
		SKIP_RETURN_CODE 77
		TIMEOUT 600)
	set_property(TARGET mdProjectStateRestoreTest PROPERTY FOLDER "Elektron/test")
endif()
