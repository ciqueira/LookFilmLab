if(NOT DEFINED INPUT OR INPUT STREQUAL "")
  message(FATAL_ERROR "ValidateLookFilmLabCalibration requires -DINPUT=<path>")
endif()

if(NOT DEFINED OUTPUT OR OUTPUT STREQUAL "")
  message(FATAL_ERROR "ValidateLookFilmLabCalibration requires -DOUTPUT=<path>")
endif()

if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "Calibration input does not exist: ${INPUT}")
endif()

file(READ "${INPUT}" content)

if(NOT content MATCHES "\"format\"[ \t\r\n]*:[ \t\r\n]*\"lookfilmlab-calibration-v1\"")
  message(FATAL_ERROR
    "Production calibration must use format lookfilmlab-calibration-v1: ${INPUT}"
  )
endif()

if(NOT content MATCHES "\"product\"[ \t\r\n]*:[ \t\r\n]*\"LookFilmLab\"")
  message(FATAL_ERROR
    "Production calibration must use product LookFilmLab: ${INPUT}"
  )
endif()

if(NOT content MATCHES "\"plugin\"[ \t\r\n]*:[ \t\r\n]*\"MCLookFilmLab\"")
  message(FATAL_ERROR
    "Production calibration must use plugin MCLookFilmLab: ${INPUT}"
  )
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT}" "${content}")
