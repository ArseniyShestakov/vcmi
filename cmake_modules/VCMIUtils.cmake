# This file should contain custom functions and macro and them only.
# It's should not alter any configuration on inclusion

#######################################
#        Build output path            #
#######################################
macro(vcmi_set_output_dir name dir)
	# Multi-config builds for Visual Studio, Xcode
	foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
		 string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIGUPPERCASE)
		 set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIGUPPERCASE} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}/${dir})
		 set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIGUPPERCASE} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}/${dir})
		 set_target_properties(${name} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIGUPPERCASE} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}/${dir})
	endforeach()

	# Generic no-config case for Makefiles, Ninja.
	# This is what Qt Creator is using
	set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${dir})
	set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${dir})
	set_target_properties(${name} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${dir})
endmacro()

#######################################
#        Project generation           #
#######################################

# Let us have proper tree-like structure in IDEs such as Visual Studio
function(assign_source_group)
	foreach(_source IN ITEMS ${ARGN})
		if(IS_ABSOLUTE "${_source}")
			file(RELATIVE_PATH _source_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_source}")
		else()
			set(_source_rel "${_source}")
		endif()
		get_filename_component(_source_path "${_source_rel}" PATH)
		string(REPLACE "/" "\\" _source_path_msvc "${_source_path}")
		source_group("${_source_path_msvc}" FILES "${_source}")
	endforeach()
endfunction(assign_source_group)

# Macro to add subdirectory and set appropriate FOLDER for generated projects files
function(add_subdirectory_with_folder _folder_name _folder)
	add_subdirectory(${_folder} ${ARGN})
	set_property(DIRECTORY "${_folder}" PROPERTY FOLDER "${_folder_name}")
endfunction()

# Macro for Xcode Projects generation
# Slightly outdated, but useful reference for all options available here:
# https://pewpewthespells.com/blog/buildsettings.html
# https://github.com/samdmarshall/Xcode-Build-Settings-Reference
if(${CMAKE_GENERATOR} MATCHES "Xcode")

	macro(set_xcode_property TARGET XCODE_PROPERTY XCODE_VALUE)
		set_property(TARGET ${TARGET} PROPERTY
			XCODE_ATTRIBUTE_${XCODE_PROPERTY} ${XCODE_VALUE})
	endmacro(set_xcode_property)

endif(${CMAKE_GENERATOR} MATCHES "Xcode")

#######################################
#        CMake debugging              #
#######################################

# Can be called to see check cmake variables and environment variables
# For "install" debugging just copy it here. There no easy way to include modules from source.
function(vcmi_get_cmake_debug_info)

		message(STATUS "Debug - Internal variables:")
		get_cmake_property(_variableNames VARIABLES)
		foreach(_variableName ${_variableNames})
				message(STATUS "${_variableName}=${${_variableName}}")
		endforeach()
		message(STATUS "Debug - Environment variables:")
		execute_process(COMMAND "${CMAKE_COMMAND}" "-E" "environment")

endfunction()
