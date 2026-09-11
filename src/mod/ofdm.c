/* TODO: DAB COFDM Transmission Mode I modulator.
 * - Energy dispersal (PRBS)
 * - Convolutional encoder, rate 1/4, punctured to EEP-3A
 * - Time and frequency interleaving
 * - DQPSK mapping, 1536 carriers
 * - 2048-pt IFFT (fftw3f), cyclic prefix 504 samples
 * - Null symbol + phase ref + 76 OFDM symbols = 96 ms frame @ 2.048 MS/s
 */
