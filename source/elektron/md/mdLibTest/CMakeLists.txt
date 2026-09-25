cmake_minimum_required(VERSION 3.15)

add_executable(mdLibTest mdRuntimeTest.cpp)
target_link_libraries(mdLibTest PRIVATE mdLib)

add_executable(mmBootFirmwareTest mmBootFirmwareTest.cpp)
target_link_libraries(mmBootFirmwareTest PRIVATE mdLib)
add_test(NAME mmBootFirmwareTest COMMAND mmBootFirmwareTest)
set_tests_properties(mmBootFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 180)
set_property(TARGET mmBootFirmwareTest PROPERTY FOLDER "Elektron/test")
add_dependencies(mdLibTest mmBootFirmwareTest)

add_test(NAME mdLibTests COMMAND mdLibTest)
set_tests_properties(mdLibTests PROPERTIES LABELS "UnitTest")

set_property(TARGET mdLibTest PROPERTY FOLDER "Elektron")

add_executable(mdUartRegisterTest uartRegisterTest.cpp)
target_link_libraries(mdUartRegisterTest PRIVATE mdLib)
add_test(NAME mdUartRegisterTest COMMAND mdUartRegisterTest)
set_tests_properties(mdUartRegisterTest PROPERTIES LABELS "UnitTest;Uart" TIMEOUT 10)
set_property(TARGET mdUartRegisterTest PROPERTY FOLDER "Elektron/test")

