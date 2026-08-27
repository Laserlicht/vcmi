set(h3AI_SRCS
		H3AdventureSpells.cpp
		H3AI.cpp
		H3ArmyPlanner.cpp
		H3ArtifactValue.cpp
		H3SpellValue.cpp
		H3CombatEstimate.cpp
		H3Kingdom.cpp
		H3Movement.cpp
		H3ObjectValue.cpp
		H3RewardValue.cpp
		H3Player.cpp
		H3Search.cpp
		H3SecondarySkills.cpp
		H3TownValue.cpp
		H3Valuations.cpp
		H3VictoryConditions.cpp
)

set(h3AI_HEADERS
		StdInc.h

		H3AdventureSpells.h
		H3AI.h
		H3ArmyPlanner.h
		H3ArtifactData.h
		H3ArtifactValue.h
		H3SpellData.h
		H3SpellValue.h
		H3CombatEstimate.h
		H3Constants.h
		H3Kingdom.h
		H3Movement.h
		H3ObjectValue.h
		H3RewardValue.h
		H3Player.h
		H3Search.h
		H3SecondarySkills.h
		H3TownValue.h
		H3Valuations.h
		H3VictoryConditions.h
)

assign_source_group(${h3AI_SRCS} ${h3AI_HEADERS})

add_library(H3AI OBJECT ${h3AI_SRCS} ${h3AI_HEADERS})
target_compile_definitions(H3AI PRIVATE VCMI_DLL=1)

target_include_directories(H3AI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(H3AI PRIVATE vcmiMain)

vcmi_set_output_dir(H3AI "AI")
enable_pch(H3AI)
