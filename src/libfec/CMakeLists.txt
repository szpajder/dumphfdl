add_library (fec OBJECT
	viterbi27.c
	viterbi27_port.c
)
target_include_directories(fec PUBLIC "..")
