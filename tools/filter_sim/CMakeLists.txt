cmake_minimum_required(VERSION 3.20)

project(filter_sim LANGUAGES C)

find_package(Threads REQUIRED)

set(FILTER_SIM_SOURCES
    main.c
    "${CMAKE_CURRENT_LIST_DIR}/../../Core/Src/pp_dsp.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Core/Src/pp_circular_buff.c"
)

foreach(sim_target filter_sim filter_sim_snap)
    add_executable(${sim_target} ${FILTER_SIM_SOURCES})

    target_compile_features(${sim_target} PRIVATE c_std_11)

    target_include_directories(${sim_target} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../../Core/Src"
    )

    target_link_libraries(${sim_target} PRIVATE Threads::Threads)

    target_compile_definitions(${sim_target} PRIVATE
        FILTER_SIM_INPUT_PATH=\"${CMAKE_CURRENT_LIST_DIR}/data/chirp_noise_48khz_s16le.pcm\"
        FILTER_SIM_OUTPUT_PATH=\"${CMAKE_CURRENT_LIST_DIR}/data/chirp_noise_xcorr_48khz_f32le.raw\"
    )
    target_compile_definitions(${sim_target} PRIVATE FILTER_SIMULATION)
endforeach()

target_compile_definitions(filter_sim_snap PRIVATE PP_XCORR_USE_SNAP_TEMPLATE)