cmake_minimum_required(VERSION 3.20)

project(filter_sim LANGUAGES C)

add_executable(filter_sim
    main.c
    "${CMAKE_CURRENT_LIST_DIR}/../../Core/Src/pp_dsp.c"
)

target_compile_features(filter_sim PRIVATE c_std_11)

target_include_directories(filter_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../Core/Src"
)

target_compile_definitions(filter_sim PRIVATE
    FILTER_SIM_INPUT_PATH=\"${CMAKE_CURRENT_LIST_DIR}/data/chirp_noise_48khz_s16le.pcm\"
)