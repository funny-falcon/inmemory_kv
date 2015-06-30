require 'mkmf'
have_func('malloc_usable_size')
have_func('rb_memhash')
create_makefile("inmemory_kv")
