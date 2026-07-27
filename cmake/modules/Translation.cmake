find_package(${QT_PACKAGE_NAME} REQUIRED COMPONENTS Core LinguistTools)

# Extract the qmake executable location
get_target_property(QT_QMAKE_EXECUTABLE ${QT_PACKAGE_NAME}::qmake IMPORTED_LOCATION)

# Find Qts own translations dir (containing qt_*.qm, qtbase_*.qm ...)
if(NOT QT_TRANSLATIONS_DIR)
  # Ask the selected Qt version where to put the translations
  execute_process(COMMAND ${QT_QMAKE_EXECUTABLE} -query QT_INSTALL_TRANSLATIONS
                  OUTPUT_VARIABLE qt_translations_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
  # For windows systems: replace \ with / in directory path
  file(TO_CMAKE_PATH "${qt_translations_dir}" qt_translations_dir)
  set(QT_TRANSLATIONS_DIR ${qt_translations_dir} CACHE PATH "The location of the Qt translations" FORCE)
endif()

get_target_property(QT_LRELEASE_EXECUTABLE ${QT_PACKAGE_NAME}::lrelease IMPORTED_LOCATION)
get_target_property(QT_LCONVERT_EXECUTABLE ${QT_PACKAGE_NAME}::lconvert IMPORTED_LOCATION)
get_target_property(QT_LUPDATE_EXECUTABLE ${QT_PACKAGE_NAME}::lupdate IMPORTED_LOCATION)

# Helper function, takes the .qm file to be generated and a variable list of .ts files
# to create a custom command that then can be used for a custom target.
function(add_qm_translation_file _qm_file)
  foreach(_current_FILE ${ARGN})
    get_filename_component(_abs_FILE ${_current_FILE} ABSOLUTE)
    list(APPEND _ts_files ${_abs_FILE})
  endforeach()
  foreach(tsfile ${_ts_files})
    SET(tsfiles_blank_sep "${tsfiles_blank_sep} ${tsfile}")
  endforeach()
  add_custom_command(OUTPUT ${_qm_file}
    COMMAND ${QT_LRELEASE_EXECUTABLE}
    ARGS ${_ts_files} -qm ${_qm_file}
    DEPENDS ${_ts_files} VERBATIM
    COMMENT "Executing: lrelease -silent ${tsfiles_blank_sep} -qm ${_qm_file}"
  )
endfunction()

# Helper function, takes the qrc filename to generate and a variable list .qm files to be included.
function(mk_translation_qrc_file _qrc_file)
  if(NOT EXISTS ${_qrc_file})
    file(WRITE ${_qrc_file} "<RCC>\n")
    file(APPEND ${_qrc_file} "  <qresource prefix=\"i18n\">\n")
    foreach(_qm_file ${ARGN})
      get_filename_component(filename "${_qm_file}" NAME)
      file(APPEND ${_qrc_file} "    <file alias=\"${filename}\">${_qm_file}</file>\n")
    endforeach()
    file(APPEND ${_qrc_file} "  </qresource>\n")
    file(APPEND ${_qrc_file} "</RCC>\n")
  endif()
endfunction()

# Helper function, takes the .qm file to be generated and a variable list .qm files
# to be combined to one. Creates a custom command for the .qm file to be created.
function(add_combined_qm_translation_file _combined_qm_file)
  foreach(_current_FILE ${ARGN})
    get_filename_component(_abs_FILE ${_current_FILE} ABSOLUTE)
    list(APPEND _single_qm_files ${_abs_FILE})
  endforeach()
  list(REMOVE_DUPLICATES _single_qm_files)
  add_custom_command(OUTPUT ${_combined_qm_file}
    COMMAND ${QT_LCONVERT_EXECUTABLE}
    ARGS -o ${_combined_qm_file} ${_single_qm_files}
    DEPENDS ${_single_qm_files} VERBATIM
    COMMENT "Executing: ${QT_LCONVERT_EXECUTABLE} -o ${_combined_qm_file} ${_single_qm_files}"
  )
endfunction()

if(NOT TARGET ts_files)
  add_custom_target(ts_files)
  set_target_properties(ts_files PROPERTIES FOLDER "translation")
  set_target_properties(ts_files PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD 1)
endif()

# Function to add an updating 'task' to the custom translations_update target.
# _prefix : prefix for the *.ts files, i.e. myprefix_de.ts
# _input_dirs : list of directories to scan for translations with lupdate
# _ourput_dir : where to produce the *.ts files
function(add_translation_update_task _prefix _input_dirs _output_dir _languages)
  foreach(_lang ${_languages})
    list(APPEND _tsfiles_lupdate "${_prefix}_${_lang}.ts")
  endforeach()

  set(_ts_files_tgt ts_files_${_prefix})
  add_custom_target(${_ts_files_tgt})
  set_target_properties(${_ts_files_tgt} PROPERTIES FOLDER "translation")
  set_target_properties(${_ts_files_tgt} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD 1)
  add_custom_command(TARGET ${_ts_files_tgt} PRE_BUILD
    COMMAND ${QT_LUPDATE_EXECUTABLE}
    ARGS ${_input_dirs}
    ARGS -locations relative
    ARGS -ts
    ARGS -noobsolete
    ARGS ${_tsfiles_lupdate}
    WORKING_DIRECTORY ${_output_dir}
    COMMENT "Updating translations (${_prefix})..."
  )
  add_dependencies(ts_files ${_ts_files_tgt})
endfunction()

if(NOT TARGET qm_files)
  add_custom_target(qm_files)
  set_target_properties(qm_files PROPERTIES FOLDER "translation")
endif()

# Main function to be used in the main build configuration scripts.
# Will add a target 'translations' that will create/copy all the necessary
# .qm files to the given _target_dir for the given _languages.
# This includes also the translations from qt itself.
function(add_translations_target _prefix _target_dir _ts_dirs _languages)
  file(MAKE_DIRECTORY "${_target_dir}")
  # for each language
  foreach(_lang ${_languages})
    # find all .ts files in the given _ts_dirs for our translations
    foreach(_ts_dir ${_ts_dirs})
      file(GLOB _ts_files_glob LIST_DIRECTORIES false ${_ts_dir}/*_${_lang}.ts)
      list(APPEND _ts_files_all${_lang} ${_ts_files_glob})
    endforeach()
    list(LENGTH _ts_files_all${_lang} _num_ts_files)
    if(_num_ts_files)
      set(_qm_file ${_target_dir}/${_prefix}_${_lang}.qm)
      add_qm_translation_file(${_qm_file} ${_ts_files_all${_lang}})
      list(APPEND _qm_files ${_qm_file})
    endif()
  endforeach()

  list(LENGTH _qm_files _num_qm_files)
  if(_num_qm_files)
    set(_qm_files_tgt qm_files_${_prefix})
    add_custom_target(${_qm_files_tgt} ALL DEPENDS ${_qm_files})

    if(TARGET ${_prefix})
      set(_qrc_file translations.qrc)
      mk_translation_qrc_file(${_target_dir}/${_qrc_file} ${_qm_files})
      set_property(TARGET ${_prefix} APPEND PROPERTY SOURCES "${_target_dir}/${_qrc_file}" )
      add_dependencies(${_prefix} ${_qm_files_tgt})
    else()
      message(FATAL_ERROR "'${_prefix}' is not a valid target.")
    endif()

    set_target_properties(${_qm_files_tgt} PROPERTIES FOLDER "translation")
    set_target_properties(${_qm_files_tgt} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD 1)
    add_dependencies(qm_files ${_qm_files_tgt})
  endif()
endfunction()