# Compile Sim directly so the synthetic CPU uses its own memory callbacks.
add_executable(mdUartCpuInterruptTest uartCpuInterruptTest.cpp ../mdLib/mdsim.cpp)
target_link_libraries(mdUartCpuInterruptTest PRIVATE 68kEmu)
target_include_directories(mdUartCpuInterruptTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_test(NAME mdUartCpuInterruptTest COMMAND mdUartCpuInterruptTest)
set_tests_properties(mdUartCpuInterruptTest PROPERTIES LABELS "UnitTest;Uart" TIMEOUT 10)
set_property(TARGET mdUartCpuInterruptTest PROPERTY FOLDER "Elektron/test")

add_executable(mdTurboMidiUnitTest turboMidiTest.cpp)
find_package(Threads REQUIRED)
target_link_libraries(mdTurboMidiUnitTest PRIVATE mdLib Threads::Threads)
add_test(NAME mdTurboMidiUnitTest COMMAND mdTurboMidiUnitTest)
set_tests_properties(mdTurboMidiUnitTest PROPERTIES LABELS "UnitTest;Midi")
set_property(TARGET mdTurboMidiUnitTest PROPERTY FOLDER "Elektron")

# Manual peer-register check for MD 1.63/MM 1.32b, independent of the sender.
# Requires locally supplied ROM and cache/patch fixtures; no firmware is bundled.
add_executable(mdTurboMidiFirmwareTest turboMidiFirmwareTest.cpp)
target_link_libraries(mdTurboMidiFirmwareTest PRIVATE mdLib)
set_property(TARGET mdTurboMidiFirmwareTest PROPERTY FOLDER "Elektron/test")

# Manual acceptance test: user-supplied firmware/data paths make this unsuitable
# for ctest, but it exercises the same validated sender against real firmware and
# requires an observable machine-state mutation after the UART drains.
add_executable(mdUserSysexFirmwareTest userSysexFirmwareTest.cpp)
target_link_libraries(mdUserSysexFirmwareTest PRIVATE mdLib)
set_property(TARGET mdUserSysexFirmwareTest PROPERTY FOLDER "Elektron/test")

add_executable(mdMidiTimingTest midiTimingTest.cpp)
target_link_libraries(mdMidiTimingTest PRIVATE mdLib)
add_test(NAME mdMidiTimingTest COMMAND mdMidiTimingTest)
add_test(NAME mdMidiTimingFirmwareTest COMMAND mdMidiTimingTest --firmware)
set_tests_properties(mdMidiTimingTest PROPERTIES LABELS "UnitTest;Midi;Timing" FIXTURES_SETUP MdMidiTiming)
set_tests_properties(mdMidiTimingFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Midi;Timing" SKIP_RETURN_CODE 77 TIMEOUT 90)
set_property(TARGET mdMidiTimingTest PROPERTY FOLDER "Elektron/test")
add_dependencies(mdLibTest mdMidiTimingTest)

add_executable(mdHostRxTimingTest hostRxTimingTest.cpp)
target_link_libraries(mdHostRxTimingTest PRIVATE mdLib)
add_test(NAME mdHostRxTimingTest COMMAND mdHostRxTimingTest)
set_tests_properties(mdHostRxTimingTest PROPERTIES LABELS "UnitTest")
set_property(TARGET mdHostRxTimingTest PROPERTY FOLDER "Elektron")

add_executable(mdHostRxFirmwareTest hostRxFirmwareTest.cpp)
target_link_libraries(mdHostRxFirmwareTest PRIVATE mdLib)
add_test(NAME mdHostRxFirmwareTest COMMAND mdHostRxFirmwareTest)
set_tests_properties(mdHostRxFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 60)
set_property(TARGET mdHostRxFirmwareTest PROPERTY FOLDER "Elektron/test")

add_executable(mdIdleSelfBranchTest idleSelfBranchTest.cpp)
target_link_libraries(mdIdleSelfBranchTest PRIVATE mdLib)
add_test(NAME mdIdleSelfBranchTest COMMAND mdIdleSelfBranchTest)
set_tests_properties(mdIdleSelfBranchTest PROPERTIES LABELS "UnitTest;Timing")
set_property(TARGET mdIdleSelfBranchTest PROPERTY FOLDER "Elektron/test")

add_executable(mdIdleSchedulerFirmwareTest idleSchedulerFirmwareTest.cpp)
target_link_libraries(mdIdleSchedulerFirmwareTest PRIVATE mdLib)
add_test(NAME mdIdleSchedulerFirmwareTest COMMAND mdIdleSchedulerFirmwareTest)
# Both models now run 192 paired-machine comparisons each. The old 120-second
# limit covered one model; retain all 384 cases with proportional hang-guard
# headroom. This allocation-heavy fixture is not a realtime performance gate.
set_tests_properties(mdIdleSchedulerFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Timing" SKIP_RETURN_CODE 77 TIMEOUT 240)
set_property(TARGET mdIdleSchedulerFirmwareTest PROPERTY FOLDER "Elektron/test")

add_executable(mdPanelReadinessFirmwareTest panelReadinessFirmwareTest.cpp)
target_link_libraries(mdPanelReadinessFirmwareTest PRIVATE mdLib)
add_test(NAME mdPanelReadinessFirmwareTest COMMAND mdPanelReadinessFirmwareTest)
set_tests_properties(mdPanelReadinessFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Panel" SKIP_RETURN_CODE 77 TIMEOUT 180)
set_property(TARGET mdPanelReadinessFirmwareTest PROPERTY FOLDER "Elektron/test")

if(BUILD_TESTING)
	# The existing CI gate builds mdLibTest and selects only mdLibTests.
	# Build its prerequisites and let CTest fixtures include them in that
	# focused run, so a core/receive regression cannot silently skip the gate.
	add_dependencies(mdLibTest mc68kColdFireTimingTest mc68kHdi08ReceiveTest
		mc68kColdFireDivideTest mdHostRxTimingTest mdHostRxFirmwareTest
		mdIdleSelfBranchTest mdIdleSchedulerFirmwareTest mdPanelReadinessFirmwareTest
		mdUartRegisterTest mdUartCpuInterruptTest)
	set_tests_properties(mdHostRxTimingTest PROPERTIES FIXTURES_SETUP MdTimedHostRx)
	set_tests_properties(mdIdleSelfBranchTest PROPERTIES FIXTURES_SETUP MdIdleSelfBranch)
	set_tests_properties(mdUartRegisterTest mdUartCpuInterruptTest PROPERTIES FIXTURES_SETUP MdUart)
	set_tests_properties(mdLibTests PROPERTIES FIXTURES_REQUIRED "Mc68kCore;MdTimedHostRx;MdIdleSelfBranch;MdUart;MdMidiTiming")
endif()

add_executable(mmSysexWorkflowTest mmSysexWorkflowTest.cpp)
target_link_libraries(mmSysexWorkflowTest PRIVATE mdLib)
if(MSVC)
	target_compile_options(mmSysexWorkflowTest PRIVATE /fp:precise)
else()
	target_compile_options(mmSysexWorkflowTest PRIVATE -fno-fast-math)
endif()
set_property(TARGET mmSysexWorkflowTest PROPERTY FOLDER "Elektron/test")
add_test(NAME mmDigiProImportAudioOracleTest COMMAND mmSysexWorkflowTest --audio-oracle)
set_tests_properties(mmDigiProImportAudioOracleTest PROPERTIES LABELS "UnitTest;AudioIo" TIMEOUT 15)
foreach(profile 32 1024 irregular)
	add_test(NAME mmSysexBlockProfile-${profile} COMMAND mmSysexWorkflowTest --audio-oracle)
	set_tests_properties(mmSysexBlockProfile-${profile} PROPERTIES
		LABELS "UnitTest;AudioIo" TIMEOUT 15 ENVIRONMENT "MM_SYSEX_BLOCK_PROFILE=${profile}")
endforeach()

add_executable(mdSdsFirmwareTest sdsFirmwareTest.cpp)
target_link_libraries(mdSdsFirmwareTest PRIVATE mdLib)
if(MSVC)
	target_compile_options(mdSdsFirmwareTest PRIVATE /fp:precise)
else()
	target_compile_options(mdSdsFirmwareTest PRIVATE -fno-fast-math)
endif()
set_property(TARGET mdSdsFirmwareTest PROPERTY FOLDER "Elektron/test")

add_executable(mdSdsTransferTest sdsTransferTest.cpp)
target_link_libraries(mdSdsTransferTest PRIVATE mdLib)
add_test(NAME mdSdsTransferTest COMMAND mdSdsTransferTest)
set_tests_properties(mdSdsTransferTest PROPERTIES LABELS "UnitTest;Midi" TIMEOUT 15)
set_property(TARGET mdSdsTransferTest PROPERTY FOLDER "Elektron/test")

add_executable(mdFirmwareImageTest firmwareImageTest.cpp)
target_link_libraries(mdFirmwareImageTest PRIVATE mdLib)

add_test(NAME mdFirmwareImageTest COMMAND mdFirmwareImageTest)
set_tests_properties(mdFirmwareImageTest PROPERTIES LABELS "UnitTest")

set_property(TARGET mdFirmwareImageTest PROPERTY FOLDER "Elektron")

add_executable(mdAutomationMidiTest automationMidiTest.cpp)
target_link_libraries(mdAutomationMidiTest PRIVATE mdLib)
add_test(NAME mdAutomationMidiTest COMMAND mdAutomationMidiTest)
set_tests_properties(mdAutomationMidiTest PROPERTIES LABELS "UnitTest")
set_property(TARGET mdAutomationMidiTest PROPERTY FOLDER "Elektron")

add_executable(mdUwFirmwareTest mdUwFirmwareTest.cpp)
target_link_libraries(mdUwFirmwareTest PRIVATE mdLib)
# The audio oracle must detect NaN/Inf even in builds using -Ofast or /fp:fast.
if(MSVC)
	target_compile_options(mdUwFirmwareTest PRIVATE /fp:precise)
else()
	target_compile_options(mdUwFirmwareTest PRIVATE -fno-fast-math)
endif()
add_test(NAME mdUwFirmwareTest COMMAND mdUwFirmwareTest)
add_test(NAME mdRamAudioOracleTest COMMAND mdUwFirmwareTest --audio-oracle)
set_tests_properties(mdRamAudioOracleTest PROPERTIES LABELS "UnitTest;AudioIo")
set_tests_properties(mdUwFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Uw"
	SKIP_RETURN_CODE 77
	TIMEOUT 900)
set_property(TARGET mdUwFirmwareTest PROPERTY FOLDER "Elektron")

add_executable(mdFlashTest mdFlashTest.cpp)
target_link_libraries(mdFlashTest PRIVATE mdLib)
add_test(NAME mdFlashTest COMMAND mdFlashTest)
set_tests_properties(mdFlashTest PROPERTIES LABELS "UnitTest")
set_property(TARGET mdFlashTest PROPERTY FOLDER "Elektron")

add_executable(mdStateTest mdStateTest.cpp)
target_link_libraries(mdStateTest PRIVATE mdLib)
add_test(NAME mdStateTest COMMAND mdStateTest)
set_tests_properties(mdStateTest PROPERTIES LABELS "UnitTest")
set_property(TARGET mdStateTest PROPERTY FOLDER "Elektron")

add_executable(mdAudioQueueTest mdAudioQueueTest.cpp)
target_link_libraries(mdAudioQueueTest PRIVATE mdLib)

add_test(NAME mdAudioQueueTest COMMAND mdAudioQueueTest)
set_tests_properties(mdAudioQueueTest PROPERTIES LABELS "UnitTest;AudioIo")

set_property(TARGET mdAudioQueueTest PROPERTY FOLDER "Elektron/test")

if(BUILD_TESTING)
	# Windows/macOS CI selects the audio gate rather than mdLibTests. Keep the
	# same UART prerequisites in both gates without changing workflow selectors.
	add_dependencies(mdAudioQueueTest mdUartRegisterTest mdUartCpuInterruptTest mdMidiTimingTest)
	set_tests_properties(mdAudioQueueTest PROPERTIES FIXTURES_REQUIRED "MdUart;MdMidiTiming")
endif()

add_executable(mdAudioFirmwareTest mdAudioFirmwareTest.cpp)
target_link_libraries(mdAudioFirmwareTest PRIVATE mdLib)

add_test(NAME mdAudioFirmwareTest COMMAND mdAudioFirmwareTest)
set_tests_properties(mdAudioFirmwareTest PROPERTIES
	LABELS "FirmwareTest;AudioIo"
	SKIP_RETURN_CODE 77)

set_property(TARGET mdAudioFirmwareTest PROPERTY FOLDER "Elektron/test")

add_executable(mdTransportScorecardTest transportScorecardTest.cpp)
target_link_libraries(mdTransportScorecardTest PRIVATE mdLib)
add_test(NAME mdTransportScorecardTest COMMAND mdTransportScorecardTest)
set_tests_properties(mdTransportScorecardTest PROPERTIES
	LABELS "UnitTest;Timing" TIMEOUT 10 FIXTURES_SETUP MdTransportScorecard)
set_property(TARGET mdTransportScorecardTest PROPERTY FOLDER "Elektron/test")
if(BUILD_TESTING)
	add_dependencies(mdLibTest mdTransportScorecardTest)
	add_dependencies(mdAudioQueueTest mdTransportScorecardTest)
	set_property(TEST mdLibTests mdAudioQueueTest APPEND PROPERTY
		FIXTURES_REQUIRED MdTransportScorecard)
endif()

if(MD_TRANSPORT_DIAGNOSTICS)
	add_executable(mdTransportScorecardFirmwareTest
		transportScorecardFirmwareTest.cpp)
	target_link_libraries(mdTransportScorecardFirmwareTest PRIVATE mdLib)
	add_test(NAME mdTransportScorecardFirmwareTest
		COMMAND mdTransportScorecardFirmwareTest)
	set_tests_properties(mdTransportScorecardFirmwareTest PROPERTIES
		LABELS "Integration;FirmwareTest;Timing"
		SKIP_RETURN_CODE 77 TIMEOUT 180)
	set_property(TARGET mdTransportScorecardFirmwareTest
		PROPERTY FOLDER "Elektron/test")
endif()

add_executable(encoderPressFirmwareTest encoderPressFirmwareTest.cpp)
target_link_libraries(encoderPressFirmwareTest PRIVATE mdLib)
add_executable(mdRecordFirmwareTest mdRecordFirmwareTest.cpp)
add_executable(mdSectionFirmwareTest mdSectionFirmwareTest.cpp)
target_link_libraries(mdSectionFirmwareTest PRIVATE mdLib)
add_test(NAME mdSectionFirmwareTest COMMAND mdSectionFirmwareTest md)
add_test(NAME mmSectionFirmwareTest COMMAND mdSectionFirmwareTest mm)
add_test(NAME mmFourSectionFirmwareTest COMMAND mdSectionFirmwareTest mm 4)
set_tests_properties(mdSectionFirmwareTest mmSectionFirmwareTest mmFourSectionFirmwareTest PROPERTIES
	SKIP_RETURN_CODE 77 LABELS "Firmware;Maschine")
target_link_libraries(mdRecordFirmwareTest PRIVATE mdLib)
add_test(NAME mdRecordFirmwareTest COMMAND mdRecordFirmwareTest md)
add_test(NAME mdPadCursorFirmwareTest COMMAND mdRecordFirmwareTest md --md-pad-cursor)
add_test(NAME mdPadCursor64FirmwareTest COMMAND mdRecordFirmwareTest md --md-pad-cursor 64)
set_tests_properties(mdPadCursorFirmwareTest mdPadCursor64FirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 120)
add_test(NAME mdLcdDrumFirmwareTest COMMAND mdRecordFirmwareTest md --drums)
set_tests_properties(mdLcdDrumFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 180)
add_test(NAME mmRecordFirmwareTest COMMAND mdRecordFirmwareTest mm)
add_test(NAME mdPatternCopyPasteFirmwareTest COMMAND mdRecordFirmwareTest md --copy-paste)
set_tests_properties(mdPatternCopyPasteFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 90)
add_test(NAME mmClearTrackFirmwareTest COMMAND mdRecordFirmwareTest mm --clear-track)
set_tests_properties(mmClearTrackFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 90)
add_test(NAME mmFactoryResetFirmwareTest COMMAND mdRecordFirmwareTest mm --factory-reset)
set_tests_properties(mmFactoryResetFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 180)
add_test(NAME mmTrackMuteFirmwareTest COMMAND mdRecordFirmwareTest mm --track-mutes)
set_tests_properties(mmTrackMuteFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 90)
add_test(NAME mmRecordedStepLedsFirmwareTest COMMAND mdRecordFirmwareTest mm --step-colours)
add_test(NAME mmRecordedStepPagesFirmwareTest COMMAND mdRecordFirmwareTest mm --step-pages)
set_tests_properties(mmRecordedStepPagesFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 120)
set_tests_properties(mmRecordedStepLedsFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Maschine" SKIP_RETURN_CODE 77 TIMEOUT 90)
set_tests_properties(mdRecordFirmwareTest mmRecordFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 90)
set_property(TARGET mdRecordFirmwareTest PROPERTY FOLDER "Elektron/test")
add_test(NAME mdEncoderPressFirmwareTest COMMAND encoderPressFirmwareTest md)
add_test(NAME mmEncoderPressFirmwareTest COMMAND encoderPressFirmwareTest mm)
set_tests_properties(mdEncoderPressFirmwareTest mmEncoderPressFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
set_property(TARGET encoderPressFirmwareTest PROPERTY FOLDER "Elektron/test")

# Shared external regressions for the independent MM workaround removals.
add_executable(mmAudioFirmwareTest mmAudioFirmwareTest.cpp)
target_link_libraries(mmAudioFirmwareTest PRIVATE mdLib)
if(MSVC)
	target_compile_options(mmAudioFirmwareTest PRIVATE /fp:precise)
else()
	target_compile_options(mmAudioFirmwareTest PRIVATE -fno-fast-math)
endif()
add_test(NAME mmAudioFirmwareTest COMMAND mmAudioFirmwareTest)
add_test(NAME mmSineFirmwareTest COMMAND mmAudioFirmwareTest --sine)
add_test(NAME mmSineMidiFirmwareTest COMMAND mmAudioFirmwareTest --sine-midi)
add_test(NAME mmInputFirmwareTest COMMAND mmAudioFirmwareTest --input)
add_test(NAME mmDigiproFirmwareTest COMMAND mmAudioFirmwareTest --digipro)
add_test(NAME mmDigiproEnsembleFirmwareTest COMMAND mmAudioFirmwareTest --digipro-ensemble)
add_test(NAME mmSineOracleTest COMMAND mmAudioFirmwareTest --sine-oracle)
set_tests_properties(mmAudioFirmwareTest mmSineFirmwareTest mmSineMidiFirmwareTest mmInputFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 180)
set_tests_properties(mmDigiproFirmwareTest mmDigiproEnsembleFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 600)
set_tests_properties(mmSineOracleTest PROPERTIES
	LABELS "UnitTest;AudioIo" FIXTURES_SETUP MmAudioOracle TIMEOUT 10)
set_property(TARGET mmAudioFirmwareTest PROPERTY FOLDER "Elektron/test")

if(BUILD_TESTING)
	add_dependencies(mdLibTest encoderPressFirmwareTest)
	# Existing core/audio CI selectors must execute the fixture-free oracle.
	add_dependencies(mdLibTest mmAudioFirmwareTest)
	add_dependencies(mdAudioQueueTest mmAudioFirmwareTest)
	set_property(TEST mdLibTests mdAudioQueueTest APPEND PROPERTY FIXTURES_REQUIRED MmAudioOracle)
endif()

# Manual packaged-plugin measurement fixture; owns only explicitly supplied data.
add_executable(mdLatencyFixture EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/source/pluginTester/latency/create_transport_fixture.cpp)
target_link_libraries(mdLatencyFixture PRIVATE mdLib)
set_property(TARGET mdLatencyFixture PROPERTY FOLDER "Elektron/test")

if(BUILD_TESTING)
	# Existing core/audio CI selectors must exercise the sender transcripts and
	# compile the manual peer probe, even without private firmware fixtures.
	add_dependencies(mdLibTest mdTurboMidiUnitTest mdSdsTransferTest mdTurboMidiFirmwareTest)
	add_dependencies(mdAudioQueueTest mdTurboMidiUnitTest mdSdsTransferTest mdTurboMidiFirmwareTest)
	set_tests_properties(mdTurboMidiUnitTest mdSdsTransferTest PROPERTIES FIXTURES_SETUP MdSysexTransport)
	set_property(TEST mdLibTests mdAudioQueueTest APPEND PROPERTY FIXTURES_REQUIRED MdSysexTransport)
endif()
