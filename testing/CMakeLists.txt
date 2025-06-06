cmake_minimum_required(VERSION 3.15)
include(GoogleTest)

set(SOURCE_FILES
	"analyzertest.cc"
	"colortest.cc"
	"configitemtest.cc"
	"cycletest.cc"
	"fixednotegraphscalertest.cc"
	"microphones_test.cc"
	"notegraphscalerfactorytest.cc"
	"ringbuffertest.cc"
	"utiltest.cc"

	"main.cc"
	"printer.cc"
)
set(GAME_SOURCES
	"../game/analyzer.cc"
	"../game/color.cc"
	"../game/configitem.cc"
	"../game/dynamicnotegraphscaler.cc"
	"../game/execname.cc"
	"../game/fixednotegraphscaler.cc"
	"../game/fs.cc"
	"../game/log.cc"
	"../game/microphones.cc"
	"../game/musicalscale.cc"
	"../game/notes.cc"
	"../game/notegraphscalerfactory.cc"
	"../game/platform.cc"
	"../game/tone.cc"
	"../game/util.cc"
)

set(GTEST_REQUIRED "")

if(BUILD_TESTS STREQUAL "ON")
	set(GTEST_REQUIRED "REQUIRED")
endif()

find_package(GTest ${GTEST_REQUIRED})

if(GTest_FOUND)
	message(STATUS "Testing enabled: Building test host")

	set(SOURCES ${SOURCE_FILES} ${HEADER_FILES} ${GAME_SOURCES})
	
	add_executable(performous_test ${SOURCES})

	find_package(Boost 1.55 REQUIRED COMPONENTS program_options iostreams system locale)
	target_link_libraries(performous_test PRIVATE ${Boost_LIBRARIES})

	find_package(fmt REQUIRED CONFIG)

	target_link_libraries(performous_test PRIVATE fmt::fmt)

	find_package(ICU 65 REQUIRED uc data i18n io)
	target_link_libraries(performous_test PRIVATE ICU::uc ICU::data ICU::i18n ICU::io)

	if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.20)
		target_link_libraries(performous_test PRIVATE GTest::gtest GTest::gtest_main GTest::gmock)
	else()
		if(NOT TARGET GTest::gmock)
			find_package(GMock MODULE REQUIRED)
			add_library(GTest::gmock ALIAS GMock)
		endif()
		target_link_libraries(performous_test PRIVATE GTest::GTest GTest::Main GTest::gmock)
	endif()

	target_include_directories(performous_test PRIVATE ".." "../game" "${Performous_BINARY_DIR}/game")

	if(WIN32 AND MSVC)
#		target_compile_options(performous_test PUBLIC /WX)
	else()
		target_compile_options(performous_test PUBLIC -Werror)
	endif()
	
	find_package(SDL2 REQUIRED)
	if (TARGET SDL2::SDL2)
		target_link_libraries(performous_test PRIVATE SDL2::SDL2)
	else()
		list(REMOVE_ITEM SDL2_LIBRARIES SDL2::SDL2main)
		target_link_libraries(performous_test PRIVATE ${SDL2_LIBRARIES})
	endif()
	target_include_directories(performous_test PRIVATE ${SDL2_INCLUDE_DIRS}) # Newer SDL versions have proper CMake targets making this unnecessary, but we can't take it for granted.

	if(APPLE)
	target_link_libraries(performous_test PRIVATE "-framework CoreFoundation")
	endif()

	gtest_discover_tests(performous_test PROPERTIES TEST_DISCOVERY_TIMEOUT 30)

else()
	message(STATUS "Testing disabled: Package gtest missing")
endif()
