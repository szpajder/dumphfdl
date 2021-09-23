add_library (fec OBJECT
	viterbi27_port.c
)
target_include_directories(fec PUBLIC "..")
