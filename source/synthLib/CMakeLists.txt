cmake_minimum_required(VERSION 3.10)
project(synthLib)

set(SYNTHLIB_DEMO_MODE OFF CACHE BOOL "Demo Mode" FORCE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/buildconfig.h.in ${CMAKE_CURRENT_SOURCE_DIR}/buildconfig.h)

add_library(synthLib STATIC)

set(SOURCES
	audiobuffer.cpp audiobuffer.h
	audioTypes.h
	buildconfig.h buildconfig.h.in
	dac.cpp dac.h
	device.cpp device.h
	deviceException.cpp deviceException.h
	deviceTypes.h
	functionRef.h
	lv2PresetExport.cpp lv2PresetExport.h
	midiBufferParser.cpp midiBufferParser.h
	midiClock.cpp midiClock.h
	midiRateLimiter.cpp midiRateLimiter.h
	midiRoutingMatrix.cpp midiRoutingMatrix.h
	midiToSysex.cpp midiToSysex.h
	midiTranslator.cpp midiTranslator.h
	midiTypes.h
	os.cpp os.h
	plugin.cpp plugin.h
	realtimeInstrumentation.cpp realtimeInstrumentation.h
	performanceReport.cpp performanceReport.h
	resampler.cpp resampler.h
	resamplerInOut.cpp resamplerInOut.h
	sampleRateTime.h
	romLoader.cpp romLoader.h
	sounddiverLibLoader.cpp sounddiverLibLoader.h
	sysexRemoteControl.cpp sysexRemoteControl.h
	sysexToMidi.cpp sysexToMidi.h
	syntheticAudioTestDevice.h
	vstpreset.cpp vstpreset.h
	wavReader.cpp wavReader.h
	wavTypes.h
	wavWriter.cpp wavWriter.h
)

target_sources(synthLib PRIVATE ${SOURCES})
# Beat-position subtraction and finite-value guards must retain IEEE semantics
# even when the audio/DSP targets use fast-math.
if(MSVC)
	set_property(SOURCE midiClock.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:precise)
else()
	set_property(SOURCE midiClock.cpp APPEND PROPERTY COMPILE_OPTIONS -fno-fast-math)
endif()
source_group("source" FILES ${SOURCES})

target_link_libraries(synthLib PUBLIC resample dsp56kBase baseLib)

if(NOT MSVC)
	target_link_libraries(synthLib PUBLIC dl)
endif()

if(UNIX AND NOT APPLE)
	set(THREADS_PREFER_PTHREAD_FLAG TRUE)
	find_package(Threads REQUIRED)
	target_link_libraries(synthLib PRIVATE Threads::Threads)
endif()

set_property(TARGET synthLib PROPERTY FOLDER "Gearmulator")

target_include_directories(synthLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(BUILD_TESTING)
	add_executable(synthLibMidiClockTimingTest synthLibTest/midiClockTimingTest.cpp)
	target_link_libraries(synthLibMidiClockTimingTest PRIVATE synthLib)
	# The NaN/unknown-position cases in this timing oracle require IEEE semantics.
	if(MSVC)
		target_compile_options(synthLibMidiClockTimingTest PRIVATE /fp:precise)
	else()
		target_compile_options(synthLibMidiClockTimingTest PRIVATE -fno-fast-math)
	endif()
	add_test(NAME synthLibMidiClockTimingTest COMMAND synthLibMidiClockTimingTest)
	set_tests_properties(synthLibMidiClockTimingTest PROPERTIES LABELS "UnitTest;Midi;Timing" TIMEOUT 30)

	add_executable(synthLibResamplerTimingTest synthLibTest/resamplerTimingTest.cpp)
	target_link_libraries(synthLibResamplerTimingTest PRIVATE synthLib)
	add_test(NAME synthLibResamplerTimingTest COMMAND synthLibResamplerTimingTest)
	set_tests_properties(synthLibResamplerTimingTest PROPERTIES LABELS "UnitTest;AudioIo;Midi;Timing")

	add_executable(synthLibStateTransactionTest synthLibTest/stateTransactionTest.cpp)
	target_link_libraries(synthLibStateTransactionTest PRIVATE synthLib)
	add_test(NAME synthLibStateTransactionTest COMMAND synthLibStateTransactionTest)
	set_tests_properties(synthLibStateTransactionTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET synthLibStateTransactionTest PROPERTY FOLDER "Gearmulator")

	add_executable(midiRoutingMatrixTest synthLibTest/midiRoutingMatrixTest.cpp)
	target_link_libraries(midiRoutingMatrixTest PRIVATE synthLib)
	add_test(NAME midiRoutingMatrixTest COMMAND midiRoutingMatrixTest)
	set_tests_properties(midiRoutingMatrixTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET midiRoutingMatrixTest PROPERTY FOLDER "Gearmulator/test")

	add_executable(synthLibAudioTest synthLibAudioTest.cpp)
	target_link_libraries(synthLibAudioTest PRIVATE synthLib)
	add_test(NAME synthLibAudioTest COMMAND synthLibAudioTest)
	set_tests_properties(synthLibAudioTest PROPERTIES LABELS "UnitTest;AudioIo")
	set_property(TARGET synthLibAudioTest PROPERTY FOLDER "Gearmulator/test")
	# Existing platform CI selects synthLibAudioTest. Keep the independent
	# timing oracles in that gate as well as in a full CTest run.
	add_dependencies(synthLibAudioTest synthLibMidiClockTimingTest synthLibResamplerTimingTest)
	set_tests_properties(synthLibMidiClockTimingTest synthLibResamplerTimingTest
		PROPERTIES FIXTURES_SETUP SynthTiming)
	set_tests_properties(synthLibAudioTest PROPERTIES FIXTURES_REQUIRED SynthTiming)

	add_executable(synthLibPerformanceReportTest synthLibTest/performanceReportTest.cpp)
	target_link_libraries(synthLibPerformanceReportTest PRIVATE synthLib)
	add_test(NAME synthLibPerformanceReportTest COMMAND synthLibPerformanceReportTest
		"${CMAKE_CURRENT_BINARY_DIR}/performance-report-test.jsonl")
	set_tests_properties(synthLibPerformanceReportTest PROPERTIES LABELS "UnitTest" TIMEOUT 30)
	add_test(NAME synthLibAudioInstrumentationTest COMMAND synthLibAudioTest)
	set_tests_properties(synthLibAudioInstrumentationTest PROPERTIES
		LABELS "UnitTest;AudioIo" ENVIRONMENT "GEARMULATOR_RT_INSTRUMENTATION=1" TIMEOUT 60)

	add_executable(synthLibRealtimeInstrumentationTest
		synthLibTest/realtimeInstrumentationTest.cpp)
	target_link_libraries(synthLibRealtimeInstrumentationTest PRIVATE synthLib)
	add_test(NAME synthLibRealtimeInstrumentationTest
		COMMAND synthLibRealtimeInstrumentationTest)
	set_tests_properties(synthLibRealtimeInstrumentationTest PROPERTIES
		LABELS "UnitTest")
	set_property(TARGET synthLibRealtimeInstrumentationTest PROPERTY
		FOLDER "Gearmulator/test")
endif()
