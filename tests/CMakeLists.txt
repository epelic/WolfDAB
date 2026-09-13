add_executable(test_channels test_channels.c)
target_link_libraries(test_channels PRIVATE dabtx_radio)
add_test(NAME channels COMMAND test_channels)

add_executable(test_rs120 test_rs120.c)
target_link_libraries(test_rs120 PRIVATE fec_rs)
add_test(NAME rs120 COMMAND test_rs120)

add_executable(test_ring test_ring.c)
target_link_libraries(test_ring PRIVATE dabtx_common)
add_test(NAME ring COMMAND test_ring)

add_executable(test_crc16 test_crc16.c)
target_link_libraries(test_crc16 PRIVATE dabtx_common)
add_test(NAME crc16 COMMAND test_crc16)

add_executable(test_superframe test_superframe.c)
target_link_libraries(test_superframe PRIVATE dabtx_dabplus dabtx_common fec_rs)
add_test(NAME superframe COMMAND test_superframe)

add_executable(test_fib test_fib.c)
target_link_libraries(test_fib PRIVATE dabtx_mux dabtx_common)
add_test(NAME fib COMMAND test_fib)

add_executable(test_cif test_cif.c)
target_link_libraries(test_cif PRIVATE dabtx_mux)
add_test(NAME cif COMMAND test_cif)

add_executable(test_eti test_eti.c)
target_link_libraries(test_eti PRIVATE dabtx_mux dabtx_common)
add_test(NAME eti COMMAND test_eti)

add_executable(test_prbs test_prbs.c)
target_link_libraries(test_prbs PRIVATE dabtx_common)
add_test(NAME prbs COMMAND test_prbs)

add_executable(test_ofdm_symbol test_ofdm_symbol.c)
target_link_libraries(test_ofdm_symbol PRIVATE dabtx_mod)
add_test(NAME ofdm_symbol COMMAND test_ofdm_symbol)

add_executable(test_conv_enc test_conv_enc.c)
target_link_libraries(test_conv_enc PRIVATE dabtx_mod)
add_test(NAME conv_enc COMMAND test_conv_enc)

add_executable(test_puncture test_puncture.c)
target_link_libraries(test_puncture PRIVATE dabtx_mod)
add_test(NAME puncture COMMAND test_puncture)

add_executable(test_eep3a test_eep3a.c)
target_link_libraries(test_eep3a PRIVATE dabtx_mod)
add_test(NAME eep3a COMMAND test_eep3a)

add_executable(test_eep_profile test_eep_profile.c)
target_link_libraries(test_eep_profile PRIVATE dabtx_mux)
add_test(NAME eep_profile COMMAND test_eep_profile)

add_executable(test_phase_ref test_phase_ref.c)
target_link_libraries(test_phase_ref PRIVATE dabtx_mod)
add_test(NAME phase_ref COMMAND test_phase_ref)

add_executable(test_dqpsk test_dqpsk.c)
target_link_libraries(test_dqpsk PRIVATE dabtx_mod)
add_test(NAME dqpsk COMMAND test_dqpsk)

add_executable(test_freq_il test_freq_il.c)
target_link_libraries(test_freq_il PRIVATE dabtx_mod)
add_test(NAME freq_il COMMAND test_freq_il)

add_executable(test_time_il test_time_il.c)
target_link_libraries(test_time_il PRIVATE dabtx_mod)
add_test(NAME time_il COMMAND test_time_il)

add_executable(test_frame test_frame.c)
target_link_libraries(test_frame PRIVATE dabtx_mod)
add_test(NAME frame COMMAND test_frame)

add_executable(test_ring_cf test_ring_cf.c)
target_link_libraries(test_ring_cf PRIVATE dabtx_common)
add_test(NAME ring_cf COMMAND test_ring_cf)

add_executable(test_fic_code test_fic_code.c)
target_link_libraries(test_fic_code PRIVATE dabtx_mod)
add_test(NAME fic_code COMMAND test_fic_code)

add_executable(test_msc_code test_msc_code.c)
target_link_libraries(test_msc_code PRIVATE dabtx_mod)
add_test(NAME msc_code COMMAND test_msc_code)

add_executable(test_mot test_mot.c)
target_link_libraries(test_mot PRIVATE dabtx_pad)
add_test(NAME mot COMMAND test_mot)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE dabtx_common)
add_test(NAME config COMMAND test_config)

add_executable(test_epg test_epg.c)
target_link_libraries(test_epg PRIVATE dabtx_pad)
add_test(NAME epg COMMAND test_epg)

add_executable(test_spi test_spi.c)
target_link_libraries(test_spi PRIVATE dabtx_pad dabtx_mux)
add_test(NAME spi COMMAND test_spi)

add_executable(test_source test_source.cpp)
target_link_libraries(test_source PRIVATE dabtx_audio)
add_test(NAME source COMMAND test_source)

add_executable(test_aac_lowrate test_aac_lowrate.c)
target_link_libraries(test_aac_lowrate PRIVATE dabtx_encoder)
add_test(NAME aac_lowrate COMMAND test_aac_lowrate)
