add_library(dabtx_common STATIC
    common/log.c
    common/crc16.c
    common/config.c
)
target_include_directories(dabtx_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dabtx_radio STATIC
    radio/channels.c
    radio/hackrf_tx.c
)
target_include_directories(dabtx_radio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dabtx_radio PUBLIC PkgConfig::HACKRF dabtx_common)

add_library(dabtx_audio STATIC
    audio/pa_source.c
    audio/wasapi_loopback.c
    audio/source.cpp
)
# WASAPI loopback uses raw Windows COM (mmdeviceapi / audioclient) —
# needs ole32 + uuid for the GUIDs and COM runtime.
target_link_libraries(dabtx_audio PUBLIC PkgConfig::PORTAUDIO dabtx_common
                                   ole32 uuid)
target_include_directories(dabtx_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Stage stubs — filled in later commits.
add_library(dabtx_encoder STATIC encoder/aac_enc.c)
target_link_libraries(dabtx_encoder PUBLIC PkgConfig::FDKAAC dabtx_common)
target_include_directories(dabtx_encoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dabtx_dabplus STATIC dabplus/superframe.c)
target_link_libraries(dabtx_dabplus PUBLIC fec_rs dabtx_common)
target_include_directories(dabtx_dabplus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dabtx_mux STATIC
    mux/fib.c
    mux/cif.c
    mux/eti.c
    mux/eti_frame.c
    mux/eep_profile.c
)
target_link_libraries(dabtx_mux PUBLIC dabtx_common)
target_include_directories(dabtx_mux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dabtx_mod STATIC
    mod/ofdm.c
    mod/ofdm_symbol.c
    mod/conv_enc.c
    mod/puncture.c
    mod/phase_ref.c
    mod/dqpsk.c
    mod/freq_il.c
    mod/time_il.c
    mod/frame.c
    mod/fic_code.c
    mod/msc_code.c
)
target_link_libraries(dabtx_mod PUBLIC PkgConfig::FFTW3F dabtx_common dabtx_mux)
target_include_directories(dabtx_mod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dabtx_pad STATIC pad/dls.c pad/pad_inject.c pad/mot.c pad/pad_sched.c pad/epg.c pad/spi.c)
target_link_libraries(dabtx_pad PUBLIC dabtx_common)
target_include_directories(dabtx_pad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(dabtx main.cpp)
target_link_libraries(dabtx PRIVATE
    dabtx_radio dabtx_audio dabtx_encoder dabtx_dabplus dabtx_mux dabtx_mod dabtx_pad fec_rs
)

# Native light-theme Windows control room.  The transmission engine remains a
# separate process so closing the UI can never leave libhackrf streaming.
if(WIN32)
    add_executable(WolfDAB WIN32 wolfdab_win.cpp wolfdab.rc)
    target_link_libraries(WolfDAB PRIVATE dabtx_audio dabtx_mux comctl32 shell32 dwmapi uxtheme)
    target_link_options(WolfDAB PRIVATE -municode)
endif()
