# Upstream TensorFlow Lite Micro microfrontend sources consumed by the BK7258
# KWS host regression. kiss_fft_int16.cc includes the fixed-point KissFFT C
# implementation itself, so do not add separate KissFFT translation units.

VOICE_KWS_TFLM_C_SOURCES := \
	frontend.c frontend_util.c \
	filterbank.c filterbank_util.c \
	log_lut.c log_scale.c log_scale_util.c \
	noise_reduction.c noise_reduction_util.c \
	pcan_gain_control.c pcan_gain_control_util.c \
	window.c window_util.c

VOICE_KWS_TFLM_CXX_SOURCES := fft.cc fft_util.cc kiss_fft_int16.cc
