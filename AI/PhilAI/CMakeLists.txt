set(philAI_SRCS
		PhilValuation.cpp
		PhilCombatSim.cpp
		PhilSpellcaster.cpp
		PhilBattleAI.cpp
		PhilEconomy.cpp
		PhilMapScoring.cpp
		PhilAdventureAI.cpp
)

set(philAI_HEADERS
		StdInc.h

		PhilConstants.h
		PhilValuation.h
		PhilCombatSim.h
		PhilSpellcaster.h
		PhilBattleAI.h
		PhilEconomy.h
		PhilMapScoring.h
		PhilAdventureAI.h
)

assign_source_group(${philAI_SRCS} ${philAI_HEADERS})

add_library(PhilAI OBJECT ${philAI_SRCS} ${philAI_HEADERS})
target_compile_definitions(PhilAI PRIVATE VCMI_DLL=1)

target_link_libraries(PhilAI PRIVATE vcmiMain)
target_include_directories(PhilAI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

vcmi_set_output_dir(PhilAI "AI")
enable_pch(PhilAI)
