# Old user extensions fail in parallel runtime synthesis workers on this host.
# Keep the build local/single-threaded instead of changing shared GUI settings.
set_param general.maxThreads 1
puts "FRAME_STORE_SYNTH_THREADS=[get_param general.maxThreads]"
