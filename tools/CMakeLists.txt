add_executable(
	HogMaker
	HogMaker/HogFormat.cpp
	HogMaker/HogMaker.cpp
)
target_include_directories(HogMaker PRIVATE ${PROJECT_BINARY_DIR}/lib)
export(TARGETS HogMaker FILE "${PROJECT_BINARY_DIR}/HogMakerConfig.cmake")